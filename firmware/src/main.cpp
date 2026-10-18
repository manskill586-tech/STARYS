// STARYS — прошивка модуля роя для ESP32 (Wokwi или настоящая плата).
//
// main.cpp — «тело» модуля: датчики, светодиод, защёлка, дисплей и связь.
// Вся логика решений — в swarm_core.h, здесь её только кормят данными.
//
// Связь:
//   * основной режим — MQTT через Wi-Fi (в Wokwi сеть "Wokwi-GUEST");
//   * запасной — Serial Monitor: каждое исходящее сообщение печатается
//     строкой JSON, а строку JSON, вставленную в монитор, модуль примет
//     как сообщение от соседа.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "swarm_core.h"

// ---- Настройки -------------------------------------------------------------

#ifndef MODULE_ID
#define MODULE_ID 1  // во втором проекте Wokwi поставьте 2
#endif

#ifndef USE_MQTT
#define USE_MQTT 1  // 0 — только Serial-режим (запасной вариант)
#endif

static const char* WIFI_SSID = "Wokwi-GUEST";
static const char* WIFI_PASS = "";
static const char* MQTT_HOST = "broker.hivemq.com";
static const uint16_t MQTT_PORT = 1883;
// Общий «эфир» роя. Поменяйте суффикс, чтобы чужие демо не мешали вашему.
static const char* MQTT_TOPIC = "starys/swarm/demo";

// ---- Пины (совпадают с wokwi/*/diagram.json) -------------------------------

static const int PIN_LED_R = 25;
static const int PIN_LED_G = 26;
static const int PIN_LED_B = 27;
static const int PIN_TRIG = 5;
static const int PIN_ECHO = 18;
static const int PIN_CONTACT = 32;  // кнопка «Контакт» → GND
static const int PIN_FAULT = 33;    // кнопка «Авария» → GND
static const int PIN_SERVO = 13;

static const int SERVO_OPEN = 0;    // градусы
static const int SERVO_CLOSED = 90;

// ---- Объекты ---------------------------------------------------------------

starys::SwarmModule module(MODULE_ID);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Servo latch;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool oledOk = false;

float distanceCm = 400;
int battery = 100;
uint32_t lastDistanceRead = 0;
uint32_t lastMqttAttempt = 0;
uint32_t lastBatteryTick = 0;
uint32_t lastOled = 0;
starys::State lastState = starys::State::Boot;
bool lastLatch = false;
String serialLine;

// ---- Железо ----------------------------------------------------------------

void setLed(starys::Color color, bool blink, uint32_t now) {
  bool r = false, g = false, b = false;
  switch (color) {
    case starys::Color::White:  r = g = b = true; break;
    case starys::Color::Blue:   b = true; break;
    case starys::Color::Yellow: r = g = true; break;
    case starys::Color::Green:  g = true; break;
    case starys::Color::Red:    r = true; break;
    case starys::Color::Off:    break;
  }
  if (blink && (now / 400) % 2) r = g = b = false;
  digitalWrite(PIN_LED_R, r);
  digitalWrite(PIN_LED_G, g);
  digitalWrite(PIN_LED_B, b);
}

float readDistance() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long us = pulseIn(PIN_ECHO, HIGH, 30000);
  if (us == 0) return 400;
  return us / 58.0f;
}

bool isContact() { return digitalRead(PIN_CONTACT) == LOW; }
bool isFault() { return digitalRead(PIN_FAULT) == LOW; }
void lockServo(bool closed) { latch.write(closed ? SERVO_CLOSED : SERVO_OPEN); }

// ---- Сообщения <-> JSON ----------------------------------------------------

String toJson(const starys::Message& m) {
  JsonDocument doc;
  doc["type"] = starys::msgTypeName(m.type);
  switch (m.type) {
    case starys::MsgType::DockReq:
    case starys::MsgType::DockAck:
      doc["from"] = m.from;
      doc["to"] = m.to;
      break;
    case starys::MsgType::Data:
      doc["id"] = m.from;
      doc["battery"] = m.battery;
      doc["neighbor"] = m.neighbor;
      break;
    case starys::MsgType::Hello:
      doc["id"] = m.from;
      doc["near"] = m.near;
      break;
    default:
      doc["id"] = m.from;
      if (m.to) doc["to"] = m.to;
      break;
  }
  String s;
  serializeJson(doc, s);
  return s;
}

bool fromJson(const char* text, starys::Message& m) {
  JsonDocument doc;
  if (deserializeJson(doc, text)) return false;
  m.type = starys::msgTypeFromName(doc["type"] | "");
  if (m.type == starys::MsgType::None) return false;
  m.from = doc["from"] | (doc["id"] | 0);
  m.to = doc["to"] | 0;
  m.battery = doc["battery"] | 0;
  m.neighbor = doc["neighbor"] | 0;
  m.near = doc["near"] | false;
  return m.from != 0;
}

void onMqtt(char*, byte* payload, unsigned int len) {
  String text;
  text.reserve(len);
  for (unsigned int i = 0; i < len; i++) text += (char)payload[i];
  starys::Message m;
  if (fromJson(text.c_str(), m)) module.onMessage(m, millis());
}

void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      serialLine.trim();
      starys::Message m;
      if (serialLine.startsWith("{") && fromJson(serialLine.c_str(), m)) {
        Serial.printf("<< %s\n", serialLine.c_str());
        module.onMessage(m, millis());
      }
      serialLine = "";
    } else if (serialLine.length() < 200) {
      serialLine += c;
    }
  }
}

void flushOutgoing() {
  starys::Message m;
  while (module.popOutgoing(m)) {
    String json = toJson(m);
    Serial.printf(">> %s\n", json.c_str());
#if USE_MQTT
    if (mqtt.connected()) mqtt.publish(MQTT_TOPIC, json.c_str());
#endif
  }
}

// ---- Сеть ------------------------------------------------------------------

bool networkReady(uint32_t now) {
#if USE_MQTT
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!mqtt.connected() && now - lastMqttAttempt > 2000) {
    lastMqttAttempt = now;
    String clientId = "starys-" + String(MODULE_ID) + "-" + String(random(0xffff), HEX);
    if (mqtt.connect(clientId.c_str())) {
      mqtt.subscribe(MQTT_TOPIC);
      Serial.println("MQTT: подключено");
    }
  }
  mqtt.loop();
  return mqtt.connected();
#else
  (void)now;
  return true;
#endif
}

// ---- Дисплей ---------------------------------------------------------------

void drawOled() {
  if (!oledOk) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.printf("STARYS  module #%d", MODULE_ID);
  oled.setTextSize(2);
  oled.setCursor(0, 14);
  oled.print(starys::stateName(module.state()));
  oled.setTextSize(1);
  oled.setCursor(0, 36);
  oled.printf("dist %3.0fcm  bat %d%%", distanceCm, battery);
  oled.setCursor(0, 46);
  if (module.neighbor())
    oled.printf("neighbor #%d", module.neighbor());
  else
    oled.print("neighbor  -");
  oled.setCursor(0, 56);
  if (module.neighborBattery() >= 0)
    oled.printf("its bat %d%%  latch %s", module.neighborBattery(),
                module.latchClosed() ? "ON" : "off");
  else
    oled.printf("latch %s", module.latchClosed() ? "CLOSED" : "open");
  oled.display();
}

// ---- setup / loop ----------------------------------------------------------

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_CONTACT, INPUT_PULLUP);
  pinMode(PIN_FAULT, INPUT_PULLUP);
  latch.attach(PIN_SERVO);
  lockServo(false);
  setLed(starys::Color::White, false, 0);

  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  randomSeed(esp_random());

  Serial.printf("\nSTARYS module #%d — запуск\n", MODULE_ID);
#if USE_MQTT
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqtt);
#else
  Serial.println("Serial-режим: вставляйте JSON соседа в монитор");
#endif
}

void loop() {
  const uint32_t now = millis();

  if (now - lastDistanceRead >= 100) {
    lastDistanceRead = now;
    distanceCm = readDistance();
  }
  if (now - lastBatteryTick >= 20000) {  // имитация разряда
    lastBatteryTick = now;
    if (battery > 5) battery--;
  }

  readSerial();

  starys::Inputs in;
  in.now = now;
  in.sensorsOk = true;
  in.netReady = networkReady(now);
  in.distanceCm = distanceCm;
  in.contact = isContact();
  in.fault = isFault();
  in.battery = battery;
  module.tick(in);
  flushOutgoing();

  if (module.state() != lastState) {
    Serial.printf("[%lu ms] %s -> %s (сосед #%d)\n", (unsigned long)now,
                  starys::stateName(lastState), starys::stateName(module.state()),
                  module.neighbor());
    lastState = module.state();
  }
  if (module.latchClosed() != lastLatch) {
    lastLatch = module.latchClosed();
    lockServo(lastLatch);
  }

  starys::Led led = module.led();
  setLed(led.color, led.blink, now);

  if (now - lastOled >= 250) {
    lastOled = now;
    drawOled();
  }
}

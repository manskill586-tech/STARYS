// Тесты «мозга» модуля на обычном компьютере — без ESP32 и без Wokwi.
//
//   cd firmware/test && make
//
// Два модуля соединены виртуальным «эфиром»: всё, что один отправил,
// другой получает на следующем такте. Проверяем сценарий демо из README
// и отдельные тайм-ауты.

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>

#include "../src/flywheel.h"
#include "../src/swarm_core.h"

using namespace starys;

static int failures = 0;
static int checks = 0;
static bool verbose = false;

#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) {                                                      \
      ++failures;                                                       \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                   \
  } while (0)

// Мир из нескольких модулей и общего эфира.
struct World {
  std::vector<SwarmModule> modules;
  std::vector<Inputs> inputs;
  bool linkUp = true;  // false — «обрыв связи»: сообщения теряются
  uint32_t now = 0;

  explicit World(int n) {
    for (int i = 1; i <= n; i++) {
      modules.emplace_back(i);
      inputs.emplace_back();
    }
  }

  SwarmModule& m(int id) { return modules[id - 1]; }
  Inputs& in(int id) { return inputs[id - 1]; }

  // Прогнать время вперёд шагами по 50 мс.
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 50) {
      now += 50;
      std::vector<Message> air;
      for (size_t i = 0; i < modules.size(); i++) {
        State before = modules[i].state();
        inputs[i].now = now;
        modules[i].tick(inputs[i]);
        Message msg;
        while (modules[i].popOutgoing(msg)) {
          if (verbose && msg.type != MsgType::Hello && msg.type != MsgType::Data)
            std::printf("    %6u ms  #%d >> %s to=%d\n", now, msg.from,
                        msgTypeName(msg.type), msg.to);
          if (linkUp) air.push_back(msg);
        }
        logChange(i, before);
      }
      for (const Message& msg : air)
        for (size_t i = 0; i < modules.size(); i++) {
          State before = modules[i].state();
          modules[i].onMessage(msg, now);
          logChange(i, before);
        }
    }
  }

  void logChange(size_t i, State before) {
    if (verbose && modules[i].state() != before)
      std::printf("    %6u ms  #%d  %s -> %s\n", now, modules[i].id(),
                  stateName(before), stateName(modules[i].state()));
  }
};

static void testDemoScenario() {
  std::printf("Сценарий демо: поиск -> стыковка -> связь -> авария -> снова связь\n");
  World w(2);

  // 1. Запуск: оба уходят в поиск и мигают синим.
  w.run(200);
  CHECK(w.m(1).state() == State::Search);
  CHECK(w.m(2).state() == State::Search);
  CHECK(w.m(1).led().color == Color::Blue && w.m(1).led().blink);

  // Соседи далеко — стыковки нет, сколько ни жди.
  w.run(3000);
  CHECK(w.m(1).state() == State::Search);

  // 2. «Приближаем» соседа ползунком — модуль 1 просит стыковку.
  w.in(1).distanceCm = 12;
  w.in(2).distanceCm = 12;
  w.run(300);
  CHECK(w.m(1).state() == State::Docking);
  CHECK(w.m(2).state() == State::Docking);
  CHECK(w.m(1).neighbor() == 2);
  CHECK(w.m(2).neighbor() == 1);
  CHECK(w.m(1).led().color == Color::Yellow);

  // 3. «Контакт» на одном модуле — оба в связи, защёлки закрыты.
  w.in(1).contact = true;
  w.run(100);
  w.in(1).contact = false;
  w.run(2000);
  CHECK(w.m(1).state() == State::Linked);
  CHECK(w.m(2).state() == State::Linked);
  CHECK(w.m(1).latchClosed() && w.m(2).latchClosed());
  CHECK(w.m(1).neighborBattery() == 100);
  CHECK(w.m(1).led().color == Color::Green);

  // Связь держится долго, пока идут DATA.
  w.run(20000);
  CHECK(w.m(1).state() == State::Linked);
  CHECK(w.m(2).state() == State::Linked);

  // 4. «Авария» на модуле 2: красный, BYE, защёлка открыта.
  w.in(2).fault = true;
  w.run(100);
  w.in(2).fault = false;
  CHECK(w.m(2).state() == State::Fault);
  CHECK(!w.m(2).latchClosed());
  CHECK(w.m(2).led().color == Color::Red);
  CHECK(w.m(1).state() == State::Search);  // получил BYE
  CHECK(!w.m(1).latchClosed());

  // После паузы модули сами находят друг друга и стыкуются снова.
  w.run(4000);
  CHECK(w.m(1).state() == State::Docking);
  CHECK(w.m(2).state() == State::Docking);
  w.in(2).contact = true;
  w.run(1500);
  CHECK(w.m(1).state() == State::Linked);
  CHECK(w.m(2).state() == State::Linked);
}

static void testLinkLoss() {
  std::printf("Обрыв связи: молчание соседа -> возврат в поиск\n");
  World w(2);
  w.in(1).distanceCm = w.in(2).distanceCm = 10;
  w.in(1).contact = w.in(2).contact = true;
  w.run(3000);
  CHECK(w.m(1).state() == State::Linked);

  w.linkUp = false;
  w.run(5000);
  CHECK(w.m(1).state() == State::Search);
  CHECK(w.m(2).state() == State::Search);
  CHECK(!w.m(1).latchClosed());

  w.linkUp = true;
  w.run(3000);
  CHECK(w.m(1).state() == State::Linked);
}

static void testNoAck() {
  std::printf("Сосед не ответил на DOCK_REQ -> тайм-аут, снова поиск\n");
  World w(2);
  w.in(2).distanceCm = 10;
  w.run(1500);  // модуль 1 услышал от 2: «я рядом»
  w.linkUp = false;
  w.in(1).distanceCm = 10;
  w.run(200);
  CHECK(w.m(1).state() == State::Docking);
  w.run(5500);
  CHECK(w.m(1).state() == State::Search);
}

static void testNoContact() {
  std::printf("Договорились, но касания нет -> BYE и поиск\n");
  World w(2);
  w.in(1).distanceCm = w.in(2).distanceCm = 10;
  w.run(2000);
  CHECK(w.m(1).state() == State::Docking);
  w.in(1).distanceCm = w.in(2).distanceCm = 300;  // отъехали
  w.run(16000);
  CHECK(w.m(1).state() == State::Search);
  CHECK(w.m(2).state() == State::Search);
}

static void testBootWaitsForNetwork() {
  std::printf("Запуск ждёт сеть, светодиод белый\n");
  SwarmModule m(7);
  Inputs in;
  in.netReady = false;
  for (uint32_t t = 0; t < 2000; t += 50) { in.now = t; m.tick(in); }
  CHECK(m.state() == State::Boot);
  CHECK(m.led().color == Color::White);
  in.netReady = true;
  in.now = 2050;
  m.tick(in);
  CHECK(m.state() == State::Search);
}

static void testIgnoresForeignTraffic() {
  std::printf("Чужие адресные сообщения и собственное эхо игнорируются\n");
  World w(3);
  w.in(1).distanceCm = w.in(2).distanceCm = 10;
  w.run(2000);
  // Модуль 3 далеко и не должен влезть в чужую стыковку.
  CHECK(w.m(3).state() == State::Search);
  CHECK(w.m(1).neighbor() != 3 && w.m(2).neighbor() != 3);
}

static void testRoles() {
  std::printf("Роль соседа известна после стыковки\n");
  World w(0);
  w.modules.emplace_back(1, Role::Builder);
  w.modules.emplace_back(2, Role::Power);
  w.inputs.resize(2);
  w.in(1).distanceCm = w.in(2).distanceCm = 10;
  w.run(2000);
  CHECK(w.m(1).state() == State::Docking);
  CHECK(w.m(1).neighborRole() == Role::Power);
  CHECK(w.m(2).neighborRole() == Role::Builder);

  Role r = Role::Builder;
  CHECK(roleFromName("relay", r) && r == Role::Relay);
  CHECK(!roleFromName("pilot", r));
}


// Мир из модулей с заданными ролями.
static World worldWithRoles(std::initializer_list<Role> roles) {
  World w(0);
  int id = 1;
  for (Role r : roles) {
    w.modules.emplace_back(id++, r);
    w.inputs.emplace_back();
  }
  return w;
}

static void testStormFromScout() {
  std::printf("Буря: разведчик предупреждает, рой замирает и возвращается к работе\n");
  World w = worldWithRoles({Role::Builder, Role::Builder, Role::Scout});
  w.in(1).distanceCm = w.in(2).distanceCm = 10;
  w.in(1).contact = w.in(2).contact = true;
  w.run(3000);
  CHECK(w.m(1).state() == State::Linked);
  CHECK(w.m(2).state() == State::Linked);
  CHECK(w.m(3).state() == State::Search);

  w.in(3).stormSensor = true;
  w.run(300);
  CHECK(w.m(1).state() == State::Storm);
  CHECK(w.m(2).state() == State::Storm);
  CHECK(w.m(3).state() == State::Storm);
  CHECK(w.m(1).latchClosed() && w.m(2).latchClosed());  // держимся за соседа
  CHECK(w.m(1).led().color == Color::Violet && w.m(1).led().slow);

  // Буря длинная: разведчик повторяет STORM, никто не выходит раньше времени.
  w.run(25000);
  CHECK(w.m(1).state() == State::Storm);
  CHECK(w.m(1).latchClosed());

  w.in(3).stormSensor = false;
  w.run(300);
  CHECK(w.m(1).state() == State::Linked);
  CHECK(w.m(2).state() == State::Linked);
  CHECK(w.m(3).state() == State::Search);
  w.run(8000);  // связь после бури живая, тайм-аута нет
  CHECK(w.m(1).state() == State::Linked);
  CHECK(w.m(1).neighbor() == 2);
}

static void testStormEndsWithoutScout() {
  std::printf("Разведчик пропал во время бури -> через 10 с буря считается законченной\n");
  World w = worldWithRoles({Role::Builder, Role::Scout});
  w.run(500);
  w.in(2).stormSensor = true;
  w.run(300);
  CHECK(w.m(1).state() == State::Storm);
  w.linkUp = false;
  w.run(9000);
  CHECK(w.m(1).state() == State::Storm);
  w.run(2000);
  CHECK(w.m(1).state() == State::Search);
}

static void testStormOwnSensor() {
  std::printf("Свой датчик бури: SEARCH -> STORM -> SEARCH, без сообщений\n");
  SwarmModule m(4);
  Inputs in;
  for (uint32_t t = 50; t <= 500; t += 50) { in.now = t; m.tick(in); }
  CHECK(m.state() == State::Search);
  in.stormSensor = true;
  in.now = 550; m.tick(in);
  CHECK(m.state() == State::Storm);
  Message msg;
  while (m.popOutgoing(msg)) {}
  for (uint32_t t = 600; t <= 5000; t += 50) { in.now = t; m.tick(in); }
  CHECK(!m.popOutgoing(msg));  // радиомолчание
  in.stormSensor = false;
  in.now = 5050; m.tick(in);
  CHECK(m.state() == State::Search);
}

static void testLowPowerCharging() {
  std::printf("Мало заряда: найти энергомодуль, зарядиться, вернуться к работе\n");
  World w = worldWithRoles({Role::Builder, Role::Power});
  w.in(1).battery = 20;
  w.in(2).battery = 100;
  w.run(300);
  CHECK(w.m(1).state() == State::LowPower);
  CHECK(w.m(1).led().color == Color::Orange);

  w.in(1).distanceCm = w.in(2).distanceCm = 10;
  w.run(2000);
  CHECK(w.m(1).state() == State::Docking);
  CHECK(w.m(1).neighborRole() == Role::Power);

  w.in(1).contact = true;
  w.run(300);
  w.in(1).contact = false;
  w.run(3000);
  CHECK(w.m(1).state() == State::Charging);
  CHECK(w.m(2).state() == State::Linked);
  CHECK(w.m(1).latchClosed() && w.m(2).latchClosed());
  CHECK(w.m(1).led().color == Color::Orange && w.m(1).led().blink);

  w.in(1).battery = 95;
  w.run(100);
  CHECK(w.m(1).state() == State::Search);
  CHECK(w.m(2).state() == State::Search);  // получил BYE
  CHECK(!w.m(1).latchClosed() && !w.m(2).latchClosed());
}

static void testLowPowerIgnoresBuilders() {
  std::printf("Мало заряда: со строителем не стыкуемся, ждём энергомодуль\n");
  World w(2);
  w.in(1).battery = 10;
  w.in(1).distanceCm = w.in(2).distanceCm = 10;
  w.run(12000);
  CHECK(w.m(1).state() == State::LowPower);
  CHECK(w.m(1).neighbor() == 0);
  CHECK(!w.m(1).latchClosed());
}

static void testPowerNeverLow() {
  std::printf("Энергомодуль сам не уходит в LOW_POWER\n");
  World w = worldWithRoles({Role::Power});
  w.in(1).battery = 5;
  w.run(2000);
  CHECK(w.m(1).state() == State::Search);
}

static void testStormThenLowPowerCharging() {
  std::printf("Буря во время зарядки: не отцепляемся, после бури заряжаемся дальше\n");
  World w = worldWithRoles({Role::Builder, Role::Power, Role::Scout});
  w.in(1).battery = 15;
  w.in(1).distanceCm = w.in(2).distanceCm = 10;
  w.in(1).contact = true;
  w.run(3000);
  CHECK(w.m(1).state() == State::Charging);
  w.in(3).stormSensor = true;
  w.run(5000);
  CHECK(w.m(1).state() == State::Storm && w.m(1).latchClosed());
  w.in(3).stormSensor = false;
  w.run(2000);
  CHECK(w.m(1).state() == State::Charging);
}

static void testFlywheel() {
  std::printf("Маховик: обороты для переката на Земле, Марсе и Луне\n");
  FlywheelSpec spec;
  const float earth = rpmForPivot(spec, 9.81f, 1.0f);
  const float mars = rpmForPivot(spec, 3.71f, 1.0f);
  const float moon = rpmForPivot(spec, 1.62f, 1.0f);
  std::printf("    без запаса: Земля %.0f, Марс %.0f, Луна %.0f об/мин\n", earth, mars, moon);
  CHECK(earth > 8500 && earth < 9300);   // ≈ 8900 об/мин
  CHECK(mars > 5200 && mars < 5800);     // ≈ 5500 об/мин
  CHECK(mars < earth && moon < mars);    // слабее гравитация — меньше оборотов
  CHECK(pivotPossible(spec, 9.81f));     // мотор справляется даже на Земле
  FlywheelSpec weak = spec;
  weak.maxRpm = 5000;                    // слабый мотор: на Земле нет, на Марсе почти
  CHECK(!pivotPossible(weak, 9.81f));
}

static void testMessageNames() {
  std::printf("Имена сообщений туда-обратно\n");
  const MsgType all[] = {MsgType::Hello, MsgType::DockReq, MsgType::DockAck,
                         MsgType::Data, MsgType::Bye, MsgType::Storm};
  for (MsgType t : all) CHECK(msgTypeFromName(msgTypeName(t)) == t);
  CHECK(msgTypeFromName("HACK") == MsgType::None);
}

int main(int argc, char**) {
  verbose = argc > 1;
  testDemoScenario();
  testLinkLoss();
  testNoAck();
  testNoContact();
  testBootWaitsForNetwork();
  testIgnoresForeignTraffic();
  testRoles();
  testStormFromScout();
  testStormEndsWithoutScout();
  testStormOwnSensor();
  testLowPowerCharging();
  testLowPowerIgnoresBuilders();
  testPowerNeverLow();
  testStormThenLowPowerCharging();
  testFlywheel();
  testMessageNames();
  std::printf("\n%d проверок, %d ошибок\n", checks, failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

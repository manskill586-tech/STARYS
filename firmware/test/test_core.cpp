// Тесты «мозга» модуля на обычном компьютере — без ESP32 и без Wokwi.
//
//   cd firmware/test && make
//
// Два модуля соединены виртуальным «эфиром»: всё, что один отправил,
// другой получает на следующем такте. Проверяем сценарий демо из README
// и отдельные тайм-ауты.

#include <cstdio>
#include <cstdlib>
#include <vector>

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

static void testMessageNames() {
  std::printf("Имена сообщений туда-обратно\n");
  const MsgType all[] = {MsgType::Hello, MsgType::DockReq, MsgType::DockAck,
                         MsgType::Data, MsgType::Bye};
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
  testMessageNames();
  std::printf("\n%d проверок, %d ошибок\n", checks, failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

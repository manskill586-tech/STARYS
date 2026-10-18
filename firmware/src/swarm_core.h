// STARYS — «мозг» модуля самособирающегося роя для Марса.
//
// Здесь только логика: конечный автомат и протокол рукопожатия.
// Файл не зависит от Arduino, поэтому один и тот же код:
//   * работает на ESP32 (src/main.cpp подключает датчики, MQTT и светодиод);
//   * проверяется тестами на компьютере (test/test_core.cpp);
//   * переносится на настоящее железо без изменений.
//
// Состояния:
//   BOOT → SEARCH ⇄ DOCKING → LINKED      основной цикл стыковки
//   FAULT                                 авария, BYE, пауза, снова поиск
//   STORM                                 пылевая буря: замереть и переждать
//   LOW_POWER → DOCKING → CHARGING        мало заряда: найти энергомодуль
#pragma once

#include <stdint.h>

namespace starys {

// ---------------------------------------------------------------------------
// Состояния и сообщения
// ---------------------------------------------------------------------------

enum class State : uint8_t {
  Boot, Search, Docking, Linked, Fault, Storm, LowPower, Charging
};

enum class MsgType : uint8_t { None, Hello, DockReq, DockAck, Data, Bye, Storm };

// Роль модуля. Корпус, грани и мозг у всех одинаковые, отличается начинка.
// Роль приходит в HELLO, поэтому сосед заранее знает, с кем стыкуется.
// Подробнее — docs/ideas.md.
enum class Role : uint8_t {
  Builder,      // строитель: ходит по конструкции и встаёт в неё
  Power,        // энергомодуль: большая батарея, заряжает соседей
  Scout,        // разведчик-метеостанция: ветер, пыль, грунт
  Relay,        // связист: ретранслятор до орбитального спутника
  Coordinator,  // координатор: считает план сборки
  Anchor,       // якорь: фундамент, закрепляется в грунте
};

// Одно сообщение протокола. В эфир уходит как JSON, см. docs/protocol.md:
//   HELLO    {"type":"HELLO","id":1,"near":false,"role":"builder"}
//   DOCK_REQ {"type":"DOCK_REQ","from":1,"to":2}
//   DOCK_ACK {"type":"DOCK_ACK","from":2,"to":1}
//   DATA     {"type":"DATA","id":2,"battery":87,"neighbor":1}
//   BYE      {"type":"BYE","id":1}
//   STORM    {"type":"STORM","id":5,"on":true}
struct Message {
  MsgType type = MsgType::None;
  int from = 0;      // отправитель (в HELLO/DATA/BYE/STORM это поле "id")
  int to = 0;        // получатель, 0 — всем
  int battery = 0;   // заряд, %, только для DATA
  int neighbor = 0;  // с кем состыкован отправитель, только для DATA
  bool near = false; // HELLO: «мой датчик видит кого-то вплотную»
  Role role = Role::Builder;  // HELLO: кто я
  bool on = false;   // STORM: буря началась (true) или кончилась (false)
};

// Цвет светодиода — прямо из таблицы состояний.
enum class Color : uint8_t { Off, White, Blue, Yellow, Green, Red, Violet, Orange };

struct Led {
  Color color = Color::Off;
  bool blink = false;  // мигать раз в ~0,8 с
  bool slow = false;   // мигать редко (буря: экономим энергию)
};

// Всё, что модуль «видит» в момент такта.
struct Inputs {
  uint32_t now = 0;         // мс с запуска (millis())
  bool sensorsOk = true;    // самопроверка датчиков прошла
  bool netReady = true;     // есть Wi-Fi и MQTT (или Serial-режим)
  float distanceCm = 400;   // HC-SR04: расстояние до соседа
  bool contact = false;     // датчик касания (кнопка «Контакт»)
  bool fault = false;       // кнопка «Авария»
  int battery = 100;        // собственный заряд, %
  bool stormSensor = false; // свой датчик пыли/ветра видит бурю
};

// Тайминги и пороги. Значения подобраны так, чтобы демо укладывалось в 3 минуты.
struct Config {
  uint32_t helloPeriodMs = 1000;    // как часто кричать HELLO в поиске
  uint32_t peerTtlMs = 3000;        // сколько помним услышанного соседа
  float dockDistanceCm = 20;        // ближе — можно стыковаться
  uint32_t dockTimeoutMs = 5000;    // ждём DOCK_ACK не дольше
  uint32_t contactTimeoutMs = 15000;// ждём касания после согласия
  uint32_t dataPeriodMs = 1000;     // как часто слать DATA в связи
  uint32_t linkTimeoutMs = 4000;    // молчание соседа = разрыв
  uint32_t faultPauseMs = 3000;     // пауза после аварии
  int lowBatteryPct = 25;           // ниже — бросаем всё и ищем энергомодуль
  int chargedPct = 90;              // выше — зарядка закончена
  uint32_t stormRepeatMs = 2000;    // разведчик повторяет STORM, пока буря идёт
  uint32_t stormHoldMs = 10000;     // без повторов буря считается законченной
};

const char* stateName(State s);
const char* msgTypeName(MsgType t);
MsgType msgTypeFromName(const char* name);
const char* roleName(Role r);
bool roleFromName(const char* name, Role& out);
Led ledFor(State s);

// ---------------------------------------------------------------------------
// Модуль
// ---------------------------------------------------------------------------

class SwarmModule {
 public:
  explicit SwarmModule(int id, Role role = Role::Builder, Config cfg = Config());

  // Вызывать каждый проход loop(). Никаких delay() — только сравнение времени.
  void tick(const Inputs& in);

  // Вызывать на каждое принятое сообщение (чужие и свои — фильтр внутри).
  void onMessage(const Message& m, uint32_t now);

  // Очередь исходящих: main.cpp забирает сообщения и публикует их в MQTT.
  bool popOutgoing(Message& out);

  int id() const { return id_; }
  Role role() const { return role_; }
  // Роль текущего соседа (известна из его HELLO).
  Role neighborRole() const { return neighborRole_; }
  State state() const { return state_; }
  int neighbor() const { return neighbor_; }
  bool latchClosed() const { return latchClosed_; }
  int neighborBattery() const { return neighborBattery_; }
  uint32_t stateSince() const { return stateSince_; }
  Led led() const { return ledFor(state_); }
  // Буря сейчас идёт (по своему датчику или по сообщению разведчика).
  bool stormActive() const { return stormActive_; }

  // Счётчик переходов — удобно для отладки и тестов.
  uint32_t transitions() const { return transitions_; }

 private:
  void enter(State s, uint32_t now);
  void send(MsgType type, int to, const Inputs* in = nullptr);
  void searchStep(uint32_t now, bool powerOnly);
  bool attached() const {
    return state_ == State::Linked || state_ == State::Charging;
  }

  int id_;
  Role role_;
  Config cfg_;
  State state_ = State::Boot;
  uint32_t stateSince_ = 0;
  uint32_t transitions_ = 0;

  // Поиск: последний сосед, который сказал, что он рядом.
  // Датчик расстояния не знает, КТО перед ним, поэтому стыкуемся только
  // с тем, чей датчик тоже видит соседа вплотную (флаг near в HELLO).
  int heardPeer_ = 0;
  Role heardRole_ = Role::Builder;
  Role neighborRole_ = Role::Builder;
  uint32_t heardAt_ = 0;
  uint32_t lastHello_ = 0;
  bool helloSent_ = false;
  bool near_ = false;

  // Стыковка и связь.
  int neighbor_ = 0;
  bool agreed_ = false;
  uint32_t agreedAt_ = 0;
  bool latchClosed_ = false;
  uint32_t lastData_ = 0;
  uint32_t lastPeerData_ = 0;
  int neighborBattery_ = -1;
  bool seekingPower_ = false;  // эта стыковка — ради зарядки

  // Буря.
  bool stormActive_ = false;
  bool stormSensorWas_ = false;
  uint32_t stormHeardAt_ = 0;
  bool stormHeard_ = false;
  uint32_t lastStormSent_ = 0;
  State resumeState_ = State::Search;  // куда вернуться после бури

  // Кольцевая очередь исходящих.
  static const int kQueue = 8;
  Message out_[kQueue];
  int head_ = 0;
  int count_ = 0;

  bool faultWasPressed_ = false;
};

// ---------------------------------------------------------------------------
// Реализация (header-only: удобно копировать в проект Wokwi одним файлом)
// ---------------------------------------------------------------------------

inline const char* stateName(State s) {
  switch (s) {
    case State::Boot:     return "BOOT";
    case State::Search:   return "SEARCH";
    case State::Docking:  return "DOCKING";
    case State::Linked:   return "LINKED";
    case State::Fault:    return "FAULT";
    case State::Storm:    return "STORM";
    case State::LowPower: return "LOW_POWER";
    case State::Charging: return "CHARGING";
  }
  return "?";
}

inline const char* msgTypeName(MsgType t) {
  switch (t) {
    case MsgType::Hello:   return "HELLO";
    case MsgType::DockReq: return "DOCK_REQ";
    case MsgType::DockAck: return "DOCK_ACK";
    case MsgType::Data:    return "DATA";
    case MsgType::Bye:     return "BYE";
    case MsgType::Storm:   return "STORM";
    default:               return "NONE";
  }
}

inline bool starysStrEq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}

inline MsgType msgTypeFromName(const char* name) {
  if (starysStrEq(name, "HELLO"))    return MsgType::Hello;
  if (starysStrEq(name, "DOCK_REQ")) return MsgType::DockReq;
  if (starysStrEq(name, "DOCK_ACK")) return MsgType::DockAck;
  if (starysStrEq(name, "DATA"))     return MsgType::Data;
  if (starysStrEq(name, "BYE"))      return MsgType::Bye;
  if (starysStrEq(name, "STORM"))    return MsgType::Storm;
  return MsgType::None;
}

inline const char* roleName(Role r) {
  switch (r) {
    case Role::Builder:     return "builder";
    case Role::Power:       return "power";
    case Role::Scout:       return "scout";
    case Role::Relay:       return "relay";
    case Role::Coordinator: return "coordinator";
    case Role::Anchor:      return "anchor";
  }
  return "builder";
}

inline bool roleFromName(const char* name, Role& out) {
  const Role all[] = {Role::Builder, Role::Power, Role::Scout,
                      Role::Relay, Role::Coordinator, Role::Anchor};
  for (Role r : all)
    if (starysStrEq(name, roleName(r))) { out = r; return true; }
  return false;
}

inline Led ledFor(State s) {
  Led l;
  switch (s) {
    case State::Boot:     l.color = Color::White;  break;
    case State::Search:   l.color = Color::Blue;   l.blink = true; break;
    case State::Docking:  l.color = Color::Yellow; break;
    case State::Linked:   l.color = Color::Green;  break;
    case State::Fault:    l.color = Color::Red;    break;
    case State::Storm:    l.color = Color::Violet; l.blink = true; l.slow = true; break;
    case State::LowPower: l.color = Color::Orange; break;
    case State::Charging: l.color = Color::Orange; l.blink = true; break;
  }
  return l;
}

inline SwarmModule::SwarmModule(int id, Role role, Config cfg)
    : id_(id), role_(role), cfg_(cfg) {}

inline void SwarmModule::send(MsgType type, int to, const Inputs* in) {
  if (count_ == kQueue) {  // переполнение: выбрасываем самое старое
    head_ = (head_ + 1) % kQueue;
    --count_;
  }
  Message& m = out_[(head_ + count_) % kQueue];
  m = Message();
  m.type = type;
  m.from = id_;
  m.to = to;
  if (in) m.battery = in->battery;
  m.neighbor = neighbor_;
  m.near = near_;
  m.role = role_;
  m.on = stormActive_;
  ++count_;
}

inline bool SwarmModule::popOutgoing(Message& out) {
  if (count_ == 0) return false;
  out = out_[head_];
  head_ = (head_ + 1) % kQueue;
  --count_;
  return true;
}

inline void SwarmModule::enter(State s, uint32_t now) {
  // Защёлку открываем при уходе из стыковки или связи.
  // Исключение — буря: модуль остаётся сцеплен с соседом и пережидает.
  const bool wasAttached = state_ == State::Linked || state_ == State::Docking ||
                           state_ == State::Charging;
  const bool stormHold = s == State::Storm ||
                         (state_ == State::Storm && (s == State::Linked || s == State::Charging));
  if (wasAttached && !stormHold) latchClosed_ = false;

  state_ = s;
  stateSince_ = now;
  ++transitions_;

  // Действия на входе в состояние.
  switch (s) {
    case State::Search:
    case State::LowPower:
      neighbor_ = 0;
      agreed_ = false;
      neighborBattery_ = -1;
      seekingPower_ = false;
      helloSent_ = false;  // сразу крикнуть HELLO
      break;
    case State::Docking:
      agreed_ = false;
      break;
    case State::Linked:
    case State::Charging:
      latchClosed_ = true;
      lastPeerData_ = now;
      lastData_ = 0;
      break;
    case State::Fault:
      latchClosed_ = false;
      send(MsgType::Bye, 0);
      neighbor_ = 0;
      seekingPower_ = false;
      break;
    case State::Storm:
    case State::Boot:
      break;
  }
}

inline void SwarmModule::onMessage(const Message& m, uint32_t now) {
  if (m.from == id_) return;                 // своё эхо из MQTT
  if (m.to != 0 && m.to != id_) return;      // адресовано не нам

  switch (m.type) {
    case MsgType::Hello:
      if (m.near) {
        heardPeer_ = m.from;
        heardRole_ = m.role;
        heardAt_ = now;
      } else if (m.from == heardPeer_) {
        heardPeer_ = 0;  // отъехал
      }
      break;

    case MsgType::DockReq: {
      // Модуль с низким зарядом стыкуется только с энергомодулем.
      const bool fromPower = m.from == heardPeer_ && heardRole_ == Role::Power;
      const bool canAccept = near_ && (state_ == State::Search ||
                                       (state_ == State::LowPower && fromPower));
      if (canAccept) {
        // Сосед заметил нас первым — соглашаемся.
        seekingPower_ = state_ == State::LowPower;
        neighbor_ = m.from;
        neighborRole_ = m.from == heardPeer_ ? heardRole_ : Role::Builder;
        enter(State::Docking, now);
        agreed_ = true;
        agreedAt_ = now;
        send(MsgType::DockAck, m.from);
      } else if (state_ == State::Docking && m.from == neighbor_) {
        // Встречный запрос: оба увидели друг друга одновременно.
        if (!agreed_) { agreed_ = true; agreedAt_ = now; }
        send(MsgType::DockAck, m.from);
      }
      break;
    }

    case MsgType::DockAck:
      if (state_ == State::Docking && m.from == neighbor_ && !agreed_) {
        agreed_ = true;
        agreedAt_ = now;
      }
      break;

    case MsgType::Data:
      if (m.from != neighbor_) break;
      if (state_ == State::Docking && agreed_) {
        // Сосед уже защёлкнулся — значит, касание есть и у нас.
        enter(seekingPower_ ? State::Charging : State::Linked, now);
      }
      if (attached()) {
        lastPeerData_ = now;
        neighborBattery_ = m.battery;
      }
      break;

    case MsgType::Bye:
      if (m.from == neighbor_ && (attached() || state_ == State::Docking)) {
        // Сосед ушёл — ищем снова (энергомодуль, если ехали заряжаться).
        enter(seekingPower_ ? State::LowPower : State::Search, now);
      }
      if (m.from == heardPeer_) heardPeer_ = 0;
      break;

    case MsgType::Storm:
      stormHeard_ = m.on;
      stormHeardAt_ = now;
      break;

    case MsgType::None:
      break;
  }
}

// Общий шаг поиска для SEARCH и LOW_POWER: кричать HELLO и звать соседа.
inline void SwarmModule::searchStep(uint32_t now, bool powerOnly) {
  if (!helloSent_ || now - lastHello_ >= cfg_.helloPeriodMs) {
    send(MsgType::Hello, 0);
    lastHello_ = now;
    helloSent_ = true;
  }
  const bool peerFresh = heardPeer_ != 0 && now - heardAt_ <= cfg_.peerTtlMs;
  const bool suitable = !powerOnly || heardRole_ == Role::Power;
  if (peerFresh && near_ && suitable) {
    seekingPower_ = powerOnly;
    neighbor_ = heardPeer_;
    neighborRole_ = heardRole_;
    enter(State::Docking, now);
    send(MsgType::DockReq, neighbor_);
  }
}

inline void SwarmModule::tick(const Inputs& in) {
  const uint32_t now = in.now;

  // Флаг «рядом кто-то есть». Изменился — сразу сообщаем соседям.
  const bool nearNow = in.distanceCm <= cfg_.dockDistanceCm;
  if (nearNow != near_) {
    near_ = nearNow;
    helloSent_ = false;
  }

  // Буря: свой датчик или свежее сообщение STORM от разведчика.
  if (stormHeard_ && now - stormHeardAt_ > cfg_.stormHoldMs) stormHeard_ = false;
  stormActive_ = in.stormSensor || stormHeard_;
  // Разведчик предупреждает рой: сразу при смене и повторно, пока буря идёт.
  if (role_ == Role::Scout && state_ != State::Boot) {
    const bool edge = in.stormSensor != stormSensorWas_;
    if (edge || (in.stormSensor && now - lastStormSent_ >= cfg_.stormRepeatMs)) {
      stormActive_ = in.stormSensor;
      send(MsgType::Storm, 0);
      lastStormSent_ = now;
    }
  }
  stormSensorWas_ = in.stormSensor;

  // «Авария» срабатывает по нажатию (фронт), из любого рабочего состояния.
  const bool faultEdge = in.fault && !faultWasPressed_;
  faultWasPressed_ = in.fault;
  if (faultEdge && state_ != State::Boot && state_ != State::Fault) {
    enter(State::Fault, now);
    return;
  }

  // Начало бури — из любого рабочего состояния, кроме аварии.
  if (stormActive_ && state_ != State::Boot && state_ != State::Fault &&
      state_ != State::Storm) {
    resumeState_ = attached() ? state_ : State::Search;
    enter(State::Storm, now);
    return;
  }

  switch (state_) {
    case State::Boot:
      if (in.sensorsOk && in.netReady) enter(State::Search, now);
      break;

    case State::Search:
      if (role_ != Role::Power && in.battery < cfg_.lowBatteryPct) {
        enter(State::LowPower, now);
        break;
      }
      searchStep(now, false);
      break;

    case State::LowPower:
      searchStep(now, true);
      break;

    case State::Docking: {
      const State back = seekingPower_ ? State::LowPower : State::Search;
      if (!agreed_) {
        if (now - stateSince_ >= cfg_.dockTimeoutMs) enter(back, now);
      } else if (in.contact) {
        // Касание — закрываем защёлку.
        enter(seekingPower_ ? State::Charging : State::Linked, now);
      } else if (now - agreedAt_ >= cfg_.contactTimeoutMs) {
        send(MsgType::Bye, neighbor_);
        enter(back, now);
      }
      break;
    }

    case State::Linked:
    case State::Charging:
      if (lastData_ == 0 || now - lastData_ >= cfg_.dataPeriodMs) {
        send(MsgType::Data, neighbor_, &in);
        lastData_ = now == 0 ? 1 : now;
      }
      if (now - lastPeerData_ >= cfg_.linkTimeoutMs) {
        // Сосед замолчал — разрыв.
        enter(state_ == State::Charging ? State::LowPower : State::Search, now);
      } else if (state_ == State::Charging && in.battery >= cfg_.chargedPct) {
        // Заряжены — отцепляемся и возвращаемся к работе.
        send(MsgType::Bye, neighbor_);
        enter(State::Search, now);
      }
      break;

    case State::Fault:
      if (now - stateSince_ >= cfg_.faultPauseMs) enter(State::Search, now);
      break;

    case State::Storm:
      // Радиомолчание: ничего не шлём, ждём конца бури.
      if (!stormActive_) {
        // Защёлка закрыта — значит, сосед никуда не делся: возвращаемся к нему.
        if (resumeState_ == State::Linked || resumeState_ == State::Charging)
          enter(resumeState_, now);
        else
          enter(State::Search, now);
      }
      break;
  }
}

}  // namespace starys

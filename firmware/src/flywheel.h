// STARYS — расчёт маховика для переката и прыжка.
//
// Модуль перекатывается через ребро так: маховик внутри раскручивается,
// храповый тормоз резко его останавливает, и момент импульса маховика
// переходит в поворот всего куба вокруг нижнего ребра (как MIT M-Blocks).
//
// Сила толчка зависит от гравитации, поэтому модуль меряет g своим
// акселерометром и считает нужные обороты сам: один и тот же код
// работает на Земле, на Марсе и в симуляции. Расчёт без потерь на трение
// и упругость, поэтому в прошивке к нему добавляется запас (margin).
#pragma once

#include <math.h>

namespace starys {

struct FlywheelSpec {
  float cubeMassKg = 0.45f;     // масса модуля целиком
  float cubeEdgeM = 0.08f;      // ребро куба
  float wheelMassKg = 0.04f;    // маховик: ≈ 23 г PETG + 8 стальных шариков по 2 г
  float wheelRadiusM = 0.03f;   // Ø60 мм
  float maxRpm = 12000.0f;      // предел мотора с маховиком
};

// Момент инерции маховика (сплошной диск): I = m·r² / 2.
inline float wheelInertia(const FlywheelSpec& s) {
  return 0.5f * s.wheelMassKg * s.wheelRadiusM * s.wheelRadiusM;
}

// Момент инерции куба относительно ребра: I = 2/3 · m · a².
inline float cubeEdgeInertia(const FlywheelSpec& s) {
  return 2.0f / 3.0f * s.cubeMassKg * s.cubeEdgeM * s.cubeEdgeM;
}

// Энергия, чтобы перевалить куб через ребро: центр масс поднимается
// с a/2 до a/√2, то есть на Δh = a·(√2 − 1)/2.
inline float pivotEnergy(const FlywheelSpec& s, float g) {
  const float dh = s.cubeEdgeM * (sqrtf(2.0f) - 1.0f) / 2.0f;
  return s.cubeMassKg * g * dh;
}

// Обороты маховика (об/мин), при которых торможение перекатит куб.
// L = I_маховика · ω_маховика переходит в I_куба · ω_куба,
// а E = I_куба · ω_куба² / 2 должна покрыть pivotEnergy.
inline float rpmForPivot(const FlywheelSpec& s, float g, float margin = 1.3f) {
  const float wCube = sqrtf(2.0f * pivotEnergy(s, g) * margin / cubeEdgeInertia(s));
  const float wWheel = cubeEdgeInertia(s) * wCube / wheelInertia(s);
  return wWheel * 60.0f / (2.0f * 3.14159265f);
}

// Можно ли перекатиться при данной гравитации с этим мотором.
inline bool pivotPossible(const FlywheelSpec& s, float g, float margin = 1.3f) {
  return rpmForPivot(s, g, margin) <= s.maxRpm;
}

}  // namespace starys

// STARYS — маховик с храповым тормозом, прототип v0.2.
//
// Заменяет идею «8 соленоидов по углам»: один механизм внутри закрытого
// корпуса, без щелей для пыли.
//
// Как работает:
//   1. Коллекторный мотор 8520 (от микродрона) раскручивает маховик.
//   2. Сервопривод SG90 опускает собачку на зубья обода.
//   3. Маховик останавливается за доли секунды, его момент импульса
//      переходит в поворот всего куба вокруг нижнего ребра — перекат.
//   4. Больше оборотов — больше энергии: куб не только перекатывается,
//      но и подпрыгивает (на Марсе это в 2,6 раза выше, чем на Земле).
//
// Нужные обороты модуль считает сам по измеренному g:
// firmware/src/flywheel.h → rpmForPivot(). При этих размерах (маховик ≈ 40 г):
//   без запаса   — Земля ≈ 8900, Марс ≈ 5500, Луна ≈ 3600 об/мин;
//   с запасом 30 % — Земля ≈ 10100, Марс ≈ 6200, Луна ≈ 4100 об/мин.
// Мотор 8520 без нагрузки даёт десятки тысяч об/мин, так что запас есть.
//
// Печать: PETG, заполнение 100 %. В гнёзда обода вклеить 8 стальных
// шариков Ø8 мм (≈ 2 г каждый): масса уходит на край, инерция растёт.

/* [Маховик] */
wheel_d = 60;        // диаметр
wheel_t = 8;         // толщина
teeth = 12;          // зубья храповика по ободу
tooth_h = 2.5;       // высота зуба
ball_d = 8.2;        // гнёзда под стальные шарики Ø8
balls = 8;
shaft_d = 0.9;       // вал мотора 8520 (~1 мм), посадка с натягом
hub_d = 7;
hub_h = 4;

/* [Собачка тормоза] */
pawl_len = 18;
pawl_w = 5;
pawl_t = 4;

/* [Держатель мотора] */
motor_d = 8.6;       // корпус мотора 8520 — Ø8,5 мм
holder_h = 12;

/* [Кронштейн] */
axis_h = 37.6;       // высота оси над дном полости: центр куба (40 − стенка 2,4)
bracket_t = 3;

/* [Вид] */
part = "all";        // wheel | pawl | holder | bracket | all
$fn = 64;

module ratchet_profile() {
  // Пила: каждый зуб — пологий подъём и отвесный уступ, о который упирается собачка.
  r = wheel_d / 2;
  polygon([for (i = [0 : teeth - 1]) each [
    [ (r - tooth_h) * cos(i * 360 / teeth), (r - tooth_h) * sin(i * 360 / teeth) ],
    [ r * cos((i + 0.95) * 360 / teeth), r * sin((i + 0.95) * 360 / teeth) ],
  ]]);
}

module wheel() {
  difference() {
    union() {
      linear_extrude(wheel_t) ratchet_profile();
      translate([0, 0, wheel_t]) cylinder(d = hub_d, h = hub_h);
    }
    // гнёзда под шарики — как можно ближе к ободу
    for (i = [0 : balls - 1])
      rotate([0, 0, i * 360 / balls + 360 / balls / 2])
        translate([wheel_d / 2 - tooth_h - ball_d / 2 - 1.2, 0, wheel_t / 2])
          sphere(d = ball_d);
    // облегчение центра: вся масса должна быть на краю
    for (i = [0 : 3])
      rotate([0, 0, i * 90 + 45])
        translate([10, 0, -1]) cylinder(d = 9, h = wheel_t + 2);
    translate([0, 0, -1]) cylinder(d = shaft_d, h = wheel_t + hub_h + 2);
  }
}

module pawl() {
  // Надевается на качалку SG90; зуб входит в уступ храповика.
  difference() {
    hull() {
      cylinder(d = pawl_w + 3, h = pawl_t);
      translate([pawl_len, 0, 0]) cylinder(d = pawl_w, h = pawl_t);
    }
    translate([0, 0, -1]) cylinder(d = 4.8, h = pawl_t + 2);  // вал SG90
  }
  translate([pawl_len - 1, -pawl_w / 2, 0])
    linear_extrude(pawl_t) polygon([[0, 0], [4, 0], [1, -4]]);
}

module holder() {
  // Хомут для мотора, крепится двумя винтами M2 к стойкам в корпусе.
  difference() {
    union() {
      cylinder(d = motor_d + 4, h = holder_h);
      translate([-(motor_d + 14) / 2, -3, 0]) cube([motor_d + 14, 6, 3]);
    }
    translate([0, 0, 2]) cylinder(d = motor_d, h = holder_h);
    translate([0, 0, -1]) cylinder(d = 2.5, h = 4);  // выход вала
    for (x = [-(motor_d + 9) / 2, (motor_d + 9) / 2])
      translate([x, 0, -1]) cylinder(d = 2.2, h = 5);
  }
}

module bracket() {
  // Г-образный кронштейн: основание на дно корпуса (2 винта M3),
  // стойка держит хомут мотора и сервопривод собачки.
  // Положение сервопривода — прототип, подгоняется по месту.
  difference() {
    union() {
      translate([-15, 0, 0]) cube([50, 20, bracket_t]);
      translate([-15, 0, 0]) cube([50, bracket_t, axis_h + 12]);
    }
    for (x = [-8, 28]) translate([x, 12, -1]) cylinder(d = 3.4, h = bracket_t + 2);
    // ось мотора и винты хомута
    translate([0, -1, axis_h]) rotate([-90, 0, 0]) cylinder(d = 3, h = bracket_t + 2);
    for (dx = [-(motor_d + 9) / 2, (motor_d + 9) / 2])
      translate([dx, -1, axis_h]) rotate([-90, 0, 0]) cylinder(d = 2.2, h = bracket_t + 2);
    // окно под SG90 (23 × 12,5 мм): собачка на его валу достаёт до обода
    translate([20 - 11.6, -1, axis_h - 22 - 6.3]) cube([23.2, bracket_t + 2, 12.6]);
  }
}

if (part == "bracket") bracket();
if (part == "wheel") wheel();
if (part == "pawl") pawl();
if (part == "holder") holder();
if (part == "all") {
  wheel();
  translate([wheel_d / 2 + 12, 0, 0]) pawl();
  translate([0, wheel_d / 2 + 15, 0]) holder();
  translate([-wheel_d / 2 - 50, 0, 0]) bracket();
}

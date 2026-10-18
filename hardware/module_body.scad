// STARYS — корпус модуля роя, прототип v0.1 для 3D-печати (PLA/PETG).
//
// Куб с ребром 80 мм. Четыре боковые грани одинаковые — стыковочные:
//   * 4 гнезда под неодимовые магниты Ø10×3 мм (грубое выравнивание);
//   * окно под крюк защёлки, сервопривод SG90 крепится изнутри;
//   * пара отверстий под датчик HC-SR04 (на передней грани) или ИК-пару.
// Сверху — окно под светодиод и выход антенны, снизу — посадка шасси.
//
// Открыть в OpenSCAD (https://openscad.org), F5 — просмотр, F6 — рендер,
// File → Export → STL. Параметры ниже можно менять в Customizer.

/* [Размеры] */
edge = 80;          // ребро куба, мм
wall = 2.4;         // толщина стенки
corner_r = 4;       // скругление рёбер

/* [Магниты] */
magnet_d = 10.3;    // гнездо под магнит Ø10 (+зазор)
magnet_h = 3.2;
magnet_offset = 24; // от центра грани по диагонали

/* [Защёлка] */
latch_w = 10;       // окно под крюк
latch_h = 26;

/* [Датчики] */
sonar_d = 16.5;     // HC-SR04: два «глаза» Ø16, шаг 26 мм
sonar_pitch = 26;
led_d = 5.2;        // RGB-светодиод 5 мм

/* [Вид] */
show = "body";      // body | lid | both
$fn = 48;

module rounded_cube(size, r) {
  hull() for (x = [r, size - r], y = [r, size - r], z = [r, size - r])
    translate([x, y, z]) sphere(r);
}

// Вырезы одной стыковочной грани. Грань лежит в плоскости XZ (y = 0).
module dock_face_cuts(with_sonar = false) {
  c = edge / 2;
  // магниты в углах грани
  for (dx = [-1, 1], dz = [-1, 1])
    translate([c + dx * magnet_offset, -0.01, c + dz * magnet_offset])
      rotate([-90, 0, 0]) cylinder(d = magnet_d, h = magnet_h);
  // окно под крюк защёлки
  translate([c - latch_w / 2, -1, c - latch_h / 2])
    cube([latch_w, wall + 2, latch_h]);
  // датчик расстояния (только на передней грани)
  if (with_sonar)
    for (dx = [-sonar_pitch / 2, sonar_pitch / 2])
      translate([c + dx, -1, 14]) rotate([-90, 0, 0]) cylinder(d = sonar_d, h = wall + 2);
}

module body() {
  difference() {
    rounded_cube(edge, corner_r);
    // полость; крышка снимается сверху
    translate([wall, wall, wall]) cube([edge - 2 * wall, edge - 2 * wall, edge]);
    // четыре стыковочные грани
    dock_face_cuts(with_sonar = true);
    translate([edge, 0, 0]) rotate([0, 0, 90]) dock_face_cuts();
    translate([edge, edge, 0]) rotate([0, 0, 180]) dock_face_cuts();
    translate([0, edge, 0]) rotate([0, 0, 270]) dock_face_cuts();
    // дно: отверстия под винты шасси M3 и провода моторов
    for (x = [15, edge - 15], y = [15, edge - 15])
      translate([x, y, -1]) cylinder(d = 3.4, h = wall + 2);
    translate([edge / 2 - 8, edge / 2 - 4, -1]) cube([16, 8, wall + 2]);
  }
  // стойки под плату ESP32 DevKit (шаг 23 × 48 мм)
  for (x = [edge / 2 - 11.5, edge / 2 + 11.5], y = [edge / 2 - 24, edge / 2 + 24])
    translate([x, y, wall]) difference() {
      cylinder(d = 6, h = 6);
      cylinder(d = 2.5, h = 7);
    }
}

module lid() {
  difference() {
    union() {
      translate([0, 0, 0]) cube([edge, edge, wall]);
      // юбка, входит в полость с зазором 0.3 мм
      translate([wall + 0.3, wall + 0.3, -4])
        cube([edge - 2 * wall - 0.6, edge - 2 * wall - 0.6, 4]);
    }
    translate([wall + 1.5, wall + 1.5, -5])
      cube([edge - 2 * wall - 3, edge - 2 * wall - 3, 5]);
    // светодиод и антенна
    translate([edge / 2, edge / 2 - 14, -1]) cylinder(d = led_d, h = wall + 2);
    translate([edge / 2, edge / 2 + 14, -1]) cylinder(d = 3.5, h = wall + 2);
    // углубление под солнечную панель 60×60
    translate([10, 10, wall - 0.8]) cube([60, 60, 1]);
  }
}

if (show == "body" || show == "both") body();
if (show == "lid") lid();
if (show == "both") translate([0, 0, edge + 20]) lid();

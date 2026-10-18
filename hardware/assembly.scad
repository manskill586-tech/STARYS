// STARYS — сборка модуля v0.2 в разрезе: корпус, маховик, кронштейн, ESP32.
// Нужна, чтобы проверить, что детали не пересекаются. F5 — просмотр.
// cut = true — срезать переднюю половину корпуса, чтобы видеть начинку.

use <module_body.scad>
use <flywheel.scad>

edge = 80;
wall = 2.4;
axis_h = 37.6;
cut = true;

// корпус (в разрезе)
color("Gainsboro", 0.55) difference() {
  body();
  if (cut) translate([-1, -1, -1]) cube([edge + 2, edge / 2 - 14, edge + 2]);
}

// кронштейн маховика на дне
color("SteelBlue") translate([edge / 2, edge / 2, wall]) bracket();

// маховик перед стойкой кронштейна, ось вдоль Y
color("Orange") translate([edge / 2, edge / 2 - 13, wall + axis_h]) rotate([-90, 0, 0]) wheel();

// мотор 8520 за стойкой
color("DimGray") translate([edge / 2, edge / 2 + 3, wall + axis_h]) rotate([-90, 0, 0]) cylinder(d = 8.5, h = 20);

// сервопривод собачки (габарит SG90 23 × 12,5 × 22,5 мм) за стойкой
color("RoyalBlue") translate([edge / 2 + 20 - 11.5, edge / 2 + 3, wall + axis_h - 22 - 6.2]) cube([23, 22.5, 12.4]);

// плата ESP32 DevKit на левой стенке
color("SeaGreen") translate([wall + 6.05, edge / 2 + 6, wall + 9]) cube([1.6, 28, 54]);
color("Silver") translate([wall + 7.65, edge / 2 + 11, wall + 40]) cube([3.2, 18, 20]);

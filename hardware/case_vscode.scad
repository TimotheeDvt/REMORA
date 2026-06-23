// PARAMETERS
staff_diameter  = 40;       // Diameter of the staff in mm
case_length     = 180;
case_width      = 35;
case_height     = 30;
wall_thickness  = 0.06;
strap_width     = 8;        // Width of the strap
strap_thickness = 2;        // Thickness of the strap
screw_d         = 1.5;      // Diameter of the screw holes

// STL IMPORT POSITIONS
esp32_file  = "Sparkfun Thing Plus v8.stl";
esp32_pos   = [-65, 11, 2];
esp32_rot   = [90, 0, 270];

mpu_file    = "MPU-9250.stl";
mpu_pos     = [50, 7, 8];
mpu_rot     = [90, 0, 90];

battery_file= "2xAAA BATTERY HOLDER.stl";
battery_pos = [23, 0, 4];
battery_rot = [180, 180, 0];

$fn = 100;

// IMPORT GHOSTS
% color("green") translate(esp32_pos)   rotate(esp32_rot)   import(esp32_file);
% color("green") translate(mpu_pos)     rotate(mpu_rot)     import(mpu_file);
% color("green") translate(battery_pos) rotate(battery_rot) import(battery_file);

// CASING
difference() {
    union() {
        // Floor
        intersection() {
            scale([case_length/2, case_width/2, case_height]) {
                sphere(r = 1 - wall_thickness);
            }
            translate([-case_length/2, -case_width/2, 1]) {
                cube([case_length, case_width, strap_thickness]);
            }
        }

        // Walls
        difference() {
            // Outer Dome Shape
            scale([case_length/2, case_width/2, case_height]) {
                sphere(r = 1);
            }
            // Inner Hollow Wall Pocket
            scale([case_length/2, case_width/2, case_height]) {
                sphere(r = 1 - wall_thickness);
            }
        }
    }

    // Holes
    // Staff
    rotate([0, 90, 0]) {
        translate([20, 0, -case_length/2 - 15]) {
            cylinder(r = staff_diameter/2, h = case_length + 30);
        }
    }

    // Strap Slots
    strap_offset_x = case_length / 9 * 2.5;

    translate([strap_offset_x, -case_width/2, case_height/2])
        cube([strap_width, case_width, strap_thickness]); // Front Slot

    translate([-strap_offset_x, -case_width/2, case_height/2])
        cube([strap_width, case_width, strap_thickness]); // Back Slot

    translate([-strap_width/2, -case_width/2, case_height/3*2])
        cube([strap_width, case_width, strap_thickness]); // Center Slot

    // USB-C Slot
    translate([-case_length/2 + 2.5, -strap_width/2 - 1, 8])
        cube([20, strap_width + 2, strap_thickness * 2]);

    // Screws Holes
    // ESP32
    translate([-61.2, -9.3, -1]) cylinder(r = screw_d, h = 10);
    translate([-61.2, 8.6, -1])  cylinder(r = screw_d, h = 10);

    // MPU
    translate([52.2, -6, -1]) cylinder(r = screw_d, h = 10);
    translate([72.8, -6, -1]) cylinder(r = screw_d, h = 10);

    // Battery
    translate([8, 2.35, -1])    cylinder(r = screw_d, h = 10);
    translate([23, 0, -1])       cylinder(r = screw_d, h = 10);
    translate([38, -2.35, -1])   cylinder(r = screw_d, h = 10);
}
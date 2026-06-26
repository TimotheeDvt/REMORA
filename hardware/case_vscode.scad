// PARAMETERS
staff_diameter  = 30;       // Diameter of the staff in mm
case_length     = 180;
case_width      = 35;
case_height     = 30;
wall_thickness  = 0.06;
strap_width     = 7;        // Width of the strap
strap_thickness = 2.2;        // Thickness of the strap
screw_d         = 1.5;      // Diameter of the screw holes
screw_head_d    = screw_d * 2;

// RENDER MODE
// Change this to change what you see or export:
// "both" -> Visual preview of the closed box
// "base" -> Render only the bottom piece to print it
// "cap"  -> Render only the top lid to print it
render_mode = "both";

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

staff_pos = [20, 0, -case_length/2 - 15];
staff_rot = [0, 90, 0];

$fn = 100;

// IMPORT GHOSTS
if (render_mode == "both") {
    % color("green", 0.3) translate(esp32_pos)   rotate(esp32_rot)   import(esp32_file);
    % color("green", 0.3) translate(mpu_pos)     rotate(mpu_rot)     import(mpu_file);
    % color("green", 0.3) translate(battery_pos) rotate(battery_rot) import(battery_file);
    % color("green", 0.3) rotate(staff_rot) translate(staff_pos) cylinder(r = staff_diameter/2, h = case_length + 30);
}

// FULL GEOMETRY MODULE
module raw_uncut_case() {
    union() {
        // Floor
        intersection() {
            scale([case_length/2, case_width/2, case_height]) {
                sphere(r = 1 - wall_thickness);
            }
            translate([-case_length/2, -case_width/2, 1]) {
                cube([case_length, case_width, strap_thickness-1]);
            }
        }

        // Walls
        difference() {
            scale([case_length/2, case_width/2, case_height]) {
                sphere(r = 1);
            }
            scale([case_length/2, case_width/2, case_height]) {
                sphere(r = 1 - wall_thickness);
            }
        }
    }
}

// HOLES MODULE
module standard_hole_cuts() {
    // Staff
    rotate([0, 90, 0]) {
        translate([20, 0, -case_length/2 - 15]) {
            cylinder(r = staff_diameter/2, h = case_length + 30);
        }
    }

    // Strap Slots
    strap_offset_x = case_length / 9 * 2.5;

    translate([strap_offset_x, -case_width/2, case_height/2])
        cube([strap_width, case_width, strap_thickness]);

    translate([-strap_offset_x, -case_width/2, case_height/2])
        cube([strap_width, case_width, strap_thickness]);

    translate([-strap_width/2, -case_width/2, case_height/3*2])
        cube([strap_width, case_width, strap_thickness]);

    // Screws Holes
    // ESP
    translate([-61.2, -9.3, -5]) cylinder(r = screw_d, h = 30);
    translate([-61.2, 8.6, -5])  cylinder(r = screw_d, h = 30);
    // MPU
    translate([52.2, -6, -5])    cylinder(r = screw_d, h = 10);
    translate([72.8, -6, -5])    cylinder(r = screw_d, h = 30);
    // Battery
    translate([8, 2.35, -5])     cylinder(r = screw_d, h = 20);
    translate([23, 0, -5])       cylinder(r = screw_d, h = 20);
    translate([38, -2.35, -5])   cylinder(r = screw_d, h = 20);

    translate([-61.2, -9.3, 13]) cylinder(r = screw_head_d, h = 9);
    translate([-61.2, 8.6, 13])  cylinder(r = screw_head_d, h = 8);
    translate([72.8, -6, 13])    cylinder(r = screw_head_d, h = 9);
}

module screw_reinforcements() {
    intersection() {
        scale([case_length/2, case_width/2, case_height]) {
            sphere(r = 1);
        }

        union() {
            translate([-61.2, -9.3, 4]) cylinder(r = screw_head_d + 1.5, h = 25);
            translate([-61.2, 8.6, 13])  cylinder(r = screw_head_d + 1.5, h = 8);
            translate([72.8, -6, 4])    cylinder(r = screw_head_d + 1.5, h = 25);
        }
    }

    translate([-61.2, -9.3, 4]) cylinder(r = screw_head_d, h = 9);
    translate([-61.2, 8.6, 9])  cylinder(r = screw_head_d, h = 4);
    translate([72.8, -6, 4])    cylinder(r = screw_head_d, h = 9);
}


// GENERATING THE PIECES

// Base
if (render_mode == "base" || render_mode == "both") {
    color("blue") difference() {
        intersection() {
            raw_uncut_case();
            // Bounding block to isolate the lower part
            translate([-case_length/2 - 5, -case_width/2 - 5, -case_height])
                cube([case_length + 10, case_width + 10, case_height + strap_thickness + 1]);
        }
        standard_hole_cuts();
    }
}

// Cap
if (render_mode == "cap" || render_mode == "both") {
    // Slightly lifts the cap in "both" preview mode to see inside
    preview_lift = (render_mode == "both") ? 15 : 0;

    translate([0, 0, preview_lift]) difference() {
        union() {
            intersection() {
                raw_uncut_case();
                // Bounding block to isolate the upper part
                translate([-case_length/2 - 5, -case_width/2 - 5, strap_thickness + 1])
                    cube([case_length + 10, case_width + 10, case_height]);
            }
            screw_reinforcements();
        }

        standard_hole_cuts();

        // USB-C Slot
        translate([-case_length/2 + 2.5, -strap_width/2 - 1, 8])
            cube([20, strap_width + 2, strap_thickness * 2]);
    }
}
preview_lift_cap = 15;
preview_cut = 0;
render_mode = "all"; // ["all", "cap", "base", "none"]
show_all_ghosts           = false; // when checked, shows every ghost regardless of the checkboxes below
show_ghost_esp32          = true;
show_ghost_mpu            = true;
show_ghost_battery        = true;
show_ghost_staff          = true;
show_ghost_button         = true;

$fn = 100;
module __Customizer_Limit__ () {}
staff_diameter  = 30;       // Diameter of the staff in mm
case_length     = 200;
case_width      = 54;
case_height     = 30;
wall_thickness  = 0.06;
strap_width     = 7;        // Width of the strap
strap_thickness = 2.2;        // Thickness of the strap
screw_d         = 1.5;      // Diameter of the screw holes
screw_head_d    = screw_d * 2 + 1;

// STL IMPORT POSITIONS
esp32_file  = "Assets/Sparkfun Thing Plus v8.stl";
esp32_pos   = [-85, 11.5, -2];
esp32_rot   = [90, 0, 270];
// Raw STL bounding box (native axes) is 22.86 x 15.59 x 59.06mm;
// rescale to the real board size (23.5 x 65mm) without touching the file.
esp32_target_width  = 23.5;
esp32_target_length = 65;
esp32_raw_width     = 22.860000610351562; // native X, becomes case-width axis after rotation
esp32_raw_length    = 59.06365966796875;  // native Z, becomes case-length axis after rotation
esp32_scale = [esp32_target_width / esp32_raw_width, 1, esp32_target_length / esp32_raw_length];
esp32_screw_x = -80.8;
esp32_screw_y_1 = -9.5;
esp32_screw_y_2 = 8.8;

mpu_file    = "Assets/MPU-9250.stl";
mpu_pos     = [57, 7, 3];
mpu_rot     = [90, 0, 90];
mpu_screw_y = -6;
mpu_screw_x_1 = 59;
mpu_screw_x_2 = 80;

staff_pos = [20, 0, -case_length/2 - 50];
staff_rot = [0, 90, 0];

button_pos = [-30-22.5, -8, 13];
button_rot = [0, 0, 0];

// LIPO BATTERY BOX (ghost) + CORNER SUPPORTS
battery_box_pos      = [18, 0, 7];
battery_box_size     = [60, 40, 6.5];
battery_box_corner_r = 3;

battery_support_leg       = 10;
battery_support_wall      = 1.5;
battery_support_clearance = 0.3;
battery_support_height    = 5;

battery_strap_slot_x_width = 15;
battery_strap_slot_y_width = 2.5;
battery_strap_x = battery_box_pos[0] - battery_strap_slot_x_width/2;
battery_strap_y = [
    battery_box_pos[1] - battery_box_size[1]/2 - 2.7,
    battery_box_pos[1] + battery_box_size[1]/2 + 0.3
];

module rounded_rect_2d(half_x, half_y, r) {
    hull() {
        for (sx = [-1, 1])
            for (sy = [-1, 1])
                translate([sx * (half_x - r), sy * (half_y - r)])
                    circle(r = r);
    }
}

module lipo_battery_ghost(size = battery_box_size, corner_r = battery_box_corner_r) {
    linear_extrude(height = size[2], center = true)
        rounded_rect_2d(size[0]/2, size[1]/2, corner_r);
}

module battery_corner_supports() {
    inner_half_x = battery_box_size[0]/2 + battery_support_clearance;
    inner_half_y = battery_box_size[1]/2 + battery_support_clearance;
    outer_half_x = inner_half_x + battery_support_wall;
    outer_half_y = inner_half_y + battery_support_wall;
    inner_r = battery_box_corner_r + battery_support_clearance;
    outer_r = inner_r + battery_support_wall;
    leg = battery_support_leg;

    floor_overlap = 0.4;

    translate([battery_box_pos[0], battery_box_pos[1], strap_thickness - floor_overlap]) {
        linear_extrude(height = battery_support_height + floor_overlap) {
            intersection() {
                difference() {
                    rounded_rect_2d(outer_half_x, outer_half_y, outer_r);
                    rounded_rect_2d(inner_half_x, inner_half_y, inner_r);
                }
                union() {
                    for (sy = [-1, 1])
                        translate([-1 * (outer_half_x - leg/2), sy * (outer_half_y - leg/2)])
                            square([leg, leg], center = true);
                }
            }
        }
        translate([outer_half_x - 1.5, -outer_half_y + 6])
            cube([battery_support_wall, 32, strap_thickness*2 + floor_overlap]);
    }
}

// IMPORT GHOSTS
if (show_all_ghosts || show_ghost_esp32)
    % color("green", 0.3) translate(esp32_pos)   rotate(esp32_rot)   scale(esp32_scale) import(esp32_file);
if (show_all_ghosts || show_ghost_mpu)
    % color("green", 0.3) translate(mpu_pos)     rotate(mpu_rot)     import(mpu_file);
if (show_all_ghosts || show_ghost_battery)
    % color("green", 0.3) translate(battery_box_pos) lipo_battery_ghost();
if (show_all_ghosts || show_ghost_staff)
    % color("green", 0.3) rotate(staff_rot) translate(staff_pos) cylinder(r = staff_diameter/2, h = case_length + 100);
if (show_all_ghosts || show_ghost_button)
    % color("green", 0.3) translate(button_pos) rotate(button_rot) button_cap();

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

// BUTTON MODULE
module button_cap() {
    difference() {
        minkowski() {
            intersection() {
                translate([1.5, 1.25, 1.5])
                cube([45 - 3, 16.5 - 3, 13 - 3]);

                translate([22.5, 8, -178])
                sphere(r = 190 - 1.5);
            }
            sphere(r = 1.5);
        }
        translate([22.5, 8, 11.5]) cylinder(r = 4.5, h = 3);
    }
    translate([22.5, 8, 11.2]) cylinder(r = 4.25, h = 0.8);
    translate([22.5, 8, 12]) cylinder(r = 2.25, h = 2.5);
}

module button_cap_boss(margin = 2) {
    minkowski() {
        intersection() {
            translate([1.5, 1.25, 1.5])
            cube([45 - 3, 16.5 - 3, 13 - 3]);

            translate([22.5, 8, -178])
            sphere(r = 190 - 1.5);
        }
        sphere(r = 1.5 + margin);
    }
}

module button_socket(clearance = 0.5, side_relief = 3) {
    union() {
        minkowski() {
            intersection() {
                translate([1.5, 1.25, 1.5])
                cube([45 - 3, 16.5 - 3, 13 - 3]);

                translate([22.5, 8, -178])
                sphere(r = 190 - 1.5);
            }
            sphere(r = 1.5 + clearance);
        }

        translate([-side_relief, -side_relief, -4])
            cube([45 + 2 * side_relief, 16 + 2 * side_relief, 8]);
    }
}

// HOLES MODULE
module standard_hole_cuts(draw_button_shere = false) {
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

    translate([-case_length/4 + 16, -case_width/2, case_height/2])
        cube([strap_width, case_width, strap_thickness]);

    translate([-strap_width/2, -case_width/2, case_height/3*2])
        cube([strap_width, case_width, strap_thickness]);

    // Screws Holes
    // ESP
    translate([esp32_screw_x, esp32_screw_y_1, -5]) cylinder(r = screw_d, h = 30);
    translate([esp32_screw_x, esp32_screw_y_2, -5])  cylinder(r = screw_d, h = 30);
    // MPU
    translate([mpu_screw_x_1, mpu_screw_y, -5])    cylinder(r = screw_d, h = 20);
    translate([mpu_screw_x_2, mpu_screw_y, -5])    cylinder(r = screw_d, h = 30);
    // Battery strap slots (through the floor, either side of the battery)
    for (y = battery_strap_y)
        translate([battery_strap_x, y, -1])
            cube([battery_strap_slot_x_width, battery_strap_slot_y_width, 12]);

    translate([esp32_screw_x, esp32_screw_y_1, 13]) cylinder(r = screw_head_d, h = 9);
    translate([esp32_screw_x, esp32_screw_y_2, 13])  cylinder(r = screw_head_d, h = 8);
    translate([mpu_screw_x_2, mpu_screw_y, 12])    cylinder(r = screw_head_d+1, h = 100);

    // button
    translate([button_pos[0]+22.5, 0, 16])    cylinder(r = screw_head_d + 1, h = 15);

    // Cable pass-through
    translate([button_pos[0]-3, -3.5, 16]) cube([3, 7, 5]);
    translate([-8,  -3.5, 16]) cube([3, 7, 5]);

    translate(button_pos) rotate(button_rot) button_socket();
}

module screw_reinforcements() {
    union() {
        intersection() {
            scale([case_length/2, case_width/2, case_height]) {
                sphere(r = 1);
            }

            union() {
                translate([esp32_screw_x, esp32_screw_y_1, 13]) cylinder(r = screw_head_d + 1.5, h = 25);
                translate([esp32_screw_x, esp32_screw_y_2, 13])  cylinder(r = screw_head_d + 1.5, h = 8);
                translate([mpu_screw_x_2, mpu_screw_y, 9])    cylinder(r = screw_head_d + 1.5, h = 25);
            }
        }

        translate([esp32_screw_x, esp32_screw_y_1, 9]) cylinder(r = screw_head_d, h = 4);
        translate([esp32_screw_x, esp32_screw_y_2, 9])  cylinder(r = screw_head_d, h = 4);
    }
}


// GENERATING THE PIECES
show_base   = (render_mode == "base"   || render_mode == "all");
show_cap    = (render_mode == "cap"    || render_mode == "all");

// Base
if (show_base) {
    difference() {
        union() {
            intersection() {
                raw_uncut_case();
                // Bounding block to isolate the lower part
                translate([-case_length/2 - 5, -case_width/2 - 5, -case_height])
                    cube([case_length + 10, case_width + 10, case_height + strap_thickness + 1]);
            }
            battery_corner_supports();
        }
        standard_hole_cuts();
    }
}

// Cap
if (show_cap) {
    // Slightly lifts the cap in "all" preview mode to see inside
    preview_lift = (render_mode == "all") ? preview_lift_cap : 0;

    cut_width = (case_length + 10) * (100 - abs(preview_cut)) / 100;
    cut_x0 = (preview_cut >= 0)
        ? case_length/2 + 5 - cut_width
        : -case_length/2 - 5;

    translate([0, 0, preview_lift]) intersection() {
        difference() {
            union() {
                intersection() {
                    raw_uncut_case();
                    // Bounding block to isolate the upper part
                    translate([-case_length/2 - 5, -case_width/2 - 5, strap_thickness + 1])
                        cube([case_length + 10, case_width + 10, case_height]);
                }
                screw_reinforcements();
                translate(button_pos) rotate(button_rot) button_cap_boss(2);
            }

            standard_hole_cuts(true);

            // USB-C Slot
            translate([-case_length/2 + 0, -strap_width/2 - 3.5, 3])
                cube([20, strap_width + 7, 8.5]);
        }

        translate([cut_x0, -case_width/2 - 5, -case_height])
            cube([cut_width, case_width + 10, case_height * 2]);
    }
}
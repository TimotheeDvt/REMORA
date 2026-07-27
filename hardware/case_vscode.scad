preview_lift_cap = 15;
preview_cut = 0;
render_mode = "all"; // ["all", "cap", "base", "holder", "none"]
show_ghosts = true;
module __Customizer_Limit__ () {}
staff_diameter  = 30;       // Diameter of the staff in mm
case_length     = 200;
case_width      = 50;
case_height     = 30;
wall_thickness  = 0.06;
strap_width     = 7;        // Width of the strap
strap_thickness = 2.2;        // Thickness of the strap
screw_d         = 1.5;      // Diameter of the screw holes
screw_head_d    = screw_d * 2 + 1;

// STL IMPORT POSITIONS
esp32_file  = "Assets/Sparkfun Thing Plus v8.stl";
esp32_pos   = [-80, 11.5, -2];
esp32_rot   = [90, 0, 270];
esp32_screw_x = -76.2;
esp32_screw_y_1 = -9;
esp32_screw_y_2 = 8.6;

mpu_file    = "Assets/MPU-9250.stl";
mpu_pos     = [57, 7, 3];
mpu_rot     = [90, 0, 90];
mpu_screw_y = -6;
mpu_screw_x_1 = 59;
mpu_screw_x_2 = 80;

battery_file= "Assets/battery holder 3 x AAA.stl";
battery_pos = [-5, 0, 2];
battery_rot = [90, 0, 90];

staff_pos = [20, 0, -case_length/2 - 50];
staff_rot = [0, 90, 0];

button_pos = [-30-22.5, -8, 13];
button_rot = [0, 0, 0];

$fn = 100;

// LIPO BATTERY BOX (ghost) + CORNER SUPPORTS
battery_box_pos      = [18, 0, 7];
battery_box_size     = [60, 40, 6.5];
battery_box_corner_r = 3;

battery_support_leg       = 10;
battery_support_wall      = 1.5;
battery_support_clearance = 0.3;
battery_support_height    = 5;

battery_holder_screw_x = [
    battery_box_pos[0] - battery_box_size[0]/2 - 4,
    battery_box_pos[0] + battery_box_size[0]/2 + 4
];
battery_holder_screw_y = battery_box_pos[1];
battery_holder_strap_width     = 10;
battery_holder_strap_thickness = 1.7;
battery_holder_base_z = battery_box_pos[2] + battery_box_size[2]/2;
battery_holder_top_z  = battery_holder_base_z + battery_holder_strap_thickness;
battery_holder_leg_r  = screw_head_d + 1 ;
battery_holder_leg_top_z = 8.5;

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

    translate([battery_box_pos[0], battery_box_pos[1], strap_thickness])
    linear_extrude(height = battery_support_height)
    intersection() {
        difference() {
            rounded_rect_2d(outer_half_x, outer_half_y, outer_r);
            rounded_rect_2d(inner_half_x, inner_half_y, inner_r);
        }
        union() {
            for (sx = [-1, 1])
                for (sy = [-1, 1])
                    translate([sx * (outer_half_x - leg/2), sy * (outer_half_y - leg/2)])
                        square([leg, leg], center = true);
        }
    }
}

module battery_holder_legs() {
    for (x = battery_holder_screw_x)
        translate([x, battery_holder_screw_y, strap_thickness])
            cylinder(r = battery_holder_leg_r - 2, h = battery_holder_leg_top_z - strap_thickness);
}

module battery_top_holder_shape() {
    for (x = battery_holder_screw_x)
        translate([x, battery_holder_screw_y, battery_holder_base_z - 2])
            cylinder(r = battery_holder_leg_r - 2, h = battery_holder_strap_thickness + 2);

    translate([
        battery_holder_screw_x[0] - 2,
        battery_holder_screw_y - battery_holder_strap_width/2,
        battery_holder_base_z
    ])
        cube([
            battery_holder_screw_x[1] - battery_holder_screw_x[0] + 4,
            battery_holder_strap_width,
            battery_holder_strap_thickness
        ]);
}

// Printed as a separate part, so it only gets its own 2 screw holes,
module battery_top_holder() {
    difference() {
        battery_top_holder_shape();
        for (x = battery_holder_screw_x)
            translate([x, battery_holder_screw_y, battery_holder_base_z - 3])
                cylinder(r = screw_d, h = battery_holder_strap_thickness + 5);
    }
}

// IMPORT GHOSTS
if (show_ghosts) {
    % color("green", 0.3) translate(esp32_pos)   rotate(esp32_rot)   import(esp32_file);
    % color("green", 0.3) translate(mpu_pos)     rotate(mpu_rot)     import(mpu_file);
    % color("green", 0.3) translate(battery_box_pos) lipo_battery_ghost();
    % color("green", 0.3) rotate(staff_rot) translate(staff_pos) cylinder(r = staff_diameter/2, h = case_length + 100);
    % color("green", 0.3) translate(button_pos) rotate(button_rot) button_cap();
    % color("purple", 0.6) battery_top_holder();
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

    translate([-case_length/4 + 6, -case_width/2, case_height/2])
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
    // Battery holder
    for (x = battery_holder_screw_x)
        translate([x, battery_holder_screw_y, -1]) cylinder(r = screw_d, h = 20);

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
    translate([mpu_screw_x_2, mpu_screw_y,9])    cylinder(r = screw_head_d, h = 4);
}


// GENERATING THE PIECES
show_base   = (render_mode == "base"   || render_mode == "all");
show_cap    = (render_mode == "cap"    || render_mode == "all");
show_holder = (render_mode == "holder");

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
            battery_holder_legs();
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

// Battery top holder
if (show_holder) {
    battery_top_holder();
}
$fn = 80;

staff_diameter = 30;    // staff diameter, mm
fit_clearance  = 0.6;   // total diametral clearance for a comfortable slip fit
wall           = 15;    // wall thickness left around each hole

staff_hole_d = staff_diameter + fit_clearance;
staff_hole_r = staff_hole_d / 2;
cube_size    = staff_hole_d + 2 * wall;

module calibration_cube() {
    difference() {
        cube([cube_size, cube_size, cube_size], center = true);
        rotate([0, 90, 0]) cylinder(r = staff_hole_r, h = cube_size + 2, center = true); // X
        rotate([90, 0, 0]) cylinder(r = staff_hole_r, h = cube_size + 2, center = true); // Y
        cylinder(r = staff_hole_r, h = cube_size + 2, center = true);                    // Z
    }
}

tetra_angle = 54.7356;

module horizontal_cut() {
    translate([-cube_size/2 - 1, -cube_size/2 - 1, -cube_size/2 - 1])
        cube([cube_size + 2, cube_size + 2, cube_size/2 + 1]);
}
module horizontal_cut_2() {
    translate([-1, -50, -cube_size/4 - 0.1])
        cube([cube_size + 2, cube_size + 2, cube_size/2 + 1]);


    translate([-50, -1, -cube_size/4 - 0.1])
        cube([cube_size + 2, cube_size + 2, cube_size/2 + 1]);
}

module diagonal_cut() {
    rotate(a = tetra_angle, v = [-1, 1, 0])
        translate([-1000, -1000, -1015])
            cube([2000, 2000, 1000]);
}

union() {
    difference() {
        intersection() {
            calibration_cube();
            horizontal_cut();
            diagonal_cut();
        }
        horizontal_cut_2();
    }
    translate([-cube_size/2, -cube_size/2]) cube([wall, wall, 11]);
}
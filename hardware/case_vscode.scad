// --- PARAMETERS (Adjust these to match your staff and electronics) ---
staff_diameter = 40;       // Diameter of your Bô staff in mm
case_length = 170;          // How long the case is along the staff
case_width = 35;           // How wide the case is
case_height = 30;          // How tall the case is
wall_thickness = 2.5;      // Thickness of the casing walls
strap_width = 8;          // Width of your velcro strap
strap_thickness = 2;       // Thickness of your velcro strap

$fn = 100; // Makes circles smooth (render quality)

translate([-65, 11, 0]) rotate([90, 0, 270]) import("Sparkfun Thing Plus v8.stl");
translate([50, 7, 5]) rotate([90, 0, 90]) import("MPU-9250.stl");
translate([23, 0, 0]) rotate([180, 180, 0]) import("2xAAA BATTERY HOLDER.stl");

% difference() {
    // // 1. THE OUTER DOME (Scaled sphere to make an ellipsoid)
    scale([case_length/2, case_width/2, case_height]) {
        sphere(r = 1);
    }

    // // 4. THE STAFF GROOVE (Cuts a cylinder out of the bottom)
    rotate([0, 90, 0]) { // Align cylinder with the length of the staff
        translate([20, 0, -case_length/2 - 15]) {
            cylinder(r = staff_diameter/2, h = case_length + 30);
        }
    }

    // 5. STRAP SLOTS (Cuts holes for the straps on both sides)
    translate([case_length/5, -case_width/2, case_height/3])  {
        cube([strap_width, case_width, strap_thickness]);
    }

    translate([-case_length/5, -case_width/2, case_height/3])  {
        cube([strap_width, case_width, strap_thickness]);
    }
}
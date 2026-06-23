// --- PARAMETERS (Adjust these to match your staff and electronics) ---
staff_diameter = 40;       // Diameter of your Bô staff in mm
case_length = 180;          // How long the case is along the staff
case_width = 35;           // How wide the case is
case_height = 30;          // How tall the case is
wall_thickness = 0.01;      // Thickness of the casing walls
strap_width = 8;          // Width of your velcro strap
strap_thickness = 2;       // Thickness of your velcro strap
plate_lenght = case_length/3 * 2;
plate_width = case_width - 5;

$fn = 100; // Makes circles smooth (render quality)

color("green") translate([-65, 11, 2]) rotate([90, 0, 270]) import("Sparkfun Thing Plus v8.stl");
color("green") translate([50, 7, 8]) rotate([90, 0, 90]) import("MPU-9250.stl");
color("green") translate([23, 0, 4]) rotate([180, 180, 0]) import("2xAAA BATTERY HOLDER.stl");

plate1_l = plate_lenght/2;
plate2_l = plate_lenght/3*2 - 3;
plate3_l = plate_lenght/3*2 - 3;
color("blue") union() {
    translate([-plate_lenght/2, -plate_width/2, 1]) {
        // center plate
        translate([plate_lenght/4, 0, 0]) cube([plate1_l, plate_width, strap_thickness]);
        // MPU plate
        translate([plate_lenght/2, plate_width/6 + 2, 0]) cube([plate2_l, plate_width/3*2-4, strap_thickness]);
        // ESP plate
        translate([-4, plate_width/6 - 2, 0]) cube([plate3_l, plate_width/3*2+4, strap_thickness]);


        linear_extrude(height = strap_thickness) {
            polygon(points=[[plate_lenght/4, 0], [-4, plate_width/6 - 2], [plate_lenght/4, plate_width/6 - 2]]);
            polygon(points=[[plate_lenght/4, plate_width], [-4, plate_width - 3], [plate_lenght/4, plate_width - 3]]);
            polygon(points=[[plate1_l + plate2_l, plate_width/3-3], [plate1_l/2*3, 7], [plate1_l/2*3, 0]]);
            polygon(points=[[plate1_l + plate2_l, plate_width/3*2+3], [plate1_l/2*3, plate_width/3*2+3], [plate1_l/2*3, plate_width/3*2+10]]);
        }
    }
}

difference() {
    // THE OUTER DOME (Scaled sphere to make an ellipsoid)
    scale([case_length/2, case_width/2, case_height]) {
        sphere(r = 1);
    }

    scale([case_length/2, case_width/2, case_height]) {
        sphere(r = 1-wall_thickness);
    }

    // THE STAFF GROOVE (Cuts a cylinder out of the bottom)
    rotate([0, 90, 0]) { // Align cylinder with the length of the staff
        translate([20, 0, -case_length/2 - 15]) {
            cylinder(r = staff_diameter/2, h = case_length + 30);
        }
    }

    // STRAP SLOTS (Cuts holes for the straps on both sides)
    translate([case_length/9 * 2.5, -case_width/2, case_height/2])  {
        cube([strap_width, case_width, strap_thickness]);
    }

    translate([-case_length/9 * 2.5, -case_width/2, case_height/2])  {
        cube([strap_width, case_width, strap_thickness]);
    }

    // middle
    translate([-strap_width/2, -case_width/2, case_height/3*2])  {
        cube([strap_width, case_width, strap_thickness]);
    }

    // USB SLOT To move
    translate([-case_length/2 + 2.5, -strap_width/2 - 1, 8])  {
        cube([10, strap_width+2, strap_thickness*2]);
    }
}
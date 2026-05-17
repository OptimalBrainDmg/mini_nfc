

m25 = 2.22;
m3 = 2.76;

wall = 2.6;

display = [70, 60, 5];
display_offset = 3.578;

nfc = [50, 120, wall];

inner_dims = [110, 121];
bottom_height = 20;

//display();
//nfc();

difference() {
    union() {
        difference() {
            rcube([inner_dims.x + 2*wall, inner_dims.y + 2*wall, bottom_height+5], 2);
            translate([-0.01, -0.01, bottom_height]) cube([inner_dims.x + 2*wall+1, inner_dims.y + 2*wall+1, bottom_height]);
            translate([wall, wall, wall]) cube([inner_dims.x, inner_dims.y, bottom_height]);

            // clear the post hole areas for the magnets
            translate([4.5,4.5,wall]) cylinder(d=9, h=bottom_height);
            translate([inner_dims.x+2*wall-4.5,4.5,wall]) cylinder(d=9, h=bottom_height);
            translate([inner_dims.x+2*wall-4.5,inner_dims.y+2*wall-4.5,wall]) cylinder(d=9, h=bottom_height);
            translate([4.5,inner_dims.y+2*wall-4.5,wall]) cylinder(d=9, h=bottom_height);

            // clear area for the nfc mount
            translate([wall+(inner_dims.x-nfc.x)/2, wall+(inner_dims.y-nfc.y)/2 ,0.1]) 
                cube([nfc.x,nfc.y,10]);
            

        }
       
        // corner magnets
        translate([4.5,4.5,wall]) post(9, 6, bottom_height-wall, 2.8);
        translate([inner_dims.x+2*wall-4.5,4.5,wall]) post(9, 6, bottom_height-wall, 2.8);
        translate([inner_dims.x+2*wall-4.5,inner_dims.y+2*wall-4.5,wall]) post(9, 6, bottom_height-wall, 2.8);
        translate([4.5,inner_dims.y+2*wall-4.5,wall]) post(9, 6, bottom_height-wall, 2.8);
        
        // NFC mount
        translate([wall+(inner_dims.x-nfc.x)/2, wall+(inner_dims.y-nfc.y)/2 ,0.1]) nfc();

        // power switch mount block
        translate([wall+10, 2*wall+inner_dims.y-5, wall]) cube([12, 5, 7]);
    }

    // power switch opening
    translate([wall+11.5, 2*wall+inner_dims.y-6, wall+1.5]) cube([9, 4, 4]);
    translate([wall+11.5+2.7, 2*wall+inner_dims.y-5, wall+1.5+1]) cube([3.6, 10, 2]);
    
    
}


module nfc() {
    $fn = 10;
    cube(nfc);
    translate([5, 5, 0]) post(6, m3, 5, 3); 
    translate([nfc.x - 5, 5, 0]) post(6, m3, 5, 3); 
    translate([nfc.x - 5, nfc.y - 5, 0]) post(6, m3, 5, 3); 
    translate([5, nfc.y - 5, 0]) post(6, m3, 5, 3); 
}


module display() {
    $fn = 10;
    mount = display;
    screws = [59.69, 47.752];
    screen = [60.5,43];
    opening = [51, 40];

    difference() {
        dx = (mount.x-screws.x)/2;
        dy = (mount.y-screws.y)/2;
        
        cube(mount);

        translate([dx, dy, mount.z/2]) cylinder(h=10,d=m25);
        translate([dx, dy + screws.y, mount.z/2]) cylinder(h=10,d=m25);
        translate([dx + screws.x, dy, mount.z/2]) cylinder(h=10,d=m25);
        translate([dx + screws.x, dy + screws.y, mount.z/2]) cylinder(h=10,d=m25);
        
        translate([dx+1.016, dy+2.54, 1]) cube([screen.x, screen.y, mount.z]);
        translate([dx+1.016+1.5, dy+2.54+(screen.y-opening.y)/2, -1]) cube([opening.x, opening.y, mount.z]);
        
    }   
}

module post(od, id, height, depth) {
    difference() {
        cylinder(d=od, h=height);
        translate([0, 0, height - depth]) cylinder(d=id, h=depth+0.01);
    }
}

module rcube(dims_or_size, r, center = false) {
    $fn = 16;
    dims = (dims_or_size[0] == undef) ? [dims_or_size, dims_or_size, dims_or_size] : dims_or_size;
    translate(center ? [-dims.x/2, -dims.y/2, -dims.z/2] : [0,0,0]) 
    union() {
        // cubes in x,y,z directions for the middle
        translate([r,r,0]) cube([dims.x - 2*r, dims.y - 2*r, dims.z]); 
        translate([r,0,r]) cube([dims.x - 2*r, dims.y, dims.z - 2*r]);
        translate([0,r,r]) cube([dims.x, dims.y - 2*r, dims.z - 2*r]);
        
        // cylinders for the vertices
        translate([r,r,r]) cylinder(dims.z - 2*r, r = r);
        translate([dims.x-r,r,r]) cylinder(dims.z - 2*r, r = r);
        translate([r,dims.y-r,r]) cylinder(dims.z - 2*r, r = r);
        translate([dims.x-r,dims.y-r,r]) cylinder(dims.z - 2*r, r = r);
        
        translate([r,r,r]) rotate([-90,0,0]) cylinder(dims.y - 2*r, r = r);
        translate([dims.x-r,r,r]) rotate([-90,0,0]) cylinder(dims.y - 2*r, r = r);
        translate([r,r,dims.z-r]) rotate([-90,0,0]) cylinder(dims.y - 2*r, r = r);
        translate([dims.x-r,r,dims.z-r]) rotate([-90,0,0]) cylinder(dims.y - 2*r, r = r);
        
        translate([r,r,r]) rotate([0,90,0]) cylinder(dims.x - 2*r, r = r);
        translate([r,r,dims.z-r]) rotate([0,90,0]) cylinder(dims.x - 2*r, r = r);
        translate([r,dims.y-r,r]) rotate([0,90,0]) cylinder(dims.x - 2*r, r = r);
        translate([r,dims.y-r,dims.z-r]) rotate([0,90,0]) cylinder(dims.x - 2*r, r = r);

        // spheres for the corners
        for (x = [r, dims.x - r]) {
            for (y = [r, dims.y - r]) {
                for (z = [r, dims.z - r]) {
                    translate([x,y,z]) sphere(r);
                }
            }
        }
    }
}

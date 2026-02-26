import bpy
import bmesh
import math

def cleanup_scene():
    """Clears the scene to ensure a clean slate."""
    if bpy.context.active_object and bpy.context.active_object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

def assign_color(obj, r, g, b):
    """Assigns a solid viewport color."""
    mat = bpy.data.materials.new(name=f"Mat_{obj.name}")
    mat.use_nodes = False
    mat.diffuse_color = (r, g, b, 1.0)
    if obj.data.materials: obj.data.materials[0] = mat
    else: obj.data.materials.append(mat)

def apply_boolean(target, tool, operation='DIFFERENCE'):
    """Applies a boolean modifier using the EXACT solver."""
    bpy.context.view_layer.objects.active = target
    bool_mod = target.modifiers.new(name=f"Bool_{tool.name}", type='BOOLEAN')
    bool_mod.operation = operation
    bool_mod.object = tool
    bool_mod.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_mod.name)
    bpy.data.objects.remove(tool, do_unlink=True)

# --- GEAR GENERATION HELPERS ---
def create_gear_mesh(name, teeth, modulus, thickness):
    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    bm = bmesh.new()
    pitch_radius = (teeth * modulus) / 2.0
    outer_radius = pitch_radius + modulus
    root_radius = pitch_radius - (1.25 * modulus)
    verts = []
    angle_per_tooth = (2 * math.pi) / teeth
    for i in range(teeth):
        angle = i * angle_per_tooth
        a1, a2 = angle - angle_per_tooth * 0.25, angle - angle_per_tooth * 0.10
        a3, a4 = angle + angle_per_tooth * 0.10, angle + angle_per_tooth * 0.25
        verts.append(bm.verts.new((math.cos(a1)*root_radius, math.sin(a1)*root_radius, 0)))
        verts.append(bm.verts.new((math.cos(a2)*outer_radius, math.sin(a2)*outer_radius, 0)))
        verts.append(bm.verts.new((math.cos(a3)*outer_radius, math.sin(a3)*outer_radius, 0)))
        verts.append(bm.verts.new((math.cos(a4)*root_radius, math.sin(a4)*root_radius, 0)))
    face = bm.faces.new(verts)
    extracted = bmesh.ops.extrude_face_region(bm, geom=[face])
    extrude_verts = [v for v in extracted['geom'] if isinstance(v, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, vec=(0, 0, thickness), verts=extrude_verts)
    bm.to_mesh(mesh)
    bm.free()
    return obj

def create_worm_drive(modulus, pitch):
    worm_length, worm_radius = 0.020, 0.006
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=worm_radius, depth=worm_length, location=(0,0,worm_length/2))
    worm = bpy.context.active_object
    worm.name = "Worm_Gear"
    
    mesh = bpy.data.meshes.new("Thread_Profile")
    spiral_cutter = bpy.data.objects.new("Thread_Profile", mesh)
    bpy.context.collection.objects.link(spiral_cutter)
    bm = bmesh.new()
    v1, v2 = bm.verts.new((worm_radius - 0.00225, 0, -pitch/4)), bm.verts.new((worm_radius + 0.001, 0, -pitch/3))
    v3, v4 = bm.verts.new((worm_radius + 0.001, 0, pitch/3)), bm.verts.new((worm_radius - 0.00225, 0, pitch/4))
    bm.faces.new((v1, v2, v3, v4))
    bm.to_mesh(mesh)
    bm.free()

    screw = spiral_cutter.modifiers.new(name="Screw", type='SCREW')
    screw.axis, screw.screw_offset = 'Z', pitch
    screw.iterations, screw.steps, screw.render_steps = int(worm_length / pitch) + 2, 32, 32
    spiral_cutter.location = (0, 0, -pitch)
    
    # Manifold Fix
    bpy.context.view_layer.objects.active = spiral_cutter
    bpy.ops.object.modifier_apply(modifier=screw.name)
    bm_cutter = bmesh.new()
    bm_cutter.from_mesh(spiral_cutter.data)
    boundary = [e for e in bm_cutter.edges if e.is_boundary]
    if boundary: bmesh.ops.holes_fill(bm_cutter, edges=boundary)
    bmesh.ops.recalc_face_normals(bm_cutter, faces=bm_cutter.faces)
    bm_cutter.to_mesh(spiral_cutter.data)
    bm_cutter.free()
    
    apply_boolean(worm, spiral_cutter, 'DIFFERENCE')

    # Spline Cut
    bpy.ops.mesh.primitive_cube_add(size=1)
    cross_1 = bpy.context.active_object
    cross_1.scale, cross_1.location = (0.005, 0.0012, 0.008), (0,0,0)
    bpy.ops.mesh.primitive_cube_add(size=1)
    cross_2 = bpy.context.active_object
    cross_2.scale, cross_2.location = (0.0012, 0.005, 0.008), (0,0,0)
    apply_boolean(cross_1, cross_2, 'UNION')
    apply_boolean(worm, cross_1, 'DIFFERENCE')
    return worm

# --- MAIN GENERATOR ---
def generate_assembly():
    cleanup_scene()
    modulus = 0.001 
    pitch = math.pi * modulus 

    # ==========================================
    # 1. PROTOTYPING BASE (CORRECTED) - Grey
    # ==========================================
    bpy.ops.mesh.primitive_cube_add(size=1)
    base = bpy.context.active_object
    base.name = "Prototyping_Base_Corrected"
    base.scale = (0.085, 0.070, 0.004) 
    base.location = (0.0025, 0.010, -0.002) 

    # Features
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=0.00135, depth=0.012, location=(0, 0, 0.006))
    apply_boolean(base, bpy.context.active_object, 'UNION') # Axle
    
    bpy.ops.mesh.primitive_cube_add(size=1)
    rear_wall = bpy.context.active_object
    rear_wall.scale, rear_wall.location = (0.075, 0.0073, 0.008), (0, -0.01635, 0.004)
    apply_boolean(base, rear_wall, 'UNION') # Rear Wall

    bpy.ops.mesh.primitive_cube_add(size=1)
    front_left = bpy.context.active_object
    front_left.scale, front_left.location = (0.020, 0.0063, 0.008), (-0.025, -0.00315, 0.004)
    apply_boolean(base, front_left, 'UNION') # Front Left Clip

    bpy.ops.mesh.primitive_cube_add(size=1)
    front_right = bpy.context.active_object
    front_right.scale, front_right.location = (0.020, 0.0063, 0.008), (0.025, -0.00315, 0.004)
    apply_boolean(base, front_right, 'UNION') # Front Right Clip

    # Motor Block (Corrected X=25mm position)
    bpy.ops.mesh.primitive_cube_add(size=1)
    motor_block = bpy.context.active_object
    motor_block.scale, motor_block.location = (0.034, 0.030, 0.016), (0.025, 0.025, 0.008) 
    apply_boolean(base, motor_block, 'UNION') 

    # Motor Cavity (Corrected X=25mm position)
    bpy.ops.mesh.primitive_cube_add(size=1)
    motor_cavity = bpy.context.active_object
    motor_cavity.scale, motor_cavity.location = (0.0265, 0.030, 0.030), (0.025, 0.026, 0.01675) 
    apply_boolean(base, motor_cavity, 'DIFFERENCE')

    # Worm Hole (Corrected 13mm diameter / 6.5mm radius at X=25mm)
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=0.0065, depth=0.020, location=(0.025, 0.005, 0.009), rotation=(math.pi/2, 0, 0))
    apply_boolean(base, bpy.context.active_object, 'DIFFERENCE')

    # --- THE CRITICAL FIXES ---
    # 1. Pinion Relief Recess (2mm deep pocket under the gear stack)
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=0.010, depth=0.004, location=(0, 0, 0))
    apply_boolean(base, bpy.context.active_object, 'DIFFERENCE')

    # 2. Worm Wheel Clearance (22.5mm radius bite out of the motor block)
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=0.0225, depth=0.020, location=(0, 0, 0.015))
    apply_boolean(base, bpy.context.active_object, 'DIFFERENCE')
    
    assign_color(base, 0.2, 0.2, 0.2) # Dark Grey

    # ==========================================
    # 2. THE RACK - Blue
    # ==========================================
    rack_length = 0.070
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, rack_length/2))
    rack = bpy.context.active_object
    rack.name = "Pump_Rack"
    rack.scale = (0.006, 0.006, rack_length)
    bpy.ops.object.transform_apply(scale=True)
    
    bpy.ops.mesh.primitive_cube_add(size=1)
    cutter = bpy.context.active_object
    cutter.scale = (0.008, pitch * 0.5, 0.00225) 
    for i in range(int(rack_length / pitch) - 2):
        cutter.location = (0, 0.003, (i * pitch) + 0.005)
        bpy.context.view_layer.objects.active = rack
        bool_mod = rack.modifiers.new(name=f"Cut_{i}", type='BOOLEAN')
        bool_mod.operation, bool_mod.object, bool_mod.solver = 'DIFFERENCE', cutter, 'EXACT'
        bpy.ops.object.modifier_apply(modifier=bool_mod.name)
    bpy.data.objects.remove(cutter, do_unlink=True)
    
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=0.00475, depth=0.002, location=(0, 0, rack_length + 0.001))
    apply_boolean(rack, bpy.context.active_object, 'UNION')
    
    # Position Rack
    rack.rotation_euler = (0, math.pi/2, 0) 
    rack.location = (0, -0.0095, 0.003)
    assign_color(rack, 0.1, 0.4, 0.8) # Blue

    # ==========================================
    # 3. PINION & WHEEL ASSEMBLY - Orange
    # ==========================================
    pinion = create_gear_mesh("Pinion_Gear", 15, modulus, 0.006)
    wheel = create_gear_mesh("Worm_Wheel", 40, modulus, 0.006)
    wheel.location.z = 0.006 
    bpy.context.view_layer.objects.active = pinion
    apply_boolean(pinion, wheel, 'UNION')
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=0.0015, depth=0.02, location=(0,0,0.006))
    apply_boolean(pinion, bpy.context.active_object, 'DIFFERENCE')
    
    # Position Assembly on Axle (Z=0, sitting in the new recess)
    pinion.location = (0, 0, 0)
    assign_color(pinion, 0.9, 0.4, 0.1) # Orange

    # ==========================================
    # 4. WORM DRIVE - Green
    # ==========================================
    worm = create_worm_drive(modulus, pitch)
    # Position Worm (Corrected X=25mm meshing distance)
    worm.rotation_euler = (math.pi/2, 0, 0) 
    worm.location = (0.025, 0.012, 0.009)
    assign_color(worm, 0.2, 0.8, 0.2) # Green

    # Viewport Setup
    for area in bpy.context.screen.areas:
        if area.type == 'VIEW_3D':
            ctx = bpy.context.copy()
            ctx['area'] = area
            ctx['region'] = area.regions[-1]
            bpy.ops.view3d.view_all(ctx, center=True)
            break
    print("Assembly Visualizer Complete. Check fitment in viewport.")

if __name__ == "__main__":
    generate_assembly()
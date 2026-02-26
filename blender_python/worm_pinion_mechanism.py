import bpy
import bmesh
import math
import os

def cleanup_scene():
    """Clears the scene before generation."""
    if bpy.context.active_object and bpy.context.active_object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

def create_gear_mesh(name, teeth, modulus, thickness):
    """
    Mathematically generates a solid, manifold 3D printed gear.
    Uses a simplified trapezoidal involute profile for FDM printing.
    """
    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    
    bm = bmesh.new()
    
    # Gear formulas
    pitch_radius = (teeth * modulus) / 2.0
    outer_radius = pitch_radius + modulus
    root_radius = pitch_radius - (1.25 * modulus)
    
    verts = []
    angle_per_tooth = (2 * math.pi) / teeth
    
    for i in range(teeth):
        angle = i * angle_per_tooth
        a1 = angle - angle_per_tooth * 0.25  
        a2 = angle - angle_per_tooth * 0.10  
        a3 = angle + angle_per_tooth * 0.10  
        a4 = angle + angle_per_tooth * 0.25  
        
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

def generate_pump_mechanism(export_dir):
    cleanup_scene()
    os.makedirs(export_dir, exist_ok=True)
    
    # --- GLOBAL PARAMS ---
    modulus = 0.001 # 1.0mm
    pitch = math.pi * modulus # 3.1416mm
    
    # ==========================================
    # 1. THE RACK (Piston Rod)
    # ==========================================
    rack_length = 0.070
    rack_width = 0.006
    rack_height = 0.006
    
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, rack_length/2))
    rack = bpy.context.active_object
    rack.name = "Pump_Rack"
    rack.scale = (rack_width, rack_height, rack_length)
    bpy.ops.object.transform_apply(scale=True)
    
    num_rack_teeth = int(rack_length / pitch) - 2
    
    bpy.ops.mesh.primitive_cube_add(size=1)
    cutter = bpy.context.active_object
    cutter.scale = (rack_width + 0.002, pitch * 0.5, 0.00225) 
    
    for i in range(num_rack_teeth):
        z_pos = (i * pitch) + 0.005 
        cutter.location = (0, rack_height/2, z_pos)
        
        bpy.context.view_layer.objects.active = rack
        bool_mod = rack.modifiers.new(name=f"Cut_{i}", type='BOOLEAN')
        bool_mod.operation = 'DIFFERENCE'
        bool_mod.object = cutter
        bool_mod.solver = 'EXACT'
        bpy.ops.object.modifier_apply(modifier=bool_mod.name)
        
    bpy.data.objects.remove(cutter, do_unlink=True)
    
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=0.00475, depth=0.002, location=(0, 0, rack_length + 0.001))
    pad = bpy.context.active_object
    
    bpy.context.view_layer.objects.active = rack
    bool_pad = rack.modifiers.new(name="Add_Pad", type='BOOLEAN')
    bool_pad.operation = 'UNION'
    bool_pad.object = pad
    bool_pad.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_pad.name)
    bpy.data.objects.remove(pad, do_unlink=True)
    
    bpy.ops.object.select_all(action='DESELECT')
    rack.select_set(True)
    bpy.ops.wm.stl_export(filepath=os.path.join(export_dir, "1_Pump_Rack.stl"), export_selected_objects=True)
    rack.location.x -= 0.03 

    # ==========================================
    # 2. PINION & WORM WHEEL ASSEMBLY
    # ==========================================
    pinion = create_gear_mesh("Pinion_Gear", 15, modulus, 0.006)
    wheel = create_gear_mesh("Worm_Wheel", 40, modulus, 0.006)
    wheel.location.z = 0.006 
    
    bpy.context.view_layer.objects.active = pinion
    bool_merge = pinion.modifiers.new(name="Merge_Wheel", type='BOOLEAN')
    bool_merge.operation = 'UNION'
    bool_merge.object = wheel
    bool_merge.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_merge.name)
    bpy.data.objects.remove(wheel, do_unlink=True)
    
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=0.0015, depth=0.02, location=(0,0,0.006))
    axle_hole = bpy.context.active_object
    
    bpy.context.view_layer.objects.active = pinion
    bool_hole = pinion.modifiers.new(name="Axle_Hole", type='BOOLEAN')
    bool_hole.operation = 'DIFFERENCE'
    bool_hole.object = axle_hole
    bool_hole.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_hole.name)
    bpy.data.objects.remove(axle_hole, do_unlink=True)

    bpy.ops.object.select_all(action='DESELECT')
    pinion.select_set(True)
    bpy.ops.wm.stl_export(filepath=os.path.join(export_dir, "2_Pinion_Wheel_Assembly.stl"), export_selected_objects=True)
    pinion.location.x += 0.03 

    # ==========================================
    # 3. 1-START WORM (With SG90 Mount)
    # ==========================================
    worm_length = 0.020
    worm_radius = 0.006
    
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=worm_radius, depth=worm_length, location=(0,0,worm_length/2))
    worm = bpy.context.active_object
    worm.name = "Worm_Gear"
    
    mesh = bpy.data.meshes.new("Thread_Profile")
    spiral_cutter = bpy.data.objects.new("Thread_Profile", mesh)
    bpy.context.collection.objects.link(spiral_cutter)
    
    bm = bmesh.new()
    v1 = bm.verts.new((worm_radius - 0.00225, 0, -pitch/4))
    v2 = bm.verts.new((worm_radius + 0.001, 0, -pitch/3))
    v3 = bm.verts.new((worm_radius + 0.001, 0, pitch/3))
    v4 = bm.verts.new((worm_radius - 0.00225, 0, pitch/4))
    bm.faces.new((v1, v2, v3, v4))
    bm.to_mesh(mesh)
    bm.free()

    screw = spiral_cutter.modifiers.new(name="Screw", type='SCREW')
    screw.axis = 'Z'
    screw.screw_offset = pitch
    screw.iterations = int(worm_length / pitch) + 2
    screw.steps = 32
    screw.render_steps = 32
    spiral_cutter.location = (0, 0, -pitch)
    
    # --- THE FIX: Convert cutter to a Manifold Mesh ---
    bpy.context.view_layer.objects.active = spiral_cutter
    bpy.ops.object.modifier_apply(modifier=screw.name)
    
    bm_cutter = bmesh.new()
    bm_cutter.from_mesh(spiral_cutter.data)
    
    # Identify open ends and cap them
    boundary_edges = [e for e in bm_cutter.edges if e.is_boundary]
    if boundary_edges:
        bmesh.ops.holes_fill(bm_cutter, edges=boundary_edges)
        
    # Recalculate face normals so the boolean solver knows outside from inside
    bmesh.ops.recalc_face_normals(bm_cutter, faces=bm_cutter.faces)
    
    bm_cutter.to_mesh(spiral_cutter.data)
    bm_cutter.free()
    # ---------------------------------------------------
    
    # Now the EXACT boolean solver will properly cut the threads
    bpy.context.view_layer.objects.active = worm
    bool_worm = worm.modifiers.new(name="Cut_Threads", type='BOOLEAN')
    bool_worm.operation = 'DIFFERENCE'
    bool_worm.object = spiral_cutter
    bool_worm.solver = 'EXACT' 
    bpy.ops.object.modifier_apply(modifier=bool_worm.name)
    bpy.data.objects.remove(spiral_cutter, do_unlink=True)

    # Cut the SG90 Spline Hole (Cross shape) into the bottom
    bpy.ops.mesh.primitive_cube_add(size=1)
    cross_1 = bpy.context.active_object
    cross_1.scale = (0.005, 0.0012, 0.008) 
    cross_1.location = (0,0,0)
    
    bpy.ops.mesh.primitive_cube_add(size=1)
    cross_2 = bpy.context.active_object
    cross_2.scale = (0.0012, 0.005, 0.008)
    cross_2.location = (0,0,0)
    
    bpy.context.view_layer.objects.active = cross_1
    bool_c = cross_1.modifiers.new(name="Merge", type='BOOLEAN')
    bool_c.operation = 'UNION'
    bool_c.object = cross_2
    bpy.ops.object.modifier_apply(modifier=bool_c.name)
    bpy.data.objects.remove(cross_2, do_unlink=True)
    
    bpy.context.view_layer.objects.active = worm
    bool_spline = worm.modifiers.new(name="Cut_Spline", type='BOOLEAN')
    bool_spline.operation = 'DIFFERENCE'
    bool_spline.object = cross_1
    bool_spline.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_spline.name)
    bpy.data.objects.remove(cross_1, do_unlink=True)

    bpy.ops.object.select_all(action='DESELECT')
    worm.select_set(True)
    bpy.ops.wm.stl_export(filepath=os.path.join(export_dir, "3_Worm_Drive.stl"), export_selected_objects=True)

    print(f"Success! All files exported. The Worm Drive now has threads.")

# ==========================================
# EXECUTION
# ==========================================
desktop_path = os.path.join(os.path.expanduser("~"), "Desktop", "ESP32_Pump")

if __name__ == "__main__":
    generate_pump_mechanism(desktop_path)
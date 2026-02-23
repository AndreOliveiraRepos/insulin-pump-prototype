import bpy
import bmesh
import math
import os

def cleanup_scene():
    """Clears the existing scene to ensure a clean slate."""
    if bpy.context.active_object and bpy.context.active_object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

def generate_actuator_piston(export_dir):
    cleanup_scene()

    # --- 1. DIMENSIONS (in meters) ---
    piston_length = 0.030     # 30mm long (Z-axis)
    piston_radius = 0.005     # 10mm diameter (5mm radius)
    guide_radius = 0.001      # 2mm wide tracking guides (1mm radius)
    flat_cut_depth = 0.002    # Shave 2mm off the back to create a D-profile
    
    # M3 Bolt parameters (3mm wide rod)
    thread_major_radius = 0.0015   
    thread_minor_radius = 0.00125  
    thread_pitch = 0.0005          
    thread_depth = piston_length + 0.004 # Ensure threads clear the whole length

    # --- 2. BASE GEOMETRY: PISTON ---
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=128, 
        radius=piston_radius, 
        depth=piston_length, 
        location=(0, 0, piston_length / 2.0)
    )
    piston = bpy.context.active_object
    piston.name = "Actuator_Piston"

    # --- 3. ADD 3 TRACKING GUIDES (UNION) ---
    # Position 3 protruding ribs on the Front (0°), Right (90°), and Left (-90°)
    angles = [0, math.pi/2, -math.pi/2]
    overlap = 0.0002 # 0.2mm inward shift to guarantee watertight boolean union
    
    for i, angle in enumerate(angles):
        x = math.sin(angle) * (piston_radius - overlap)
        y = math.cos(angle) * (piston_radius - overlap)
        
        # Adding this cylinder changes the active context!
        bpy.ops.mesh.primitive_cylinder_add(
            vertices=32, 
            radius=guide_radius, 
            depth=piston_length, 
            location=(x, y, piston_length / 2.0)
        )
        guide = bpy.context.active_object
        guide.name = f"Guide_Rib_{i+1}"
        
        # FIX: Re-select the main piston as the active object before applying modifier
        bpy.context.view_layer.objects.active = piston 
        
        bool_union = piston.modifiers.new(name=f"Merge_{guide.name}", type='BOOLEAN')
        bool_union.operation = 'UNION'
        bool_union.object = guide
        bool_union.solver = 'EXACT'
        bpy.ops.object.modifier_apply(modifier=bool_union.name)
        
        bpy.data.objects.remove(guide, do_unlink=True)

    # --- 4. FLATTEN THE BACK (DIFFERENCE) ---
    # Cut a slice off the back (Negative Y-axis) to prevent rotation
    bpy.ops.mesh.primitive_cube_add(size=1)
    flat_cutter = bpy.context.active_object
    flat_cutter.name = "Flat_Back_Cutter"
    flat_cutter.scale = (0.020, 0.010, piston_length + 0.010)
    
    cutter_y = -piston_radius - 0.005 + flat_cut_depth
    flat_cutter.location = (0, cutter_y, piston_length / 2.0)

    # FIX: Re-select the main piston as the active object
    bpy.context.view_layer.objects.active = piston
    
    bool_flat = piston.modifiers.new(name="Cut_Flat_Back", type='BOOLEAN')
    bool_flat.operation = 'DIFFERENCE'
    bool_flat.object = flat_cutter
    bool_flat.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_flat.name)
    
    bpy.data.objects.remove(flat_cutter, do_unlink=True)

    # --- 5. THREAD CUTTER 1: BORE HOLE ---
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=64, 
        radius=thread_minor_radius, 
        depth=thread_depth, 
        location=(0, 0, piston_length / 2.0)
    )
    bore_cutter = bpy.context.active_object
    bore_cutter.name = "Bore_Cutter"

    # --- 6. THREAD CUTTER 2: SPIRAL GROOVES ---
    mesh = bpy.data.meshes.new("Thread_Profile")
    spiral_cutter = bpy.data.objects.new("Thread_Profile", mesh)
    bpy.context.collection.objects.link(spiral_cutter)
    
    bm = bmesh.new()
    v1 = bm.verts.new((thread_minor_radius, 0, -thread_pitch / 2.1))
    v2 = bm.verts.new((thread_major_radius, 0, 0))
    v3 = bm.verts.new((thread_minor_radius, 0, thread_pitch / 2.1))
    bm.faces.new((v1, v2, v3))
    bm.to_mesh(mesh)
    bm.free()

    screw = spiral_cutter.modifiers.new(name="Screw", type='SCREW')
    screw.axis = 'Z'
    screw.screw_offset = thread_pitch
    screw.iterations = int(thread_depth / thread_pitch) + 4 
    screw.steps = 32
    screw.render_steps = 32
    spiral_cutter.location = (0, 0, -0.002)

    # --- 7. APPLY INTERNAL THREAD BOOLEANS ---
    # FIX: Re-select the main piston as the active object
    bpy.context.view_layer.objects.active = piston

    bool_bore = piston.modifiers.new(name="Cut_Bore", type='BOOLEAN')
    bool_bore.operation = 'DIFFERENCE'
    bool_bore.object = bore_cutter
    bool_bore.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_bore.name)

    bool_threads = piston.modifiers.new(name="Cut_Threads", type='BOOLEAN')
    bool_threads.operation = 'DIFFERENCE'
    bool_threads.object = spiral_cutter
    bool_threads.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_threads.name)

    bpy.data.objects.remove(bore_cutter, do_unlink=True)
    bpy.data.objects.remove(spiral_cutter, do_unlink=True)

    # --- 8. FINALIZE MESH & EXPORT ---
    bpy.context.view_layer.objects.active = piston
    bpy.ops.object.convert(target='MESH')
    bpy.ops.object.select_all(action='DESELECT')
    piston.select_set(True)

    os.makedirs(export_dir, exist_ok=True)
    filepath = os.path.join(export_dir, "Linear_Actuator_Piston.stl")

    bpy.ops.wm.stl_export(
        filepath=filepath,
        export_selected_objects=True,
        global_scale=1.0,
        apply_modifiers=True
    )
    
    print(f"Success! Actuator piston exported to: {filepath}")

# ==========================================
# EXECUTION
# ==========================================
desktop_path = os.path.join(os.path.expanduser("~"), "Desktop", "3D_Prints")

if __name__ == "__main__":
    generate_actuator_piston(desktop_path)
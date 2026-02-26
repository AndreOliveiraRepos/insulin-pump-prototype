import bpy
import math
import os

def cleanup_scene():
    """Clears the existing scene to ensure a clean slate."""
    if bpy.context.active_object and bpy.context.active_object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

def generate_actuator_housing(export_dir):
    cleanup_scene()

    # --- 1. DIMENSIONS & CLEARANCES (in meters) ---
    housing_length = 0.050         # 50mm tall housing for piston travel
    housing_outer_radius = 0.010   # 20mm outer diameter for wall thickness
    clearance = 0.0003             # 0.3mm tolerance gap for smooth sliding
    
    # Track Dimensions (Original Piston + Clearance)
    track_radius = 0.005 + clearance
    track_guide_radius = 0.001 + (clearance * 0.8)
    track_flat_y = -0.003 - clearance
    
    # SG90 Servo Dimensions
    servo_w = 0.030  # 24mm wide (X)
    servo_d = 0.014  # 14mm deep (Y)
    servo_h = 0.018  # 18mm tall (Z)

    # --- 2. BUILD THE DUMMY NEGATIVE TOOL (The "Swollen" Piston) ---
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=128, radius=track_radius, depth=housing_length + 0.002, location=(0, 0, housing_length / 2.0)
    )
    dummy_tool = bpy.context.active_object
    dummy_tool.name = "Dummy_Negative_Tool"

    angles = [0, math.pi/2, -math.pi/2]
    overlap = 0.0002
    
    for i, angle in enumerate(angles):
        x = math.sin(angle) * (track_radius - overlap)
        y = math.cos(angle) * (track_radius - overlap)
        
        bpy.ops.mesh.primitive_cylinder_add(
            vertices=32, radius=track_guide_radius, depth=housing_length + 0.002, location=(x, y, housing_length / 2.0)
        )
        guide = bpy.context.active_object
        
        bpy.context.view_layer.objects.active = dummy_tool
        bool_union = dummy_tool.modifiers.new(name=f"Merge_Guide_{i}", type='BOOLEAN')
        bool_union.operation = 'UNION'
        bool_union.object = guide
        bool_union.solver = 'EXACT'
        bpy.ops.object.modifier_apply(modifier=bool_union.name)
        bpy.data.objects.remove(guide, do_unlink=True)

    bpy.ops.mesh.primitive_cube_add(size=1)
    flat_cutter = bpy.context.active_object
    flat_cutter.scale = (0.025, 0.010, housing_length + 0.010)
    flat_cutter.location = (0, track_flat_y - 0.005, housing_length / 2.0)

    bpy.context.view_layer.objects.active = dummy_tool
    bool_flat = dummy_tool.modifiers.new(name="Cut_Flat", type='BOOLEAN')
    bool_flat.operation = 'DIFFERENCE'
    bool_flat.object = flat_cutter
    bool_flat.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_flat.name)
    bpy.data.objects.remove(flat_cutter, do_unlink=True)

    # --- 3. BUILD THE MAIN HOUSING BODY ---
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=128, radius=housing_outer_radius, depth=housing_length, location=(0, 0, housing_length / 2.0)
    )
    housing = bpy.context.active_object
    housing.name = "Actuator_Housing"

    # Solid Mount Block at the bottom
    bpy.ops.mesh.primitive_cube_add(size=1)
    servo_mount = bpy.context.active_object
    # Create an outer block with 4mm thick walls: 24+8=32, 14+8=22
    servo_mount.scale = (servo_w + 0.008, servo_d + 0.008, servo_h + 0.004) 
    servo_mount.location = (0, 0, -(servo_h + 0.004) / 2.0)
    
    bpy.context.view_layer.objects.active = housing
    bool_mount = housing.modifiers.new(name="Merge_Servo_Mount", type='BOOLEAN')
    bool_mount.operation = 'UNION'
    bool_mount.object = servo_mount
    bool_mount.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_mount.name)
    bpy.data.objects.remove(servo_mount, do_unlink=True)

    # --- 4. CUT THE TRACK & OPEN SERVO BAY ---
    
    # 4A. Cut the internal piston track
    bpy.context.view_layer.objects.active = housing
    bool_track = housing.modifiers.new(name="Cut_Track", type='BOOLEAN')
    bool_track.operation = 'DIFFERENCE'
    bool_track.object = dummy_tool
    bool_track.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_track.name)
    bpy.data.objects.remove(dummy_tool, do_unlink=True)

    # 4B. Cut out the OPEN SG90 Servo bay
    # We scale the cutter dramatically in Y and Z to blow open the front and bottom faces.
    bpy.ops.mesh.primitive_cube_add(size=1)
    servo_cavity = bpy.context.active_object
    
    # Scale: 24mm wide. 40mm deep (to cut out front). 40mm tall (to cut out bottom).
    servo_cavity.scale = (servo_w, servo_d + 0.026, servo_h + 0.022)
    
    # Position: We align the back wall and roof exactly where the motor needs to sit.
    # Roof is at Z = -0.003 (leaves 3mm structural floor).
    # Back wall is at Y = -0.007.
    servo_cavity.location = (0, 0.013, -0.023) 
    
    bpy.context.view_layer.objects.active = housing
    bool_cavity = housing.modifiers.new(name="Cut_Servo_Cavity", type='BOOLEAN')
    bool_cavity.operation = 'DIFFERENCE'
    bool_cavity.object = servo_cavity
    bool_cavity.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_cavity.name)
    bpy.data.objects.remove(servo_cavity, do_unlink=True)

    # 4C. Drill a hole for the servo shaft / leadscrew coupling
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=64, radius=0.004, depth=0.015, location=(0, 0, 0) # 8mm hole
    )
    shaft_hole = bpy.context.active_object
    
    bpy.context.view_layer.objects.active = housing
    bool_shaft = housing.modifiers.new(name="Cut_Shaft_Hole", type='BOOLEAN')
    bool_shaft.operation = 'DIFFERENCE'
    bool_shaft.object = shaft_hole
    bool_shaft.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_shaft.name)
    bpy.data.objects.remove(shaft_hole, do_unlink=True)

    # --- 5. FINALIZE & EXPORT ---
    bpy.context.view_layer.objects.active = housing
    bpy.ops.object.convert(target='MESH')
    bpy.ops.object.select_all(action='DESELECT')
    housing.select_set(True)

    os.makedirs(export_dir, exist_ok=True)
    filepath = os.path.join(export_dir, "Linear_Actuator_Housing.stl")

    bpy.ops.wm.stl_export(
        filepath=filepath,
        export_selected_objects=True,
        global_scale=1.0,
        apply_modifiers=True
    )
    
    print(f"Success! Open-bay housing exported to: {filepath}")

# ==========================================
# EXECUTION
# ==========================================
desktop_path = os.path.join(os.path.expanduser("~"), "Desktop", "3D_Prints")

if __name__ == "__main__":
    generate_actuator_housing(desktop_path)
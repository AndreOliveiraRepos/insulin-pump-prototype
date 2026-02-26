import bpy
import math
import os

def cleanup_scene():
    if bpy.context.active_object and bpy.context.active_object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

def apply_boolean(target, tool, operation='DIFFERENCE'):
    bpy.context.view_layer.objects.active = target
    bool_mod = target.modifiers.new(name=f"Bool_{tool.name}", type='BOOLEAN')
    bool_mod.operation = operation
    bool_mod.object = tool
    bool_mod.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier=bool_mod.name)
    bpy.data.objects.remove(tool, do_unlink=True)

def generate_prototyping_base(export_dir):
    cleanup_scene()
    os.makedirs(export_dir, exist_ok=True)

    # ==========================================
    # 1. MAIN BASE PLATE
    # ==========================================
    bpy.ops.mesh.primitive_cube_add(size=1)
    base = bpy.context.active_object
    base.name = "Prototyping_Base"
    base.scale = (0.085, 0.070, 0.004) 
    base.location = (0.0025, 0.010, -0.002) 

    # ==========================================
    # 2. RACK SLIDE CHANNEL
    # ==========================================
    # Rear thrust wall
    bpy.ops.mesh.primitive_cube_add(size=1)
    rear_wall = bpy.context.active_object
    rear_wall.scale = (0.075, 0.0073, 0.008) 
    rear_wall.location = (0, -0.01635, 0.004)
    apply_boolean(base, rear_wall, 'UNION')

    # Front retaining clips
    bpy.ops.mesh.primitive_cube_add(size=1)
    front_left = bpy.context.active_object
    front_left.scale = (0.020, 0.0063, 0.008)
    front_left.location = (-0.025, -0.00315, 0.004)
    apply_boolean(base, front_left, 'UNION')

    bpy.ops.mesh.primitive_cube_add(size=1)
    front_right = bpy.context.active_object
    front_right.scale = (0.020, 0.0063, 0.008)
    front_right.location = (0.025, -0.00315, 0.004)
    apply_boolean(base, front_right, 'UNION')

    # ==========================================
    # 3. MOTOR MOUNT BLOCK & CAVITY
    # ==========================================
    bpy.ops.mesh.primitive_cube_add(size=1)
    motor_block = bpy.context.active_object
    motor_block.scale = (0.034, 0.030, 0.016)
    motor_block.location = (0.025, 0.025, 0.008) 
    apply_boolean(base, motor_block, 'UNION')

    # Motor housing cavity
    bpy.ops.mesh.primitive_cube_add(size=1)
    motor_cavity = bpy.context.active_object
    motor_cavity.scale = (0.0265, 0.030, 0.030)
    motor_cavity.location = (0.025, 0.026, 0.01675) 
    apply_boolean(base, motor_cavity, 'DIFFERENCE')

    # Worm drive pass-through hole
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=64, radius=0.0065, depth=0.020, location=(0.025, 0.005, 0.009), rotation=(math.pi/2, 0, 0)
    )
    apply_boolean(base, bpy.context.active_object, 'DIFFERENCE')

    # ==========================================
    # 4. GEAR ROTATION CLEARANCES (NEGATIVE CUTS)
    # ==========================================
    # A. Pinion Relief Recess
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=64, radius=0.010, depth=0.004, location=(0, 0, 0)
    )
    pinion_recess = bpy.context.active_object
    apply_boolean(base, pinion_recess, 'DIFFERENCE')

    # B. Worm Wheel Clearance
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=64, radius=0.0225, depth=0.020, location=(0, 0, 0.015)
    )
    wheel_clearance = bpy.context.active_object
    apply_boolean(base, wheel_clearance, 'DIFFERENCE')

    # ==========================================
    # 5. CENTRAL GEAR AXLE (THE FIX: ADDED LAST)
    # ==========================================
    # Now that the pocket is carved out, we build the axle from the floor of the pocket up.
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=64, radius=0.00135, depth=0.014, location=(0, 0, 0.005)
    )
    axle = bpy.context.active_object
    axle.name = "Gear_Axle"
    apply_boolean(base, axle, 'UNION')

    # ==========================================
    # 6. FINAL EXPORT
    # ==========================================
    bpy.context.view_layer.objects.active = base
    bpy.ops.object.convert(target='MESH')
    bpy.ops.object.select_all(action='DESELECT')
    base.select_set(True)

    filepath = os.path.join(export_dir, "4_Prototyping_Base.stl")
    bpy.ops.wm.stl_export(
        filepath=filepath, export_selected_objects=True, global_scale=1.0, apply_modifiers=True
    )
    print(f"Success! Axle anchored firmly. Base exported to: {filepath}")

# ==========================================
# EXECUTION
# ==========================================
desktop_path = os.path.join(os.path.expanduser("~"), "Desktop", "ESP32_Pump")

if __name__ == "__main__":
    generate_prototyping_base(desktop_path)
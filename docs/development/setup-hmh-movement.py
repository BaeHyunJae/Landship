"""HMH 개인 맵에만 성 클릭 이동 실험 환경을 추가한다."""
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.load_level('/Game/Developers/Minhyeok/HMH'):
    raise RuntimeError('HMH load failed')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def ensure_actor(label, cls, location):
    matches = [a for a in actors.get_all_level_actors() if a.get_actor_label() == label]
    if len(matches) > 1:
        raise RuntimeError('Duplicate actor: ' + label)
    actor = matches[0] if matches else actors.spawn_actor_from_class(cls, unreal.Vector(*location))
    if not actor:
        raise RuntimeError('Spawn failed: ' + label)
    actor.set_actor_label(label)
    return actor


castle_class = unreal.load_class(None, '/Script/Landship.LSHMHMovementCastle')
if not castle_class:
    raise RuntimeError('Build LandshipEditor first')
ensure_actor('HMH_MovementCastle', castle_class, (-700, -400, 100))
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
obstacle = ensure_actor('HMH_MovementObstacle', unreal.StaticMeshActor, (0, -400, 200))
obstacle.static_mesh_component.set_static_mesh(cube)
obstacle.set_actor_scale3d(unreal.Vector(2, 6, 4))
obstacle.static_mesh_component.set_collision_profile_name('BlockAll')
obstacle.static_mesh_component.set_collision_object_type(unreal.CollisionChannel.ECC_LS_PATH_OBSTACLE)

for actor in actors.get_all_level_actors():
    if actor.get_actor_label() == 'Floor':
        actor.static_mesh_component.set_collision_object_type(unreal.CollisionChannel.ECC_LS_GROUND)

volume = ensure_actor('HMH_MovementNavBounds', unreal.NavMeshBoundsVolume, (0, 0, 100))
volume.set_actor_scale3d(unreal.Vector(24, 20, 5))
unreal.log('NAV_BOUNDS ' + str(volume.get_actor_bounds(False)))
unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.RecastNavMesh):
        actor.set_editor_property('agent_radius', 142.0)
        actor.set_editor_property('agent_height', 200.0)
unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')

camera = ensure_actor('HMH_TestCamera', unreal.CameraActor, (500, -850, 1050))
location = unreal.Vector(1700, -2400, 2600)
camera.set_actor_location(location, False, False)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(0, 0, 0)), False)
camera.set_editor_property('auto_activate_for_player', unreal.AutoReceiveInput.DISABLED)

if not levels.save_current_level():
    raise RuntimeError('HMH save failed')
unreal.log('HMH_MOVEMENT_SETUP_OK')

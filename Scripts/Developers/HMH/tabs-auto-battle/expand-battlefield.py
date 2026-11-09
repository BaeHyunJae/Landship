"""개인 TABS 맵의 지면·배치 경계·NavMesh만 확장한다. 편성·경제·성·카메라 시작 위치는 유지한다."""
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.load_level('/Game/Developers/HMH/TABS/L_TABS'):
    raise RuntimeError('Could not load personal TABS map')
all_actors = actors.get_all_level_actors()
scenarios = [a for a in all_actors if a.get_class().get_path_name() == '/Script/Landship.LSAutoBattleScenario']
floors = [a for a in all_actors if a.get_actor_label() == 'TABS_Ground' and isinstance(a, unreal.StaticMeshActor)]
volumes = [a for a in all_actors if isinstance(a, unreal.NavMeshBoundsVolume)]
if len(scenarios) != 1 or len(floors) != 1 or len(volumes) != 1:
    raise RuntimeError('Expected one scenario, ground and navigation bounds volume')
scenario, floor, volume = scenarios[0], floors[0], volumes[0]
_, extent = volume.get_actor_bounds(False)
if min(extent.x, extent.y, extent.z) <= 0:
    raise RuntimeError('Navigation volume has invalid dimensions')
old_scale = volume.get_actor_scale3d()
volume.set_actor_scale3d(unreal.Vector(old_scale.x * 5200 / extent.x,
                                     old_scale.y * 4000 / extent.y,
                                     old_scale.z * 1000 / extent.z))
center, _ = volume.get_actor_bounds(False)
location = volume.get_actor_location()
volume.set_actor_location(unreal.Vector(location.x - center.x, location.y - center.y, location.z - center.z), False, True)
floor.set_actor_scale3d(unreal.Vector(104, 80, 0.5))
scenario.set_editor_property('bounds_min', unreal.Vector2D(-4800, -3600))
scenario.set_editor_property('bounds_max', unreal.Vector2D(4800, 3600))
scenario.set_editor_property('deployment_min', unreal.Vector2D(-4400, -3200))
scenario.set_editor_property('deployment_max', unreal.Vector2D(-300, 3200))
# 이동용 NavMesh는 LSNavigationSetup이 플레이 시작 시 확장된 볼륨에서 다시 생성한다.
if not levels.save_current_level():
    raise RuntimeError('Could not save expanded personal TABS map')
unreal.log('TABS_MAP_EXPANDED_OK battlefield=96x72m deployment=41x64m')

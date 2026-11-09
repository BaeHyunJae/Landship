"""개인 TABS 맵과 최초 WBP를 준비한다. 공용 전투 에셋과 기존 맵은 수정하지 않는다."""
import unreal

path = '/Game/Developers/HMH/TABS/L_TABS'
assets = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not assets.does_asset_exist(path):
    if not assets.duplicate_asset('/Game/Developers/HMH/HMH_TroopTypes', path):
        raise RuntimeError('Could not duplicate personal troop map')
levels.load_level(path)
for actor in list(actors.get_all_level_actors()):
    class_path = actor.get_class().get_path_name()
    if class_path.startswith('/Script/Landship.') or isinstance(actor, unreal.StaticMeshActor):
        actors.destroy_actor(actor)

floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -25))
floor.set_actor_label('TABS_Ground')
mesh = floor.static_mesh_component
mesh.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
mesh.set_world_scale3d(unreal.Vector(52, 40, 0.5))
mesh.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
mesh.set_collision_profile_name('BlockAll')
mesh.set_collision_object_type(unreal.CollisionChannel.ECC_LS_GROUND)

scenario_class = unreal.load_class(None, '/Script/Landship.LSAutoBattleScenario')
mode_class = unreal.load_class(None, '/Script/Landship.LSAutoBattleGameMode')
navigation_class = unreal.load_class(None, '/Script/Landship.LSNavigationSetup')
probe_class = unreal.load_class(None, '/Script/Landship.LSAutoBattleProbe')
if not all([scenario_class, mode_class, navigation_class, probe_class]):
    raise RuntimeError('Build new native classes first')
actors.spawn_actor_from_class(scenario_class, unreal.Vector()).set_actor_label('TABS_Scenario')
actors.spawn_actor_from_class(navigation_class, unreal.Vector()).set_actor_label('TABS_Navigation')
actors.spawn_actor_from_class(probe_class, unreal.Vector()).set_actor_label('TABS_Probe')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode', mode_class)
if not any(isinstance(a, unreal.NavMeshBoundsVolume) for a in actors.get_all_level_actors()):
    raise RuntimeError('Source personal map must contain NavMesh bounds')
if not unreal.LSAutoBattleEditorLibrary.create_auto_battle_widget():
    raise RuntimeError('Could not create personal auto battle WBP')
unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
levels.save_current_level()
unreal.log('TABS_MAP_SETUP_OK ' + path)

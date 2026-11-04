"""공용 에셋을 변경하지 않고 적 전용 NavMesh 검증 맵 복사본만 저작한다."""
import unreal

source = '/Game/Developers/HMH/HMH_TroopTypes'
target = '/Game/Developers/BHJ/EnemyCombat/L_EnemyCombat'
assets = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not assets.does_asset_exist(target) and not assets.duplicate_asset(source, target):
    raise RuntimeError('Enemy map duplication failed')
if not levels.load_level(target):
    raise RuntimeError('Enemy map load failed')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if world.get_path_name().split('.')[0] != target:
    raise RuntimeError('Refusing to modify a different world')

bounds = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.NavMeshBoundsVolume)]
if not bounds:
    raise RuntimeError('Copied HMH map must contain authored NavMeshBoundsVolume geometry')
navigation = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.LSNavigationSetup)]
if len(navigation) != 1:
    raise RuntimeError('Copied HMH map must contain exactly one LSNavigationSetup')

# 검증 복사본의 충돌 지형만 평탄화하며 조명·카메라·내비게이션 데이터는 보존한다.
for actor in actors.get_all_level_actors():
    path = actor.get_class().get_path_name()
    if actor in bounds or actor in navigation:
        continue
    if path.startswith('/Script/Landship.') or isinstance(actor, unreal.StaticMeshActor) or actor.get_actor_label() == 'EnemyCombat_PlayerCastle':
        actors.destroy_actor(actor)

for volume in bounds:
    volume.set_actor_scale3d(unreal.Vector(1, 1, 1))
    _, extent = volume.get_actor_bounds(False)
    if min(extent.x, extent.y, extent.z) <= 0:
        raise RuntimeError('Copied NavMeshBoundsVolume has no usable brush bounds')
    volume.set_actor_scale3d(unreal.Vector(4000 / extent.x, 3500 / extent.y, 600 / extent.z))
    volume.set_actor_location(unreal.Vector(0, 0, 200), False, False)
navigation[0].set_editor_property('agent_radius', 142.0)
navigation[0].set_editor_property('agent_height', 284.0)
# 기본 해상도를 유지해 적 거점 시제품의 실제 접근 가능성을 검증한다.
for nav_data in actors.get_all_level_actors():
    if isinstance(nav_data, unreal.RecastNavMesh):
        resolutions = nav_data.get_editor_property('nav_mesh_resolution_params')
        for index, resolution in enumerate(resolutions):
            resolution.set_editor_property('cell_size', 38.0 if index == 0 else 19.0)
            resolutions[index] = resolution
        nav_data.set_editor_property('nav_mesh_resolution_params', resolutions)
        if [p.get_editor_property('cell_size') for p in nav_data.get_editor_property('nav_mesh_resolution_params')] != [38.0, 19.0, 19.0]:
            raise RuntimeError('NavMesh cell size was not applied')
        unreal.log('ENEMY_NAV_AUTHORING: ' + nav_data.get_path_name() + '; ' +
                   str([p.get_editor_property('cell_size') for p in nav_data.get_editor_property('nav_mesh_resolution_params')]))

def spawn(class_name, label, xyz):
    cls = unreal.load_class(None, '/Script/Landship.' + class_name)
    if not cls:
        raise RuntimeError('Build native class first: ' + class_name)
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*xyz))
    if not actor:
        raise RuntimeError('Actor spawn failed: ' + label)
    actor.set_actor_label(label)
    return actor

floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -50))
floor.set_actor_label('EnemyCombat_Floor')
mesh = floor.static_mesh_component
mesh.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
mesh.set_collision_profile_name('BlockAll')
floor.set_actor_scale3d(unreal.Vector(80, 70, 1))
_, floor_extent = floor.get_actor_bounds(False)
if floor_extent.x < 3900 or floor_extent.y < 3400:
    raise RuntimeError('Enemy floor geometry does not cover the authored scene')

castle_table = unreal.load_asset('/Game/Landship/Combat/Data/DT_CastleStats')
castle_parent = unreal.load_class(None, '/Script/Landship.LSCastle')
if not castle_table or not castle_parent:
    raise RuntimeError('Castle class or stats table missing')
castle_blueprint_path = '/Game/Developers/BHJ/EnemyCombat/BP_EnemyCombatCastle'
if assets.does_asset_exist(castle_blueprint_path):
    castle_blueprint = unreal.load_asset(castle_blueprint_path)
else:
    castle_blueprint = assets.duplicate_asset(
        '/Game/Landship/Maps/Blueprints/BP_BattlefieldCastle', castle_blueprint_path)
if not isinstance(castle_blueprint, unreal.Blueprint) or unreal.BlueprintEditorLibrary.get_blueprint_parent_class(castle_blueprint) != castle_parent:
    raise RuntimeError('BP_EnemyCombatCastle must derive directly from ALSCastle')
castle_class = unreal.BlueprintEditorLibrary.generated_class(castle_blueprint)
if not castle_class:
    raise RuntimeError('Castle fixture requires a valid compiled Blueprint template')
castle_defaults = unreal.get_default_object(castle_class)
row = unreal.DataTableRowHandle()
row.set_editor_property('data_table', castle_table)
row.set_editor_property('row_name', 'BattlefieldPrototype')
castle_defaults.set_editor_property('stats_row', row)
castle_defaults.get_editor_property('root_component').set_capsule_size(142.0, 142.0)
body = castle_defaults.get_editor_property('body')
body.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Sphere'))
body.set_editor_property('relative_scale3d', unreal.Vector(2.84, 2.84, 2.84))
movement = castle_defaults.get_movement_component()
nav = movement.get_editor_property('nav_agent_props')
nav.set_editor_property('agent_radius', 142.0)
nav.set_editor_property('agent_height', 284.0)
movement.set_editor_property('nav_agent_props', nav)
movement.set_editor_property('turn_before_forward', False)
weapons = castle_defaults.get_editor_property('weapons')
weapons.set_editor_property('cannon_launch_offset', unreal.Vector(0, 0, 150))
weapons.set_editor_property('shotgun_launch_offset', unreal.Vector(0, 0, 0))
weapons.set_editor_property('ballista_launch_offset', unreal.Vector(0, 0, 0))
unreal.BlueprintEditorLibrary.compile_blueprint(castle_blueprint)
if not assets.save_loaded_asset(castle_blueprint):
    raise RuntimeError('Castle fixture Blueprint save failed')
castle_class = unreal.BlueprintEditorLibrary.generated_class(castle_blueprint)
if not castle_class:
    raise RuntimeError('Castle fixture has no generated class after compilation')
readback = unreal.get_default_object(castle_class)
readback_row = readback.get_editor_property('stats_row')
readback_nav = readback.get_movement_component().get_editor_property('nav_agent_props')
if readback_row.get_editor_property('data_table') != castle_table or str(readback_row.get_editor_property('row_name')) != 'BattlefieldPrototype':
    raise RuntimeError('Castle fixture typed stats defaults were not preserved')
if readback.get_editor_property('root_component').get_unscaled_capsule_radius() != 142.0 or readback.get_editor_property('body').get_editor_property('relative_scale3d') != unreal.Vector(2.84, 2.84, 2.84):
    raise RuntimeError('Castle fixture body defaults were not preserved')
if readback_nav.get_editor_property('agent_radius') != 142.0 or readback_nav.get_editor_property('agent_height') != 284.0 or readback.get_movement_component().get_editor_property('turn_before_forward'):
    raise RuntimeError('Castle fixture movement defaults were not preserved')
castle = actors.spawn_actor_from_class(castle_class, unreal.Vector(-1800, -1200, 142))
if not castle:
    raise RuntimeError('Actor spawn failed: EnemyCombat_PlayerCastle')
castle.set_actor_label('EnemyCombat_PlayerCastle')
castle.set_editor_property('faction', unreal.LSFaction.ALLY)
core = spawn('LSEnemyOutpost', 'EnemyCombat_Core', (1800, 1500, 100))
# 실제 전투 검증에서도 전장 경계와 지형을 명시해 시야를 활성화한다.
settings = spawn('LSBattlefieldSettings', 'EnemyCombat_VisionSettings', (0, 0, 200))
settings.set_editor_property('bounds_min', unreal.Vector2D(-4000, -3500))
settings.set_editor_property('bounds_max', unreal.Vector2D(4000, 3500))
settings.set_editor_property('terrain_actors', [floor])
spawn('LSVisionPresentation', 'EnemyCombat_VisionPresentation', (0, 0, 200))
# 공개 위치 표식은 실물의 시야·선택·공격 가능 여부와 구분한다.
core.get_component_by_class(unreal.LSVisionParticipantComponent).set_editor_property('public_core_position', True)
setup = spawn('LSBattlefieldSetup', 'EnemyCombat_Setup', (0, 0, 200))
setup.set_editor_property('player_castle', castle)
setup.set_editor_property('core_outpost', core)
for label, role, kind, xyz in [
    ('AssaultMelee', unreal.LSEnemyRole.ASSAULT, unreal.LSTroopType.SPEARMAN, (0, -1200, 88)),
    ('AssaultRanged', unreal.LSEnemyRole.ASSAULT, unreal.LSTroopType.ARCHER, (0, -600, 88)),
    ('GuardMelee', unreal.LSEnemyRole.GUARD, unreal.LSTroopType.SPEARMAN, (1400, 1500, 88)),
    ('GuardRanged', unreal.LSEnemyRole.GUARD, unreal.LSTroopType.ARCHER, (1400, 2100, 88)),
]:
    unit = spawn('LSEnemyTroop', 'EnemyCombat_' + label, xyz)
    if not unit.set_initial_troop_type(kind):
        raise RuntimeError('Enemy troop type rejected: ' + label)
    behavior = unit.get_editor_property('behavior')
    behavior.set_editor_property('role', role)
    behavior.set_editor_property('guarded_outpost', core)
spawn('LSEnemyCombatProbe', 'EnemyCombat_Probe', (0, 0, 200))
spawn('LSEnemyVisionRuntimeProbe', 'EnemyCombat_VisionRuntimeProbe', (0, 0, 200))
spawn('LSEnemyBoundaryProbe', 'EnemyCombat_BoundaryProbe', (0, 0, 200))
spawn('LSEnemySupportProbe', 'EnemyCombat_SupportProbe', (-500, -2500, 88))
spawn('LSEnemyRegressionProbe', 'EnemyCombat_RegressionProbe', (0, 0, 200))
if not levels.save_current_level():
    raise RuntimeError('Enemy map save failed')
unreal.log('ENEMY_COMBAT_SETUP_OK: ' + target + '; navigation still requires runtime probe')

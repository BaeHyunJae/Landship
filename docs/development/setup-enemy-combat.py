"""공용 에셋을 변경하지 않고 적 전용 NavMesh 검증 맵 복사본만 저작한다."""
import unreal

source = '/Game/Developers/Minhyeok/HMH_TroopTypes'
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
    if path.startswith('/Script/Landship.') or isinstance(actor, unreal.StaticMeshActor):
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

castle = spawn('LSMobileCastle', 'EnemyCombat_PlayerCastle', (-1800, -1200, 142))
castle.set_editor_property('faction', unreal.LSFaction.ALLY)
core = spawn('LSEnemyOutpost', 'EnemyCombat_Core', (1800, 1500, 100))
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
spawn('LSEnemyBoundaryProbe', 'EnemyCombat_BoundaryProbe', (0, 0, 200))
spawn('LSEnemySupportProbe', 'EnemyCombat_SupportProbe', (-500, -2500, 88))
spawn('LSEnemyRegressionProbe', 'EnemyCombat_RegressionProbe', (0, 0, 200))
if not levels.save_current_level():
    raise RuntimeError('Enemy map save failed')
unreal.log('ENEMY_COMBAT_SETUP_OK: ' + target + '; navigation still requires runtime probe')

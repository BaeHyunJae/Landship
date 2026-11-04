"""전용 병종 맵과 병종 데이터·표시 머티리얼을 준비한다. 기존 HMH와 공용 전투 데이터는 수정하지 않는다."""
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir())
import runpy
runpy.run_path(str(project / 'Scripts/Developers/HMH/hmh-troop-types-map-check/setup-castle-weapon-data.py'))
assets = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

table_path = '/Game/Landship/Troops/Data/DT_TroopStats'
if editor_assets.does_asset_exist(table_path):
    table = unreal.load_asset(table_path)
else:
    factory = unreal.DataTableFactory()
    factory.set_editor_property('struct', unreal.load_object(None, '/Script/Landship.LSUnitStatRow'))
    table = assets.create_asset('DT_TroopStats', '/Game/Landship/Troops/Data', unreal.DataTable, factory)
if not table or not unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(table, str(project / 'Data/Troops/DT_TroopStats.csv')):
    raise RuntimeError('Troop stats CSV import failed')
editor_assets.save_loaded_asset(table)

material_path = '/Game/Landship/Troops/Materials/M_TroopPrototype'
if editor_assets.does_asset_exist(material_path):
    material = unreal.load_asset(material_path)
else:
    material = assets.create_asset('M_TroopPrototype', '/Game/Landship/Troops/Materials', unreal.Material, unreal.MaterialFactoryNew())
    color = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -300, 0)
    color.set_editor_property('parameter_name', 'TeamColor')
    color.set_editor_property('default_value', unreal.LinearColor(0.08, 0.55, 1.0, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
if not material:
    raise RuntimeError('Prototype material missing')
unreal.MaterialEditingLibrary.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH, True)
if not unreal.MaterialEditingLibrary.has_material_usage(material, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH):
    raise RuntimeError('Prototype material must support skeletal meshes')
if not editor_assets.save_loaded_asset(material):
    raise RuntimeError('Prototype material save failed')

source_map = '/Game/Developers/HMH/HMH'
target_map = '/Game/Developers/HMH/HMH_TroopTypes'
if not editor_assets.does_asset_exist(target_map):
    if not editor_assets.duplicate_asset(source_map, target_map):
        raise RuntimeError('Troop map duplication failed')
if not levels.load_level(target_map):
    raise RuntimeError('Troop map load failed')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if world.get_path_name().split('.')[0] != target_map:
    raise RuntimeError('Refusing to change a different level')
# 전용 복사본에서만 기존 HMH 검증 병력·충돌 표시기를 치운다.
for actor in actors.get_all_level_actors():
    class_path = actor.get_class().get_path_name()
    if class_path.startswith('/Script/Landship.LSHMH') and class_path not in ('/Script/Landship.LSHMHMovementCastle', '/Script/Landship.LSHMHTroopProbe'):
        actors.destroy_actor(actor)

unit_class = unreal.load_class(None, '/Script/Landship.LSTroopUnit')
castle_parent = unreal.load_class(None, '/Script/Landship.LSCastle')
movement_castle_class = unreal.load_class(None, '/Script/Landship.LSMobileCastle')
castle_table = unreal.load_asset('/Game/Landship/Combat/Data/DT_CastleStats')
if not castle_table or not castle_parent:
    raise RuntimeError('Castle class or stats table missing')
castle_blueprint_path = '/Game/Developers/HMH/BP_HMHActualCastle'
if editor_assets.does_asset_exist(castle_blueprint_path):
    castle_blueprint = unreal.load_asset(castle_blueprint_path)
else:
    castle_blueprint = editor_assets.duplicate_asset(
        '/Game/Landship/Maps/Blueprints/BP_BattlefieldCastle', castle_blueprint_path)
if not isinstance(castle_blueprint, unreal.Blueprint) or unreal.BlueprintEditorLibrary.get_blueprint_parent_class(castle_blueprint) != castle_parent:
    raise RuntimeError('BP_HMHActualCastle must derive directly from ALSCastle')
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
body.set_material(0, material)
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
if not editor_assets.save_loaded_asset(castle_blueprint):
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
probe_class = unreal.load_class(None, '/Script/Landship.LSHMHTroopProbe')
navigation_class = unreal.load_class(None, '/Script/Landship.LSNavigationSetup')
production_probe_class = unreal.load_class(None, '/Script/Landship.LSHMHProductionProbe')
weapons_probe_class = unreal.load_class(None, '/Script/Landship.LSHMHCastleWeaponsProbe')
if not unit_class or not probe_class or not castle_class or not movement_castle_class or not navigation_class or not production_probe_class or not weapons_probe_class:
    raise RuntimeError('Build the troop classes first')

def ensure_actor(label, actor_class, location):
    found = [a for a in actors.get_all_level_actors() if a.get_actor_label() == label]
    if len(found) > 1:
        raise RuntimeError('Duplicate actor: ' + label)
    if found and label in ('HMH_Castle_Ally', 'HMH_Castle_Enemy') and found[0].get_class() != actor_class:
        if not actors.destroy_actor(found[0]):
            raise RuntimeError('Could not replace actual castle fixture: ' + label)
        found = []
    actor = found[0] if found else actors.spawn_actor_from_class(actor_class, unreal.Vector(*location))
    if not actor:
        raise RuntimeError('Actor spawn failed: ' + label)
    actor.set_actor_label(label)
    actor.set_actor_location(unreal.Vector(*location), False, False)
    return actor

types = [unreal.LSTroopType.SPEARMAN, unreal.LSTroopType.ARCHER, unreal.LSTroopType.CAVALRY]
for actor in actors.get_all_level_actors():
    if actor.get_class().get_path_name() == '/Script/Landship.LSHMHMovementCastle':
        actors.destroy_actor(actor)
for faction, y, suffix in [(unreal.LSFaction.ALLY, -1100, 'Ally'), (unreal.LSFaction.ENEMY, 1100, 'Enemy')]:
    castle = ensure_actor('HMH_Castle_' + suffix, castle_class, (-800 if suffix == 'Ally' else 900, y, 144))
    castle.set_editor_property('faction', faction)
    movement_castle = ensure_actor('HMH_MobileCastle_' + suffix, movement_castle_class, (-2100, -1700 if suffix == 'Ally' else 1700, 144))
    movement_castle.set_editor_property('faction', faction)
for faction, x, suffix in [(unreal.LSFaction.ALLY, -800, 'Ally'), (unreal.LSFaction.ENEMY, 900, 'Enemy')]:
    for index, troop_type in enumerate(types):
        unit = ensure_actor('HMH_Troop_' + str(index) + '_' + suffix, unit_class, (x, -650 + index * 650, 96))
        unit.set_editor_property('faction', faction)
        unit.set_editor_property('troop_type', troop_type)
        # 편집 상태에서도 타입·진영에 따른 표시를 갱신한다.
        if not unit.set_initial_troop_type(troop_type):
            raise RuntimeError('Troop configuration rejected')
ensure_actor('HMH_Troop_Probe', probe_class, (0, 0, 0))
ensure_actor('HMH_Production_Probe', production_probe_class, (0, 0, 0))
ensure_actor('HMH_Weapons_Probe', weapons_probe_class, (0, 0, 0))
orders_probe_class = unreal.load_class(None, '/Script/Landship.LSHMHCombatOrdersProbe')
if not orders_probe_class:
    raise RuntimeError('Build combat orders probe first')
ensure_actor('HMH_Orders_Probe', orders_probe_class, (0, 0, 0))
navigation = ensure_actor('HMH_NavigationSetup', navigation_class, (0, 0, 0))
navigation.set_editor_property('agent_radius', 142.0)
navigation.set_editor_property('agent_height', 284.0)
settings_class = unreal.load_class(None, '/Script/Landship.LSBattlefieldSettings')
settings = ensure_actor('HMH_BattlefieldSettings', settings_class, (0, 0, 0))
settings.set_editor_property('bounds_min', unreal.Vector2D(-2400, -2000))
settings.set_editor_property('bounds_max', unreal.Vector2D(2400, 2000))
settings.set_editor_property('ground_height', 0.0)
# 경로 장애물은 지형 캡처와 분리하고 공통 시야 참여 규칙을 따른다.
subobjects = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
for actor in actors.get_all_level_actors():
    primitives = actor.get_components_by_class(unreal.PrimitiveComponent)
    if any(p.get_collision_object_type() == unreal.CollisionChannel.ECC_LS_PATH_OBSTACLE for p in primitives):
        if not actor.get_component_by_class(unreal.LSVisionParticipantComponent):
            handles = subobjects.k2_gather_subobject_data_for_instance(actor)
            params = unreal.AddNewSubobjectParams(parent_handle=handles[0], new_class=unreal.LSVisionParticipantComponent)
            handle, reason = subobjects.add_new_subobject(params)
            if not actor.get_component_by_class(unreal.LSVisionParticipantComponent):
                raise RuntimeError('Obstacle vision component creation failed: ' + str(reason))
terrain = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.StaticMeshActor) and not a.get_component_by_class(unreal.LSVisionParticipantComponent)]
settings.set_editor_property('terrain_actors', terrain)
if not levels.save_current_level():
    raise RuntimeError('Troop map save failed')
unreal.log('HMH_TROOP_TYPES_SETUP_OK')

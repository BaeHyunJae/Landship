"""전용 병종 맵과 병종 데이터·표시 머티리얼을 준비한다. 기존 HMH와 공용 전투 데이터는 수정하지 않는다."""
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir())
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
    unreal.MaterialEditingLibrary.recompile_material(material)
    editor_assets.save_loaded_asset(material)
if not material:
    raise RuntimeError('Prototype material missing')

source_map = '/Game/Developers/Minhyeok/HMH'
target_map = '/Game/Developers/Minhyeok/HMH_TroopTypes'
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
castle_class = unreal.load_class(None, '/Script/Landship.LSMobileCastle')
probe_class = unreal.load_class(None, '/Script/Landship.LSHMHTroopProbe')
navigation_class = unreal.load_class(None, '/Script/Landship.LSNavigationSetup')
production_probe_class = unreal.load_class(None, '/Script/Landship.LSHMHProductionProbe')
if not unit_class or not probe_class or not castle_class or not navigation_class or not production_probe_class:
    raise RuntimeError('Build the troop classes first')

def ensure_actor(label, actor_class, location):
    found = [a for a in actors.get_all_level_actors() if a.get_actor_label() == label]
    if len(found) > 1:
        raise RuntimeError('Duplicate actor: ' + label)
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
    castle = ensure_actor('HMH_MobileCastle_' + suffix, castle_class, (-800 if suffix == 'Ally' else 900, y, 144))
    castle.set_editor_property('faction', faction)
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
navigation = ensure_actor('HMH_NavigationSetup', navigation_class, (0, 0, 0))
navigation.set_editor_property('agent_radius', 142.0)
navigation.set_editor_property('agent_height', 284.0)
if not levels.save_current_level():
    raise RuntimeError('Troop map save failed')
unreal.log('HMH_TROOP_TYPES_SETUP_OK')

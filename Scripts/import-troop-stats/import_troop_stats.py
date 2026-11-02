"""Data/Troops/DT_TroopStats.csv로 DT_TroopStats DataTable을 만들거나 다시 채운다. 에디터에서 고친 행 값은 CSV 값으로 덮어쓴다."""
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir())
assets = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary

table_path = '/Game/Landship/Troops/Data/DT_TroopStats'
if editor_assets.does_asset_exist(table_path):
    table = unreal.load_asset(table_path)
else:
    factory = unreal.DataTableFactory()
    factory.set_editor_property('struct', unreal.load_object(None, '/Script/Landship.LSUnitStatRow'))
    table = assets.create_asset('DT_TroopStats', '/Game/Landship/Troops/Data', unreal.DataTable, factory)
if not table or not unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(table, str(project / 'Data/Troops/DT_TroopStats.csv')):
    raise RuntimeError('Troop stats CSV import failed')
if not editor_assets.save_loaded_asset(table):
    raise RuntimeError('Troop stats asset save failed')
unreal.log('TROOP_STATS_IMPORTED')

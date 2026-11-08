"""개인 맵 복사본에서 적 편성을 저장하고, 별도 프로세스에서 재로드해 검증한다. 원본 맵은 수정하지 않는다."""
import json
from pathlib import Path
import unreal

source = '/Game/Developers/HMH/TABS/L_TABS'
fixture = '/Game/Developers/HMH/TABS/L_EnemyPlacementToolVerify'
state_path = Path(unreal.Paths.project_saved_dir()) / 'EnemyPlacementToolVerify.json'
assets = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
scenario_class = unreal.load_class(None, '/Script/Landship.LSAutoBattleScenario')
marker_class = unreal.load_class(None, '/Script/Landship.LSAutoBattleEnemyMarker')
read = '-EnemyPlacementVerifyRead' in unreal.SystemLibrary.get_command_line()

def scenarios():
    return [a for a in actors.get_all_level_actors() if a.get_class() == scenario_class]

def snapshot(scenario):
    return [{'type': str(p.type), 'position': [p.position.x, p.position.y, p.position.z]}
            for p in scenario.get_editor_property('enemy_formation')]

if not read:
    if assets.does_asset_exist(fixture):
        raise RuntimeError('Verification fixture already exists; complete read phase before retrying')
    if not assets.duplicate_asset(source, fixture) or not levels.load_level(fixture):
        raise RuntimeError('Could not create/load personal verification copy')
    scenario, = scenarios()
    initial_count = len(scenario.get_editor_property('enemy_formation'))
    scenario.hide_enemy_markers()
    scenario.load_enemy_markers()
    scenario.set_editor_property('placement_troop_type', unreal.LSTroopType.ARCHER)
    scenario.add_enemy_marker()
    markers = [a for a in actors.get_all_level_actors() if a.get_class() == marker_class]
    if len(markers) != initial_count + 1:
        raise RuntimeError('Editor add tool did not create an additional enemy')
    scenario.apply_enemy_markers()
    if len(scenario.get_editor_property('enemy_formation')) != initial_count + 1:
        raise RuntimeError('Editor apply tool did not update formation: ' + scenario.get_editor_property('placement_tool_status'))
    state_path.write_text(json.dumps(snapshot(scenario)), encoding='utf-8')
    if not levels.save_current_level():
        raise RuntimeError('Saving verification map failed')
    unreal.log('ENEMY_PLACEMENT_WRITE_OK')
else:
    if not levels.load_level(fixture):
        raise RuntimeError('Could not load saved verification map')
    scenario, = scenarios()
    expected = json.loads(state_path.read_text(encoding='utf-8'))
    if snapshot(scenario) != expected:
        raise RuntimeError('Saved enemy formation differs after a fresh process reload')
    scenario.hide_enemy_markers()
    scenario.load_enemy_markers()
    if len([a for a in actors.get_all_level_actors() if a.get_class() == marker_class]) != len(expected):
        raise RuntimeError('Reloaded formation did not reconstruct the editor markers')
    scenario = None
    if not levels.load_level(source):
        raise RuntimeError('Could not leave verification map')
    if not assets.delete_asset(fixture):
        raise RuntimeError('Could not clean up personal verification fixture')
    # 명령줄 에디터에서 파일 삭제가 지연되었으면 이 스크립트가 생성한 단일 복사본만 정리한다.
    fixture_file = Path(unreal.Paths.project_content_dir()) / 'Developers/HMH/TABS/L_EnemyPlacementToolVerify.umap'
    if fixture_file.exists():
        fixture_file.unlink()
    state_path.unlink()
    unreal.log('ENEMY_PLACEMENT_RELOAD_OK')

"""기존 개인 자동 전투 WBP에 인게임 시나리오 편집 패널을 추가한다. 맵 편성은 변경하지 않는다."""
import unreal
if not unreal.LSAutoBattleEditorLibrary.upgrade_auto_battle_widget():
    raise RuntimeError('Could not upgrade the personal auto battle widget')
unreal.log('TABS_RUNTIME_EDITOR_UPGRADE_OK')

"""UMG 전투 패널의 최초 WBP를 만든다. 기존 디자이너 변경은 덮어쓰지 않는다."""
import unreal

if not unreal.LSBattleHUDEditorLibrary.create_battle_hud_asset():
    raise RuntimeError('Battle HUD blueprint creation failed')
unreal.log('BATTLE_HUD_ASSET_READY')

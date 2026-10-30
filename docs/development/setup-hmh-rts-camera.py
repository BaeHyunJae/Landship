"""기존 고정 카메라를 보존하되 자동 활성화만 해제한다."""
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not levels.load_level('/Game/Developers/Minhyeok/HMH'):
    raise RuntimeError('HMH load failed')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in actors.get_all_level_actors():
    if actor.get_actor_label() == 'HMH_TestCamera' and isinstance(actor, unreal.CameraActor):
        actor.set_editor_property('auto_activate_for_player', unreal.AutoReceiveInput.DISABLED)
if not levels.save_current_level():
    raise RuntimeError('HMH save failed')
unreal.log('HMH_RTS_CAMERA_SETUP_OK')

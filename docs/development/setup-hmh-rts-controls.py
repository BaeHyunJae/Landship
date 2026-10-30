"""HMH에만 직접 조종 실험용 병력과 자동 검증 액터를 추가한다."""
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.load_level('/Game/Developers/Minhyeok/HMH'):
    raise RuntimeError('HMH load failed')
unit_class = unreal.load_class(None, '/Script/Landship.LSHMHRTSUnit')
probe_class = unreal.load_class(None, '/Script/Landship.LSHMHRTSProbe')
if not unit_class or not probe_class:
    raise RuntimeError('Build LandshipEditor first')


def ensure_actor(label, cls, location):
    matches = [a for a in actors.get_all_level_actors() if a.get_actor_label() == label]
    if len(matches) > 1:
        raise RuntimeError('Duplicate actor: ' + label)
    actor = matches[0] if matches else actors.spawn_actor_from_class(cls, unreal.Vector(*location))
    if not actor:
        raise RuntimeError('Spawn failed: ' + label)
    actor.set_actor_label(label)
    return actor


for i, point in enumerate([(-1000, -500, 96), (-850, -250, 96), (-900, 50, 96), (-1050, 250, 96)]):
    unit = ensure_actor('HMH_RTS_Ally_' + str(i), unit_class, point)
    unit.set_editor_property('faction', unreal.LSFaction.ALLY)
    unit.set_editor_property('tags', [unreal.Name('RTSAlly' + str(i))])
for i, point in enumerate([(700, -500, 96), (900, 300, 96)]):
    unit = ensure_actor('HMH_RTS_Enemy_' + str(i), unit_class, point)
    unit.set_editor_property('faction', unreal.LSFaction.ENEMY)
    unit.set_editor_property('tags', [unreal.Name('RTSEnemy' + str(i))])
ensure_actor('HMH_RTS_Probe', probe_class, (0, 0, 0))
for actor in actors.get_all_level_actors():
    if actor.get_actor_label() == 'HMH_TestCamera' and isinstance(actor, unreal.CameraActor):
        actor.set_editor_property('auto_activate_for_player', unreal.AutoReceiveInput.DISABLED)
if not levels.save_current_level():
    raise RuntimeError('HMH save failed')
unreal.log('HMH_RTS_CONTROLS_SETUP_OK')

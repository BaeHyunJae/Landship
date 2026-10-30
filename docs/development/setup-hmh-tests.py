"""언리얼 Python commandlet에서 실행한다. HMH만 수정하고 템플릿 맵은 유지한다."""
import unreal

LEVEL = "/Game/Developers/Minhyeok/HMH"
level_system = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_system = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not level_system.load_level(LEVEL):
    raise RuntimeError("HMH 레벨을 불러올 수 없습니다.")

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
game_mode = unreal.load_class(None, "/Script/Landship.LSGameMode")
probe_class = unreal.load_class(None, "/Script/Landship.LSHMHSkeletonProbe")
if not game_mode or not probe_class:
    raise RuntimeError("테스트 클래스를 먼저 빌드해야 합니다.")
world.get_world_settings().set_editor_property("default_game_mode", game_mode)

# 재실행해도 사용자가 만든 액터를 지우거나 테스트 액터를 중복 생성하지 않는다.
actors = actor_system.get_all_level_actors()
probes = [a for a in actors if a.get_class() == probe_class]
if len(probes) > 1:
    raise RuntimeError("테스트 액터가 여러 개입니다. 수동으로 확인해야 합니다.")
if not probes:
    probe = actor_system.spawn_actor_from_class(probe_class, unreal.Vector(0, 600, 30))
    if not probe:
        raise RuntimeError("테스트 액터 생성에 실패했습니다.")
    probe.set_actor_label("HMH_SkeletonProbe")

cameras = [a for a in actors if a.get_actor_label() == "HMH_TestCamera"]
if not cameras:
    location = unreal.Vector(500, -850, 1050)
    rotation = unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(500, 600, 100))
    camera = actor_system.spawn_actor_from_class(unreal.CameraActor, location, rotation)
    if not camera:
        raise RuntimeError("관찰 카메라 생성에 실패했습니다.")
    camera.set_actor_label("HMH_TestCamera")
    camera.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)

if not level_system.save_current_level():
    raise RuntimeError("HMH 레벨 저장에 실패했습니다.")
unreal.log("HMH_SETUP_OK: game mode, probe and observation camera saved")

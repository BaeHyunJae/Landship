"""공개 공격 명령 검증용 임시 배치를 저장한다. 검증 뒤 정식 빌더로 복원한다."""
import os
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[2]
project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
if os.path.normcase(os.path.normpath(project)) != os.path.normcase(os.path.normpath(ROOT)):
    raise RuntimeError('Refusing to modify a different project: ' + project)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
path = '/Game/Developers/BHJ/EnemyCombat/L_EnemyCombat'
if not levels.load_level(path):
    raise RuntimeError('Could not load development combat map')
placed = list(actors.get_all_level_actors())
floor = next(a for a in placed if a.get_actor_label() == 'EnemyCombat_Floor')
core = next(a for a in placed if a.get_actor_label() == 'EnemyCombat_Core')
castle = next(a for a in placed if a.get_actor_label() == 'EnemyCombat_PlayerCastle')
settings_actors = [a for a in placed if isinstance(a, unreal.LSBattlefieldSettings)]
if len(settings_actors) > 1:
    raise RuntimeError('Multiple battlefield settings')
settings = settings_actors[0] if settings_actors else actors.spawn_actor_from_class(unreal.LSBattlefieldSettings, unreal.Vector(0, 0, 200))
settings.set_editor_property('bounds_min', unreal.Vector2D(-4000, -3500))
settings.set_editor_property('bounds_max', unreal.Vector2D(4000, 3500))
settings.set_editor_property('terrain_actors', [floor])
if not any(isinstance(a, unreal.LSVisionPresentation) for a in placed):
    actors.spawn_actor_from_class(unreal.LSVisionPresentation, unreal.Vector(0, 0, 200))
if not any(isinstance(a, unreal.LSEnemyVisionRuntimeProbe) for a in placed):
    actors.spawn_actor_from_class(unreal.LSEnemyVisionRuntimeProbe, unreal.Vector(0, 0, 200))
core.get_component_by_class(unreal.LSVisionParticipantComponent).set_editor_property('public_core_position', True)
# 명령 제출 전에 이미 확정된 아군 시야가 목표를 포함하도록 배치한다.
castle.set_actor_location(core.get_actor_location() + unreal.Vector(0, -1000, 42), False, True)
for nav in [a for a in placed if isinstance(a, unreal.RecastNavMesh)]:
    if nav.get_editor_property('nav_mesh_resolution_params')[1].get_editor_property('cell_size') != 19.0:
        raise RuntimeError('Expected default 19cm navigation')
if not levels.save_current_level():
    raise RuntimeError('Could not save development vision-combat map')
unreal.log('ENEMY_VISION_COMBAT_MAP_SAVED: explicit vision and public core; default cell19')

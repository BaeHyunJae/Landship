"""Real PIE vision integration evidence for the current development project.
Pending support queues are sampled through the development-only test library.
"""
import json
import copy
import hashlib
import os
from pathlib import Path
import time
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)

ROOT = Path(__file__).resolve().parents[2]
OUT = os.path.join(ROOT, 'Saved', 'Review', 'TASK-46', 'integrated-branch')
MAP = '/Game/Developers/BHJ/EnemyCombat/L_EnemyCombat'
os.makedirs(OUT, exist_ok=True)
REPORT = {'runs': [], 'checks': [], 'screenshots': [], 'errors': [],
          'limitations': ['Active-support details and stale native weak references are not inspected directly; pending queues and capture state are sampled across PIE.',
                          'Screenshot files require visual inspection; requesting a screenshot is not visual validation.']}
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDIT = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
UE = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
START = time.monotonic()
STATE = {'phase': 'prepare', 'since': START, 'run': 0, 'callback': None}


def save():
    REPORT['elapsed_seconds'] = time.monotonic() - START
    with open(os.path.join(OUT, 'vision-pie.json'), 'w', encoding='utf-8') as stream:
        json.dump(REPORT, stream, indent=2)


def check(name, passed, **details):
    REPORT['checks'].append(dict(run=STATE['run'], name=name, passed=bool(passed), **details))
    save()


def cls(name):
    result = unreal.load_class(None, '/Script/Landship.' + name)
    if not result:
        raise RuntimeError('Missing native class: ' + name)
    return result


def component(actor, name):
    result = actor.get_component_by_class(cls(name))
    if not result:
        raise RuntimeError('Missing component ' + name + ' on ' + actor.get_path_name())
    return result


def all_in(world, name):
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, cls(name)))


def subsystem(world, name):
    matches = [obj for obj in unreal.ObjectIterator(cls(name)) if obj.get_outer() == world]
    if len(matches) != 1:
        raise RuntimeError('Expected one world subsystem: ' + name + '; found ' + str(len(matches)))
    return matches[0]


def vector(value):
    return [value.x, value.y, value.z]


def health(actor):
    value = component(actor, 'LSHealthComponent').try_get_health()
    if value is None:
        raise RuntimeError('Health is not initialized: ' + actor.get_path_name())
    return {'current': value.current_health, 'maximum': value.max_health, 'dead': value.is_dead}


def hidden(actor):
    return actor.get_editor_property('hidden')


def markers():
    return [{'location': vector(m.location), 'kind': str(m.kind),
             'friendly': m.friendly, 'public': m.public_position}
            for m in STATE['vision'].get_minimap_markers()]


def shot(label):
    path = os.path.join(OUT, 'observed-vision-pie-%d-%s.png' % (STATE['run'], label))
    command = ('Shot SHOWUI filename="%s" -nosuffix' if label == 'public-core-hidden'
               else 'HighResShot 1600x1000 filename="%s"') % path
    unreal.SystemLibrary.execute_console_command(STATE['world'], command)
    REPORT['screenshots'].append({'path': path, 'label': label, 'requested': True})


def phase(name):
    STATE['phase'] = name
    STATE['since'] = time.monotonic()
    save()


def finish():
    if STATE['callback'] is not None:
        unreal.unregister_slate_post_tick_callback(STATE['callback'])
        STATE['callback'] = None
    for item in REPORT['screenshots']:
        item['exists'] = os.path.isfile(item['path'])
        item['bytes'] = os.path.getsize(item['path']) if item['exists'] else 0
    save()
    unreal.SystemLibrary.execute_console_command(None, 'QUIT_EDITOR')


def prepare():
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    if os.path.normcase(os.path.normpath(project)) != os.path.normcase(os.path.normpath(ROOT)):
        raise RuntimeError('Refusing to modify a different project: ' + project)
    dll = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), 'Binaries', 'Win64', 'UnrealEditor-Landship.dll')
    with open(dll, 'rb') as stream:
        REPORT['integration_dll_sha256'] = hashlib.sha256(stream.read()).hexdigest()
    if not LEVEL.load_level(MAP):
        raise RuntimeError('Could not load integration map')
    actors = list(EDIT.get_all_level_actors())
    floor = next(a for a in actors if a.get_actor_label() == 'EnemyCombat_Floor')
    core = next(a for a in actors if a.get_actor_label() == 'EnemyCombat_Core')
    castle = next(a for a in actors if a.get_actor_label() == 'EnemyCombat_PlayerCastle')
    castle.set_actor_location(unreal.Vector(-1800, -1200, 142), False, True)
    for actor in actors:
        behavior = actor.get_component_by_class(cls('LSEnemyBehaviorComponent'))
        if behavior:
            behavior.set_component_tick_enabled(False)
    settings_actors = all_in(UE.get_editor_world(), 'LSBattlefieldSettings')
    if len(settings_actors) > 1:
        raise RuntimeError('Multiple battlefield settings')
    settings = settings_actors[0] if settings_actors else EDIT.spawn_actor_from_class(cls('LSBattlefieldSettings'), unreal.Vector(0, 0, 200))
    settings.set_editor_property('bounds_min', unreal.Vector2D(-4000, -3500))
    settings.set_editor_property('bounds_max', unreal.Vector2D(4000, 3500))
    settings.set_editor_property('terrain_actors', [floor])
    if not all_in(UE.get_editor_world(), 'LSVisionPresentation'):
        EDIT.spawn_actor_from_class(cls('LSVisionPresentation'), unreal.Vector(0, 0, 200))
    component(core, 'LSVisionParticipantComponent').set_editor_property('public_core_position', True)
    source = EDIT.spawn_actor_from_class(cls('LSTroopUnit'), unreal.Vector(-3500, 3000, 88))
    source.set_actor_label('EnemyCombat_VerificationAlly')
    camera = EDIT.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 6500), unreal.Rotator(pitch=-90, yaw=0, roll=0))
    camera.set_actor_location(unreal.Vector(500, 400, 4000), False, True)
    camera.set_actor_rotation(unreal.Rotator(pitch=-90, yaw=0, roll=0), False)
    camera.set_actor_label('EnemyCombat_VerificationCamera')
    # Changes remain unsaved in the development editor world; each PIE duplicates this same baseline.
    LEVEL.editor_request_begin_play()
    phase('wait_begin')


def begin_run(world):
    STATE['run'] += 1
    STATE['world'] = world
    STATE['vision'] = subsystem(world, 'LSVisionSubsystem')
    STATE['battle'] = subsystem(world, 'LSBattlefieldStateSubsystem')
    STATE['castle'] = all_in(world, 'LSMobileCastle')[0]
    STATE['core'] = STATE['battle'].get_core_outpost()
    STATE['units'] = all_in(world, 'LSEnemyTroop')
    STATE['source'] = next(a for a in all_in(world, 'LSTroopUnit') if 'VerificationAlly' in a.get_actor_label())
    STATE['home'] = STATE['castle'].get_actor_location()
    STATE['sight'] = component(STATE['castle'], 'LSVisionParticipantComponent')
    STATE['sight'].set_sight_radius(200.0)
    for actor in [STATE['castle'], STATE['core']] + STATE['units']:
        component(actor, 'LSBasicAttackComponent').stop_attack()
    component(STATE['castle'], 'LSOrderComponent').set_component_tick_enabled(False)
    for actor in [STATE['core']] + STATE['units']:
        component(actor, 'LSEnemyBehaviorComponent').set_component_tick_enabled(False)
        controller = unreal.AIHelperLibrary.get_ai_controller(actor)
        if controller:
            controller.stop_movement()
    assault = next(a for a in STATE['units'] if 'AssaultRanged' in a.get_actor_label())
    STATE['assault'] = assault
    STATE['assault_start'] = assault.get_actor_location()
    component(assault, 'LSEnemyBehaviorComponent').set_component_tick_enabled(True)
    # Use a real PIE camera, keeping a wide top-down view of the four authored roles.
    camera = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
                  if 'VerificationCamera' in a.get_actor_label())
    STATE['camera'] = camera
    unreal.GameplayStatics.get_player_controller(world, 0).set_view_target_with_blend(camera, 0)
    initial = {'world': world.get_path_name(), 'castle_health': health(STATE['castle']),
               'core_health': health(STATE['core']), 'discovered': STATE['vision'].has_discovered_enemy(),
               'battle_ended': STATE['battle'].is_battle_ended_for_enemies(),
               'core_capture': str(STATE['battle'].try_get_core_capture()),
               'unit_paths': [a.get_path_name() for a in STATE['units']]}
    initial['unit_health'] = [health(a) for a in STATE['units']]
    initial['pending_support'] = [unreal.LSEnemyVisionTestLibrary.pending_support_count(a) for a in STATE['units']]
    initial['camera_location'] = vector(camera.get_actor_location())
    initial['camera_rotation'] = str(camera.get_actor_rotation())
    hud = unreal.GameplayStatics.get_player_controller(world, 0).get_hud()
    initial['hud_class'] = hud.get_class().get_path_name()
    widgets = unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.LSMinimapWidget, True)
    check('minimap_widget_present', any(widget.is_in_viewport() for widget in widgets), hud_class=initial['hud_class'])
    REPORT['runs'].append(initial)
    check('fresh_health_and_battle', initial['castle_health']['current'] == initial['castle_health']['maximum']
          and initial['core_health']['current'] == initial['core_health']['maximum'] and not initial['battle_ended']
          and all(h['current'] == h['maximum'] and not h['dead'] for h in initial['unit_health'])
          and all(n == 0 for n in initial['pending_support']) and STATE['battle'].try_get_core_capture() is None, initial=copy.deepcopy(initial))
    phase('initial_hidden')


def tick(delta):
    try:
        now = time.monotonic()
        if now - START > 120:
            raise TimeoutError('PIE verification exceeded 120 seconds')
        current = STATE['phase']
        elapsed = now - STATE['since']
        if current == 'prepare':
            prepare()
        elif current == 'wait_begin':
            world = UE.get_game_world()
            if world and elapsed > 0.5:
                begin_run(world)
        elif current == 'initial_hidden' and elapsed > 1:
            vision, core = STATE['vision'], STATE['core']
            values = markers()
            check('public_core_hidden_without_discovery', vision.is_vision_enabled()
                  and not vision.is_actor_visible_to_friendly(core) and hidden(core)
                  and not vision.has_discovered_enemy() and any(m['public'] for m in values), markers=values)
            pc = unreal.GameplayStatics.get_player_controller(STATE['world'], 0)
            check('hidden_core_cannot_be_selected', not unreal.LSEnemyVisionTestLibrary.try_select(pc, core))
            check('hidden_attack_fixture_ready', component(STATE['castle'], 'LSBasicAttackComponent').is_configured())
            check('hidden_core_rejects_attack_order', not unreal.LSEnemyVisionTestLibrary.try_attack_order(STATE['castle'], core))
            check('hidden_core_rejects_basic_attack', not unreal.LSEnemyVisionTestLibrary.try_basic_attack(STATE['castle'], core))
            STATE['hidden_core_health'] = health(core)['current']
            STATE['hidden_attack_position'] = vector(STATE['castle'].get_actor_location())
            shot('public-core-hidden')
            phase('hold_hidden_capture')
        elif current == 'hold_hidden_capture' and elapsed > 0.5:
            STATE['sight'].set_sight_radius(6000)
            phase('visible_attack_control')
        elif current == 'visible_attack_control' and elapsed > 0.5:
            position = vector(STATE['castle'].get_actor_location())
            fixed = (STATE['castle'].get_actor_location() - unreal.Vector(*STATE['hidden_attack_position'])).length() < 0.01
            check('visible_core_accepts_attack_order', fixed and unreal.LSEnemyVisionTestLibrary.try_attack_order(STATE['castle'], STATE['core']),
                  hidden_position=STATE['hidden_attack_position'], visible_position=position)
            check('visible_core_accepts_basic_attack', fixed and unreal.LSEnemyVisionTestLibrary.try_basic_attack(STATE['castle'], STATE['core']))
            STATE['sight'].set_sight_radius(200)
            phase('independent_ai')
        elif current == 'independent_ai' and (elapsed > 8 or (elapsed > 4 and health(STATE['castle'])['current'] < REPORT['runs'][-1]['castle_health']['current'])):
            actor = STATE['assault']
            movement = (actor.get_actor_location() - STATE['assault_start']).length()
            check('ai_moves_outside_friendly_sight', movement > 20 and not STATE['vision'].is_actor_visible_to_friendly(actor),
                  displacement=movement, state=str(component(actor, 'LSEnemyBehaviorComponent').get_state()))
            check('hidden_enemy_applies_real_damage', health(STATE['castle'])['current'] < REPORT['runs'][-1]['castle_health']['current']
                  and not STATE['vision'].is_actor_visible_to_friendly(actor), health=health(STATE['castle']),
                  wait_seconds=elapsed, distance=(actor.get_actor_location() - STATE['castle'].get_actor_location()).length(),
                  target=str(component(actor, 'LSBasicAttackComponent').get_attack_target()))
            check('hidden_core_took_no_public_attack_damage', health(STATE['core'])['current'] == STATE['hidden_core_health'])
            component(actor, 'LSEnemyBehaviorComponent').set_component_tick_enabled(False)
            controller = unreal.AIHelperLibrary.get_ai_controller(actor)
            if controller:
                controller.stop_movement()
            STATE['castle'].set_actor_location(STATE['core'].get_actor_location() + unreal.Vector(-700, -500, 42), False, True)
            STATE['sight'].set_sight_radius(6000)
            phase('revealed')
        elif current == 'revealed' and elapsed > 1:
            check('core_and_four_roles_visible_discovery', STATE['vision'].has_discovered_enemy()
                  and all(STATE['vision'].is_actor_visible_to_friendly(a) and not hidden(a)
                          for a in [STATE['core']] + STATE['units']), markers=markers())
            pc = unreal.GameplayStatics.get_player_controller(STATE['world'], 0)
            check('visible_core_selectable_for_health', unreal.LSEnemyVisionTestLibrary.try_select(pc, STATE['core']))
            shot('four-roles-revealed')
            phase('hold_revealed_capture')
        elif current == 'hold_revealed_capture' and elapsed > 0.5:
            STATE['castle'].set_actor_location(STATE['home'], False, True)
            STATE['sight'].set_sight_radius(700)
            phase('reconcealed')
        elif current == 'reconcealed' and elapsed > 1:
            check('core_reconcealed_discovery_retained', hidden(STATE['core'])
                  and not STATE['vision'].is_actor_visible_to_friendly(STATE['core'])
                  and STATE['vision'].has_discovered_enemy() and any(m['public'] for m in markers()), markers=markers())
            pc = unreal.GameplayStatics.get_player_controller(STATE['world'], 0)
            check('hidden_core_prunes_health_selection', not pc.get_selected_actors())
            shot('reconcealed')
            phase('hold_reconcealed_capture')
        elif current == 'hold_reconcealed_capture' and elapsed > 0.5:
            archer = next(a for a in STATE['units'] if 'GuardRanged' in a.get_actor_label())
            STATE['archer'] = archer
            camera_location = archer.get_actor_location() + unreal.Vector(0, -1100, 900)
            STATE['camera'].set_actor_location(camera_location, False, True)
            STATE['camera'].set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera_location, archer.get_actor_location()), False)
            STATE['castle'].set_actor_location(archer.get_actor_location() + unreal.Vector(400, 0, 54), False, True)
            STATE['sight'].set_sight_radius(6000)
            component(archer, 'LSEnemyBehaviorComponent').set_component_tick_enabled(True)
            STATE['projectile_seen'] = False
            phase('projectile')
        elif current == 'projectile':
            projectiles = all_in(STATE['world'], 'LSBasicProjectile')
            if projectiles and not STATE['projectile_seen']:
                STATE['projectile_seen'] = True
                REPORT['runs'][-1]['projectile_paths'] = [p.get_path_name() for p in projectiles]
                shot('actual-projectile')
            if elapsed > 5:
                check('actual_projectile_observed', STATE['projectile_seen'])
                component(STATE['archer'], 'LSEnemyBehaviorComponent').set_component_tick_enabled(False)
                victim = next(a for a in STATE['units'] if 'GuardMelee' in a.get_actor_label())
                STATE['victim'] = victim
                request = unreal.LSHealthComponent.make_damage_request(100000, STATE['castle'], STATE['castle'])
                component(victim, 'LSHealthComponent').apply_damage(request)
                phase('death')
        elif current == 'death' and elapsed > 0.5:
            check('death_observed', health(STATE['victim'])['dead'], health=health(STATE['victim']))
            shot('death')
            phase('hold_death_capture')
        elif current == 'hold_death_capture' and elapsed > 0.5:
            request = unreal.LSHealthComponent.make_damage_request(1, STATE['source'], STATE['source'])
            component(STATE['core'], 'LSHealthComponent').apply_damage(request)
            pending = [unreal.LSEnemyVisionTestLibrary.pending_support_count(a) for a in STATE['units']]
            check('support_seeded_before_pie_end', any(n > 0 for n in pending), pending=pending)
            request = unreal.LSHealthComponent.make_damage_request(100000, STATE['castle'], STATE['castle'])
            component(STATE['core'], 'LSHealthComponent').apply_damage(request)
            check('capture_recorded_before_pie_end', STATE['battle'].is_battle_ended_for_enemies()
                  and STATE['battle'].try_get_core_capture() is not None)
            LEVEL.editor_request_end_play()
            # Release Python actor/subsystem references before the next PIE world.
            for key in ('world', 'vision', 'battle', 'castle', 'core', 'units', 'sight', 'assault', 'archer', 'victim', 'camera', 'source'):
                STATE.pop(key, None)
            phase('wait_end')
        elif current == 'wait_end' and not UE.get_game_world() and elapsed > 1:
            if STATE['run'] < 2:
                LEVEL.editor_request_begin_play()
                phase('wait_begin')
            else:
                check('two_fresh_pie_worlds', len(REPORT['runs']) == 2
                      and all(not r['discovered'] for r in REPORT['runs']))
                finish()
    except Exception:
        REPORT['errors'].append(traceback.format_exc())
        try:
            LEVEL.editor_request_end_play()
        finally:
            finish()

STATE['callback'] = unreal.register_slate_post_tick_callback(tick)
save()

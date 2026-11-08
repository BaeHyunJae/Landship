"""Author a separate arena in a tracer phase and an additive full phase."""
import argparse
import csv
import io
import importlib.util
from pathlib import Path
import sys
import unreal

ROOT = '/Game/Landship/ShellExperiment'
MAP = ROOT + '/Maps/L_ShellChainArena'
SOURCE = ROOT + '/Maps/L_ShellExperiment'
TAG = 'LS_ChainArena_v1_'
RIM_HEIGHT_PREFIX = TAG + 'rim_height_'
RIM_HEIGHT_MARKER = RIM_HEIGHT_PREFIX + '120'
RIM_SPECS = {
    'WestRim': (-3180, 0, .4, 40),
    'NorthRim': (0, 1980, 64, .4),
    'SouthRim': (0, -1980, 64, .4),
    'EastRimMiddle': (3180, 0, .4, 26),
    'EastRimNorth': (3180, 1950, .4, 1),
    'EastRimSouth': (3180, -1950, .4, 1),
}
HEAVY_LANE_PREFIX = TAG + 'heavy_lane_'
HEAVY_LANE_MARKER = HEAVY_LANE_PREFIX + 'south'
HEAVY_OLD = (450, 0, 140)
DEPLOY_OLD = (-1100, 0, 140)
HEAVY_SOUTH = (450, -950, 140)
DEPLOY_SOUTH = (-1100, -900, 140)

spec = importlib.util.spec_from_file_location('supply_authoring', str(Path(__file__).with_name('setup-shell-supply-experiment.py')))
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
require, native, load, generated = helper.require, helper.native, helper.load_asset, helper.generated
vec, near = unreal.Vector, helper.near_vector
assets = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def create_blueprint(name, parent, configure):
    path = ROOT + '/Blueprints/' + name
    require(not assets.does_asset_exist(path), 'Arena asset already exists; preserving it: ' + path)
    return helper.blueprint(name, parent, configure)


def create_table():
    path = ROOT + '/Data/DT_ShellArenaEnemyStats'
    require(not assets.does_asset_exist(path), 'Arena table already exists; preserving it: ' + path)
    factory = unreal.DataTableFactory()
    factory.set_editor_property('struct', unreal.load_object(None, '/Script/Landship.LSUnitStatRow'))
    table = require(unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'DT_ShellArenaEnemyStats', ROOT + '/Data', unreal.DataTable, factory), 'Arena table creation failed')
    csv = Path(unreal.Paths.project_dir()) / 'Data/ShellExperiment/DT_ShellArenaEnemyStats.csv'
    require(unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(table, str(csv)), 'Arena CSV import failed')
    helper.save(table)
    return table


def spawn(cls, label, location, rotation=None):
    actor = require(actors.spawn_actor_from_class(cls, vec(*location), rotation or unreal.Rotator()), 'Spawn failed: ' + label)
    actor.set_actor_label('Arena_' + label)
    actor.set_editor_property('tags', list(actor.get_editor_property('tags')) + [unreal.Name(TAG + 'actor')])
    return actor


def terrain(label, center, half, material, physical, yaw=0, blocks_blast=False):
    obj = spawn(unreal.StaticMeshActor, label, center, unreal.Rotator(yaw=yaw))
    mesh = obj.static_mesh_component
    mesh.set_static_mesh(load('/Engine/BasicShapes/Cube'))
    require(unreal.LSShellSupplyEditorLibrary.configure_floor_collision(mesh), 'Terrain collision setup failed')
    mesh.set_material(0, material)
    mesh.set_phys_material_override(physical)
    if blocks_blast:
        mesh.set_mobility(unreal.ComponentMobility.STATIC)
        mesh.set_collision_object_type(unreal.CollisionChannel.ECC_WORLD_STATIC)
        mesh.set_collision_response_to_channel(unreal.CollisionChannel.ECC_LS_EXPLOSION_OCCLUSION, unreal.CollisionResponseType.ECR_BLOCK)
    obj.set_actor_scale3d(vec(*(x / 50 for x in half)))
    return obj


def configure_coordinator(obj, castle, core):
    obj.set_editor_property('player_castle', castle)
    obj.set_editor_property('core_outpost', core)
    obj.set_editor_property('rule_profile', unreal.LSShellRuleProfile.SUPPLY_STAGE_ONE)
    obj.set_editor_property('supply_stage', unreal.LSShellSupplyStage.COMPLETE)
    modules = [getattr(unreal.LSShellModule, name) for name in
               ('BLAST', 'THRUST', 'ELECTRIC', 'GRAVITY', 'IRON', 'SPLIT', 'ADHESION', 'COOLING')]
    obj.set_editor_property('available_modules', modules)
    obj.set_editor_property('initial_first', unreal.LSShellModule.ELECTRIC)
    obj.set_editor_property('initial_second', unreal.LSShellModule.THRUST)
    obj.set_editor_property('enable_events', False)
    tuning = obj.get_editor_property('tuning')
    for key, value in (('reload_seconds', 4.), ('module_delay', .6), ('gravity_radius', 600.),
                       ('gravity_seconds', 2.), ('blast_damage', 150.), ('blast_radius', 500.)):
        tuning.set_editor_property(key, value)
    obj.set_editor_property('tuning', tuning)
    tuning = obj.get_editor_property('supply_tuning')
    tuning.set_editor_property('thrust_momentum', 660000.)
    tuning.set_editor_property('blast_momentum', 650000.)
    arena = tuning.get_editor_property('arena')
    arena.set_editor_property('enabled', True)
    tuning.set_editor_property('arena', arena)
    obj.set_editor_property('supply_tuning', tuning)
    for key, name in (('fire_sound', 'S_ShellFire'), ('explosion_sound', 'S_ShellExplosion'), ('impact_sound', 'S_ShellImpact')):
        obj.set_editor_property(key, load(ROOT + '/Audio/' + name))


def create_private_assets():
    table = create_table()
    def enemy_defaults(obj):
        obj.set_editor_property('stats_table', table)
        obj.set_editor_property('stats_row_name', unreal.Name('None'))
        siege = obj.get_editor_property('siege')
        for key, value in (('initial_delay_seconds', 2.), ('deploy_seconds', 1.),
                           ('initial_load_seconds', 10.), ('reload_seconds', 4.)):
            siege.set_editor_property(key, value)
    enemy = create_blueprint('BP_ShellArenaEnemy', native('LSShellExperimentEnemy'), enemy_defaults)
    def barrel_defaults(obj):
        obj.get_editor_property('reactive').set_editor_property('detonation_radius', 500.)
    barrel = create_blueprint('BP_ShellArenaBarrel', native('LSShellBarrel'), barrel_defaults)
    def plate_defaults(obj):
        half = vec(15, 350, 450)
        obj.set_editor_property('half_extent', half)
        helper.actor_body(obj).set_box_extent(half)
        obj.get_editor_property('visual').set_relative_scale3d(vec(.3, 7, 9))
        obj.set_editor_property('intrinsic_mass_kg', 250.)
        obj.set_editor_property('max_health', 300.)
        obj.set_editor_property('initially_supported', False)
    plate = create_blueprint('BP_ShellArenaCargoPlate', native('LSShellPlate'), plate_defaults)
    return table, enemy, barrel, plate


def authored():
    found = {}
    for actor in actors.get_all_level_actors():
        label = actor.get_actor_label()
        if label.startswith('Arena_'):
            require(label not in found, 'Duplicate arena label: ' + label)
            require(unreal.Name(TAG + 'actor') in actor.get_editor_property('tags'), 'Arena actor ownership changed: ' + label)
            found[label] = actor
    return found


def verify_tracer(p, classes):
    expected = {
        'PlayerCastle': (classes['castle'], (-2400, 0, 500)), 'Core': (classes['core'], (2200, 0, 400)),
        'BallNorth': (native('LSShellMetalBall'), (-650, 500, 70)),
        'BarrelNorth1': (classes['barrel'], (-300, 600, 60)), 'BarrelNorth2': (classes['barrel'], (-50, 510, 60)),
        'AssaultNorth1': (classes['enemy'], (-350, 350, 88)), 'AssaultNorth2': (classes['enemy'], (-170, 430, 88)),
        'AssaultNorth3': (classes['enemy'], (100, 650, 88)), 'PlayerStart': (unreal.PlayerStart.static_class(), (-200, 0, 0)),
        'Floor': (unreal.StaticMeshActor.static_class(), (0, 0, -50)),
        'GuideWall': (unreal.StaticMeshActor.static_class(), (300, 750, 120)),
        'Coordinator': (native('LSShellExperimentCoordinator'), (0, 0, 0)),
        'BattlefieldSetup': (native('LSBattlefieldSetup'), (0, 0, 0)),
        'BattlefieldSettings': (native('LSBattlefieldSettings'), (0, 0, 0)),
    }
    for label, (cls, location) in expected.items():
        actor = require(p.get('Arena_' + label), 'Missing arena actor: ' + label)
        require(actor.get_class() == cls and near(actor.get_actor_location(), location), 'Arena type or location changed: ' + label)
        rotation = actor.get_actor_rotation()
        expected_yaw = -35 if label == 'GuideWall' else 0
        require(abs(rotation.pitch) < .01 and abs(rotation.roll) < .01 and abs(rotation.yaw - expected_yaw) < .01,
                'Arena rotation changed: ' + label)
        expected_scale = (64, 40, 1) if label == 'Floor' else ((6, .8, 2.4) if label == 'GuideWall' else (1, 1, 1))
        require(near(actor.get_actor_scale3d(), expected_scale), 'Arena scale changed: ' + label)
    require(abs(p['Arena_BallNorth'].get_editor_property('intrinsic_mass_kg') - 750.) < .01, 'North ball mass changed')
    co = p['Arena_Coordinator']
    require(co.get_editor_property('player_castle') == p['Arena_PlayerCastle'] and
            co.get_editor_property('core_outpost') == p['Arena_Core'] and
            co.get_editor_property('supply_stage') == unreal.LSShellSupplyStage.COMPLETE and
            co.get_editor_property('supply_tuning').get_editor_property('arena').get_editor_property('enabled'),
            'Arena coordinator changed; preserving map')
    require(co.get_editor_property('rule_profile') == unreal.LSShellRuleProfile.SUPPLY_STAGE_ONE and
            co.get_editor_property('initial_first') == unreal.LSShellModule.ELECTRIC and
            co.get_editor_property('initial_second') == unreal.LSShellModule.THRUST and
            not co.get_editor_property('enable_events'), 'Arena choices changed')
    tuning = co.get_editor_property('tuning')
    for key, value in (('reload_seconds', 4.), ('module_delay', .6), ('gravity_radius', 600.),
                       ('gravity_seconds', 2.), ('blast_damage', 150.), ('blast_radius', 500.)):
        require(abs(tuning.get_editor_property(key) - value) < .001, 'Arena tuning changed: ' + key)
    tuning = co.get_editor_property('supply_tuning')
    for key, value in (('thrust_momentum', 660000.), ('blast_momentum', 650000.)):
        require(abs(tuning.get_editor_property(key) - value) < .01, 'Arena tuning changed: ' + key)
    expected_modules = [getattr(unreal.LSShellModule, name) for name in
                        ('BLAST', 'THRUST', 'ELECTRIC', 'GRAVITY', 'IRON', 'SPLIT', 'ADHESION', 'COOLING')]
    require(list(co.get_editor_property('available_modules')) == expected_modules, 'Arena module choices changed')
    setup = p['Arena_BattlefieldSetup']
    require(setup.get_editor_property('player_castle') == p['Arena_PlayerCastle'] and
            setup.get_editor_property('core_outpost') == p['Arena_Core'], 'Arena battlefield setup references changed')
    for index in range(1, 4):
        enemy = p['Arena_AssaultNorth%d' % index]
        require(enemy.get_editor_property('enemy_kind') == unreal.LSShellEnemyKind.ASSAULT and
                enemy.get_editor_property('stats_table') == classes['table'], 'Arena enemy role changed')


def tracer(classes, physical, visual):
    require(levels.new_level_from_template(MAP, SOURCE), 'Arena template creation failed')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name().split('.')[0] == MAP, 'Wrong world')
    for obj in list(actors.get_all_level_actors()):
        if helper.is_copied_gameplay(obj):
            require(actors.destroy_actor(obj), 'Cannot remove template gameplay actor')
    floor = terrain('Floor', (0, 0, -50), (3200, 2000, 50), visual, physical)
    wall = terrain('GuideWall', (300, 750, 120), (300, 40, 120), visual, physical, -35)
    castle = spawn(classes['castle'], 'PlayerCastle', (-2400, 0, 500))
    core = spawn(classes['core'], 'Core', (2200, 0, 400))
    for obj in (castle, core): helper.no_concealment(obj, obj == core)
    spawn(native('LSShellMetalBall'), 'BallNorth', (-650, 500, 70)).set_editor_property('intrinsic_mass_kg', 750.)
    for i, loc in enumerate(((-300, 600, 60), (-50, 510, 60)), 1):
        spawn(classes['barrel'], 'BarrelNorth%d' % i, loc)
    for i, loc in enumerate(((-350, 350, 88), (-170, 430, 88), (100, 650, 88)), 1):
        spawn(classes['enemy'], 'AssaultNorth%d' % i, loc)
    setup = spawn(native('LSBattlefieldSetup'), 'BattlefieldSetup', (0, 0, 0))
    for key, value in (('player_castle', castle), ('core_outpost', core)): setup.set_editor_property(key, value)
    co = spawn(native('LSShellExperimentCoordinator'), 'Coordinator', (0, 0, 0))
    configure_coordinator(co, castle, core)
    co.set_editor_property('tags', list(co.get_editor_property('tags')) + [unreal.Name(TAG + 'tracer')])
    settings = spawn(native('LSBattlefieldSettings'), 'BattlefieldSettings', (0, 0, 0))
    for key, value in (('bounds_min', unreal.Vector2D(-3200, -2000)), ('bounds_max', unreal.Vector2D(3200, 2000)),
                       ('ground_height', 0.), ('outside_darkness', 0.), ('terrain_actors', [floor, wall])):
        settings.set_editor_property(key, value)
    spawn(unreal.PlayerStart, 'PlayerStart', (-200, 0, 0))
    world.get_world_settings().set_editor_property('default_game_mode', generated(load(ROOT + '/Blueprints/BP_ShellSupplyGameMode')))
    verify_tracer(authored(), classes)
    require(levels.save_current_level(), 'Arena tracer save failed')
    unreal.log('ARENA_TRACER_SAVED: PIE, navigation and gameplay unobserved')


def configure_route(route, start, end, first, repeat, slots, charged, offsets, exits, deployment):
    for key, value in (('start', vec(*start)), ('end', vec(*end)), ('first_departure_seconds', first),
                       ('repeat_seconds', repeat), ('cargo_slots', slots), ('charged_cargo_slots', charged),
                       ('unload_offsets', [vec(*v) for v in offsets]), ('exit_waypoints', [vec(*v) for v in exits]),
                       ('siege_deployment_location', vec(*deployment))):
        route.set_editor_property(key, value)
    return route


def full(p, classes, physical, visual):
    verify_tracer(p, classes)
    require(not any(str(tag).startswith(HEAVY_LANE_PREFIX) for tag in
                    p['Arena_Coordinator'].get_editor_property('tags')), 'Unexpected Heavy lane marker on tracer')
    require(not any(str(tag).startswith(RIM_HEIGHT_PREFIX) for tag in
                    p['Arena_Coordinator'].get_editor_property('tags')), 'Unexpected rim height marker on tracer')
    require(set(p) == {'Arena_' + x for x in ('PlayerCastle', 'Core', 'BallNorth', 'BarrelNorth1', 'BarrelNorth2',
        'AssaultNorth1', 'AssaultNorth2', 'AssaultNorth3', 'PlayerStart', 'Floor', 'GuideWall',
        'Coordinator', 'BattlefieldSetup', 'BattlefieldSettings')}, 'Unexpected or partial arena actors; preserving map')
    settings = p['Arena_BattlefieldSettings']
    require(list(settings.get_editor_property('terrain_actors')) == [p['Arena_Floor'], p['Arena_GuideWall']],
            'Arena terrain references changed; preserving map')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    helper.navigation(world, list(actors.get_all_level_actors()))
    require(world.get_world_settings().get_editor_property('default_game_mode') ==
            generated(load(ROOT + '/Blueprints/BP_ShellSupplyGameMode')), 'Arena GameMode changed')
    for name in ('Floor', 'GuideWall'):
        error = unreal.LSShellSupplyEditorLibrary.get_floor_physics_error(
            p['Arena_' + name].static_mesh_component, physical)
        require(not error, 'Arena terrain physics changed: %s: %s' % (name, error))
    # Validate the original tracer wall before upgrading it to fixed blast cover.
    channel = unreal.CollisionChannel.ECC_LS_EXPLOSION_OCCLUSION
    response = unreal.CollisionResponseType.ECR_BLOCK
    wall = p['Arena_GuideWall'].static_mesh_component
    wall.set_mobility(unreal.ComponentMobility.STATIC)
    wall.set_collision_object_type(unreal.CollisionChannel.ECC_WORLD_STATIC)
    wall.set_collision_response_to_channel(channel, response)
    terrain_actors = list(settings.get_editor_property('terrain_actors'))
    for label, center, half in (
        ('WestRim', (-3180, 0, 60), (20, 2000, 60)),
        ('NorthRim', (0, 1980, 60), (3200, 20, 60)),
        ('SouthRim', (0, -1980, 60), (3200, 20, 60)),
        ('EastRimMiddle', (3180, 0, 60), (20, 1300, 60)),
        ('EastRimNorth', (3180, 1950, 60), (20, 50, 60)),
        ('EastRimSouth', (3180, -1950, 60), (20, 50, 60)),
        ('ExitApron', (3600, 0, -51), (400, 2000, 50))):
        terrain_actors.append(terrain(label, center, half, visual, physical, blocks_blast=label != 'ExitApron'))
    settings.set_editor_property('terrain_actors', terrain_actors)
    spawn(native('LSShellMetalBall'), 'BallIntercept', (-1800, 160, 70)).set_editor_property('intrinsic_mass_kg', 750.)
    spawn(native('LSShellMetalBall'), 'BallSouth', (-1850, -300, 70)).set_editor_property('intrinsic_mass_kg', 750.)
    spawn(classes['barrel'], 'BarrelSouth', (-1500, -490, 60))
    spawn(native('LSShellBattery'), 'BatterySouth', (-1300, -600, 55))
    plate = spawn(native('LSShellPlate'), 'PlateSouth', (-1260, -455, 15))
    plate.set_editor_property('half_extent', vec(180, 100, 15))
    plate.set_editor_property('intrinsic_mass_kg', 250.)
    plate.set_editor_property('initially_supported', False)
    south = []
    for i, loc in enumerate(((-900, -800, 88), (-650, -700, 88), (-400, -500, 88)), 1):
        south.append(spawn(classes['enemy'], 'AssaultSouth%d' % i, loc))
    heavy = spawn(classes['enemy'], 'Heavy', HEAVY_SOUTH)
    heavy.set_editor_property('enemy_kind', unreal.LSShellEnemyKind.HEAVY)
    heavy.get_editor_property('siege').set_editor_property('deployment_location', vec(*DEPLOY_SOUTH))
    plate_slot = helper.cargo_slot(classes['plate'], (-100, 0, 520), keep_racked=True)
    shield = spawn(native('LSShellTransport'), 'Shield', (1250, 0, 70), unreal.Rotator(yaw=30))
    for key, value in (('auto_start_route', False), ('route_start', vec(1250, 0, 70)),
                       ('route_end', vec(1400, 0, 70)), ('cargo_slots', [plate_slot])):
        shield.set_editor_property(key, value)
    ball_slot = helper.cargo_slot(native('LSShellMetalBall'), (0, -60, 140), 750)
    barrel_a = helper.cargo_slot(classes['barrel'], (0, 70, 130), 60, True)
    barrel_b = helper.cargo_slot(classes['barrel'], (50, 0, 130), 60, True)
    battery = helper.cargo_slot(native('LSShellBattery'), (50, 0, 125))
    vehicle = spawn(native('LSShellTransport'), 'TransportA', (2400, -1500, 70))
    for key, value in (('route_start', vec(2400, -1500, 70)), ('route_end', vec(1150, -500, 70)),
                       ('cargo_slots', [ball_slot, barrel_a])): vehicle.set_editor_property(key, value)
    schedule = spawn(native('LSShellSupplySchedule'), 'Schedule', (0, 0, 0))
    a = configure_route(schedule.get_editor_property('route_a'), (2400, -1500, 70), (1150, -500, 70), 0., 24.,
                        [ball_slot, barrel_a], [], [(0, 0, 0), (0, -500, 0), (-400, -250, 0)],
                        [(1150, -1600, 70), (3600, -1600, 70)], (-1100, -900, 140))
    b = configure_route(schedule.get_editor_property('route_b'), (2400, 1500, 70), (1150, 850, 70), 12., 36.,
                        [plate_slot, barrel_b], [plate_slot, battery], [(0, 0, 0), (0, 500, 0), (0, -500, 0)],
                        [(1150, 1600, 70), (3600, 1600, 70)], (-1100, 900, 140))
    values = (('route_a', a), ('route_b', b), ('initial_route_a_transport', vehicle),
              ('initial_assaults', [p['Arena_AssaultNorth%d' % i] for i in range(1, 4)] + south),
              ('initial_heavy', heavy), ('transport_class', native('LSShellTransport')),
              ('enemy_class', classes['enemy']), ('enemy_stats_table', classes['table']), ('warning_seconds', 10.),
              ('arena_pressure_enabled', True), ('arena_pressure_times', [12., 32., 52., 72.]),
              ('arena_pressure_entries', [vec(-900, 400, 88), vec(-900, -400, 88)]),
              ('arena_pressure_enemy_cap', 8), ('arena_pressure_retry_seconds', 1.),
              ('arena_pressure_maximum_attempts', 8), ('arena_pressure_alternate_offset', 150.))
    for key, value in values: schedule.set_editor_property(key, value)
    co = p['Arena_Coordinator']
    tags = [tag for tag in co.get_editor_property('tags') if str(tag) != TAG + 'tracer']
    co.set_editor_property('tags', tags + [unreal.Name(TAG + 'full'), unreal.Name(HEAVY_LANE_MARKER), unreal.Name(RIM_HEIGHT_MARKER)])
    require(levels.save_current_level(), 'Arena full save failed')
    unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
    unreal.log('ARENA_FULL_SAVED: wait for navigation build and save; PIE and gameplay unobserved')


def heavy_lane(p, classes):
    co = p['Arena_Coordinator']
    markers = [str(tag) for tag in co.get_editor_property('tags') if str(tag).startswith(HEAVY_LANE_PREFIX)]
    require(markers in ([], [HEAVY_LANE_MARKER]), 'Heavy lane marker changed; preserving map')
    heavy = require(p.get('Arena_Heavy'), 'Arena Heavy missing')
    require(heavy.get_class() == classes['enemy'] and
            heavy.get_editor_property('enemy_kind') == unreal.LSShellEnemyKind.HEAVY, 'Arena Heavy type changed')
    current = HEAVY_SOUTH if markers else HEAVY_OLD
    deployment = DEPLOY_SOUTH if markers else DEPLOY_OLD
    location = heavy.get_actor_location()
    siege = require(heavy.get_editor_property('siege'), 'Arena Heavy siege component missing')
    point = siege.get_editor_property('deployment_location')
    require((location.x, location.y, location.z) == current and
            (point.x, point.y, point.z) == deployment, 'Heavy lane position or deployment changed; preserving map')
    return bool(markers)


def revise_heavy_lane(p, classes):
    if heavy_lane(p, classes):
        unreal.log('ARENA_HEAVY_LANE_PRESERVED')
        return
    heavy = p['Arena_Heavy']
    require(heavy.set_actor_location(vec(*HEAVY_SOUTH), False, True), 'Heavy lane move failed')
    heavy.get_editor_property('siege').set_editor_property('deployment_location', vec(*DEPLOY_SOUTH))
    co = p['Arena_Coordinator']
    co.set_editor_property('tags', list(co.get_editor_property('tags')) + [unreal.Name(HEAVY_LANE_MARKER)])
    require(heavy_lane(p, classes), 'Heavy lane revision validation failed')
    require(levels.save_current_level(), 'Heavy lane revision save failed')
    unreal.log('ARENA_HEAVY_LANE_SAVED: navigation and gameplay unobserved')


def rim_height(p):
    markers = [str(tag) for tag in p['Arena_Coordinator'].get_editor_property('tags')
               if str(tag).startswith(RIM_HEIGHT_PREFIX)]
    require(markers in ([], [RIM_HEIGHT_MARKER]), 'Rim height marker changed; preserving map')
    revised = bool(markers)
    for name, (x, y, scale_x, scale_y) in RIM_SPECS.items():
        actor = require(p.get('Arena_' + name), 'Arena rim missing: ' + name)
        require(actor.get_class() == unreal.StaticMeshActor.static_class(), 'Arena rim type changed: ' + name)
        position = actor.get_actor_location()
        scale = actor.get_actor_scale3d()
        rotation = actor.get_actor_rotation()
        expected = (x, y, 60 if revised else 120, scale_x, scale_y, 1.2 if revised else 2.4, 0, 0, 0)
        actual = (position.x, position.y, position.z, scale.x, scale.y, scale.z,
                  rotation.pitch, rotation.yaw, rotation.roll)
        require(all(abs(a - b) < .0001 for a, b in zip(actual, expected)),
                'Arena rim pose or scale changed: ' + name)
    return revised


def revise_rim_height(p):
    if rim_height(p):
        unreal.log('ARENA_RIM_HEIGHT_PRESERVED')
        return
    for name, (x, y, scale_x, scale_y) in RIM_SPECS.items():
        actor = p['Arena_' + name]
        require(actor.set_actor_location(vec(x, y, 60), False, True), 'Arena rim move failed: ' + name)
        actor.set_actor_scale3d(vec(scale_x, scale_y, 1.2))
    co = p['Arena_Coordinator']
    co.set_editor_property('tags', list(co.get_editor_property('tags')) + [unreal.Name(RIM_HEIGHT_MARKER)])
    require(rim_height(p), 'Arena rim height revision validation failed')
    require(levels.save_current_level(), 'Arena rim height revision save failed')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
    unreal.log('ARENA_RIM_HEIGHT_SAVED: wait for navigation build and save; gameplay unobserved')


def verify_full(p, classes):
    revised = heavy_lane(p, classes)
    rim_revised = rim_height(p)
    expected = {
        'BallIntercept': (native('LSShellMetalBall'), (-1800, 160, 70)),
        'BallSouth': (native('LSShellMetalBall'), (-1850, -300, 70)),
        'BarrelSouth': (classes['barrel'], (-1500, -490, 60)),
        'BatterySouth': (native('LSShellBattery'), (-1300, -600, 55)),
        'PlateSouth': (native('LSShellPlate'), (-1260, -455, 15)),
        'AssaultSouth1': (classes['enemy'], (-900, -800, 88)),
        'AssaultSouth2': (classes['enemy'], (-650, -700, 88)),
        'AssaultSouth3': (classes['enemy'], (-400, -500, 88)),
        'Heavy': (classes['enemy'], HEAVY_SOUTH if revised else HEAVY_OLD),
        'Shield': (native('LSShellTransport'), (1250, 0, 70)),
        'TransportA': (native('LSShellTransport'), (2400, -1500, 70)),
        'Schedule': (native('LSShellSupplySchedule'), (0, 0, 0)),
        'WestRim': (unreal.StaticMeshActor.static_class(), (-3180, 0, 60 if rim_revised else 120)),
        'NorthRim': (unreal.StaticMeshActor.static_class(), (0, 1980, 60 if rim_revised else 120)),
        'SouthRim': (unreal.StaticMeshActor.static_class(), (0, -1980, 60 if rim_revised else 120)),
        'EastRimMiddle': (unreal.StaticMeshActor.static_class(), (3180, 0, 60 if rim_revised else 120)),
        'EastRimNorth': (unreal.StaticMeshActor.static_class(), (3180, 1950, 60 if rim_revised else 120)),
        'EastRimSouth': (unreal.StaticMeshActor.static_class(), (3180, -1950, 60 if rim_revised else 120)),
        'ExitApron': (unreal.StaticMeshActor.static_class(), (3600, 0, -51)),
    }
    for label, (cls, location) in expected.items():
        obj = require(p.get('Arena_' + label), 'Missing full arena actor: ' + label)
        require(obj.get_class() == cls and near(obj.get_actor_location(), location), 'Full arena actor changed: ' + label)
    for label in ('BallIntercept', 'BallSouth'):
        require(abs(p['Arena_' + label].get_editor_property('intrinsic_mass_kg') - 750.) < .01, 'Ball mass changed: ' + label)
    require(p['Arena_Heavy'].get_editor_property('enemy_kind') == unreal.LSShellEnemyKind.HEAVY,
            'Heavy role changed')
    schedule = p['Arena_Schedule']
    require(schedule.get_editor_property('enemy_class') == classes['enemy'] and
            schedule.get_editor_property('enemy_stats_table') == classes['table'] and
            schedule.get_editor_property('initial_heavy') == p['Arena_Heavy'] and
            schedule.get_editor_property('initial_route_a_transport') == p['Arena_TransportA'] and
            list(schedule.get_editor_property('initial_assaults')) ==
            [p['Arena_AssaultNorth%d' % i] for i in range(1, 4)] +
            [p['Arena_AssaultSouth%d' % i] for i in range(1, 4)], 'Schedule references changed')
    for route_name in ('route_a', 'route_b'):
        route = schedule.get_editor_property(route_name)
        require(route.get_editor_property('cargo_slots')[1].get_editor_property('cargo_class') == classes['barrel'],
                'Arena barrel cargo class changed: ' + route_name)
    require(p['Arena_TransportA'].get_editor_property('cargo_slots')[1].get_editor_property('cargo_class') == classes['barrel'],
            'Initial transport barrel class changed')


def arena_stats_csv(text, exported=False):
    rows = list(csv.reader(io.StringIO(text)))
    columns = ['Health', 'AttackPower', 'AttackInterval', 'AttackRange', 'DetectionRange', 'VisionRadius', 'MovementSpeed']
    require(rows and len(rows[0]) == len(columns) + 1 and
            rows[0][0] in (('Name', '---') if exported else ('Name',)) and
            rows[0][1:] == columns, 'Arena stats schema differs; preserving table')
    result = {}
    for row in rows[1:]:
        require(len(row) == len(columns) + 1 and row[0] not in result,
                'Arena stats rows differ; preserving table')
        try:
            result[row[0]] = tuple(float(value) for value in row[1:])
        except ValueError:
            raise RuntimeError('Arena stats contain a nonnumeric value; preserving table')
    return result


def revise_siege_pressure(table):
    require(isinstance(table, unreal.DataTable) and
            table.get_path_name() == ROOT + '/Data/DT_ShellArenaEnemyStats.DT_ShellArenaEnemyStats' and
            table.get_editor_property('row_struct') == unreal.load_object(None, '/Script/Landship.LSUnitStatRow'),
            'Arena stats table or row type differs; preserving table')
    expected = {
        'Assault': (100, 15, 2, 180, 10000, 5000, 140),
        'Carrier': (120, 0, 2, 180, 10000, 5000, 90),
        'Heavy': (600, 90, 3, 1300, 10000, 5000, 100),
    }
    csv_path = Path(unreal.Paths.project_dir()) / 'Data/ShellExperiment/DT_ShellArenaEnemyStats.csv'
    require(arena_stats_csv(csv_path.read_text(encoding='utf-8-sig')) == expected,
            'Arena stats CSV differs from the requested revision; preserving table')
    def exported_rows():
        content = unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(table)
        require(isinstance(content, str) and content, 'Arena stats export failed; preserving table')
        return arena_stats_csv(content, exported=True)
    current = exported_rows()
    if current == expected:
        unreal.log('ARENA_SIEGE_PRESSURE_PRESERVED')
        return
    original = dict(expected)
    original['Heavy'] = (600, 45, 3, 1300, 10000, 5000, 100)
    require(current == original, 'Arena stats were manually changed; preserving table')
    require(unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(table, str(csv_path)),
            'Arena stats CSV import failed')
    require(exported_rows() == expected, 'Arena stats import differs from the requested revision')
    helper.save(table)
    unreal.log('ARENA_SIEGE_PRESSURE_SAVED: gameplay unobserved')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--phase', choices=('tracer', 'full'), required=True)
    parser.add_argument('--revise-heavy-lane', action='store_true')
    parser.add_argument('--revise-siege-pressure', action='store_true')
    parser.add_argument('--revise-rim-height', action='store_true')
    args, _ = parser.parse_known_args(sys.argv[1:])
    revisions = (args.revise_heavy_lane, args.revise_siege_pressure, args.revise_rim_height)
    require(sum(revisions) <= 1, 'Select only one revision')
    require(not any(revisions) or args.phase == 'full',
            'Revisions require the full phase')
    classes = {'castle': generated(load(ROOT + '/Blueprints/BP_ShellCastle')),
               'core': generated(load(ROOT + '/Blueprints/BP_ShellCore'))}
    physical = load(ROOT + '/Physics/PM_ShellSupplyFloor')
    helper.validate_floor_material(physical)
    visual = load(ROOT + '/Materials/MI_ShellFloor')
    load(ROOT + '/Blueprints/BP_ShellSupplyGameMode')
    require(assets.does_asset_exist(SOURCE), 'Template map missing')
    if not assets.does_asset_exist(MAP):
        require(not any(revisions), 'Revision requires an existing full arena')
        require(args.phase == 'tracer', 'Create and observe tracer first')
        for path in ('Blueprints/BP_ShellArenaEnemy', 'Blueprints/BP_ShellArenaBarrel',
                     'Blueprints/BP_ShellArenaCargoPlate', 'Data/DT_ShellArenaEnemyStats'):
            require(not assets.does_asset_exist(ROOT + '/' + path), 'Arena asset already exists: ' + path)
        table, enemy, barrel, plate = create_private_assets()
        classes.update(table=table, enemy=enemy, barrel=barrel, plate=plate)
        tracer(classes, physical, visual)
        return
    for name, key in (('BP_ShellArenaEnemy', 'enemy'), ('BP_ShellArenaBarrel', 'barrel'), ('BP_ShellArenaCargoPlate', 'plate')):
        classes[key] = generated(load(ROOT + '/Blueprints/' + name))
    classes['table'] = load(ROOT + '/Data/DT_ShellArenaEnemyStats')
    barrel_default = unreal.get_default_object(classes['barrel'])
    require(isinstance(barrel_default, unreal.LSShellBarrel) and
            abs(barrel_default.get_editor_property('reactive').get_editor_property('detonation_radius') - 500.) < .01,
            'Arena barrel class or radius changed; preserving assets')
    enemy_default = unreal.get_default_object(classes['enemy'])
    require(isinstance(enemy_default, unreal.LSShellExperimentEnemy) and
            enemy_default.get_editor_property('stats_table') == classes['table'] and
            str(enemy_default.get_editor_property('stats_row_name')) == 'None', 'Arena enemy class or table changed')
    plate_default = unreal.get_default_object(classes['plate'])
    require(isinstance(plate_default, unreal.LSShellPlate) and
            near(plate_default.get_editor_property('half_extent'), (15, 350, 450)) and
            not plate_default.get_editor_property('initially_supported'), 'Arena cargo plate changed')
    require(levels.load_level(MAP), 'Arena load failed')
    p = authored()
    co = require(p.get('Arena_Coordinator'), 'Arena coordinator missing')
    markers = [str(tag) for tag in co.get_editor_property('tags') if str(tag) in (TAG + 'tracer', TAG + 'full')]
    require(len(markers) == 1, 'Arena phase marker invalid; preserving map')
    if args.revise_rim_height:
        require(markers[0] == TAG + 'full' and co.get_class() == native('LSShellExperimentCoordinator'),
                'Rim height revision requires the authored full arena coordinator')
        revise_rim_height(p)
        return
    if args.revise_siege_pressure:
        require(markers[0] == TAG + 'full' and co.get_class() == native('LSShellExperimentCoordinator'),
                'Siege pressure revision requires the authored full arena coordinator')
        revise_siege_pressure(classes['table'])
        return
    if args.revise_heavy_lane:
        require(markers[0] == TAG + 'full' and co.get_class() == native('LSShellExperimentCoordinator'),
                'Heavy lane revision requires the authored full arena coordinator')
        revise_heavy_lane(p, classes)
        return
    verify_tracer(p, classes)
    if markers[0] == TAG + 'full':
        verify_full(p, classes)
    if args.phase == 'tracer' or markers[0] == TAG + 'full':
        unreal.log('ARENA_PRESERVED: no authored values changed')
        return
    schedule = unreal.get_default_object(native('LSShellSupplySchedule'))
    for key in ('arena_pressure_enabled', 'arena_pressure_times', 'arena_pressure_entries',
                'arena_pressure_enemy_cap', 'arena_pressure_retry_seconds',
                'arena_pressure_maximum_attempts', 'arena_pressure_alternate_offset'):
        schedule.get_editor_property(key)
    helper.validate_cargo_slot_api(classes['plate'])
    full(p, classes, physical, visual)


if __name__ == '__main__':
    main()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)


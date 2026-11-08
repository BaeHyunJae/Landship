"""실험 전용 에셋과 작은 연쇄 포격 전장을 저작한다. 기존 실험 맵은 덮어쓰지 않는다."""
from pathlib import Path
import math
import random
import struct
import wave
import sys
import unreal


ROOT = '/Game/Landship/ShellExperiment'
MAP = ROOT + '/Maps/L_ShellExperiment'
SOURCE_MAP = '/Game/Landship/Maps/L_BattlefieldPrototype'
TRACER = True
RESUME_PARTIAL = '--resume-authoring' in sys.argv
UPDATE_FEEDBACK = '--update-feedback' in sys.argv
POPULATE_P1 = '--populate-p1' in sys.argv
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
project = Path(unreal.Paths.project_dir())


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def native(name):
    return require(unreal.load_class(None, '/Script/Landship.' + name),
                   'Build native class first: ' + name)


def save(asset):
    require(assets.save_loaded_asset(asset), 'Asset save failed: ' + asset.get_path_name())


def blueprint(name, parent=None, source=None):
    path = ROOT + '/Blueprints/' + name
    if assets.does_asset_exist(path):
        bp = unreal.load_asset(path)
    elif source:
        bp = assets.duplicate_asset(source, path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', parent)
        bp = tools.create_asset(name, ROOT + '/Blueprints', unreal.Blueprint, factory)
    return require(bp, 'Blueprint creation failed: ' + path)


def compiled(bp):
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cls = require(unreal.BlueprintEditorLibrary.generated_class(bp),
                  'Blueprint has no generated class: ' + bp.get_path_name())
    save(bp)
    return cls


def defaults(bp):
    return unreal.get_default_object(require(unreal.BlueprintEditorLibrary.generated_class(bp),
                                            'Blueprint must compile: ' + bp.get_path_name()))


def row(table, name):
    handle = unreal.DataTableRowHandle()
    handle.set_editor_property('data_table', table)
    handle.set_editor_property('row_name', name)
    return handle


def table(name, struct_name):
    path = ROOT + '/Data/' + name
    if assets.does_asset_exist(path):
        result = unreal.load_asset(path)
    else:
        factory = unreal.DataTableFactory()
        factory.set_editor_property('struct', unreal.load_object(None, '/Script/Landship.' + struct_name))
        result = tools.create_asset(name, ROOT + '/Data', unreal.DataTable, factory)
    require(result, 'DataTable creation failed: ' + name)
    require(unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(
        result, str(project / 'Data' / 'ShellExperiment' / (name + '.csv'))),
        'DataTable CSV import failed: ' + name)
    save(result)
    return result


def material(name, color):
    path = ROOT + '/Materials/' + name
    if assets.does_asset_exist(path):
        return unreal.load_asset(path)
    result = tools.create_asset(name, ROOT + '/Materials', unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
    require(result, 'Material creation failed: ' + name)
    unreal.MaterialEditingLibrary.set_material_instance_parent(
        result, require(unreal.load_asset('/Game/Landship/Troops/Materials/M_TroopPrototype'),
                        'Shared prototype material missing'))
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        result, 'TeamColor', unreal.LinearColor(*color))
    save(result)
    return result


def effect_material():
    path = ROOT + '/Materials/M_ShellEffect'
    if assets.does_asset_exist(path):
        return unreal.load_asset(path)
    result = require(tools.create_asset('M_ShellEffect', ROOT + '/Materials',
                                       unreal.Material, unreal.MaterialFactoryNew()),
                     'Effect material creation failed')
    result.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    result.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    result.set_editor_property('two_sided', True)
    tint = unreal.MaterialEditingLibrary.create_material_expression(
        result, unreal.MaterialExpressionVectorParameter, -300, 0)
    tint.set_editor_property('parameter_name', 'Tint')
    tint.set_editor_property('default_value', unreal.LinearColor(1, 0.3, 0.02, 1))
    opacity = unreal.MaterialEditingLibrary.create_material_expression(
        result, unreal.MaterialExpressionScalarParameter, -300, 160)
    opacity.set_editor_property('parameter_name', 'Opacity')
    opacity.set_editor_property('default_value', 0.35)
    require(unreal.MaterialEditingLibrary.connect_material_property(
        tint, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR), 'Effect Tint connection failed')
    require(unreal.MaterialEditingLibrary.connect_material_property(
        opacity, '', unreal.MaterialProperty.MP_OPACITY), 'Effect Opacity connection failed')
    errors = unreal.MaterialEditingLibrary.recompile_material(result)
    require(not errors, 'Effect material compilation failed: ' + str(errors))
    save(result)
    return result


def ring_material():
    path = ROOT + '/Materials/M_ShellRing'
    if assets.does_asset_exist(path):
        return unreal.load_asset(path)
    result = require(tools.create_asset('M_ShellRing', ROOT + '/Materials',
                                       unreal.Material, unreal.MaterialFactoryNew()),
                     'Ring material creation failed')
    result.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    result.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    result.set_editor_property('two_sided', True)
    tint = unreal.MaterialEditingLibrary.create_material_expression(
        result, unreal.MaterialExpressionVectorParameter, -600, 0)
    tint.set_editor_property('parameter_name', 'Tint')
    tint.set_editor_property('default_value', unreal.LinearColor(1, 0.45, 0.08, 1))
    opacity = unreal.MaterialEditingLibrary.create_material_expression(
        result, unreal.MaterialExpressionScalarParameter, -600, 160)
    opacity.set_editor_property('parameter_name', 'Opacity')
    opacity.set_editor_property('default_value', 0.65)
    fresnel = unreal.MaterialEditingLibrary.create_material_expression(
        result, unreal.MaterialExpressionFresnel, -600, 300)
    fresnel.set_editor_property('exponent', 4.0)
    fresnel.set_editor_property('base_reflect_fraction', 0.0)
    multiply = unreal.MaterialEditingLibrary.create_material_expression(
        result, unreal.MaterialExpressionMultiply, -300, 200)
    require(unreal.MaterialEditingLibrary.connect_material_expressions(opacity, '', multiply, 'A'),
            'Ring Opacity input connection failed')
    require(unreal.MaterialEditingLibrary.connect_material_expressions(fresnel, '', multiply, 'B'),
            'Ring Fresnel input connection failed')
    require(unreal.MaterialEditingLibrary.connect_material_property(
        tint, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR), 'Ring Tint connection failed')
    require(unreal.MaterialEditingLibrary.connect_material_property(
        multiply, '', unreal.MaterialProperty.MP_OPACITY), 'Ring Opacity connection failed')
    errors = unreal.MaterialEditingLibrary.recompile_material(result)
    require(not errors, 'Ring material compilation failed: ' + str(errors))
    save(result)
    return result


def no_concealment(actor, public_core=False, configure_defaults=False):
    vision = require(actor.get_component_by_class(unreal.LSVisionParticipantComponent),
                     'Vision participant missing: ' + actor.get_name())
    if configure_defaults:
        vision.set_editor_property('concealed_outside_sight', False)
    vision.set_editor_property('public_core_position', public_core)


def sound(name, frequency, seconds, replace_existing=False):
    require(name in ('S_ShellFire', 'S_ShellExplosion', 'S_ShellImpact'),
            'Sound authoring is restricted to the three experiment cues')
    path = ROOT + '/Audio/' + name
    if assets.does_asset_exist(path) and not replace_existing:
        return unreal.load_asset(path)
    output = project / 'Intermediate' / 'ShellExperimentAudio'
    output.mkdir(parents=True, exist_ok=True)
    filename = output / (name + '.wav')
    rng = random.Random(1701)
    rate = 44100
    values = []
    low_noise = 0.0
    for index in range(int(rate * seconds)):
        time = index / rate
        attack = 1 - math.exp(-time / 0.0015)
        body_envelope = math.exp(-time / (seconds * 0.26))
        noise_envelope = math.exp(-time / (seconds * 0.13))
        tail_envelope = math.exp(-time / (seconds * 0.48))
        phase = 2 * math.pi * frequency * (time - 0.3 * time * time / seconds)
        noise = rng.uniform(-1, 1)
        low_noise += 0.025 * (noise - low_noise)
        body = (0.8 * math.sin(phase) + 0.16 * math.sin(phase * 1.97)) * body_envelope
        crack = 0.4 * (noise - low_noise) * noise_envelope
        rumble = low_noise * 1.6 * tail_envelope
        fade_out = min(1.0, (seconds - time) / 0.012)
        values.append(math.tanh((body + crack + rumble) * 1.6) * attack * fade_out)
    peak = max(abs(value) for value in values)
    gain = 0.92 * 32767 / peak
    samples = b''.join(struct.pack('<h', int(value * gain)) for value in values)
    with wave.open(str(filename), 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(samples)
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', str(filename))
    task.set_editor_property('destination_path', ROOT + '/Audio')
    task.set_editor_property('destination_name', name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', replace_existing)
    task.set_editor_property('save', True)
    tools.import_asset_tasks([task])
    return require(unreal.load_asset(path), 'Sound import failed: ' + name)


def widget_blueprint():
    native('LSShellExperimentEditorLibrary')
    bp = require(unreal.LSShellExperimentEditorLibrary.upgrade_feedback_widget_asset() if UPDATE_FEEDBACK
                 else unreal.LSShellExperimentEditorLibrary.build_widget_asset(),
                 'Native Shell UI authoring failed; inspect the first editor error')
    require(isinstance(bp, unreal.WidgetBlueprint), 'Shell UI helper returned a different asset type')
    require(bp.get_path_name() == ROOT + '/UI/WBP_ShellExperiment.WBP_ShellExperiment',
            'Shell UI helper returned an asset outside the experiment path')
    require(unreal.BlueprintEditorLibrary.get_blueprint_parent_class(bp) == native('LSShellExperimentWidget'),
            'Shell UI must derive directly from LSShellExperimentWidget')
    save(bp)
    return bp


def spawn(cls, label, xyz):
    actor = require(actors.spawn_actor_from_class(cls, unreal.Vector(*xyz)), 'Spawn failed: ' + label)
    actor.set_actor_label(label)
    return actor


def body_distance(a, b):
    # 이 저작의 고정 폭발물·보조 시설은 회전하지 않은 상자 몸체다.
    pa = a.get_actor_location()
    pb = b.get_actor_location()
    ea = a.get_editor_property('root_component').get_scaled_box_extent()
    eb = b.get_editor_property('root_component').get_scaled_box_extent()
    dx = max(0, abs(pa.x - pb.x) - ea.x - eb.x)
    dy = max(0, abs(pa.y - pb.y) - ea.y - eb.y)
    return math.hypot(dx, dy)


def blast_distance(source, target):
    point = source.get_actor_location()
    center = target.get_actor_location()
    extent = target.get_editor_property('root_component').get_scaled_box_extent()
    return math.hypot(max(0, abs(point.x - center.x) - extent.x),
                      max(0, abs(point.y - center.y) - extent.y))


def validate_chain(fixed, auxiliary):
    for actor in fixed + auxiliary:
        rotation = actor.get_actor_rotation()
        require(max(abs(rotation.pitch), abs(rotation.yaw), abs(rotation.roll)) < 0.01,
                'Chain spacing validation requires unrotated boxes: ' + actor.get_name())
    require(all(body_distance(a, b) <= 400 for a, b in zip(fixed, fixed[1:])),
            'Fixed explosive chain exceeds body-distance spacing')
    links = [(fixed[-1], auxiliary[0]), (auxiliary[0], auxiliary[1])]
    require(all(body_distance(a, b) <= 150 for a, b in links), 'Auxiliary body spacing exceeds 150cm')
    # 초기 250 피해·900cm 반경에서 200HP 시설을 기폭하는 중심→몸체 거리는 180cm 이하다.
    require(all(blast_distance(a, b) <= 180 for a, b in links), 'Auxiliary ignition is outside lethal blast distance')


def feedback_assets():
    require(assets.does_asset_exist(MAP), 'Feedback update requires an authored experiment map')
    require(levels.load_level(MAP), 'Feedback experiment map load failed')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name().split('.')[0] == MAP, 'Refusing to update feedback in a different world')
    ring_material()
    sound('S_ShellFire', 90, 0.55, replace_existing=True)
    sound('S_ShellExplosion', 50, 1.05, replace_existing=True)
    sound('S_ShellImpact', 180, 0.24, replace_existing=True)
    widget_blueprint()
    auxiliary_count = 0
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.LSShellExplosive) and actor.get_actor_label().startswith('Shell_Auxiliary_'):
            actor.set_editor_property('auxiliary_facility', True)
            auxiliary_count += 1
    require(levels.save_current_level(), 'Feedback experiment map save failed')
    unreal.log('SHELL_EXPERIMENT_FEEDBACK_UPDATED: ring, three audio cues, additive widget upgrade; auxiliary roles=' + str(auxiliary_count))


def population_enemy_class(kind, enemy_table):
    path = ROOT + '/Blueprints/BP_Shell' + kind
    existed = assets.does_asset_exist(path)
    bp = blueprint('BP_Shell' + kind, native('LSShellExperimentEnemy'))
    require(unreal.BlueprintEditorLibrary.get_blueprint_parent_class(bp) == native('LSShellExperimentEnemy'),
            'Population Blueprint must derive directly from LSShellExperimentEnemy: ' + path)
    definition = defaults(bp)
    enemy_kind = getattr(unreal.LSShellEnemyKind, kind.upper())
    if existed:
        require(definition.get_editor_property('enemy_kind') == enemy_kind
                and definition.get_editor_property('stats_table') == enemy_table
                and str(definition.get_editor_property('stats_row_name')) == kind,
                'Existing population Blueprint has different stats; preserving it: ' + path)
        return require(unreal.BlueprintEditorLibrary.generated_class(bp), 'Population Blueprint must compile: ' + path)
    definition.set_editor_property('stats_table', enemy_table)
    definition.set_editor_property('stats_row_name', kind)
    definition.set_editor_property('enemy_kind', enemy_kind)
    no_concealment(definition, configure_defaults=True)
    return compiled(bp)


def populate_p1():
    require(assets.does_asset_exist(MAP), 'P1 population requires the authored tracer map')
    require(levels.load_level(MAP), 'P1 experiment map load failed')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name().split('.')[0] == MAP, 'Refusing to populate a different world')
    placements = [
        ('Shell_Assault_01', 'Assault', (-120, 860, 88)),
        ('Shell_Assault_02', 'Assault', (1500, 1150, 88)),
        ('Shell_Assault_03', 'Assault', (900, 1450, 88)),
        ('Shell_Assault_04', 'Assault', (2200, 1350, 88)),
        ('Shell_Assault_05', 'Assault', (1450, -700, 88)),
        ('Shell_Assault_06', 'Assault', (700, -1250, 88)),
        ('Shell_Assault_07', 'Assault', (1900, -1050, 88)),
        ('Shell_Assault_08', 'Assault', (2800, 1450, 88)),
        ('Shell_Carrier_01', 'Carrier', (2450, 900, 88)),
        ('Shell_Carrier_02', 'Carrier', (2800, -900, 88)),
        ('Shell_Heavy_01', 'Heavy', (2400, -1500, 140)),
    ]
    static_placements = [
        ('Shell_Fixed_01', False, (150, 1000, 60)),
        ('Shell_Fixed_02', False, (550, 1000, 60)),
        ('Shell_Fixed_03', False, (950, 1000, 60)),
        ('Shell_Fixed_04', False, (1350, 800, 60)),
        ('Shell_Auxiliary_01', True, (1450, 630, 60)),
        ('Shell_Auxiliary_02', True, (1650, 630, 60)),
    ]
    expected_enemies = {label: kind for label, kind, _ in placements}
    expected_static = {label for label, _, _ in static_placements}
    expected_labels = set(expected_enemies) | expected_static | {'Shell_Cover_01'}
    present = {}
    current = list(actors.get_all_level_actors())
    for actor in current:
        label = actor.get_actor_label()
        if label in expected_labels:
            require(label not in present, 'Duplicate population actor label: ' + label)
            present[label] = actor
        if isinstance(actor, unreal.LSShellExperimentEnemy):
            require(label in expected_enemies, 'Unplanned experiment enemy; preserving the map: ' + label)
            require(actor.get_editor_property('enemy_kind') == getattr(unreal.LSShellEnemyKind, expected_enemies[label].upper()),
                    'Existing enemy kind differs from its population label: ' + label)
        elif isinstance(actor, unreal.LSShellExplosive):
            require(label in expected_static, 'Unplanned experiment explosive; preserving the map: ' + label)
            require(actor.get_editor_property('auxiliary_facility') == label.startswith('Shell_Auxiliary_'),
                    'Existing explosive role differs from its population label: ' + label)
    for label, actor in present.items():
        expected_type = unreal.LSShellExperimentEnemy if label in expected_enemies else (
            unreal.LSShellExplosive if label in expected_static else unreal.StaticMeshActor)
        require(isinstance(actor, expected_type), 'Population label belongs to a different actor type: ' + label)
    if expected_static.issubset(present):
        validate_chain([present['Shell_Fixed_%02d' % i] for i in range(1, 5)],
                       [present['Shell_Auxiliary_%02d' % i] for i in range(1, 3)])
    coordinators = [a for a in current if isinstance(a, unreal.LSShellExperimentCoordinator)]
    settings = [a for a in current if isinstance(a, unreal.LSBattlefieldSettings)]
    require(len(coordinators) == 1 and len(settings) == 1, 'P1 population requires one coordinator and battlefield settings')
    coordinators[0].get_editor_property('carrier_class')
    require(levels.save_current_level(), 'Could not save the experiment map before additive population')
    enemy_table = require(unreal.load_asset(ROOT + '/Data/DT_ShellEnemyStats'), 'Experiment enemy stats missing')
    enemy_classes = {kind: population_enemy_class(kind, enemy_table) for kind in ('Assault', 'Carrier', 'Heavy')}
    widget = require(unreal.LSShellExperimentEditorLibrary.upgrade_p1_widget_asset(), 'Native P1 widget upgrade failed')
    save(widget)
    created = 0
    for label, kind, xyz in placements:
        if label not in present:
            present[label] = spawn(enemy_classes[kind], label, xyz)
            created += 1
    for label, auxiliary, xyz in static_placements:
        if label not in present:
            obj = spawn(native('LSShellExplosive'), label, xyz)
            obj.set_editor_property('auxiliary_facility', auxiliary)
            obj.set_editor_property('max_health', 200.0 if auxiliary else 100.0)
            present[label] = obj
            created += 1
    if 'Shell_Cover_01' not in present:
        cover = spawn(unreal.StaticMeshActor, 'Shell_Cover_01', (600, 0, 350))
        mesh = cover.static_mesh_component
        mesh.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
        mesh.set_collision_profile_name('BlockAll')
        mesh.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_BLOCK)
        mesh.set_material(0, require(unreal.load_asset(ROOT + '/Materials/MI_ShellFloor'), 'Experiment cover material missing'))
        cover.set_actor_scale3d(unreal.Vector(4, 4, 7))
        present['Shell_Cover_01'] = cover
        created += 1
    terrain = list(settings[0].get_editor_property('terrain_actors'))
    if present['Shell_Cover_01'] not in terrain:
        settings[0].set_editor_property('terrain_actors', terrain + [present['Shell_Cover_01']])
    coordinator = coordinators[0]
    coordinator.set_editor_property('carrier_class', enemy_classes['Carrier'])
    coordinator.set_editor_property('convoy_spawn_points', [unreal.Vector(2900, 1300, 88), unreal.Vector(2900, -1200, 88)])
    coordinator.set_editor_property('convoy_approach_point', unreal.Vector(1000, 1100, 0))
    coordinator.set_editor_property('enable_events', True)
    all_actors = actors.get_all_level_actors()
    require(sum(isinstance(a, unreal.LSShellExperimentEnemy) for a in all_actors) == 11,
            'P1 population must contain exactly eleven experiment enemies')
    require(sum(isinstance(a, unreal.LSShellExplosive) for a in all_actors) == 6,
            'P1 population must contain exactly six static explosives')
    validate_chain([present['Shell_Fixed_%02d' % i] for i in range(1, 5)],
                   [present['Shell_Auxiliary_%02d' % i] for i in range(1, 3)])
    require(levels.save_current_level(), 'P1 population map save failed')
    unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
    unreal.log('SHELL_EXPERIMENT_P1_POPULATED: created=' + str(created) + '; existing actor transforms preserved; verify routes and combat in PIE')


def main():
    existing_map = assets.does_asset_exist(MAP)
    require(sum((RESUME_PARTIAL, UPDATE_FEEDBACK, POPULATE_P1)) <= 1,
            'Choose only one of --resume-authoring, --update-feedback, --populate-p1')
    if POPULATE_P1:
        populate_p1()
        return
    if UPDATE_FEEDBACK:
        require(not RESUME_PARTIAL, 'Run feedback updates separately from partial map reconstruction')
        feedback_assets()
        return
    if existing_map and not RESUME_PARTIAL:
        unreal.log('SHELL_EXPERIMENT_PRESERVED: destination already exists; no assets or actors changed')
        return
    require(TRACER, 'Only the P0 tracer layout is authored by this script')
    classes = {name: native(name) for name in [
        'LSShellExperimentEnemy', 'LSShellExplosive', 'LSShellExperimentGameMode',
        'LSShellExperimentController', 'LSShellExperimentWidget', 'LSShellExperimentCoordinator',
        'LSBattlefieldSetup', 'LSBattlefieldSettings']}
    enemy_table = table('DT_ShellEnemyStats', 'LSUnitStatRow')
    core_table = table('DT_ShellCoreStats', 'LSOutpostStatRow')
    floor_material = material('MI_ShellFloor', (0.07, 0.10, 0.14, 1))
    fixed_material = material('MI_ShellFixed', (1.0, 0.27, 0.015, 1))
    auxiliary_material = material('MI_ShellAuxiliary', (0.8, 0.08, 0.015, 1))
    effect_material()
    ring_material()
    fire_sound = sound('S_ShellFire', 90, 0.55)
    explosion_sound = sound('S_ShellExplosion', 50, 1.05)
    impact_sound = sound('S_ShellImpact', 180, 0.24)
    widget = widget_blueprint()
    controller_bp = blueprint('BP_ShellController', classes['LSShellExperimentController'])
    defaults(controller_bp).set_editor_property('widget_class', compiled(widget))
    controller_class = compiled(controller_bp)
    camera_bp = blueprint('BP_ShellCamera', source='/Game/Landship/Maps/Blueprints/BP_BattlefieldCamera')
    camera = defaults(camera_bp)
    camera.set_editor_property('max_zoom_distance', 6500.0)
    camera.get_editor_property('camera_boom').set_editor_property('target_arm_length', 6500.0)
    camera.get_editor_property('camera_boom').set_editor_property('relative_rotation', unreal.Rotator(pitch=-55, yaw=-90, roll=0))
    camera_class = compiled(camera_bp)
    game_bp = blueprint('BP_ShellGameMode', classes['LSShellExperimentGameMode'])
    defaults(game_bp).set_editor_property('player_controller_class', controller_class)
    defaults(game_bp).set_editor_property('default_pawn_class', camera_class)
    game_class = compiled(game_bp)

    castle_bp = blueprint('BP_ShellCastle', source='/Game/Landship/Maps/Blueprints/BP_BattlefieldCastle')
    castle = defaults(castle_bp)
    castle.set_editor_property('stats_row', row(require(unreal.load_asset(
        '/Game/Landship/Combat/Data/DT_CastleStats'), 'Shared castle stats missing'), 'BattlefieldPrototype'))
    no_concealment(castle, configure_defaults=True)
    castle_class = compiled(castle_bp)
    core_bp = blueprint('BP_ShellCore', source='/Game/Landship/Maps/Blueprints/BP_CoreOutpost')
    core = defaults(core_bp)
    core.set_editor_property('stats_row', row(core_table, 'Core'))
    core.set_editor_property('armed', False)
    core.get_component_by_class(unreal.LSHealthComponent).set_editor_property('destroy_on_death', False)
    no_concealment(core, True, configure_defaults=True)
    core_class = compiled(core_bp)
    enemy_bp = blueprint('BP_ShellAssault', classes['LSShellExperimentEnemy'])
    enemy = defaults(enemy_bp)
    enemy.set_editor_property('stats_table', enemy_table)
    enemy.set_editor_property('stats_row_name', 'Assault')
    enemy.set_editor_property('enemy_kind', unreal.LSShellEnemyKind.ASSAULT)
    no_concealment(enemy, configure_defaults=True)
    enemy_class = compiled(enemy_bp)

    # 배치 프리셋과 지역별 출격은 가져오지 않고 조명·실제 내비게이션 브러시만 보존한다.
    if existing_map:
        require(levels.load_level(MAP), 'Partial experiment map load failed')
    else:
        require(levels.new_level_from_template(MAP, SOURCE_MAP), 'Experiment template level creation failed')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name().split('.')[0] == MAP, 'Refusing to modify a different world')
    world_settings = world.get_world_settings()
    require(unreal.LSShellExperimentEditorLibrary.get_configured_navigation_class(world)
            == '/Script/Landship.LSBattlefieldNavigationSystem',
            'Copied map must retain LSBattlefieldNavigationSystem configuration')
    bounds = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.NavMeshBoundsVolume)]
    require(bounds, 'Source map has no authored NavMeshBoundsVolume brush')
    for actor in actors.get_all_level_actors():
        if actor in bounds or isinstance(actor, unreal.RecastNavMesh):
            continue
        class_path = actor.get_class().get_path_name()
        if class_path.startswith('/Script/Landship.') or class_path.startswith('/Game/Landship/') or isinstance(actor, (unreal.StaticMeshActor, unreal.PlayerStart)):
            require(actors.destroy_actor(actor), 'Could not remove copied gameplay: ' + actor.get_name())
    for volume in bounds:
        volume.set_actor_scale3d(unreal.Vector(1, 1, 1))
        _, extent = volume.get_actor_bounds(False)
        require(min(extent.x, extent.y, extent.z) > 0, 'NavMeshBoundsVolume has no usable brush')
        volume.set_actor_scale3d(unreal.Vector(3200 / extent.x, 2000 / extent.y, 700 / extent.z))
        volume.set_actor_location(unreal.Vector(0, 0, 300), False, False)
    floor = spawn(unreal.StaticMeshActor, 'Shell_Floor', (0, 0, -50))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.static_mesh_component.set_collision_profile_name('BlockAll')
    floor.static_mesh_component.set_material(0, floor_material)
    floor.set_actor_scale3d(unreal.Vector(64, 40, 1))
    castle_actor = spawn(castle_class, 'Shell_PlayerCastle', (-1800, 0, 500))
    core_actor = spawn(core_class, 'Shell_Core', (2400, 0, 400))
    setup = spawn(classes['LSBattlefieldSetup'], 'Shell_BattlefieldSetup', (0, 0, 0))
    setup.set_editor_property('player_castle', castle_actor)
    setup.set_editor_property('core_outpost', core_actor)
    settings = spawn(classes['LSBattlefieldSettings'], 'Shell_BattlefieldSettings', (0, 0, 0))
    settings.set_editor_property('bounds_min', unreal.Vector2D(-3200, -2000))
    settings.set_editor_property('bounds_max', unreal.Vector2D(3200, 2000))
    settings.set_editor_property('ground_height', 0.0)
    settings.set_editor_property('outside_darkness', 0.0)
    settings.set_editor_property('terrain_actors', [floor])
    no_concealment(castle_actor)
    no_concealment(core_actor, True)
    coordinator = spawn(classes['LSShellExperimentCoordinator'], 'Shell_Coordinator', (0, 0, 0))
    coordinator.set_editor_property('player_castle', castle_actor)
    coordinator.set_editor_property('core_outpost', core_actor)
    coordinator.set_editor_property('enable_events', False)
    coordinator.set_editor_property('fire_sound', fire_sound)
    coordinator.set_editor_property('explosion_sound', explosion_sound)
    coordinator.set_editor_property('impact_sound', impact_sound)
    unit = spawn(enemy_class, 'Shell_Assault_01', (-120, 860, 88))
    no_concealment(unit)
    fixed = []
    auxiliary = []
    for index, xyz in enumerate([(150, 1000, 60), (550, 1000, 60), (950, 1000, 60), (1350, 800, 60)], 1):
        obj = spawn(classes['LSShellExplosive'], 'Shell_Fixed_%02d' % index, xyz)
        obj.set_editor_property('max_health', 100.0)
        obj.get_editor_property('visual').set_material(0, fixed_material)
        no_concealment(obj)
        fixed.append(obj)
    for index, xyz in enumerate([(1450, 630, 60), (1650, 630, 60)], 1):
        obj = spawn(classes['LSShellExplosive'], 'Shell_Auxiliary_%02d' % index, xyz)
        obj.set_editor_property('auxiliary_facility', True)
        obj.set_editor_property('max_health', 200.0)
        obj.get_editor_property('visual').set_material(0, auxiliary_material)
        no_concealment(obj)
        auxiliary.append(obj)
    validate_chain(fixed, auxiliary)
    spawn(unreal.PlayerStart, 'Shell_PlayerStart', (300, 0, 100))
    world_settings.set_editor_property('default_game_mode', game_class)
    for cls in [classes['LSBattlefieldSetup'], classes['LSBattlefieldSettings'], classes['LSShellExperimentCoordinator']]:
        require(sum(a.get_class() == cls for a in actors.get_all_level_actors()) == 1,
                'Experiment requires exactly one ' + cls.get_name())
    require(levels.save_current_level(), 'Experiment map save failed')
    unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
    unreal.log('SHELL_EXPERIMENT_AUTHORED: ' + MAP + '; wait for navigation build, save map, then observe PIE')


main()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

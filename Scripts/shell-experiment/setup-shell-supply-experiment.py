"""별도 개선 전장을 단계별로 확장한다. 기존 배치와 튜닝은 보존한다."""
import argparse
import math
import sys
import unreal


ROOT = '/Game/Landship/ShellExperiment'
MAP = ROOT + '/Maps/L_ShellSupplyExperiment'
SOURCE_MAP = ROOT + '/Maps/L_ShellExperiment'
WIDGET = ROOT + '/UI/WBP_ShellSupplyExperiment'
FLOOR_PHYSICS = ROOT + '/Physics/PM_ShellSupplyFloor'
STAGES = ('physics', 'charge', 'transport', 'materials', 'complete')
AUTHORING_TAG = 'LS_SupplyAuthoring_v1_'
ACTOR_TAG = 'LS_SupplyActor_v1_'


def stage_config(stage):
    modules = [unreal.LSShellModule.BLAST, unreal.LSShellModule.THRUST]
    if STAGES.index(stage) >= STAGES.index('charge'):
        modules.append(unreal.LSShellModule.ELECTRIC)
    if STAGES.index(stage) >= STAGES.index('materials'):
        modules.extend((unreal.LSShellModule.GRAVITY, unreal.LSShellModule.IRON))
    if stage == 'complete':
        modules.extend((unreal.LSShellModule.SPLIT, unreal.LSShellModule.ADHESION, unreal.LSShellModule.COOLING))
    first = unreal.LSShellModule.THRUST if stage == 'physics' else unreal.LSShellModule.ELECTRIC
    second = unreal.LSShellModule.BLAST if stage == 'physics' else unreal.LSShellModule.THRUST
    return modules, first, second


def current_stage(coordinator):
    value = coordinator.get_editor_property('supply_stage')
    stages = {getattr(unreal.LSShellSupplyStage, name.upper()): name for name in STAGES}
    require(value in stages, 'Unknown supply stage; preserving map: ' + str(value))
    return stages[value]


def validate_stage_ownership(coordinator):
    stage = current_stage(coordinator)
    markers = [str(tag) for tag in coordinator.get_editor_property('tags') if str(tag).startswith(AUTHORING_TAG)]
    expected_marker = AUTHORING_TAG + stage
    require(markers == [expected_marker] or (stage == 'physics' and not markers),
            'Authoring marker differs; preserving map: expected=%s current=%s' % (expected_marker, markers))
    expected = dict(zip(('available_modules', 'initial_first', 'initial_second'), stage_config(stage)))
    for key, value in expected.items():
        actual = coordinator.get_editor_property(key)
        if key == 'available_modules':
            actual = list(actual)
        require(actual == value,
                'Stage-owned field differs; preserving map: %s expected=%s current=%s' % (key, value, actual))
    return stage


def set_authoring_stage(coordinator, stage):
    for key, value in zip(('available_modules', 'initial_first', 'initial_second'), stage_config(stage)):
        coordinator.set_editor_property(key, value)
    coordinator.set_editor_property('supply_stage', getattr(unreal.LSShellSupplyStage, stage.upper()))
    tags = [tag for tag in coordinator.get_editor_property('tags') if not str(tag).startswith(AUTHORING_TAG)]
    coordinator.set_editor_property('tags', tags + [unreal.Name(AUTHORING_TAG + stage)])


def actor_class(path):
    cls = require(unreal.load_class(None, path), 'Build/load the requested actor class first: ' + path)
    require(isinstance(unreal.get_default_object(cls), unreal.Actor), 'An actor class is required: ' + path)
    return cls


def stage_actor_specs(stage, classes):
    specs = {}
    if STAGES.index(stage) >= STAGES.index('transport'):
        specs['Supply_Transport_A'] = (classes['Transport'], 'transport')
    if STAGES.index(stage) >= STAGES.index('materials'):
        specs['Supply_Plate_01'] = (classes['Plate'], 'materials')
        specs['Supply_Battery_01'] = (classes['Battery'], 'materials')
    if stage == 'complete':
        specs['Supply_Assault_02'] = (classes['Assault'], 'complete')
        specs['Supply_Heavy_01'] = (classes['Heavy'], 'complete')
        specs['Supply_Schedule'] = (classes['Schedule'], 'complete')
        specs['Supply_ExitApron'] = (unreal.StaticMeshActor.static_class(), 'complete')
    return specs


def validate_stage_actors(present, specs, stage):
    for label, (_, owner_stage) in specs.items():
        markers = [str(tag) for tag in present[label].get_editor_property('tags') if str(tag).startswith(ACTOR_TAG)]
        require(markers == [ACTOR_TAG + owner_stage],
                'Stage actor ownership differs; preserving map: %s expected=%s current=%s' %
                (label, ACTOR_TAG + owner_stage, markers))
    if STAGES.index(stage) >= STAGES.index('transport'):
        transport = present['Supply_Transport_A']
        require(len(transport.get_editor_property('cargo_slots')) == 2,
                'Existing route A must retain its two actual cargo slots; preserving map')
    if stage == 'complete':
        schedule = present['Supply_Schedule']
        expected = {'initial_route_a_transport': present['Supply_Transport_A'],
                    'initial_assaults': [present['Supply_Assault_01'], present['Supply_Assault_02']],
                    'initial_heavy': present['Supply_Heavy_01']}
        for key, value in expected.items():
            current = schedule.get_editor_property(key)
            if key == 'initial_assaults':
                current = list(current)
            require(current == value, 'Schedule actor reference differs; preserving map: ' + key)
        require(present['Supply_Heavy_01'].get_editor_property('enemy_kind') == unreal.LSShellEnemyKind.HEAVY,
                'Initial Heavy role differs; preserving map')
        apron_error = unreal.LSShellSupplyEditorLibrary.get_floor_physics_error(
            present['Supply_ExitApron'].static_mesh_component, load_asset(FLOOR_PHYSICS))
        require(not apron_error, 'Existing exit apron physics differs; preserving map: ' + apron_error)


def cargo_plate_class(parent, override_path):
    if override_path:
        result = actor_class(override_path)
    else:
        def configure(obj):
            half = unreal.Vector(15, 550, 450)
            obj.set_editor_property('half_extent', half)
            actor_body(obj).set_box_extent(half)
            obj.get_editor_property('visual').set_relative_scale3d(unreal.Vector(.3, 11, 9))
            obj.set_editor_property('intrinsic_mass_kg', 250.0)
            obj.set_editor_property('max_health', 300.0)
            obj.set_editor_property('initially_supported', False)
            obj.set_editor_property('body_color', unreal.LinearColor(.45, .23, .08, 1))
        result = blueprint('BP_ShellSupplyCargoPlate', parent, configure)
    definition = unreal.get_default_object(result)
    require(isinstance(definition, unreal.LSShellPlate), 'Cargo plate must derive from LSShellPlate')
    require(not definition.get_editor_property('initially_supported'),
            'Cargo plate must use only the carrier rack, not a world support; preserving its class')
    half = definition.get_editor_property('half_extent')
    require(min(vector_values(half)) > 0, 'Cargo plate has invalid authored half extents')
    require(isinstance(actor_body(definition), unreal.BoxComponent) and
            near_vector(actor_body(definition).get_scaled_box_extent(), vector_values(half)),
            'Cargo plate class collider differs from its authored extent; preserving its class')
    return result


def cargo_slot(cls, location, mass=0, insulated=False, keep_racked=False):
    slot = unreal.LSShellCargoSlot()
    slot.set_editor_property('cargo_class', cls)
    slot.set_editor_property('local_transform', unreal.Transform(
        location=unreal.Vector(*location), rotation=unreal.Rotator(), scale=unreal.Vector(1, 1, 1)))
    slot.set_editor_property('mass_override_kg', mass)
    slot.set_editor_property('insulated', insulated)
    slot.set_editor_property('keep_racked_on_delivery', keep_racked)
    return slot


def validate_cargo_slot_api(plate_class):
    # NativeMake 함수는 Python MathLibrary 메서드가 아니라 Transform 생성자로 노출된다.
    slot = cargo_slot(plate_class, (-100, 0, 520), 250, True)
    transform = slot.get_editor_property('local_transform')
    require(slot.get_editor_property('cargo_class') == plate_class and
            slot.get_editor_property('mass_override_kg') == 250 and slot.get_editor_property('insulated'),
            'Cargo-slot class/mass/insulation Python property preflight failed; no writes performed')
    require(near_vector(transform.get_editor_property('Translation'), (-100, 0, 520)) and
            near_vector(transform.get_editor_property('Scale3D'), (1, 1, 1)),
            'Cargo-slot Transform location/scale Python preflight failed; no writes performed')
    rotation = transform.get_editor_property('Rotation')
    require(max(abs(rotation.x), abs(rotation.y), abs(rotation.z), abs(rotation.w - 1)) < .0001,
            'Cargo-slot Transform rotation Python preflight failed; no writes performed')


def configure_schedule(schedule, present, classes, cargo_plate):
    transport = present['Supply_Transport_A']
    route_a = schedule.get_editor_property('route_a')
    route_a.set_editor_property('start', transport.get_editor_property('route_start'))
    route_a.set_editor_property('end', transport.get_editor_property('route_end'))
    route_a.set_editor_property('first_departure_seconds', 0.0)
    route_a.set_editor_property('repeat_seconds', 24.0)
    route_a.set_editor_property('cargo_slots', transport.get_editor_property('cargo_slots'))
    route_a.set_editor_property('charged_cargo_slots', [])
    route_a.set_editor_property('unload_offsets', [unreal.Vector(0, 0, 0),
                                                unreal.Vector(0, 550, 0), unreal.Vector(0, -550, 0)])
    route_a.set_editor_property('exit_waypoints', [])
    route_a.set_editor_property('replenishment_offset', unreal.Vector(0, -350, 0))
    route_a.set_editor_property('siege_deployment_location', unreal.Vector(-700, -1000, 140))
    schedule.set_editor_property('route_a', route_a)
    plate_half = unreal.get_default_object(cargo_plate).get_editor_property('half_extent')
    plate = cargo_slot(cargo_plate, (-100, 0, 70 + plate_half.z))
    barrel = cargo_slot(native('LSShellBarrel'), (50, 0, 130), 60, True)
    battery_half = unreal.get_default_object(classes['Battery']).get_editor_property('half_extent')
    battery = cargo_slot(classes['Battery'], (50, 0, 70 + battery_half.z))
    route_b = schedule.get_editor_property('route_b')
    route_b.set_editor_property('start', unreal.Vector(-900, 1000, 70))
    route_b.set_editor_property('end', unreal.Vector(900, 0, 70))
    route_b.set_editor_property('first_departure_seconds', 16.0)
    route_b.set_editor_property('repeat_seconds', 36.0)
    route_b.set_editor_property('cargo_slots', [plate, barrel])
    route_b.set_editor_property('charged_cargo_slots', [plate, battery])
    route_b.set_editor_property('unload_offsets', [unreal.Vector(0, 0, 0),
                                                unreal.Vector(0, 1200, 0), unreal.Vector(0, -1250, 0)])
    route_b.set_editor_property('exit_waypoints', [])
    route_b.set_editor_property('replenishment_offset', unreal.Vector(0, 700, 0))
    route_b.set_editor_property('siege_deployment_location', unreal.Vector(-700, -600, 140))
    schedule.set_editor_property('route_b', route_b)
    schedule.set_editor_property('initial_route_a_transport', transport)
    schedule.set_editor_property('initial_assaults', [present['Supply_Assault_01'], present['Supply_Assault_02']])
    schedule.set_editor_property('initial_heavy', present['Supply_Heavy_01'])
    schedule.set_editor_property('enemy_stats_table', present['Supply_Assault_01'].get_editor_property('stats_table'))
    schedule.set_editor_property('transport_class', classes['Transport'])
    schedule.set_editor_property('enemy_class', native('LSShellExperimentEnemy'))


def revise_complete_layout(present):
    # 실행에서 확인한 배송로 점유만 명시적으로 고친다. 수동 변경은 덮어쓰지 않는다.
    marker = unreal.Name('ShellSupply.CompleteLayout1')
    coordinator = present['Supply_Coordinator']
    plate = present['Supply_Plate_01']
    tags = list(coordinator.get_editor_property('tags'))
    old_position, new_position = (900, 600, 120), (-800, -1700, 120)
    expected_position = new_position if marker in tags else old_position
    require(near_vector(plate.get_actor_location(), expected_position),
            'Standalone plate was manually moved; preserving complete layout')
    schedule = present['Supply_Schedule']
    route = schedule.get_editor_property('route_b')
    require(near_vector(route.get_editor_property('start'), (-900, 1000, 70)) and
            near_vector(route.get_editor_property('end'), (900, 0, 70)),
            'Route B was manually changed; preserving complete layout')
    prepared = {}
    for key in ('cargo_slots', 'charged_cargo_slots'):
        slots = list(route.get_editor_property(key))
        require(len(slots) == 2 and isinstance(unreal.get_default_object(slots[0].get_editor_property('cargo_class')), unreal.LSShellPlate),
                'Route B cargo ownership differs; preserving complete layout')
        require(bool(slots[0].get_editor_property('keep_racked_on_delivery')) == (marker in tags),
                'Route B rack policy was manually changed; preserving complete layout')
        prepared[key] = slots
    if marker in tags:
        unreal.log('SUPPLY_COMPLETE_LAYOUT1_PRESERVED')
        return
    for key, slots in prepared.items():
        slots[0].set_editor_property('keep_racked_on_delivery', True)
        route.set_editor_property(key, slots)
    plate.set_actor_location(unreal.Vector(*new_position), False, True)
    schedule.set_editor_property('route_b', route)
    coordinator.set_editor_property('tags', tags + [marker])
    require(unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(), 'Complete layout save failed')
    unreal.log('SUPPLY_COMPLETE_LAYOUT1_SAVED: B plate stays on its real rack; standalone plate is outside the route')


def revise_exit_seam(present):
    marker = unreal.Name('ShellSupply.ExitSeam1')
    coordinator = present['Supply_Coordinator']
    apron = present['Supply_ExitApron']
    tags = list(coordinator.get_editor_property('tags'))
    expected_z = -51 if marker in tags else -50
    require(near_vector(apron.get_actor_location(), (3600, 0, expected_z)) and
            near_vector(apron.get_actor_scale3d(), (8, 40, 1)),
            'Exit apron was manually changed; preserving its support geometry')
    if marker in tags:
        unreal.log('SUPPLY_EXIT_SEAM1_PRESERVED')
        return
    # 같은 높이의 별도 큐브 세로 면에 차대 바닥이 걸리지 않도록 실제 1cm 내리막을 둔다.
    apron.set_actor_location(unreal.Vector(3600, 0, -51), False, True)
    coordinator.set_editor_property('tags', tags + [marker])
    require(unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(), 'Exit seam save failed')
    unreal.log('SUPPLY_EXIT_SEAM1_SAVED: real one-centimetre downward step onto the exit apron')


def revise_route_a_clearance(present):
    marker = unreal.Name('ShellSupply.RouteAClearance1')
    coordinator = present['Supply_Coordinator']
    schedule = present['Supply_Schedule']
    tags = list(coordinator.get_editor_property('tags'))
    route = schedule.get_editor_property('route_a')
    expected_y = 300 if marker in tags else 550
    offsets = list(route.get_editor_property('unload_offsets'))
    require(near_vector(route.get_editor_property('start'), (-800, -1000, 70)) and
            near_vector(route.get_editor_property('end'), (700, -1000, 70)) and len(offsets) == 3 and
            all(near_vector(value, expected) for value, expected in zip(offsets,
                [(0, 0, 0), (0, expected_y, 0), (0, -550, 0)])),
            'Route A unload candidates were manually changed; preserving the route')
    if marker in tags:
        unreal.log('SUPPLY_ROUTE_A_CLEARANCE1_PRESERVED')
        return
    # 기존 두 번째 후보는 초기 축전지와 겹친다. 같은 도로의 실제 비점유 후보로만 옮긴다.
    offsets[1] = unreal.Vector(0, 300, 0)
    route.set_editor_property('unload_offsets', offsets)
    schedule.set_editor_property('route_a', route)
    coordinator.set_editor_property('tags', tags + [marker])
    require(unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(), 'Route A clearance save failed')
    unreal.log('SUPPLY_ROUTE_A_CLEARANCE1_SAVED: unload candidate clears the initial battery')


def advance_stage(world, present, classes, previous, requested, args):
    require(STAGES.index(requested) == STAGES.index(previous) + 1,
            'Advance one stage at a time; observe charge before authoring transport: current=%s requested=%s' % (previous, requested))
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    old_specs = stage_actor_specs(previous, classes)
    new_specs = stage_actor_specs(requested, classes)
    labels = {actor.get_actor_label() for actor in actors.get_all_level_actors()}
    preserved = [(actor, actor.get_actor_transform()) for actor in actors.get_all_level_actors()]
    for label in set(new_specs) - set(old_specs):
        require(label not in labels, 'New stage label already exists; preserving map: ' + label)
    if requested == 'complete':
        validate_cargo_slot_api(classes['Plate'])
    # 기존 단계 값·이름·타입 검사가 끝난 뒤에만 에셋과 새 액터를 변경한다.
    cargo_plate = cargo_plate_class(classes['Plate'], args.cargo_plate_class) if requested == 'complete' else None
    widget = require(unreal.LSShellSupplyEditorLibrary.build_supply_widget_asset(
        True, STAGES.index(requested) >= STAGES.index('materials')),
        'Stage widget upgrade failed; inspect the first editor error')
    require(widget.get_path_name() == WIDGET + '.WBP_ShellSupplyExperiment', 'Stage widget builder returned a different asset')
    save(widget)

    def spawn(key, xyz):
        cls, owner_stage = new_specs[key]
        actor = require(actors.spawn_actor_from_class(cls, unreal.Vector(*xyz)), 'Stage spawn failed: ' + key)
        actor.set_actor_label(key)
        actor.set_editor_property('tags', list(actor.get_editor_property('tags')) + [unreal.Name(ACTOR_TAG + owner_stage)])
        if actor.get_component_by_class(unreal.LSVisionParticipantComponent):
            no_concealment(actor)
        present[key] = actor
        return actor

    if requested == 'transport':
        start = unreal.get_default_object(classes['Transport']).get_editor_property('route_start')
        spawn('Supply_Transport_A', vector_values(start))
    elif requested == 'materials':
        plate_half = unreal.get_default_object(classes['Plate']).get_editor_property('half_extent')
        battery_half = unreal.get_default_object(classes['Battery']).get_editor_property('half_extent')
        spawn('Supply_Plate_01', (900, 600, plate_half.z))
        spawn('Supply_Battery_01', (600, -500, battery_half.z))
    elif requested == 'complete':
        spawn('Supply_Assault_02', (-200, -1400, 88))
        heavy = spawn('Supply_Heavy_01', (-100, -600, 140))
        heavy.set_editor_property('stats_table', present['Supply_Assault_01'].get_editor_property('stats_table'))
        heavy.set_editor_property('stats_row_name', 'Heavy')
        heavy.set_editor_property('enemy_kind', unreal.LSShellEnemyKind.HEAVY)
        heavy.get_editor_property('siege').set_editor_property('deployment_location', unreal.Vector(-700, -600, 140))
        apron = spawn('Supply_ExitApron', (3600, 0, -50))
        floor_mesh = present['Supply_Floor'].static_mesh_component
        apron.static_mesh_component.set_static_mesh(load_asset('/Engine/BasicShapes/Cube'))
        require(unreal.LSShellSupplyEditorLibrary.configure_floor_collision(apron.static_mesh_component),
                'Exit apron must satisfy the native ground/occlusion collision contract')
        apron.static_mesh_component.set_material(0, floor_mesh.get_material(0))
        apron.static_mesh_component.set_phys_material_override(load_asset(FLOOR_PHYSICS))
        apron.set_actor_scale3d(unreal.Vector(8, 40, 1))
        schedule = spawn('Supply_Schedule', (0, 0, 0))
        configure_schedule(schedule, present, classes, cargo_plate)
    set_authoring_stage(present['Supply_Coordinator'], requested)
    validate_map(world, classes, {label: value[0] for label, value in new_specs.items()})
    validate_stage_actors(present, new_specs, requested)
    for actor, transform in preserved:
        require(actor.get_actor_transform() == transform,
                'An existing transform changed during stage authoring; refusing to save: ' + actor.get_actor_label())
    require(unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(), 'Stage map save failed')
    unreal.log('SUPPLY_%s_AUTHORED: %s; existing transforms/tuning preserved; runtime/nav/feel remain unobserved' %
               (requested.upper(), MAP))


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def native(name):
    return require(unreal.load_class(None, '/Script/Landship.' + name),
                   'Build the native class first: ' + name)


def load_asset(path):
    return require(unreal.load_asset(path), 'Required asset missing: ' + path)


def generated(bp):
    return require(unreal.BlueprintEditorLibrary.generated_class(bp),
                   'Blueprint must already compile: ' + bp.get_path_name())


def save(asset):
    require(unreal.EditorAssetLibrary.save_loaded_asset(asset),
            'Asset save failed: ' + asset.get_path_name())


def vector_values(value):
    return (value.x, value.y, value.z)


def near_vector(value, expected):
    return all(math.isfinite(a) and abs(a - b) < .01 for a, b in zip(vector_values(value), expected))


def actor_body(actor):
    body = require(actor.get_editor_property('root_component'), 'Authoritative root missing: ' + actor.get_actor_label())
    require(isinstance(body, unreal.PrimitiveComponent), 'Primitive root required: ' + actor.get_actor_label())
    return body


def no_concealment(actor, public_core=False):
    vision = require(actor.get_component_by_class(unreal.LSVisionParticipantComponent),
                     'Vision component missing: ' + actor.get_actor_label())
    require(not vision.get_editor_property('concealed_outside_sight'),
            'Reusable class must already disable sight concealment: ' + actor.get_actor_label())
    vision.set_editor_property('public_core_position', public_core)


def is_copied_gameplay(actor):
    if isinstance(actor, (unreal.NavMeshBoundsVolume, unreal.RecastNavMesh)):
        return False
    if actor.get_component_by_class(unreal.LightComponentBase):
        return False
    path = actor.get_class().get_path_name()
    return (path.startswith('/Script/Landship.') or path.startswith('/Game/Landship/') or
            isinstance(actor, (unreal.StaticMeshActor, unreal.PlayerStart, unreal.CameraActor, unreal.TargetPoint)))


def navigation(world, all_actors):
    require(unreal.LSShellExperimentEditorLibrary.get_configured_navigation_class(world)
            == '/Script/Landship.LSBattlefieldNavigationSystem',
            'The source navigation-system configuration must be preserved')
    volumes = [a for a in all_actors if isinstance(a, unreal.NavMeshBoundsVolume)]
    require(volumes, 'The map must contain its authored NavMeshBoundsVolume brush')
    require(any(isinstance(a, unreal.RecastNavMesh) for a in all_actors),
            'The saved map must contain its Recast navigation data')
    for volume in volumes:
        center, extent = volume.get_actor_bounds(False)
        require(min(vector_values(extent)) > 0, 'Navigation brush has no usable bounds: ' + volume.get_actor_label())
        unreal.log('SUPPLY_NAV_BRUSH: %s center=%s extent=%s transform=%s' %
                   (volume.get_actor_label(), center, extent, volume.get_actor_transform()))


def validate_floor_material(material):
    require(material.get_path_name() == FLOOR_PHYSICS + '.PM_ShellSupplyFloor',
            'Supply floor physical material must use its authored path, found: ' + material.get_path_name())
    for key, expected in (('friction', .7), ('static_friction', .7), ('restitution', .3)):
        current = material.get_editor_property(key)
        require(abs(current - expected) < .0001,
                'Existing supply floor physical material differs; preserving it: %s expected=%s current=%s' %
                (key, expected, current))
    for key in ('friction_combine_mode', 'restitution_combine_mode'):
        current = material.get_editor_property(key)
        require(current == unreal.FrictionCombineMode.MIN,
                'Supply floor combine mode differs; preserving it: %s expected=Min current=%s' % (key, current))
    for key in ('override_friction_combine_mode', 'override_restitution_combine_mode'):
        current = material.get_editor_property(key)
        require(current, 'Supply floor combine override differs; preserving it: %s expected=True current=%s' % (key, current))


def floor_material():
    if unreal.EditorAssetLibrary.does_asset_exist(FLOOR_PHYSICS):
        material = load_asset(FLOOR_PHYSICS)
        require(isinstance(material, unreal.PhysicalMaterial), 'Supply floor material path has a different asset type')
        validate_floor_material(material)
        return material
    material = require(unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'PM_ShellSupplyFloor', ROOT + '/Physics', unreal.PhysicalMaterial, unreal.PhysicalMaterialFactoryNew()),
        'Supply floor physical-material creation failed')
    for key, value in (('friction', .7), ('static_friction', .7), ('restitution', .3),
                       ('friction_combine_mode', unreal.FrictionCombineMode.MIN),
                       ('restitution_combine_mode', unreal.FrictionCombineMode.MIN),
                       ('override_friction_combine_mode', True), ('override_restitution_combine_mode', True)):
        material.set_editor_property(key, value)
    validate_floor_material(material)
    save(material)
    return material


def blueprint(name, parent, configure):
    path = ROOT + '/Blueprints/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        bp = load_asset(path)
        require(isinstance(bp, unreal.Blueprint), 'Supply Blueprint path has a different asset type: ' + path)
        require(unreal.BlueprintEditorLibrary.get_blueprint_parent_class(bp) == parent,
                'Supply Blueprint parent differs; preserving it: ' + path)
        return generated(bp)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', parent)
    bp = require(unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, ROOT + '/Blueprints', unreal.Blueprint, factory), 'Blueprint creation failed: ' + path)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    configure(unreal.get_default_object(generated(bp)))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    save(bp)
    return generated(bp)


def camera_defaults(camera):
    camera.set_editor_property('min_zoom_distance', 700.0)
    camera.set_editor_property('max_zoom_distance', 6500.0)
    boom = camera.get_editor_property('camera_boom')
    boom.set_editor_property('target_arm_length', 5500.0)
    boom.set_editor_property('relative_rotation', unreal.Rotator(pitch=-55, yaw=-90, roll=0))
    camera.get_editor_property('camera').set_editor_property('field_of_view', 50.0)


def validate_asset_roles(game_class, controller_class, camera_class, widget_class):
    game = unreal.get_default_object(game_class)
    require(game.get_editor_property('player_controller_class') == controller_class and
            game.get_editor_property('default_pawn_class') == camera_class,
            'Supply GameMode references differ; preserving its assets')
    require(unreal.get_default_object(controller_class).get_editor_property('widget_class') == widget_class,
            'Supply controller widget differs; preserving its asset')


def new_assets(source_camera):
    widget = require(unreal.LSShellSupplyEditorLibrary.build_supply_widget_asset(False),
                     'Supply widget authoring failed; inspect the first editor error')
    require(isinstance(widget, unreal.WidgetBlueprint) and widget.get_path_name() == WIDGET + '.WBP_ShellSupplyExperiment',
            'Supply widget builder returned a different asset')
    require(unreal.BlueprintEditorLibrary.get_blueprint_parent_class(widget) == native('LSShellSupplyExperimentWidget'),
            'Supply widget must use its native supply parent')
    save(widget)
    widget_class = generated(widget)
    camera_class = blueprint('BP_ShellSupplyCamera', source_camera, camera_defaults)
    controller_class = blueprint('BP_ShellSupplyController', native('LSShellExperimentController'),
                                 lambda obj: obj.set_editor_property('widget_class', widget_class))

    def configure_mode(obj):
        obj.set_editor_property('player_controller_class', controller_class)
        obj.set_editor_property('default_pawn_class', camera_class)

    game_class = blueprint('BP_ShellSupplyGameMode', native('LSShellExperimentGameMode'), configure_mode)
    validate_asset_roles(game_class, controller_class, camera_class, widget_class)
    return game_class


def log_bodies(present):
    for label in ('Supply_Core', 'Supply_Ball', 'Supply_Barrel'):
        actor = present[label]
        body = actor_body(actor)
        bounds = unreal.SystemLibrary.get_component_bounds(body)
        unreal.log('SUPPLY_BODY: %s class=%s transform=%s body_bounds=%s object_type=%s collision=%s' %
                   (label, actor.get_class().get_path_name(), actor.get_actor_transform(), bounds,
                    body.get_collision_object_type(), body.get_collision_enabled()))
        if label != 'Supply_Core':
            material = actor.get_editor_property('body_material')
            values = {key: material.get_editor_property(key) for key in (
                'friction', 'static_friction', 'restitution', 'friction_combine_mode', 'restitution_combine_mode',
                'override_friction_combine_mode', 'override_restitution_combine_mode')}
            solver_mass = body.get_mass() if body.is_simulating_physics() else None
            unreal.log('SUPPLY_BODY_PHYSICS: %s solver_mass_kg=%s material_asset_values=%s' %
                       (label, solver_mass, values))


def validate_map(world, classes, stage_actors=None):
    current = list(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())
    expected = {
        'Supply_PlayerCastle': classes['Castle'], 'Supply_Core': classes['Core'],
        'Supply_Assault_01': classes['Assault'], 'Supply_Ball': native('LSShellMetalBall'),
        'Supply_Barrel': native('LSShellBarrel'), 'Supply_BattlefieldSetup': native('LSBattlefieldSetup'),
        'Supply_BattlefieldSettings': native('LSBattlefieldSettings'),
        'Supply_Coordinator': native('LSShellExperimentCoordinator'),
        'Supply_Floor': unreal.StaticMeshActor.static_class(), 'Supply_PlayerStart': unreal.PlayerStart.static_class()}
    expected.update(stage_actors or {})
    present = {}
    for actor in current:
        label = actor.get_actor_label()
        if label in expected:
            require(label not in present, 'Duplicate supply actor label: ' + label)
            require(actor.get_class() == expected[label], 'Supply actor has a different class: ' + label)
            present[label] = actor
        else:
            require(not is_copied_gameplay(actor), 'Unplanned gameplay actor; preserving map: ' + label)
    require(set(present) == set(expected), 'Partial supply map; missing labels: ' + str(sorted(set(expected) - set(present))))
    for label in ('Supply_BattlefieldSetup', 'Supply_Coordinator'):
        obj = present[label]
        require(obj.get_editor_property('player_castle') == present['Supply_PlayerCastle'] and
                obj.get_editor_property('core_outpost') == present['Supply_Core'], 'Supply references differ: ' + label)
    coordinator = present['Supply_Coordinator']
    require(coordinator.get_editor_property('rule_profile') == unreal.LSShellRuleProfile.SUPPLY_STAGE_ONE,
            'Existing supply profile differs; preserving map')
    validate_stage_ownership(coordinator)
    require(not coordinator.get_editor_property('enable_events'), 'Legacy random events must be disabled in the supply map')
    require(list(present['Supply_BattlefieldSettings'].get_editor_property('terrain_actors')) == [present['Supply_Floor']],
            'Supply terrain must reference only its floor')
    material = load_asset(FLOOR_PHYSICS)
    require(isinstance(material, unreal.PhysicalMaterial), 'Supply floor material path has a different asset type')
    validate_floor_material(material)
    floor_error = unreal.LSShellSupplyEditorLibrary.get_floor_physics_error(
        present['Supply_Floor'].static_mesh_component, material)
    require(not floor_error, 'Existing supply floor physics differs; preserving map: ' + floor_error)
    for label in ('Supply_PlayerCastle', 'Supply_Core', 'Supply_Assault_01', 'Supply_Ball', 'Supply_Barrel'):
        vision = require(present[label].get_component_by_class(unreal.LSVisionParticipantComponent), 'Supply vision missing: ' + label)
        require(not vision.get_editor_property('concealed_outside_sight') and
                vision.get_editor_property('public_core_position') == (label == 'Supply_Core'), 'Supply vision role differs: ' + label)
    require(not present['Supply_Core'].get_component_by_class(unreal.LSHealthComponent).get_editor_property('destroy_on_death'),
            'Core must remain after common death handling')
    require(present['Supply_Assault_01'].get_editor_property('enemy_kind') == unreal.LSShellEnemyKind.ASSAULT,
            'Supply Assault role differs')
    require(isinstance(actor_body(present['Supply_Core']), unreal.BoxComponent), 'Loaded core must use its actual box collider')
    navigation(world, current)
    game = load_asset(ROOT + '/Blueprints/BP_ShellSupplyGameMode')
    controller = load_asset(ROOT + '/Blueprints/BP_ShellSupplyController')
    camera = load_asset(ROOT + '/Blueprints/BP_ShellSupplyCamera')
    widget = load_asset(WIDGET)
    validate_asset_roles(generated(game), generated(controller), generated(camera), generated(widget))
    require(world.get_world_settings().get_editor_property('default_game_mode') == generated(game),
            'Supply world GameMode override differs')
    log_bodies(present)
    return present


def segment_hits_box(start, end, center, extent):
    low, high = 0.0, 1.0
    for a, b, c, e in zip(vector_values(start), vector_values(end), vector_values(center), vector_values(extent)):
        delta = b - a
        if abs(delta) < .0001:
            if a < c - e or a > c + e:
                return False
        else:
            first, last = sorted(((c - e - a) / delta, (c + e - a) / delta))
            low, high = max(low, first), min(high, last)
            if low > high:
                return False
    return True


def validate_initial_geometry(present):
    core = present['Supply_Core']
    body = actor_body(core)
    require(isinstance(body, unreal.BoxComponent), 'Observed core must still be a box')
    require(near_vector(core.get_actor_location(), (2400, 0, 400)) and
            near_vector(core.get_actor_scale3d(), (1, 1, 1)) and
            near_vector(body.get_scaled_box_extent(), (600, 600, 400)),
            'Loaded core geometry differs from the observed layout; do not guess its facing edge')
    core_center, _, _ = unreal.SystemLibrary.get_component_bounds(body)
    require(near_vector(core_center, (2400, 0, 400)), 'Loaded core collider center differs from the observed position')
    rotation = core.get_actor_rotation()
    require(max(abs(rotation.pitch), abs(rotation.yaw), abs(rotation.roll)) < .01,
            'Initial facing-edge layout requires the observed unrotated core')
    bounds = {}
    for label in ('Supply_PlayerCastle', 'Supply_Core', 'Supply_Assault_01', 'Supply_Ball', 'Supply_Barrel'):
        center, extent, _ = unreal.SystemLibrary.get_component_bounds(actor_body(present[label]))
        require(center.x - extent.x >= -3200 and center.x + extent.x <= 3200 and
                center.y - extent.y >= -2000 and center.y + extent.y <= 2000 and center.z - extent.z >= -.01,
                'Initial body crosses bounds or penetrates floor: ' + label)
        bounds[label] = (center, extent)
    labels = list(bounds)
    for index, label in enumerate(labels):
        center, extent = bounds[label]
        for other in labels[index + 1:]:
            other_center, other_extent = bounds[other]
            require(any(abs(a - b) >= e + f - .01 for a, b, e, f in zip(
                vector_values(center), vector_values(other_center), vector_values(extent), vector_values(other_extent))),
                'Initial body AABBs overlap: %s / %s' % (label, other))
    castle = present['Supply_PlayerCastle']
    weapons = require(castle.get_component_by_class(unreal.LSCastleWeaponComponent), 'Castle weapons missing')
    target = present['Supply_Ball'].get_actor_location()
    origin = weapons.get_weapon_fire_origin(unreal.LSCastleWeapon.CANNON, target)
    for label in ('Supply_Core', 'Supply_Assault_01', 'Supply_Barrel'):
        center, extent = bounds[label]
        expanded = unreal.Vector(extent.x + 6, extent.y + 6, extent.z + 6)
        require(not segment_hits_box(origin, target, center, expanded), 'Initial cannon-to-ball clearance is blocked by: ' + label)
    unreal.log('SUPPLY_GEOMETRY: collider AABBs clear; cannon-to-ball conservative clearance passed; actual flight/contacts require PIE')


def populate_physics(world, classes, game_class, physical_material, floor_visual, cube, sounds):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    require(world.get_path_name().split('.')[0] == MAP, 'Refusing a different world')
    require(not any(a.get_actor_label().startswith('Supply_') for a in actors.get_all_level_actors()),
            'Initial layout requires an untouched new template clone; never reset a supply layout')
    navigation(world, list(actors.get_all_level_actors()))
    for actor in list(actors.get_all_level_actors()):
        if is_copied_gameplay(actor):
            require(actors.destroy_actor(actor), 'Could not remove copied gameplay from the new target: ' + actor.get_actor_label())

    def spawn(cls, label, xyz):
        actor = require(actors.spawn_actor_from_class(cls, unreal.Vector(*xyz)), 'Spawn failed: ' + label)
        actor.set_actor_label(label)
        return actor

    floor = spawn(unreal.StaticMeshActor, 'Supply_Floor', (0, 0, -50))
    floor.static_mesh_component.set_static_mesh(cube)
    require(unreal.LSShellSupplyEditorLibrary.configure_floor_collision(floor.static_mesh_component),
            'Supply floor must satisfy the native ground/occlusion collision contract')
    floor.static_mesh_component.set_material(0, floor_visual)
    floor.static_mesh_component.set_phys_material_override(physical_material)
    floor.set_actor_scale3d(unreal.Vector(64, 40, 1))
    castle = spawn(classes['Castle'], 'Supply_PlayerCastle', (-1800, 0, 500))
    core = spawn(classes['Core'], 'Supply_Core', (2400, 0, 400))
    require(isinstance(actor_body(core), unreal.BoxComponent), 'Loaded core must use its observed box')
    require(near_vector(actor_body(core).get_scaled_box_extent(), (600, 600, 400)), 'Loaded core extent changed; aborting lane authoring')
    core_center, _, _ = unreal.SystemLibrary.get_component_bounds(actor_body(core))
    require(near_vector(core_center, (2400, 0, 400)), 'Loaded core collider center changed; aborting lane authoring')
    facing_x = core_center.x - actor_body(core).get_scaled_box_extent().x
    ball = spawn(native('LSShellMetalBall'), 'Supply_Ball', (facing_x - 600, 0, 70))
    ball.set_editor_property('intrinsic_mass_kg', 750.0)
    barrel = spawn(native('LSShellBarrel'), 'Supply_Barrel', (facing_x - 250, 0, 60))
    assault = spawn(classes['Assault'], 'Supply_Assault_01', (-300, 1200, 88))
    for obj in (castle, core, ball, barrel, assault):
        no_concealment(obj, obj == core)
    setup = spawn(native('LSBattlefieldSetup'), 'Supply_BattlefieldSetup', (0, 0, 0))
    coordinator = spawn(native('LSShellExperimentCoordinator'), 'Supply_Coordinator', (0, 0, 0))
    for obj in (setup, coordinator):
        obj.set_editor_property('player_castle', castle)
        obj.set_editor_property('core_outpost', core)
    coordinator.set_editor_property('rule_profile', unreal.LSShellRuleProfile.SUPPLY_STAGE_ONE)
    set_authoring_stage(coordinator, 'physics')
    tuning = coordinator.get_editor_property('supply_tuning')
    tuning.set_editor_property('thrust_momentum', 660000.0)
    tuning.set_editor_property('blast_momentum', 650000.0)
    coordinator.set_editor_property('supply_tuning', tuning)
    coordinator.set_editor_property('enable_events', False)
    for key, value in sounds.items():
        coordinator.set_editor_property(key, value)
    settings = spawn(native('LSBattlefieldSettings'), 'Supply_BattlefieldSettings', (0, 0, 0))
    for key, value in (('bounds_min', unreal.Vector2D(-3200, -2000)), ('bounds_max', unreal.Vector2D(3200, 2000)),
                       ('ground_height', 0.0), ('outside_darkness', 0.0), ('terrain_actors', [floor])):
        settings.set_editor_property(key, value)
    spawn(unreal.PlayerStart, 'Supply_PlayerStart', (300, 0, 0))
    world.get_world_settings().set_editor_property('default_game_mode', game_class)
    present = validate_map(world, classes)
    validate_initial_geometry(present)
    require(levels.save_current_level(), 'Supply map save failed')
    unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')
    unreal.log('SUPPLY_PHYSICS_AUTHORED: ' + MAP + '; navigation pending: wait for Build Paths and save; PIE, camera projection and user feel remain unobserved')


def load_stage_classes(stage, classes, args):
    if STAGES.index(stage) >= STAGES.index('transport'):
        classes['Transport'] = actor_class(args.transport_class)
        definition = unreal.get_default_object(classes['Transport'])
        require(isinstance(definition, unreal.LSShellTransport), 'Transport class must derive from LSShellTransport')
        require(len(definition.get_editor_property('cargo_slots')) == 2, 'Route A requires two native cargo slots')
        for key in ('route_start', 'route_end'):
            require(all(math.isfinite(value) for value in vector_values(definition.get_editor_property(key))),
                    'Transport class route is nonfinite: ' + key)
    if STAGES.index(stage) >= STAGES.index('materials'):
        for name, path in (('Plate', args.plate_class), ('Battery', args.battery_class)):
            classes[name] = actor_class(path)
            definition = unreal.get_default_object(classes[name])
            require(isinstance(definition, getattr(unreal, 'LSShell' + name)),
                    'Material class has a different native family: ' + path)
            half = definition.get_editor_property('half_extent')
            require(all(math.isfinite(value) and value > 0 for value in vector_values(half)),
                    'Material class has invalid half extents: ' + path)
    if stage == 'complete':
        classes['Schedule'] = actor_class(args.schedule_class)
        classes['Heavy'] = actor_class(args.heavy_class)
        schedule = unreal.get_default_object(classes['Schedule'])
        require(isinstance(schedule, unreal.LSShellSupplySchedule), 'Schedule class must derive from LSShellSupplySchedule')
        require(isinstance(unreal.get_default_object(classes['Heavy']), unreal.LSShellExperimentEnemy),
                'Heavy class must derive from LSShellExperimentEnemy')
        siege = require(unreal.get_default_object(classes['Heavy']).get_editor_property('siege'),
                        'Heavy class must already provide its siege component')
        siege.get_editor_property('deployment_location')
        for key in ('route_a', 'route_b', 'initial_route_a_transport', 'initial_assaults', 'initial_heavy',
                    'enemy_stats_table', 'transport_class', 'enemy_class'):
            schedule.get_editor_property(key)
        for route_key in ('route_a', 'route_b'):
            route = schedule.get_editor_property(route_key)
            for key in ('start', 'end', 'first_departure_seconds', 'repeat_seconds', 'cargo_slots',
                        'charged_cargo_slots', 'replenishment_offset', 'siege_deployment_location', 'unload_offsets', 'exit_waypoints'):
                route.get_editor_property(key)


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--stage', choices=STAGES, default='physics')
    parser.add_argument('--revise-complete-layout', action='store_true')
    parser.add_argument('--transport-class', default='/Script/Landship.LSShellTransport')
    parser.add_argument('--plate-class', default='/Script/Landship.LSShellPlate')
    parser.add_argument('--battery-class', default='/Script/Landship.LSShellBattery')
    parser.add_argument('--cargo-plate-class', default=None)
    parser.add_argument('--schedule-class', default='/Script/Landship.LSShellSupplySchedule')
    parser.add_argument('--heavy-class', default='/Script/Landship.LSShellExperimentEnemy')
    args, _ = parser.parse_known_args(sys.argv[1:])
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    classes = {name: generated(load_asset(ROOT + '/Blueprints/BP_Shell' + name)) for name in ('Castle', 'Core', 'Assault')}
    if assets.does_asset_exist(MAP):
        require(levels.load_level(MAP), 'Existing supply map load failed')
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        require(world.get_path_name().split('.')[0] == MAP, 'Refusing to inspect a different world')
        coordinators = [actor for actor in actors.get_all_level_actors() if actor.get_actor_label() == 'Supply_Coordinator']
        require(len(coordinators) == 1 and coordinators[0].get_class() == native('LSShellExperimentCoordinator'),
                'Exactly one correctly typed Supply_Coordinator is required; preserving map')
        previous = validate_stage_ownership(coordinators[0])
        target = STAGES[max(STAGES.index(previous), STAGES.index(args.stage))]
        load_stage_classes(target, classes, args)
        specs = stage_actor_specs(previous, classes)
        present = validate_map(world, classes, {label: value[0] for label, value in specs.items()})
        validate_stage_actors(present, specs, previous)
        if args.revise_complete_layout:
            require(previous == 'complete' and args.stage == 'complete', 'Layout revision requires the authored Complete stage')
            revise_complete_layout(present)
            revise_exit_seam(present)
            revise_route_a_clearance(present)
            return
        if STAGES.index(args.stage) <= STAGES.index(previous):
            unreal.log('SUPPLY_%s_PRESERVED: %s; no transforms, tuning, actors or assets changed; PIE/nav/feel remain unobserved' %
                       (previous.upper(), MAP))
            return
        if args.stage == 'complete':
            table = present['Supply_Assault_01'].get_editor_property('stats_table')
            require(table and 'Heavy' in [str(name) for name in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)],
                    'Existing enemy table must already contain the Heavy row; preserving map and table')
        advance_stage(world, present, classes, previous, args.stage, args)
        return
    require(args.stage == 'physics', 'Create and observe physics first; new maps cannot skip stages. No writes performed.')
    require(assets.does_asset_exist(SOURCE_MAP), 'The saved source experiment map is required')
    source_camera = generated(load_asset(ROOT + '/Blueprints/BP_ShellCamera'))
    for name in ('LSShellMetalBall', 'LSShellBarrel', 'LSShellSupplyEditorLibrary', 'LSShellSupplyExperimentWidget'):
        native(name)
    game_class = new_assets(source_camera)
    physical_material = floor_material()
    unreal.log('SUPPLY_FLOOR_PHYSICS: %s friction=%s static_friction=%s restitution=%s; both combine modes Min with overrides validated' %
               (physical_material.get_path_name(), physical_material.get_editor_property('friction'),
                physical_material.get_editor_property('static_friction'), physical_material.get_editor_property('restitution')))
    floor_visual = load_asset(ROOT + '/Materials/MI_ShellFloor')
    cube = load_asset('/Engine/BasicShapes/Cube')
    sounds = {key: load_asset(ROOT + '/Audio/' + asset) for key, asset in (
        ('fire_sound', 'S_ShellFire'), ('explosion_sound', 'S_ShellExplosion'), ('impact_sound', 'S_ShellImpact'))}
    require(levels.new_level_from_template(MAP, SOURCE_MAP), 'Supply template-map creation failed')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name().split('.')[0] == MAP, 'Refusing to modify a different world')
    populate_physics(world, classes, game_class, physical_material, floor_visual, cube, sounds)

if __name__ == '__main__':
    main()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)

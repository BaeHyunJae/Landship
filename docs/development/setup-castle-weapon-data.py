"""성 무장 시험용 1~3레벨 Data Asset을 생성한다. 기존 에셋의 수치는 덮어쓰지 않는다."""
import unreal

assets = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
data_class = unreal.load_class(None, '/Script/Landship.LSCastleWeaponData')
if not data_class:
    raise RuntimeError('Build LandshipEditor first')

definitions = [
    ('DA_Cannon', unreal.LSCastleWeapon.CANNON, [
        dict(damage=60, cooldown=8, range=1200, radius=250),
        dict(damage=80, cooldown=8, range=1300, radius=300),
        dict(damage=100, cooldown=8, range=1400, radius=350)]),
    ('DA_Shotgun', unreal.LSCastleWeapon.SHOTGUN, [
        dict(damage=35, cooldown=6, range=600, cone_angle=70, knockback_speed=650),
        dict(damage=45, cooldown=6, range=650, cone_angle=80, knockback_speed=750),
        dict(damage=55, cooldown=6, range=700, cone_angle=90, knockback_speed=850)]),
    ('DA_Ballista', unreal.LSCastleWeapon.BALLISTA, [
        dict(damage=160, cooldown=10, range=1800, radius=18, projectile_speed=1800),
        dict(damage=200, cooldown=10, range=2000, radius=22, projectile_speed=2000),
        dict(damage=240, cooldown=10, range=2200, radius=26, projectile_speed=2200)])]

for name, weapon, rows in definitions:
    path = '/Game/Landship/Castle/Data/' + name
    if editor_assets.does_asset_exist(path):
        continue
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', data_class)
    asset = assets.create_asset(name, '/Game/Landship/Castle/Data', data_class, factory)
    if not asset:
        raise RuntimeError('Weapon data creation failed: ' + path)
    asset.set_editor_property('weapon', weapon)
    levels = []
    for row in rows:
        settings = unreal.LSCastleWeaponSettings()
        for key, value in row.items():
            settings.set_editor_property(key, value)
        levels.append(settings)
    asset.set_editor_property('levels', levels)
    if not editor_assets.save_loaded_asset(asset):
        raise RuntimeError('Weapon data save failed: ' + path)
unreal.log('CASTLE_WEAPON_DATA_SETUP_OK')

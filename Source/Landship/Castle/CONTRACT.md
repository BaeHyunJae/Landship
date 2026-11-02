# 성 계약

## 제공하는 것

- `ALSMobileCastle`은 `ALSUnit`을 상속하는 대형 이동 성이다. 병력과 같은 선택·명령·이동·피해 경로를 쓴다. 선택 분류는 `Castle`, 선택 유형은 `MobileCastle`이다. 이동·정지·위치 사수와 수동 무장을 쓰며 기본 공격이 없다(`SupportsBasicAttack()`이 false). 공격 이동과 적 우클릭 공격에는 참여하지 않는다. 병력과 함께 드래그해 동시에 조종할 수 있다. 파괴되어도 액터를 제거하지 않는다.
- 몸체는 시각 메시이고 충돌이 없다. 이동·피격·선택은 반경 142cm·높이 284cm 캡슐로 처리한다.
- `ALSCastle`은 `APawn` 기반 성이다. 진영·체력·사망은 공통 전투 구현을 쓴다. 아군 성은 조작할 수 있고 적 성은 정보 확인만 할 수 있다.
  - `RequestMove(Destination)`: 명령 수락 여부를 반환한다. 잘못된 좌표, 내비게이션 밖, 불완전 경로, 체력 초기화 실패, 사망, 이동 속도 0은 거부한다. 수락은 도착을 보장하지 않고 이후 장애물 변경으로 중단될 수 있다. 도착 판정은 `AcceptanceRadius`와 성의 충돌 반경을 함께 적용한다.
  - `IsMoving()`: 경로 추종이 진행·대기 중이면 true이고 도착·취소·경로 실패 시 false다.
  - `StopMoving()`: 경로와 잔여 속도를 모두 정지한다.
- `ULSCastleWeaponComponent`는 성의 Q/W/E 수동 무장(일반 대포, 산탄총, 발리스타)의 상태·쿨타임·피해·넉백을 가진다. 시전은 아군 성을 선택한 플레이어 컨트롤러만 한다. 적 성은 무장을 자동으로 쓰지 않는다.
  - 조작: 아군 이동 성을 선택하고 Q/W/E 또는 명령 카드 버튼을 누르면 조준 상태가 되고, 지면을 좌클릭하면 시전한다. 우클릭·Esc는 취소한다. 성을 바꿔 선택하면 조준을 취소한다. 여러 아군 성을 선택하면 첫 성 한 곳에서만 조준·발사한다.
  - 일반 대포: 성 중심에서 사거리 안의 지면을 지정한다(수평 거리). 곡사 포탄을 발사하고 착탄 때 반경 안에 중심점이 들어온 상대 진영의 병력·성·거점에 피해를 준다. 비행 중에는 피해가 없고, 포탄은 경로의 유닛·지형에 막히지 않는다. 사거리 밖 클릭은 실패하고 조준을 유지하며 쿨타임을 쓰지 않는다. 비행 시간은 수평 거리를 `ProjectileSpeed`로 나눈 값이며 최소 0.2초다.
  - 산탄총: 성에서 마우스 방향으로 `PelletCount`개의 탄환을 부채꼴에 고르게 발사한다. 각 탄환은 경로에서 처음 만난 상대 진영 대상에만 피해를 주고 사라진다. 한 발의 총피해는 `Damage`이고 탄환 하나는 `Damage/PelletCount`이며, 같은 대상에 여러 탄환이 맞으면 합산한다. 지면을 조준하면 아군·시체·지형은 통과한다. 맞고 살아남은 상대 병력만 성에서 멀어지는 방향으로 한 발에 한 번 밀어낸다(`ULSOrderComponent::ApplyKnockback`). 성과 건물에는 피해만 준다.
  - 발리스타: 마우스 방향으로 `Range`까지 직진하는 비유도 투사체(`ALSBasicProjectile::LaunchFirstEnemy`)를 발사하고, 처음 맞은 상대 병력·성·거점 하나에 피해를 준 뒤 사라진다. 지면을 조준하면 아군·시체·지형·장식물은 통과한다.
  - 장애물 지정: `CastWeaponAtObstacle(Weapon, Target)`은 경로 장애물(`ALSPathObstacle`)을 지정해 시전한다. 조준점은 장애물 몸체의 중심(`GetObstacleAimPoint`)이고, 지면 조준과 같은 사거리·상태 검사를 거친다. 이렇게 쏜 산탄과 발리스타는 수평 방향이 아니라 조준점을 향해 날아가고, `LSObstacleProjectile` 채널을 막는 첫 몸체에서 멈춘다(발리스타는 `ALSBasicProjectile::LaunchFirstBlocking`). 어떤 조준이든 대포의 착탄 피해는 장애물에 대해 중심점이 아니라 몸체 외곽까지의 수평 거리로 판정한다. 막을 몸체와 바닥의 채널 설정은 `Obstacles` 계약을 따른다.
  - 공통: 세 무장은 독립 쿨타임을 쓰고 자원 비용이 없다. 정상 시전이면 적을 맞히지 못해도 쿨타임을 쓴다. 조준만 했거나 취소한 경우, 잘못된 방향·범위·수치, 성 선택 상실·파괴 상태에서는 시전하지 않는다. 아군은 오폭 허용 설정과 관계없이 피해·넉백 대상에서 제외한다. 피해는 공통 체력 컴포넌트로 전달한다. 발사 시점의 피해·진영·속도·범위·탄환 수를 투사체에 복사하므로, 비행 중 강화·비활성화·성 제거가 이미 발사된 탄환에 영향을 주지 않는다. 안개 밖의 적도 맞힐 수 있으며 투사체는 시야를 제공하지 않는다.
- 무장 상태는 무장 ID별 `FLSCastleWeaponState`(해금·활성·레벨·다음 사용 가능 시각)이며 공유 Data Asset은 런타임 상태를 저장하지 않는다. 시작 상태는 성의 Weapons 컴포넌트 `Initial States`에서 정하며 기본은 세 무장 모두 해금·활성·1레벨이다. 플레이 중 변경은 아래 요청을 쓴다.
  - `UnlockWeapon`, `SetWeaponUnlocked`: 해금·잠금. 활성 선택과 레벨은 유지한다.
  - `SetWeaponEnabled`: 활성·비활성. 잠긴 무장의 활성화 요청은 실패한다(`Locked`).
  - `UpgradeWeapon`: 해금된 무장을 한 레벨 강화한다. 비활성 무장도 강화할 수 있다.
  - `GetWeaponState`, `GetMaxLevel`, `GetResolvedSettings`: 현재 상태, 최대 레벨, 현재 레벨의 최종 수치를 조회한다.
  - `OnWeaponStateChanged`: 실제 상태 변경당 한 번 알린다. 같은 상태를 다시 지정하거나 실패한 요청은 알리지 않는다. 알림 중 상태를 다시 바꾸는 요청은 `Busy`로 거부한다.
  - 비활성화·잠금은 해당 무장의 조준을 즉시 취소한다. 강화하면 조준·명중 판정이 새 레벨의 수치를 쓴다. 상태 변경과 강화는 진행 중인 쿨타임을 초기화하거나 줄이지 않고, 새 쿨타임 수치는 다음 시전부터 적용한다. 발사된 투사체와 시전 직후의 범위 표시는 발사 당시 수치를 유지한다.
  - `SetWeaponDefinitions`: 생성 단계에서만 무장 정의를 바꾼다. 플레이 시작 뒤 교체는 거부한다.
- `ULSCastleWeaponData`는 무장 하나의 레벨별 최종 수치를 가진 Data Asset이다. `Weapon`이 무장 ID이고 `Levels`의 첫 항목이 1레벨이며 항목 수가 최대 레벨이다. 레벨마다 `Damage`, `Cooldown`, `Range`, `Radius`, `ConeAngle`, `KnockbackSpeed`, `KnockbackLift`, `ProjectileSpeed`, `ArcHeight`, `ShellRadius`, `PelletCount`, `PelletRadius`를 지정한다. 대포의 `Radius`는 착탄 피해 범위이며 포탄 표시 반경 `ShellRadius`와 다르다.
- `ULSCastleCommandProviderComponent`는 `ALSMobileCastle`의 기본 컴포넌트로, 선택된 아군 성의 명령 카드 칸을 제공한다. 셀 5~7은 일반 대포 Q, 산탄 W, 발리스타 E이고 기존 조준·확인 시전 기능을 쓴다. 셀 10~12는 창병 F1, 궁병 F2, 기마병 F3 생산이며 `ULSTroopProductionComponent`의 상태·쿨타임을 표시하고 `ALSPlayerController::ProduceTroop()`으로 실행한다. 생산 칸은 쿨타임 표시, 인구 한도, '공간 부족' 문구를 보인다.
- `ExecuteCommand`는 실행 직전에 `GetCommands`로 다시 검사하며 전투가 끝났으면 거부한다.
- 성 무장·생산은 선택 목록에서 처음 나오는 조작 가능한 살아 있는 아군 성을 기준으로 한다. 성마다 따로 생산하는 대기열은 없다.
- 성 무장은 안개 밖 지면 지점도 조준할 수 있다. 경로 장애물을 조준할 때는 장애물의 상호작용 위치가 현재 시야 안이어야 한다(`ULSCastleWeaponComponent::GetObstacleAimPoint`).

## 동작 조건

- `ALSMobileCastle`은 `Content/Landship/Troops/Data`의 `DT_TroopStats`에 `MobileCastle` 행이 있어야 한다. 수치는 원본 `Data/Troops/DT_TroopStats.csv`의 그 행에서 고친다.
- 무장 정의의 기본값은 `Content/Landship/Castle/Data`의 `DA_Cannon`, `DA_Shotgun`, `DA_Ballista`다. 누락되었거나 잘못된 레벨 데이터는 기본값으로 대체하지 않고 해당 무장의 사용과 강화를 막는다.
- `ALSMobileCastle`이 지나갈 맵에는 반경 142cm·높이 284cm에 맞춘 내비게이션이 필요하다(`Navigation` 계약의 `ALSNavigationSetup`).
- `ALSCastle` 파생 성은 유효한 `StatsRow`를 지정해야 한다. `MovementSpeed`가 0이거나 수치 초기화가 실패하면 이동 요청을 거부한다.
- `ALSCastle`은 `AIController`가 소유한다. 플레이어 컨트롤러는 성에 빙의하지 않고 `Controls`의 명령으로 조종한다.
- `ALSCastle`의 내비게이션 에이전트는 반경 142cm·높이 200cm다. 맵의 내비게이션은 이에 맞아야 하며 기본 에이전트 크기로 만들면 모서리에 걸릴 수 있다.

## 하지 않는 일

- 성은 기본 공격과 자동 공격을 갖지 않는다. 사거리 안의 적을 자동으로 공격하는 일은 병력과 거점이 맡는다.
- 무장 조준 입력과 상태는 `Controls`의 `ALSPlayerController`가, 조준 범위 표시는 `ALSRTSHUD`가 맡는다. 병력 생산은 `Troops`가 맡는다.
- 강화 비용 차감과 별도의 강화·토글 화면은 제공하지 않는다.
- `ALSCastle`은 수평 지면 이동만 다룬다. 중력과 경사면 높이 추종은 없다.

## 공용 테스트 맵

해당 없음.

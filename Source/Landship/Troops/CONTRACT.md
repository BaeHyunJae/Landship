# 병력 계약

## 제공하는 것

- 병력은 공격 실행 시 표적 방향으로 즉시 회전한다. 회전 완료를 피해·발사의 조건으로 사용하지 않는다. 공격 모션 중 공격 취소·표적 교체·표적 사망을 확인하면 다음 액터 틱에 대기로 복귀한다. 이 처리는 이미 확정한 피해·발사체·공격 쿨타임을 되돌리지 않는다.
- 통합 전투 피드백을 사용하지 않는 HUD에서는 비치명 피격 시 병종 표식의 색을 `HitFlashColor`로 `HitFlashDuration`초 동안 강조한다. 통합 전투 피드백을 사용하면 이 강조를 생략하고 HUD의 흰색 피격 표현을 쓴다. 연속 피격은 강조 시간을 갱신하며 종료 시 진영 색으로 복구한다. 사망 시 표식을 숨기고 표현 타이머를 정리한다.
- `ALSUnit`은 병력의 공통 기반이다. 선택·명령·시야·체력·기본 공격 컴포넌트를 갖추고 `StatsRow`의 수치로 체력·공격·이동 속도·시야를 초기화한다. 진영은 `SetInitialFaction()`으로 `BeginPlay` 전에 지정한다. 시작 후 호출은 거부된다. `IsGameplayStaged()`가 true인 생산 준비 중 병력은 전투·선택·시야 대상에서 제외한다. 수치 읽기가 성공했는지는 `IsStatsInitialized()`로 확인한다.
- `ALSTroopUnit`은 `ALSUnit`을 상속한 창병·궁병·기마병 하나의 클래스다. 병종은 `ELSTroopType`으로 지정하며 같은 클래스를 `Faction`으로 아군과 적에 배치한다.
  - 병종은 에디터에서 지정하거나 지연 생성 중 `SetInitialTroopType()`으로 지정한다. `BeginPlay` 이후 호출이나 잘못된 병종은 `false`를 반환하고 병종을 바꾸지 않는다. `GetTroopType()`으로 읽는다.
  - 병종 수치는 `GetStatsRowName()`이 돌려주는 행 이름(`Spearman`, `Archer`, `Cavalry`)으로 `DT_TroopStats`에서 읽는다. 수치는 원본 `Data/Troops/DT_TroopStats.csv`에서 고치고 절차 `docs/procedures/import-troop-stats.md`로 반영한다.
  - `ULSSelectableComponent::SelectionType`은 행 이름으로 설정된다. 같은 클래스의 세 병종을 더블클릭 선택할 때 서로 섞이지 않게 하기 위해서다. `CommandGroupName`은 병종 이름(창병·궁병·기마병)이다.
  - 충돌은 캡슐이 소유한다. 머리 위 표식과 메시는 이동·피격·클릭 충돌에 참여하지 않는다.
  - 궁병은 `RangedAttack` 설정의 투사체 클래스로 원거리 공격을 하며 화살 형태를 쓴다. 클래스·속도·수명·반경은 `ALSTroopUnit` 기본값에서 조정한다. 창병·기마병은 투사체 클래스를 비워 근접 즉시 피해를 쓴다. 투사체의 비행·명중 규칙은 `Combat` 계약을 따른다.
  - 창병·기마병은 `MeleeIdleAnimation`·`MeleeAttackAnimation`, 궁병은 `ArcherIdleAnimation`·`ArcherAttackAnimation`을 대기 반복과 공격 시 한 번 재생에 쓴다. 클립은 `Landship|Troops|Animation` 속성에서 바꾼다.
- `ULSTroopProductionComponent`는 플레이어 컨트롤러가 가지며 병종별 생산 쿨타임과 아군 인구수를 관리한다. 호출은 `ALSPlayerController::ProduceTroop()`을 거친다.
  - `Produce(Type)`: 병종을 넘기면 선택된 성 주변의 서로 겹치지 않는 빈 자리 두 곳에 같은 병종의 아군 병력 두 기를 만들고 `ELSProductionResult::Ready`를 반환한다. 생산 요청이 있을 때 부른다. 성공할 때만 해당 병종의 쿨타임이 시작되고 인구수 2가 쓰인다. 두 자리 중 하나라도 없거나 어느 한 병력의 생성·수치 초기화·컨트롤러 준비가 실패하면 두 기를 모두 취소한다. 취소 중에는 인구 알림, 시야 등록, 최초 발견, 생산 기록이 발생하지 않는다. 실패하면 인구수와 쿨타임은 바뀌지 않는다.
  - `GetStatus(Type)`: 상태를 바꾸지 않고 지금 생산할 수 있는지(`Ready`, `SelectCastle`, `Cooldown`, `PopulationFull`, `Unavailable`, `Busy`)를 반환한다. 버튼·표시를 갱신할 때 부른다. 빈 자리가 없는 경우의 `NoSpace`는 `Produce()`만 반환한다.
  - `GetRemainingCooldown(Type)`과 `GetCooldownSeconds()`: 남은 쿨타임과 전체 쿨타임(초)을 반환한다. 병종마다 쿨타임이 독립이다. 컨트롤러가 관리하므로 성을 바꾸거나 선택을 해제해도 초기화되지 않는다. 전체 쿨타임은 `CooldownSeconds`로 조정한다.
  - `GetSelectedProducer()`: 생산 위치로 쓸 성을 반환한다. 선택 목록에서 처음 나오는 조작 가능한 살아 있는 아군 성이다. 병력과 함께 선택해도 된다. 여러 성을 선택하면 처음 성만 쓴다. 해당하는 성이 없으면 `GetStatus()`는 `SelectCastle`을 반환한다.
  - `GetLastProducedUnit()`: 마지막 성공 생산 묶음의 두 번째 병력을 반환한다. `GetLastProducedBatch()`는 그 묶음의 두 병력을 반환한다. 실패한 요청은 두 기록을 바꾸지 않는다.
  - 인구수는 `ALSGameState`의 값을 쓴다. 맵 시작 다음 틱에 살아 있는 아군 병력(`ALSUnit`, 선택 종류가 Unit)을 집계하고 이후 이 컴포넌트로 만든 병력을 등록한다. 병종에 관계없이 한 기당 1이다. 병력이 죽으면 반환하고, 살아 있는 병력을 제거해도 반환한다. 같은 병력은 한 번만 반환한다. 성과 적은 세지 않는다. 현재 인구수에 2를 더하면 상한을 넘는 경우 `PopulationFull`이다.

## 동작 조건

- 병종 수치: `/Game/Landship/Troops/Data/DT_TroopStats`에 `Spearman`, `Archer`, `Cavalry` 행이 `LSUnitStatRow` 형식으로 있어야 한다. 행을 읽지 못하거나 값이 잘못되면 대체 수치 없이 그 병력은 공격·이동을 멈추고 충돌을 잃은 채 전투에 참여하지 않는다.
- 표식 머티리얼은 `/Game/Landship/Troops/Materials/M_TroopPrototype`이며 벡터 파라미터 `TeamColor`로 진영 색을 받는다.
- 애니메이션 클립 네 개는 병종 메시와 같은 스켈레톤이고 재생 길이·배율이 유효해야 한다. 기본값은 `/Game/ReflectionLab/Gameplay/` 아래 마이그레이션된 클립이며 경로를 바꾸면 속성도 바꾼다. 맞지 않으면 경고를 남기고 애니메이션 연결만 생략하며 전투 동작은 그대로다.
- 생산 컴포넌트는 로컬 플레이어 컨트롤러에서만 동작한다. 월드에 `ALSGameState`가 있고 `ALSGameMode`가 인구 상한을 설정해야 하며, 시작 시 아군 병력 수가 상한을 넘으면 오류를 남기고 생산할 수 없다(`Unavailable`). 집계가 끝나는 다음 틱 전에도 `Unavailable`이다.
- 생산 위치는 해당 병종의 NavData 위이고 다른 병력·건물·장애물·지형과 겹치거나 지뢰 접촉 영역을 침범하지 않아야 한다. 성 중심의 지면 위치를 같은 병력용 NavData에 투영한 시작점부터 후보까지 직선 접근 구간이 열려 있고 완전 경로도 이어져야 한다. 우회 경로가 있더라도 직선 접근을 막는 온전한 벽·바리케이드 너머에는 생산하지 않는다. 후보 쪽에 별도 시작점을 만들어 벽 너머의 단절된 영역을 허용하지 않으며, 성 자체가 그 후보까지 통과할 수 있을 필요는 없다. 생산 공간 계산과 실제 병력 설정은 `ALSTroopUnit::GetCapsuleDimensions()`의 몸체 크기를 함께 쓴다.
- 성의 실제 남은 이동 경로를 기준으로 후방·측면에서만 찾고, 투영한 뒤에도 성의 통행 폭을 침범하는 후보는 제외한다. 정지 중에는 성의 현재 방향을 기준으로 탐색한다. 성에서 가까운 곳부터 `MaxSearchDistance`까지 찾고, 서로 겹치지 않는 안전한 자리 두 곳이 없으면 인구수·쿨타임을 쓰지 않고 `NoSpace`를 반환한다. 생성 직후 자동 추종이나 집결 명령을 부여하지 않는다.
- 전투 결과가 확정된 뒤에는 생산할 수 없다(`Unavailable`).

## 하지 않는 일

- 생산에 자원 비용이나 예약 큐는 없다. 여러 성을 선택해도 성마다 따로 생산하지 않고 처음 성만 쓴다.
- 생산 버튼과 F1~F3 단축키는 `Castle`의 명령 제공자가 만들고, 격자 표시는 `HUD`, 입력 처리는 `Controls`가 맡는다.
- 인구수 값과 상한은 `Match`의 `ALSGameState`가 보관한다. 이 폴더는 증감만 요청한다. 다른 시스템이 런타임에 만든 병력은 인구수에 자동으로 등록되지 않는다.
- 투사체의 비행과 명중 피해는 `Combat`이 맡는다. 근접 피해와 투사체 발사 시점은 `ULSBasicAttackComponent`가 정하고, 애니메이션은 확정된 공격을 표현만 한다.
- 이동 전용 애니메이션은 없다. 대기 동작을 반복한다.

## 공용 테스트 맵

해당 없음.

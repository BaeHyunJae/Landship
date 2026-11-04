# 병력 계약

## 제공하는 것

- 의무병은 마나를 소모해 치유한다. 최대/시작 마나·초당 재생·실제 회복 HP당 비용·새 연결 최소 마나는 `ULSMedicComponent` 기본 설정에서 조정한다. 재생은 전투 활성 중에만 진행하고, 마나가 부족하면 가능한 회복량까지만 적용한 뒤 연결을 반납한다. 완치 초과분과 실패한 회복에는 비용을 남기지 않는다. 머리 위 `MP 현재/최대` 표시와 `GetCurrentMana()`로 잔량을 조회한다.
- 의무병은 환자의 공통 체력 컴포넌트에 치유 권한을 획득한 경우에만 회복한다. 이미 다른 의무병이 치료 중인 대상은 후보에서 제외하고 남은 부상자로 분산한다. 접근 중인 원거리 목표는 미리 독점하지 않는다. 완치·거리 이탈·경직·넉백·마나 고갈·사망·전투 종료·풀 반환 시 권한을 반납한다. 환자의 풀 세대와 치유 권한 세대를 함께 확인한다.
- 풀 재대여/편성 복원은 의무병의 마나를 시작값으로 준비한다. 준비 페이즈 대기만으로 마나를 비축하지 않는다. 이전 생명의 회복 콜백이 새 생명의 마나를 환불하거나 변경하지 않도록 생명 세대를 확인한다.

- `Medic`는 기본 공격 대신 `ULSMedicComponent`를 사용한다. 같은 진영의 살아 있는 병력 한 명을 치유하며 자신과 성은 대상에서 제외한다. 부상 대상을 유지하다 완치·사망·탐색 범위 이탈 시 다시 고르고, 부상자가 없으면 가까운 전투 병력을 따라간다. 치유 거리 밖·경직·넉백·사망·전투 종료에서는 치유 빔을 끈다. 최대 체력 초과 회복과 부활·우버차지는 제공하지 않는다.
- 지원 병종은 `IsSupportBehavior()`를 통해 Match로부터 아군 목록을 받는다. `EvaluateEngagement`는 따라갈 대상·거리·정지 여부를 반환하고 실제 이동은 기존 Match 경로를 사용한다. 컴포넌트는 치유와 빔 표시만 실행하며 전장 전체를 매 프레임 탐색하지 않는다. 환자의 풀 세대도 검사하고 풀 반환 시 대상·누적 치유·빔을 초기화한다.

- TABS 자동 전투의 창병·궁병·기병은 `ULSTroopPoolSubsystem`에서 클래스·병종별로 대여한다. 현재 기본 `ALSTroopUnit` 클래스만 지원하며 같은 객체의 병종은 바꾸지 않는다. 성·맵 배치 병력·기존 RTS 생산 경로를 자동 편입하지 않는다.
- `AcquireUnit(Class, Type, Faction, Transform, Owner)`는 객체와 세대가 든 `FLSTroopLease`를 반환한다. `ReleaseUnit(Lease)`는 같은 월드의 활성 대여만 한 번 소비한다. 피해·기본 공격·명령 업데이트 실행 중 반환은 거부한다. 직접 lifecycle 함수를 호출해 활성 대여를 우회하면 안 된다.
- 반환 시 공격·명령·경로·시야 참여·선택 권한·타이머를 중지하고 외부 체력/공격 구독을 정리한다. 메시 랙돌과 이동·AI의 Tick을 끈다. 다음 대여는 테이블 수치, 체력, 진영, 메시 부착·충돌, 애니메이션, 시야와 병종 행동을 다시 준비한다. 기병 돌격·창병 대비의 사용 여부와 궁병 피격 디버프도 초기화한다. `BeginPlay`를 다시 호출하거나 컴포넌트를 매번 생성하지 않는다.
- 풀 병력은 제거 설정이 켜졌을 때 기존 시체 유지 시간 후 세대별 타이머로 반환한다. 즉시 제거 설정은 피해/사망 콜백을 빠져나온 다음 틱에 반환한다. 제거 설정이 꺼진 병력은 편성 정리까지 유지한다. 풀에 남은 객체 포인터는 영구 병력 ID가 아니므로 과거 대여 핸들을 다음 생명에 재사용하면 안 된다.
- 풀은 병력과 같은 AIController를 유지하되 비활성 중 이동 경로·Brain을 중지하고 Tick을 끈다. 기본 AAIController만 지원하며 사용자 정의 AI/블루프린트 수명은 별도 초기화 구현이 필요하다. 일반 액터 제거와 월드 종료에서는 소유 AI도 제거한다.
- `PrewarmPool(Class, Type, DesiredInactiveCount)`는 비활성 개수를 목표로 한다. `ls.TroopPool.MaxInactivePerBucket`은 클래스·병종별 반환 보관 상한이며 0은 무제한이다. 활성 편성과 생성 요청을 제한하지 않고 초과 반환분만 제거한다. 상한 변경 즉시 trim하지 않는다. `ls.TroopPool.Snapshot` 개발 진단은 Shipping에서 제외한다.

- 궁병은 실제 피해를 받으면 공격 종류와 관계없이 기본 2초간 공격 간격이 1.5배가 된다. 반복 피격은 지속시간만 갱신한다. 0 피해·거부된 피해에는 적용하지 않고 이미 발사한 화살은 유지한다. 사망·전투 비활성화·제거 시 해제한다. `ULSArcherComponent`의 `SuppressionDuration`·`SuppressedIntervalMultiplier`로 조정한다. SUPPRESSED 표시는 사격 억제이며 경직과 별도로 유지된다.

- `ALSTroopUnit::CombatBehavior`는 BeginPlay에서 최종 병종에 맞춰 생성되는 전투 컴포넌트 하나다. `ULSCavalryComponent`는 돌격, `ULSSpearmanComponent`는 대비·방어·반격을 소유하며 궁병은 `ULSArcherComponent`가 피격 시 사격 억제를 소유한다. 클래스는 `CombatBehaviorClasses`에서 지정하고 누락·추상 클래스는 진단 후 전투 시작을 거부한다. 기본 비활성이다. 자동 전투 모드가 시작 때 `SetBattleEnabled(true)`, 결과 확정 때 `false`를 호출한다. 비활성화는 속도·대비·경직·틱을 정리하지만 이미 소비한 돌격과 방어를 복구하지 않는다. 재전투는 새 병력으로 복원한다.
- `ULSTroopCombatComponent`는 공통 경직과 활성 수명을 소유한다. `EvaluateEngagement`는 대상·접근 거리·위치 사수·기본 공격 억제 여부를 반환하며 Match가 실행한다. `ResolveChargeDefense`는 접촉 전 방어 결과를 확정한다. 기병은 창병 구현을 직접 참조하지 않는다. 컴포넌트 틱은 돌격·대비·경직 중에만 켠다.
- `TryStartCharge(Target)`는 기마병이 충분히 떨어진 살아 있는 적을 향해 달릴 때 시작한다. 실제 도움닫기 거리와 속도를 만족해야 경로상 첫 상대 진영에게 한 번 피해를 준다. 아군·시체는 돌격 피해 대상이 아니다. 급회전·순간 이동·공중 이동·제한시간 초과는 돌격을 취소하고 원래 이동 속도로 돌아온다. 명중 전에 취소하면 사용 횟수를 소비하지 않는다.
- `TryBrace(Threat)`는 접근하는 돌격을 보고 창병이 방향을 고정하도록 한다. 준비 시간이 지난 정지 상태에서 정면 돌격을 받으면 방어를 한 번 소비하여 피해·경직·밀림을 줄이고 반격한다. 후방 피격·준비 미완료·방어 소비 후 피격은 줄이지 않는다. 완전 무적은 아니다.
- 경직 중에는 자동 전투 모드가 명령을 내리지 않는다. 공격 중단과 `DelayNextAttack`을 함께 사용하므로 재시작으로 경직을 우회하지 않는다. 이미 발사된 화살은 유지한다. 돌격 피해와 반격은 공통 체력·진영 정책을 거친다.
- 병종 머리 위에 CHARGE·BRACE·STAGGER·GUARD USED 상태를 표시하고 기마병의 임시 랜스는 돌격 중 수평으로 내린다. 충돌 판정은 랜스 메시나 애니메이션에 의존하지 않는다.

- 병력은 공격 실행 시 표적 방향으로 즉시 회전한다. 회전 완료를 피해·발사의 조건으로 사용하지 않는다. 공격 모션 중 공격 취소·표적 교체·표적 사망을 확인하면 다음 액터 틱에 대기로 복귀한다. 이 처리는 이미 확정한 피해·발사체·공격 쿨타임을 되돌리지 않는다.
- 비치명 피격은 병종 표식의 색을 `HitFlashColor`로 `HitFlashDuration`초 동안 강조한다. 연속 피격은 강조 시간을 갱신하며 종료 시 진영 색으로 복구한다. 사망 시 표식을 숨기고 표현 타이머를 정리한다.
- `ALSUnit`은 병력의 공통 기반이다. 선택·명령·시야·체력·기본 공격 컴포넌트를 갖추고 `StatsRow`의 수치로 체력·공격·이동 속도·시야를 초기화한다. 진영은 `SetInitialFaction()`으로 `BeginPlay` 전에 지정한다. 시작 후 호출은 거부된다. 수치 읽기가 성공했는지는 `IsStatsInitialized()`로 확인한다.
- `ALSTroopUnit`은 `ALSUnit`을 상속한 창병·궁병·기마병 하나의 클래스다. 병종은 `ELSTroopType`으로 지정하며 같은 클래스를 `Faction`으로 아군과 적에 배치한다.
  - 병종은 에디터에서 지정하거나 지연 생성 중 `SetInitialTroopType()`으로 지정한다. `BeginPlay` 이후 호출이나 잘못된 병종은 `false`를 반환하고 병종을 바꾸지 않는다. `GetTroopType()`으로 읽는다.
  - 병종 수치는 `GetStatsRowName()`이 돌려주는 행 이름(`Spearman`, `Archer`, `Cavalry`)으로 `DT_TroopStats`에서 읽는다. 수치는 원본 `Data/Troops/DT_TroopStats.csv`에서 고치고 절차 `docs/procedures/import-troop-stats.md`로 반영한다.
  - `ULSSelectableComponent::SelectionType`은 행 이름으로 설정된다. 같은 클래스의 세 병종을 더블클릭 선택할 때 서로 섞이지 않게 하기 위해서다. `CommandGroupName`은 병종 이름(창병·궁병·기마병)이다.
  - 충돌은 캡슐이 소유한다. 머리 위 표식과 메시는 이동·피격·클릭 충돌에 참여하지 않는다.
  - 궁병은 `RangedAttack` 설정의 투사체 클래스로 원거리 공격을 한다. 클래스·속도·수명·반경은 `ALSTroopUnit` 기본값에서 조정한다. 창병·기마병은 투사체 클래스를 비워 근접 즉시 피해를 쓴다. 투사체의 비행·명중 규칙은 `Combat` 계약을 따른다.
  - 창병·기마병은 `MeleeIdleAnimation`·`MeleeAttackAnimation`, 궁병은 `ArcherIdleAnimation`·`ArcherAttackAnimation`을 대기 반복과 공격 시 한 번 재생에 쓴다. 클립은 `Landship|Troops|Animation` 속성에서 바꾼다.
- `ULSTroopProductionComponent`는 플레이어 컨트롤러가 가지며 병종별 생산 쿨타임과 아군 인구수를 관리한다. 호출은 `ALSPlayerController::ProduceTroop()`을 거친다.
  - `Produce(Type)`: 병종을 넘기면 선택된 성 주변의 빈 자리에 아군 병력 한 기를 만들고 `ELSProductionResult::Ready`를 반환한다. 생산 요청이 있을 때 부른다. 성공할 때만 해당 병종의 쿨타임이 시작되고 인구수 1이 쓰인다. 실패하면 인구수와 쿨타임은 바뀌지 않는다.
  - `GetStatus(Type)`: 상태를 바꾸지 않고 지금 생산할 수 있는지(`Ready`, `SelectCastle`, `Cooldown`, `PopulationFull`, `Unavailable`, `Busy`)를 반환한다. 버튼·표시를 갱신할 때 부른다. 빈 자리가 없는 경우의 `NoSpace`는 `Produce()`만 반환한다.
  - `GetRemainingCooldown(Type)`과 `GetCooldownSeconds()`: 남은 쿨타임과 전체 쿨타임(초)을 반환한다. 병종마다 쿨타임이 독립이다. 컨트롤러가 관리하므로 성을 바꾸거나 선택을 해제해도 초기화되지 않는다. 전체 쿨타임은 `CooldownSeconds`로 조정한다.
  - `GetSelectedProducer()`: 생산 위치로 쓸 성을 반환한다. 선택 목록에서 처음 나오는 조작 가능한 살아 있는 아군 성이다. 병력과 함께 선택해도 된다. 여러 성을 선택하면 처음 성만 쓴다. 해당하는 성이 없으면 `GetStatus()`는 `SelectCastle`을 반환한다.
  - `GetLastProducedUnit()`: 마지막으로 만든 병력을 반환한다.
  - 인구수는 `ALSGameState`의 값을 쓴다. 맵 시작 다음 틱에 살아 있는 아군 병력(`ALSUnit`, 선택 종류가 Unit)을 집계하고 이후 이 컴포넌트로 만든 병력을 등록한다. 병종에 관계없이 한 기당 1이다. 병력이 죽으면 반환하고, 살아 있는 병력을 제거해도 반환한다. 같은 병력은 한 번만 반환한다. 성과 적은 세지 않는다. 현재 인구수가 상한 이상이면 `PopulationFull`이다.

## 동작 조건

- 병종 수치: `/Game/Landship/Troops/Data/DT_TroopStats`에 `Spearman`, `Archer`, `Cavalry` 행이 `LSUnitStatRow` 형식으로 있어야 한다. 행을 읽지 못하거나 값이 잘못되면 대체 수치 없이 그 병력은 공격·이동을 멈추고 충돌을 잃은 채 전투에 참여하지 않는다.
- 표식 머티리얼은 `/Game/Landship/Troops/Materials/M_TroopPrototype`이며 벡터 파라미터 `TeamColor`로 진영 색을 받는다.
- 애니메이션 클립 네 개는 병종 메시와 같은 스켈레톤이고 재생 길이·배율이 유효해야 한다. 기본값은 `/Game/ReflectionLab/Gameplay/` 아래 마이그레이션된 클립이며 경로를 바꾸면 속성도 바꾼다. 맞지 않으면 경고를 남기고 애니메이션 연결만 생략하며 전투 동작은 그대로다.
- 생산 컴포넌트는 로컬 플레이어 컨트롤러에서만 동작한다. 월드에 `ALSGameState`가 있고 `ALSGameMode`가 인구 상한을 설정해야 하며, 시작 시 아군 병력 수가 상한을 넘으면 오류를 남기고 생산할 수 없다(`Unavailable`). 집계가 끝나는 다음 틱 전에도 `Unavailable`이다.
- 생산 위치를 찾으려면 NavMesh가 필요하다. 후보는 NavMesh 위이고, 다른 병력·건물·장애물·지형과 겹치지 않으며, 성 위치에서 이어지는 완전한 경로가 있는 자리다. 성에서 가까운 곳부터 `MaxSearchDistance`까지 찾고, 그 범위에 없으면 `NoSpace`를 반환한다.
- 전투 결과가 확정된 뒤에는 생산할 수 없다(`Unavailable`).

## 하지 않는 일

- 생산에 자원 비용이나 예약 큐는 없다. 여러 성을 선택해도 성마다 따로 생산하지 않고 처음 성만 쓴다.
- 생산 버튼과 F1~F3 단축키는 `Castle`의 명령 제공자가 만들고, 격자 표시는 `HUD`, 입력 처리는 `Controls`가 맡는다.
- 인구수 값과 상한은 `Match`의 `ALSGameState`가 보관한다. 이 폴더는 증감만 요청한다. 다른 시스템이 런타임에 만든 병력은 인구수에 자동으로 등록되지 않는다.
- 투사체의 비행과 명중 피해는 `Combat`이 맡는다. 근접 피해와 투사체 발사 시점은 `ULSBasicAttackComponent`가 정하고, 애니메이션은 확정된 공격을 표현만 한다.
- 이동 전용 애니메이션은 없다. 대기 동작을 반복한다.

## 공용 테스트 맵

해당 없음.

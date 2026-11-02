# 적 거점·분대 계약

## 제공하는 것

- `ALSBattlefieldSetup`은 맵에서 플레이어 성(`PlayerCastle`)과 핵심 거점(`CoreOutpost`)을 명시적으로 지정한다. 맵에 하나만 배치한다. 성은 아군 진영·성 선택 분류·체력 컴포넌트 하나를 가져야 하고, 핵심 거점은 적 진영이며 체력 컴포넌트 하나를 가지고 사망해도 액터를 유지해야 한다. 두 참조는 같은 월드에 있어야 한다. 에디터 유효성 검사가 이를 확인한다.
- 유효한 `ALSBattlefieldSettings`가 있는 맵에서는 핵심 거점의 `ULSVisionParticipantComponent`에 `bPublicCorePosition`이 켜져 있어야 하고 다른 액터에는 켜져 있으면 안 된다. 에디터 유효성 검사가 오류로 보고하며 설정을 자동으로 바꾸지 않는다. 핵심 거점을 바꾸면 인스턴스의 플래그도 옮긴다.
- `ULSBattlefieldStateSubsystem`은 Game/PIE 월드에서 BeginPlay 때 배치된 설정을 해석한다. 참조 해석과 체력 준비 완료는 별개다.
  - `IsSetupValid()`는 지정한 참조의 구성 검사 결과를 반환한다.
  - `TryGetPlayerCastleState(Out)`는 성의 현재 위치와 생존 상태를 채운다. 준비 전이거나 조회에 실패하면 false를 반환하고 출력값을 바꾸지 않는다.
  - `GetCoreOutpost()`는 지정한 핵심 거점을 반환한다. 설정이 유효하지 않거나 액터가 제거됐으면 null이다.
  - `IsBattleEndedForEnemies()`는 관찰한 성 사망 또는 기록한 핵심 거점 함락이 있으면 true다. 승패의 우선순위나 결과 UI는 정하지 않는다.
  - `HasObservedPlayerCastleDeath()`는 성 참조가 사라진 뒤에도 이미 관찰한 사망만 반환한다.
  - `TryGetCoreCapture(Out)`는 핵심 거점 함락 스냅샷(이름·위치·핵심 여부·약한 액터 참조)을 반환한다. 액터가 사라져도 기록은 남는다.
  - `OnCoreOutpostCapturedEvent`는 함락 정리가 끝난 다음 틱에 한 번 발생한다. 늦게 연결하는 쪽은 먼저 `TryGetCoreCapture`로 현재 스냅샷을 조회한 뒤 연결한다.
- `ALSOutpost`는 체력이 0이 된 필수 사망 준비 경로에서만 함락을 기록하고 공격·충돌을 정리한다. 맵 해제나 단순 Destroy는 함락이 아니다.
  - `HasBeenCaptured()`, `TryGetCapture(Out)`로 살아 있는 거점에서 함락 사실을 조회한다.
  - `OnOutpostCapturedEvent`는 다음 틱에 한 번 발생한다. 정리 도중 액터가 제거되면 개별 알림은 생략될 수 있다.
  - `IsArmed()`, `GetDefenseRadius()`로 무장 여부와 방어 반경을 조회한다.
- `ALSEnemyOutpost`는 기본값으로 공용 거점 표(`DT_OutpostStats`)의 `PrototypeTest` 행을 쓴다. 수치와 무장 여부(`bArmed`), 방어 반경(`DefenseRadius`)은 파생 블루프린트의 기본값에서 정한다. `bArmed`가 꺼져 있어도 피격 시 주변 병력에 지원을 요청한다. 본체는 시제품 재질에 진영 색으로 표시한다.
- `ALSEnemyTroop`은 공용 `ALSTroopUnit`의 병종·수치·표시를 그대로 쓰고 적 진영으로 시작한다. 병종은 전투 준비 전에 `SetInitialTroopType()`으로 지정한다.
- `ALSEnemyTroop`의 `Behavior`(`ULSEnemyBehaviorComponent`)를 설정한다.
  - 배치한 인스턴스에서 `Role`(Guard/Assault)과 Guard의 `GuardedOutpost`를 정한다(`EditInstanceOnly`). Guard의 초기 위치가 복귀 기준이고, 방어 구역은 담당 거점의 `DefenseRadius`가 정한다.
  - 수신 반경(`SupportAcceptanceRadius`), 추적 반경(`ChaseRadius`), 진격 재개 거리(`RecoveryAdvanceDistance`), 경계 여유(`BoundaryAllowance`), 복귀 영역(`ReturnAreaRadius`), 요청 수명(`PendingSupportLifetime`) 같은 수치는 파생 블루프린트의 클래스 기본값에서 정한다(`EditDefaultsOnly`).
  - `GetState()`, `GetTarget()`, `GetLastStopReason()`로 동작 상태·현재 표적·중단 사유를 조회한다.
- 런타임에 병력을 생성할 때는 `SpawnActor`의 Transform에 실제 초기 위치를 넣고, 첫 행동 전에 `Role`과 `GuardedOutpost`를 지정한다. 복귀 기준 위치는 BeginPlay 때의 위치다. 구성 오류로 준비 대기 중인 병력도 올바른 참조를 받으면 다시 판단을 시작한다.
- `ULSEnemyBehaviorComponent::EndBattleActions()`는 이동·표적·지원 상태를 비우고 이후 행동 재개를 거절한다. `Match`가 결과를 확정할 때 호출한다.

## 동작 조건

- 적 행동 컴포넌트를 쓰는 액터가 있는 월드에는 유효한 `ALSBattlefieldSetup`이 정확히 하나 있어야 한다. 없거나 유효하지 않으면 경고 로그를 남기고 `IsSetupValid()`는 false다. 이때 공격 역할 병력은 준비 대기로 남는다. 적 행동 컴포넌트를 쓰는 액터가 없는 월드는 설정을 요구하지 않는다.
- Guard 병력은 같은 월드의 `GuardedOutpost`가 있어야 하고 초기 위치가 그 거점의 방어 반경 안에 있어야 한다. 어기면 에디터 오류와 런타임 경고 한 번을 남기고 준비 대기로 남는다.
- 병력이 거점을 근접 사거리 안에서 공격하려면 NavMesh 위에 거점 둘레의 도달 가능한 지점이 있어야 한다. 거점 중심은 충돌 때문에 NavMesh에서 제외될 수 있다. 기본 `ALSEnemyOutpost`의 모형과 충돌은 이를 만족하도록 맞춰져 있다. 거점의 크기·회전·주변 장애물·높이를 바꿀 때는 물리 외곽, NavMesh 에이전트와 셀, 사거리를 함께 검사하고, 공개 지정 공격 명령으로 실제 피해가 들어가는지 확인한다. 이 접근 조건을 확인하는 에디터 경고는 없다.

## 하지 않는 일

- 승패 판정, 결과 UI, 전역 정지는 `Match`가 맡는다. `IsBattleEndedForEnemies()`와 함락 알림은 판정이나 승리 표시가 아니다. `Match`가 판정을 관리하는 월드에서는 `Match`가 결과를 확정한 뒤 각 적이 새 이동·공격·지원을 멈춘다. 판정을 관리하지 않는 월드는 핵심 함락과 관찰된 성 사망을 기준으로 종료한다.
- 핵심 거점의 공개 위치 표식은 위치만 공개한다. 실물 표시, 체력 선택, 공격 허용은 `Vision`의 시야 규칙을 따른다.
- 기존 공용 `ALSTroopUnit`과 `ALSOutpost`에는 적 판단을 붙이지 않는다. 적 판단과 AI 이동은 `ALSEnemyTroop`과 `ALSEnemyOutpost`만 켠다. 플레이어 명령과 아군 자동 교전은 `Controls`와 `Combat`이 맡는다.
- 활동 도중 역할이나 담당 거점을 바꾸는 기능은 제공하지 않는다.
- 적 이동은 `Controls`의 명령 큐와 별도 실행부를 쓴다. 요청 추적, 요청 교체 중 중단 구분, 사거리 안 공격 지점 탐색, 도착 오차 제한은 양쪽에 중복된다. `Controls`의 이동을 고친 PR은 적 이동에 같은 조건이 있는지 확인하고, 반대도 같다. 역할과 추적 경계 정책은 `Enemies`만 소유한다.

## 공용 테스트 맵

해당 없음.

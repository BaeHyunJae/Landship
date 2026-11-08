# 적 거점·분대 계약

## 제공하는 것

- 현재 표적의 사망·사거리 진입·`RepathDistance` 이상의 이동은 다음 행동 틱에 다시 판단하고, 이동 완료도 다음 판단을 앞당긴다. 이때에도 수비·진격·복귀 구역과 실패 경로 재시도 조건은 유지한다. 플레이어 명령 컴포넌트로 적 행동을 실행하지 않는다.
  - 진행 중인 경로가 무효화되면 실패 경로의 대기 시간을 적용하지 않고 다음 행동 틱에 역할별 목표·전체 경로 검사를 다시 수행한다. 실제 막힘이나 그 밖의 이동 실패에는 기존 재시도 조건을 유지한다.
- `ALSBattlefieldSetup`은 맵에서 플레이어 성(`PlayerCastle`)과 핵심 거점(`CoreOutpost`)을 명시적으로 지정한다. 맵에 하나만 배치한다. 성은 실제 게임 성인 `ALSCastle` 또는 그 파생 클래스이며 아군 진영·성 선택 분류·체력 컴포넌트 하나를 가져야 하고, 핵심 거점은 적 진영이며 체력 컴포넌트 하나를 가지고 사망해도 액터를 유지해야 한다. 두 참조는 같은 월드에 있어야 한다. 병력 이동 시험용 `ALSMobileCastle`이나 성 선택 분류만 붙인 일반 액터는 거부한다. 에디터와 런타임의 구성 검사가 같은 조건을 확인한다.
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
- `ALSEnemyOutpost`는 기본값으로 공용 거점 표(`DT_OutpostStats`)의 `PrototypeTest` 행을 쓴다. 수치와 무장 여부(`bArmed`)는 파생 블루프린트의 기본값에서 정한다. 방어 반경(`DefenseRadius`)은 그 기본값을 사용하되 맵의 거점 인스턴스에서 조정할 수 있으며, 담당 Guard의 이동 구역과 초기 배치 검사에 같은 값이 적용된다. `bArmed`가 꺼져 있어도 피격 시 주변 병력에 지원을 요청한다. 본체는 시제품 재질에 진영 색으로 표시한다.
- `ALSEnemyTroop`은 공용 `ALSTroopUnit`의 병종·수치·표시를 그대로 쓰고 적 진영으로 시작한다. 병종은 전투 준비 전에 `SetInitialTroopType()`으로 지정한다.
- 적 병력의 감지 반경과 표적 동률 거리 점수는 액터 중심의 3D 거리를 기준으로 한다. 실제 공격 대상·접근·도착 판정은 공통 전투의 충돌 몸체 외곽 사거리를 사용한다. 큰 전장 성은 감지·생존·진영 검사를 유지한 채 공격 후보에 포함하며, 실제 이동 투영은 해당 병력의 NavData를 사용한다.
- 투사체를 쓰는 적 병력은 사거리 안이어도 공통 공격의 발사 경로 검사가 막힌 위치에 멈춰 발사하지 않는다. 기존의 유한한 접근 후보를 해당 병력의 NavMesh에 투영하고, 실제 몸체 중심 높이에서 사거리와 발사 경로를 함께 검사해 이동 위치를 고른다. Guard의 담당 거점·수비 기준점 구역과 Assault의 추적 경계, 전체 경로 제한은 유지하며 휴면 병력은 이동하지 않는다. 고정 시설은 사격 위치를 바꾸지 않는다.
- `ALSEnemyTroop`의 `Behavior`(`ULSEnemyBehaviorComponent`)를 설정한다.
  - 배치한 인스턴스에서 `Role`(Guard/Assault)과 Guard의 `GuardedOutpost`를 정한다(`EditInstanceOnly`). Guard의 초기 위치가 복귀 기준이고, 방어 구역은 담당 거점의 `DefenseRadius`가 정한다.
  - 거점 없는 수비 구역은 같은 월드의 비전투 액터 `GuardAnchor`와 `GuardAnchorRadius`로 지정한다. `GuardedOutpost`와 함께 지정할 수 없다. 이 모드는 앵커 중심의 반경을 지키며 초기 위치로 복귀한다. 담당 거점의 지원 요청을 거리와 무관하게 받거나 우선하는 규칙은 거점 모드에만 적용한다.
  - `bStartRegionallyDormant`는 기본값이 false이며 Assault 인스턴스에만 켠다. 휴면 중에도 기존 시야·충돌·피해 수신을 유지하지만 이동·감지·공격·지원 송수신을 하지 않는다. `Maps`의 `ALSBattlefieldEncounterCoordinator`에 정확히 한 번 배정해야 하며 누락이나 중복은 오류다. 지역별 공격대 규모는 조정자의 `AssaultMembers` 소속 배열이 정하며 적 행동 컴포넌트는 같은 규모를 요구하지 않는다.
  - `ActivateRegion()`은 체력이 준비된 생존 Assault의 휴면만 해제한다. 사망이나 전투 종료를 되돌리지 않고, 호출 안에서 행동을 판단하지 않는다. 실제 행동은 다음 적 행동 틱에서 재개한다.
  - 수신 반경(`SupportAcceptanceRadius`), 추적 반경(`ChaseRadius`), 진격 재개 거리(`RecoveryAdvanceDistance`), 경계 여유(`BoundaryAllowance`), 복귀 영역(`ReturnAreaRadius`), 요청 수명(`PendingSupportLifetime`) 같은 수치는 파생 블루프린트의 클래스 기본값에서 정한다(`EditDefaultsOnly`).
  - `GetState()`, `GetTarget()`, `GetLastStopReason()`로 동작 상태·현재 표적·중단 사유를 조회한다.
- 런타임에 병력을 생성할 때는 `SpawnActor`의 Transform에 실제 초기 위치를 넣고, 첫 행동 전에 `Role`과 `GuardedOutpost`를 지정한다. 복귀 기준 위치는 BeginPlay 때의 위치다. 구성 오류로 준비 대기 중인 병력도 올바른 참조를 받으면 다시 판단을 시작한다.
- `ULSEnemyBehaviorComponent::EndBattleActions()`는 이동·표적·지원 상태를 비우고 이후 행동 재개를 거절한다. `Match`가 결과를 확정할 때 호출한다.
  - `ShellExperiment`의 전용 적 클래스는 `ALSUnit` 기반에서 같은 행동 컴포넌트를 사용한다. 실험 모드가 아니거나 실행 설정이 유효하지 않으면 첫 행동 틱 전에 이 종료를 요청한다. 일반 병종 초기화·플레이어 명령은 사용하지 않는다.
  - 실험의 강제 이동 중에는 전용 반응 컴포넌트가 행동 틱을 잠시 끄고 AI 이동·공격을 중단한다. 착지 후 틱을 복원한다. 이동 중단이 기존 경로 실패 처리로 기록되면 기존 재시도 간격(기본 1초)을 거친다.

## 동작 조건

- 적 행동 컴포넌트를 쓰는 액터가 있는 월드에는 유효한 `ALSBattlefieldSetup`이 정확히 하나 있어야 한다. 없거나 유효하지 않으면 경고 로그를 남기고 `IsSetupValid()`는 false다. 이때 공격 역할 병력은 준비 대기로 남는다. 적 행동 컴포넌트를 쓰는 액터가 없는 월드는 설정을 요구하지 않는다.
- Guard 병력은 같은 월드의 `GuardedOutpost`가 있어야 하고 초기 위치가 그 거점의 방어 반경 안에 있어야 한다. 어기면 에디터 오류와 런타임 경고 한 번을 남기고 준비 대기로 남는다.
- 병력이 거점을 근접 사거리 안에서 공격하려면 NavMesh 위에 거점 둘레의 도달 가능한 지점이 있어야 한다. 거점 중심은 충돌 때문에 NavMesh에서 제외될 수 있다. 기본 `ALSEnemyOutpost`의 모형과 충돌은 이를 만족하도록 맞춰져 있다. 거점의 크기·회전·주변 장애물·높이를 바꿀 때는 물리 외곽, NavMesh 에이전트와 셀, 사거리를 함께 검사하고, 공개 지정 공격 명령으로 실제 피해가 들어가는지 확인한다. 이 접근 조건을 확인하는 에디터 경고는 없다.

## 하지 않는 일

- 승패 판정, 결과 UI, 전역 정지는 `Match`가 맡는다. `IsBattleEndedForEnemies()`와 함락 알림은 판정이나 승리 표시가 아니다. `Match`가 판정을 관리하는 월드에서는 `Match`가 결과를 확정한 뒤 각 적이 새 이동·공격·지원을 멈춘다. 판정을 관리하지 않는 월드는 핵심 함락과 관찰된 성 사망을 기준으로 종료한다.
- 핵심 거점의 공개 위치 표식은 위치만 공개한다. 실물 표시, 체력 선택, 공격 허용은 `Vision`의 시야 규칙을 따른다.
- 기존 공용 `ALSTroopUnit`과 `ALSOutpost`에는 적 판단을 붙이지 않는다. 적 판단과 AI 이동은 `ALSEnemyTroop`, `ALSEnemyOutpost`, 실험 전용 `ALSShellExperimentEnemy`에서 켠다. 플레이어 명령과 아군 자동 교전은 `Controls`와 `Combat`이 맡는다.
- 활동 도중 역할이나 담당 거점을 바꾸는 기능은 제공하지 않는다.
- 적 이동은 `Controls`의 명령 큐와 별도 실행부를 쓴다. 요청 추적, 요청 교체 중 중단 구분, 사거리 안 공격 지점 탐색, 도착 오차 제한은 양쪽에 중복된다. `Controls`의 이동을 고친 PR은 적 이동에 같은 조건이 있는지 확인하고, 반대도 같다. 역할과 추적 경계 정책은 `Enemies`만 소유한다.

## 공용 테스트 맵

해당 없음.

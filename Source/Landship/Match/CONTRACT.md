# 승패 판정 계약

## 제공하는 것

- 자동 전투의 지원 병종은 적 대신 아군 목록으로 교전 판단을 하고 아군을 따라 이동한다. 적 공격 대상 판정·근접 포위 슬롯은 사용하지 않으며 피해 기본 공격도 실행하지 않는다.
- 현재 JSON 편성 형식은 네 병종 비용을 저장하는 버전 2다. 기존 버전 1은 세 병종 비용·편성을 보존하고 의무병 기본 비용을 추가해 읽는다. 기존 시나리오 에셋도 로드할 때 누락된 의무병 비용만 추가한다. 기존 병종의 enum 번호와 비용은 바꾸지 않는다.

- TABS 병력 배치·삭제·편성 복원과 프리셋 교체/실패 정리는 병력 풀의 대여·반환을 사용한다. 풀 병력의 AIController는 보관 수명에 속하므로 편성 목록에서 빠져도 제거하지 않으며, 성과 비풀 액터는 기존 제거 경로를 유지한다.

- 자동 전투 정리 시 풀 화살은 제거하지 않고 전투 공통 풀의 `ReleaseAll()`로 비활성화한다. 비행 중인 이전 전투의 화살도 반환하므로 편성 복원 뒤 피해를 전달하지 않는다.
- 자동 전투 시작 전에 풀을 지원하는 투사체를 클래스별 첫 일제 사격 수만큼 사전 생성한다. 비활성 화살이 이미 충분하면 추가 생성하지 않는다. 사전 생성 이후에도 부족한 화살은 발사 시 생성한다.

- 자동 전투의 근접 접근은 Match 소유 계획기가 아군끼리 공격 자리를 예약하고 실제 점유와 완전 경로를 확인한다. 접근이 정체되면 같은 표적의 다른 자리, 가까운 다른 표적 순으로 다시 판단하며 아군 혼잡에는 국소 경유지를 사용한다. 현재 공격 가능한 표적을 유지하고 궁병 우회 같은 병종 우선 전략은 선택하지 않는다. 돌격·대비·경직·넉백에는 일반 접근 계획을 적용하지 않는다. 예약은 사망·행동 중단·전투 재생성 시 해제한다. 경로 질의 수와 재판단 간격은 `MeleeApproach` 설정으로 제한한다.

- 자동 전투 모드는 병종 돌격 컴포넌트를 전투 시작에만 켜고 결과 확정 시 끈다. 창병은 접근하는 돌격에 대비하며 기마병은 도움닫기 후 접촉하도록 접근 거리를 바꾼다. 경직·공중 밀림이 끝나기 전에는 새 이동·공격을 내리지 않는다.
- 자동 전투는 병력의 `CombatBehavior` 유효성을 시작 전에 검사하고 전투 동안만 활성화한다. 교전 판단은 `EvaluateEngagement` 결과를 실행하며 Match가 병종별 돌격·대비 분기를 소유하지 않는다.

### 인게임 편성 편집과 프리셋

- 준비 단계의 PlaceUnit·ResetFormation·MoveCastle·SelectWeapon은 진영 인자를 받으며 생략하면 기존 아군 동작이다. 적은 아군 예산·인구를 소비하지 않는다. 배치 식별자는 아군 양수·적 음수·선택 없음 0이며 MoveUnit·RemoveUnit은 ID로 진영을 결정한다. 재도전은 양측 수정 편성을 유지한다.
- `SetPreparationLimits`는 이미 사용한 비용·인구보다 작거나 유효 범위 밖인 요청을 거부한다. 변경 성공 때만 예산·상한·알림을 확정한다.
- `FLSAutoBattlePresetStore`는 ADR-0020의 JSON 형식을 `Saved/AutoBattlePresets`에서 관리한다. 이름은 공백을 포함한 문자·숫자·밑줄·하이픈으로 제한하며 경로 입력을 거부한다. 새 저장은 기존 이름을 덮어쓰지 않고 명시적인 교체만 허용한다. 임시 파일 작성 후 파일 교체를 확정하고 실패하면 임시 파일을 정리한다.
- `GetPreset`은 양측 준비 편성과 경제·성·무장을 복사한다. `ApplyPreset`은 준비 중 형식·현재 전장·NavMesh·장애물·겹침을 검사한 후 새 액터 전부가 생성된 경우에만 이전 액터와 교체한다. 실패하면 기존 편성과 액터를 유지한다. 성공은 BattleId 갱신과 OnChanged로 알리고 ID를 새로 부여한다. 저장·불러오기는 준비 단계에서만 가능하다.

### 에디터 적 편성 도구

- `ALSAutoBattleScenario`의 `Enemy Placement Tool`은 에디터 월드에서만 작동한다. `LoadEnemyMarkers`는 저장된 적 편성을 편집 마커로 만들며 기존 마커를 덮어쓰지 않는다. `AddEnemyMarker`는 선택 병종을 지정 지면 주변 빈 자리에 추가한다. 마커는 표준 이동·Alt 복제·Delete와 Details의 병종 변경을 사용한다.
- `ApplyEnemyMarkers`는 소속 레벨·병종·지면·경계·스케일·병력/성/장애물 겹침을 먼저 검사하고 성공한 경우에만 편성 전체를 바꾼다. 실패하면 기존 편성을 유지하고 `PlacementToolStatus`에 이유를 표시한다. 맵 저장은 별도 Ctrl+S이며 원본 데이터 형식은 `EnemyFormation` 그대로다. 회전은 저장하지 않는다.
- 추가·적용·마커 정리는 에디터 트랜잭션으로 되돌릴 수 있다. `HideEnemyMarkers`는 편성을 유지한 채 마커 편집을 정리한다. `ALSAutoBattleEnemyMarker`는 쿠킹에서 제외되는 에디터 전용 액터이며 PIE에서 실제 병력이 되지 않는다.
- 개인 맵 사용과 저장·재로드 검증은 `docs/procedures/Developers/HMH/enemy-placement-tool.md`를 따른다.

### 배치 후 자동 전투 모드

- `ALSAutoBattleGameMode`는 전용 맵의 준비·전투·결과를 관리한다. 시나리오가 하나 있고 설정과 NavMesh가 유효해야 준비로 진입하며 실패는 Error와 진단 문구로 표시한다. 기존 `ALSGameMode`와 승패 GameState를 사용하지 않는다. 싱글플레이 전용이다.
- `ALSAutoBattleScenario`의 전장 경계·배치 구역·양측 성 위치·적 편성·병종 비용·예산·인구 상한을 맵에서 정한다. 배치 데이터의 Position은 지면 좌표이며 생성 시 캡슐 높이를 더한다.
- `PlaceUnit`·`MoveUnit`·`RemoveUnit`·`ResetFormation`·`MoveCastle`·`SelectWeapon`은 준비 단계만 허용한다. 성공 때만 편성·비용을 확정하고 OnChanged를 보낸다. 실패는 `ELSAutoBattleRequest`이며 상태와 PlaceUnit 출력 ID를 보존한다. 병력 제거는 비용을 환급한다. 입력 좌표는 배치 구역·NavMesh·충돌 겹침을 검사한다.
- `StartBattle`은 비어 있지 않은 편성에서 자동 지휘를 시작한다. 전투 중 편성·무장 변경과 재도전을 거부한다. 양측 병력은 공통 공격을 사용하되 RTS 명령과 역할별 적 행동은 실행하지 않는다. 성은 선택 무장 하나를 자동으로 사용하며 사거리 밖의 적에게 자동 접근하고 사거리 안에서 멈춘다. 대포는 밀집 지점, 산탄은 가까운 적, 발리스타는 현재 체력이 높은 적을 우선한다. 피해·오폭은 기존 Combat·Castle 정책을 유지한다.
- 승리는 지정된 모든 적 병력과 적 성 제거, 패배는 아군 성 사망이다. 같은 프레임 양측 조건이 충족되면 패배이며 프레임 말미에 한 번 확정하고 행동을 종료한다.
- `RetryBattle(true)`는 결과에서 편성·예산·무장·성 시작 위치를 유지한 준비로 돌아가며 내구도는 새 액터의 초기값이다. false면 빈 편성과 전체 예산이다. 요청 성공은 수락을 뜻하며 Loading으로 전환해 이전 액터·투사체를 정리하고 다음 프레임 이후 내비게이션 준비를 확인해 복원한다. 완료는 Preparation 또는 Error와 OnChanged로 알린다. 복원 시 전투 ID를 증가시킨다. 이전 사망 상태를 복구하지 않는다. 노드 진행·지속 자원·패배 페널티는 아직 제공하지 않는다.
- `OnChanged`는 표시 갱신용이며 동기 알림 중 변경 요청은 Unavailable이다. 최초 표시는 조회로 채운다. 자동 판단은 제한된 주기로 등록된 전투 액터만 사용한다.

- `ALSGameMode`는 `ALSGameState`, `ALSPlayerController`, `ALSRTSCameraPawn`, `ALSRTSHUD`를 기본 클래스로 연결한다. 게임 상태를 만들 때 인구수 상한을 지정한다.
- `ALSGameState`는 병력 생성과 UI가 공유하는 인구수를 가진다. 자체 초기값은 현재 0, 상한 0이다.
  - 조회: `GetCurrentPopulation()`, `GetPopulationLimit()`.
  - 변경: `SetCurrentPopulation`, `SetPopulationLimit`, `ChangePopulation(Delta)`(양수 증가, 음수 감소). 두 값을 함께 바꿀 때는 `SetPopulation(현재, 상한)`을 쓴다. 성공은 true이고, 음수·상한 초과·현재 값 미만으로의 상한 변경·정수 범위 초과는 false이며 이때 상태와 알림은 바뀌지 않는다.
  - `OnPopulationChanged(현재, 상한)`은 실제 값이 바뀔 때 한 번 발생한다. 같은 값 지정은 성공하지만 알리지 않는다. 구독 직후의 최초 표시는 조회 함수로 채운다. 동기 알림 중에 인구수를 다시 바꾸면 후속 알림이 생길 수 있으므로 UI 콜백은 표시 갱신에만 쓴다.
- `ALSGameMode`는 `ULSBattlefieldStateSubsystem::IsSetupValid()`가 true일 때만 결과 판정을 켠다(`ALSGameState::IsBattleOutcomeEnabled()`). 판정은 프레임 말미에 `TryGetCoreCapture()`와 `HasObservedPlayerCastleDeath()`를 모아 한 번 확정하고, 확정 뒤 적 행동·공격·무장 등 실행부를 정지한다.
- 생산과 병력 사망에 따른 인구수 증감은 `Troops`의 `ULSTroopProductionComponent`가 요청한다.

## 동작 조건

- 프로젝트 기본 GameMode는 템플릿이므로, `Match`의 기능을 쓰는 맵은 World Settings의 GameMode Override를 `ALSGameMode`로 지정한다.

## 하지 않는 일

- 인구수의 네트워크 복제는 구현하지 않는다(싱글플레이 기준).
- 상한을 넘기는 상태를 허용하려면 이 계약을 팀과 바꾼다.
- 유효한 전장 설정이 없는 월드에서는 판정을 관리하지 않는다. 이 월드의 적은 `Enemies`의 `IsBattleEndedForEnemies()`를 기준으로 스스로 종료한다.

## 공용 테스트 맵

해당 없음.

### TABS와 실제 전장 성의 경계

- TABS 편성은 `ALSAutoBattleCastle`을 생성한다. 이동 시험용 `ALSMobileCastle`의 몸체를 재사용하되 자동 전투 무장은 Match의 파생 클래스만 소유한다. 실제 전장의 `ALSCastle`과 전장 설정 검증은 변경하지 않는다.

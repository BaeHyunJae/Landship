# 시야와 미니맵 사용 안내

이 문서는 TASK-45 시야·미니맵 기능의 지도 설정, 액터 참여 방식, 조회 API와 수동 확인 절차를 설명한다. 지도 제작과 게임 기능 연결에 필요한 현재 계약만 다룬다.

## 적용 범위

- 유효한 `ALSBattlefieldSettings`가 지도에 정확히 하나 있을 때 시야 기능을 켠다.
- 설정 액터가 없는 맵은 시야 미사용 월드로 기존 동작을 유지한다. 시야를 사용할 맵은 중복·잘못된 경계·참여 설정 누락을 에디터 유효성 검사로 해결한다.
- 네이티브 `ALSGameMode`는 `ALSRTSHUD`를 사용하고 `/Game/Landship/Vision/UI/WBP_BattlefieldMinimap`을 기본 위젯으로 불러온다. 지도 설정의 기본 안개 머티리얼은 `M_BattlefieldFog`다.

## 지도 설정

1. 지도에 `ALSBattlefieldSettings`를 하나 배치하고 유효성 검사를 통과시킨다.
2. `BoundsMin`과 `BoundsMax`에 월드 XY 사각 경계를 지정하고 `GroundHeight`에 지면 기준 높이와 카메라 초점 높이를 지정한다. 액터의 위치·회전·크기는 경계를 바꾸지 않는다.
3. `TerrainActors`에는 미니맵 지형 캡처에 포함할 정적 지형 액터만 직접 등록한다. 전투 액터는 목록에 넣지 않는다.
4. 월드 XY에서 +X는 미니맵 위쪽, +Y는 오른쪽이다.
5. 시야 갱신 간격 기본값은 `0.1`초, 가시성 텍스처 폭은 `512`, 시야 밖 어둡기는 `0.72`다.

전장 경계·캡처 설정은 PIE 시작 전에 정하고, 변경 뒤에는 PIE를 다시 시작한다.

미니맵 패널의 기본 크기는 `260`이며 `WBP_BattlefieldMinimap`의 `PanelSide` 기본값에서 조정한다. HUD 레이아웃은 화면 크기에 맞춰 패널과 다른 HUD 영역을 배치한다.

## 액터 참여 설정

`ULSVisionParticipantComponent`를 액터에 추가해 역할을 명시한다. 일반 액터와 투사체는 기본적으로 시야를 제공하지 않는다.

| 역할 | 설정 |
| --- | --- |
| 아군 유닛·이동 성 | `bProvidesSight=true`; 기존 능력치 초기화가 시야 반경을 전달한다. |
| 적 전투원 | `bTriggersEnemyDiscovery=true`; 적 발견 후보로 등록한다. |
| 적·장애물 은폐 | `bConcealedOutsideSight=true`가 기본값이며 아군은 이 은폐에서 제외한다. |
| 공개 위치가 필요한 배치형 거점 | 해당 배치 인스턴스에서 `bPublicCorePosition=true`로 설정한다. |
| 터렛 | 적 진영, `MarkerKind=Building`, 발견 트리거 켬, 시야 제공 끔으로 설정한다. |
| 지뢰·장애물 | `MarkerKind=Obstacle`, 발견 트리거 끔, 시야 제공 끔으로 설정한다. |

`ALSUnit`과 `ALSMobileCastle`은 공통 시야 컴포넌트를 한 번 상속한다. `ALSCastle`과 `ALSOutpost`는 각각 별도 액터다. 지도 설정의 에디터 검증은 적 액터와 경로 장애물에 필요한 시야 컴포넌트가 빠진 경우를 진단한다.

숨김 처리는 컴포넌트의 `bVisible` 값을 초기 상태로 되돌리지 않는다. 시야에 따라 숨겨야 하는 부착 시각 액터는 컴포넌트의 부착 시각 목록을 통해 함께 처리한다. 커서 질의도 동일한 부착 시각 목록을 무시한다.

## 코드와 블루프린트 조회

`UWorld`에서 `ULSVisionSubsystem`을 얻어 현재 확정된 시야 스냅샷을 조회한다. 블루프린트에서 사용할 주요 함수는 다음과 같다.

- 참여 컴포넌트의 `SetSightRadius`는 공통 규칙에서 읽은 반경을 전달한다. BP 호출은 개발 검증용으로 제한하며, 운영 기본값은 기존 공통 수치를 사용한다.
- `IsVisionEnabled`: 이 월드에서 시야 규칙이 활성화됐는지 반환한다.
- `IsActorVisibleToFriendly`, `IsPointVisible`: 액터 또는 월드 좌표의 가시 여부를 반환한다.
- `PassesFriendlySight`: 기존 진영·생존 검사에 관찰자 시야 조건을 추가한다.
- `GetMinimapMarkers`: 현재 미니맵 마커를 반환한다.
- `HasDiscoveredEnemy`: 적 발견 여부를 반환한다.
- `HasPendingDiscoveryCaption`: 최초 발견 안내를 아직 표시하지 않았는지 반환한다.
- `MarkDiscoveryCaptionDisplayed`: 안내 문구를 실제로 화면에 표시한 뒤 완료 상태로 기록한다.

`OnSnapshotCommittedEvent`는 새 시야 스냅샷이 확정될 때 발생한다. `OnFirstEnemyDiscoveredEvent`는 최초 적 발견을 알린다. 최초 발견 문구의 렌더링은 TASK-50 소비자가 맡으며, 표시를 완료하기 전에는 완료 함수에 신호를 보내지 않는다.

## 동작 경계

시야 스냅샷은 기본 `0.1`초 간격으로 갱신한다. 전투 액터의 사망·제거는 시야 목록에 즉시 반영하며, 월드가 일시 정지되면 갱신도 멈춘다. 새 월드를 로드하면 발견 상태와 스냅샷을 초기화한다.

시야는 아군의 표적 지정·추적·신규 공격 실행을 제한한다. 적 AI 탐지와 기존 투사체 명중·오폭·피해 규칙은 유지한다. 일반 투사체는 시야 제공자가 아니다. 안개 제거용 특수 투사체는 별도 기능이며 이번 구현에 포함하지 않는다. 안개 밖의 핵심 거점은 체력이 아니라 고정 위치를 공개한다.

## 테스트 지도와 수동 확인

`/Game/Developers/BHJ/Vision/L_VisionTest`는 공통 네이티브 `LSGameMode`를 사용하는 테스트 지도다. `BP_VisionObstacleExample`은 테스트용 예시이며 TASK-47의 최종 기능 액터로 간주하지 않는다.

검증 결과는 `Saved/Task45/Automation/index.json`, `render-verification.json`, 렌더 로그와 전후 PNG에서 확인한다. 자동화·직접 명령 실행과 실제 마우스 조작의 관찰 결과를 구분한다.

수동 확인 순서:

1. 테스트 지도를 열고 PIE를 시작한다.
2. 시야를 제공하는 아군을 선택하고 실제 이동 입력을 수행한다. 이동에 따라 시야와 미니맵이 갱신되는지 확인한다.
3. 미니맵을 클릭해 카메라가 이동하는지 확인하고, 드래그 입력도 확인한다.
4. 작은 창 크기에서 HUD와 미니맵 배치를 확인한다.
5. 가려진 적을 선택·공격 대상으로 지정할 수 없고, 우클릭은 지면 이동으로 처리하는지 확인한다.
6. 적 최초 발견 시 안내 문구 소비자가 실제 문구를 표시한 뒤에만 표시 완료 상태가 되는지 확인한다.

각 항목은 테스트용 장애물 예시의 동작 확인과 구분한다. 최종 액터 기능 검증에는 TASK-46, TASK-47, TASK-48, TASK-50의 기능별 확인이 필요하다.

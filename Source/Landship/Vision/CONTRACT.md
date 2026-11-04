# 시야·미니맵 계약

## 제공하는 것

- 병력 풀의 `SuspendForPool`은 시야 참여와 숨김 복원 기록을 정리한다. 재사용 액터는 새 진영·체력·표시 상태를 준비한 후 `ResumeFromPool`로 참여를 다시 등록한다. 비활성 병력은 시야와 미니맵 참여 목록에 남지 않는다.

- `ULSVisionSubsystem`은 아군 시야 스냅샷을 관리한다. `UWorld`에서 얻어 조회한다.
  - `IsVisionEnabled()`는 이 월드에서 시야 규칙이 켜졌는지 반환한다.
  - `IsActorVisibleToFriendly(Actor)`는 액터가 현재 시야 안이거나 은폐 대상이 아니면 true다. 시야 규칙이 꺼진 월드에서는 항상 true다.
  - `IsPointVisible(Location)`은 월드 좌표가 현재 시야 안인지 반환한다.
  - `PassesFriendlySight(Observer, Target)`는 진영·생존 검사에 추가하는 관찰자 시야 조건이다. 관찰자가 아군일 때만 제한하고, 적 관찰자와 시야에 참여하지 않는 액터에는 기존 동작을 유지한다. 호출자는 아군의 표적 지정·추적·신규 공격 실행 앞에서 부른다. 지뢰는 접촉 구 중심이 시야 안일 때만 통과한다.
  - `GetMinimapMarkers()`는 현재 미니맵 마커를 반환한다.
  - `HasDiscoveredEnemy()`는 적을 처음 발견했는지 반환한다.
  - `HasPendingDiscoveryCaption()`은 최초 발견 안내를 아직 표시하지 않았는지 반환한다. 안내를 실제로 화면에 표시한 뒤에만 `MarkDiscoveryCaptionDisplayed()`를 불러 완료로 기록한다.
  - `OnSnapshotCommittedEvent`는 새 시야 스냅샷이 확정될 때, `OnFirstEnemyDiscoveredEvent`는 최초 적 발견 때 발생한다. 늦게 연결하는 쪽은 `HasPendingDiscoveryCaption()`으로 현재 상태를 조회한다.
- `ULSVisionParticipantComponent`를 액터에 추가해 시야 역할을 명시한다. 일반 액터와 투사체는 기본적으로 시야를 제공하지 않는다.
  - `bProvidesSight`: 시야를 제공한다. 아군 병력(`ALSUnit`)과 성(`ALSCastle`, `ALSMobileCastle`)은 켜져 있고, 시야 반경은 능력치 초기화에서 전달된다. `SetSightRadius()`는 개발 검증용이다.
  - `bTriggersEnemyDiscovery`: 적 진영 액터가 보이면 최초 발견 후보로 등록한다. 병력, 성, `ALSOutpost`, 터렛은 켜져 있다.
  - `bConcealedOutsideSight`: 기본 켬. 아군이 아닌 액터는 시야 밖에서 숨긴다. 숨길 때 부착된 시각 액터도 함께 처리한다.
  - `bPublicCorePosition`: 배치한 거점 인스턴스에서 켜면 시야와 무관하게 고정 위치 마커를 공개한다. 핵심 거점에만 켠다.
  - `MarkerKind`: 미니맵 마커 종류(`Unit`, `Castle`, `Building`, `Obstacle`).
- `WBP_BattlefieldMinimap`(`ULSMinimapWidget`)은 지형, 현재 시야, 마커, 카메라 범위를 그린다. 패널 크기는 블루프린트의 `PanelSide` 기본값에서 조정한다. `ALSRTSHUD`가 유효한 전장 설정이 있을 때만 생성한다.
  - 월드 XY에서 +X가 위쪽, +Y가 오른쪽이다.
  - 좌클릭과 드래그는 카메라를 옮긴다. 우클릭, 명령·조준 대기 중 입력, 그리고 `Controls`가 처리한 입력은 카메라를 옮기지 않는다.
  - 지도 여백은 입력 지점으로 쓰지 않는다.
- `M_BattlefieldFog`는 `ALSBattlefieldSettings`의 기본 안개 머티리얼이다.
- `FLSBattlefieldHUDLayout::Calculate(Viewport, bMinimap, MinimapSide)`는 미니맵, 선택 정보, 명령 카드의 화면 사각형을 비례 축소해 화면 안에 배치한다. 미니맵이 없으면(`bMinimap`이 `false`) 미니맵 폭 없이 같은 규칙으로 계산한다. 명령 카드 기준 크기는 420×336, 선택 정보는 340×125다. `HUD`의 WBP와 `ALSRTSHUD`가 이 값을 공유한다.

## 동작 조건

- 시야 규칙은 월드에 유효한 `ALSBattlefieldSettings`가 정확히 하나 있을 때만 켜진다. 설정이 없는 맵은 시야 없는 월드로 기존 동작을 유지한다. 설정 액터가 있으나 유효하지 않으면(중복, 잘못된 경계 등) 경고 로그를 남기고 시야를 켜지 않는다. 조건은 `Maps` 계약의 `ALSBattlefieldSettings`를 따른다.
- 시야를 쓰는 맵에서 적 진영 액터와 경로 장애물(`PathObstacle` 충돌 객체 타입)에는 `ULSVisionParticipantComponent`가 있어야 한다. 빠지면 `ALSBattlefieldSettings`의 에디터 유효성 검사가 오류로, 플레이 시작 때 경고 로그로 알린다.
- 스냅샷은 `ALSBattlefieldSettings`의 갱신 간격으로 갱신한다. 전투 액터의 사망·제거와 참여자 등록은 즉시 반영한다. 월드가 일시 정지되면 갱신도 멈춘다. 새 월드를 로드하면 발견 상태와 스냅샷을 초기화한다.
- 시야에 따라 숨겨야 하는 부착 시각 액터는 컴포넌트의 부착 시각 목록으로 처리된다. 컴포넌트는 숨기기 전의 가시 상태를 기억해 복원한다.

## 하지 않는 일

- 적 AI의 탐지와 투사체 명중·오폭·피해 규칙은 바꾸지 않는다. 이미 발사된 공격의 피해도 유지한다.
- 표적 지정·추적·신규 공격 제한 자체는 `Combat`과 `Controls`가 `PassesFriendlySight()`를 불러 적용한다. 이 폴더는 판정 결과만 제공한다.
- 일반 투사체는 시야 제공자가 아니다. 안개를 걷는 특수 투사체는 제공하지 않는다.
- 안개 밖의 핵심 거점은 체력이 아니라 고정 위치만 공개한다. 공개 위치만으로 실물 표시, 체력 선택, 공격을 허용하지 않는다.
- 성 무장 등으로 안개 밖 적에게 준 피해는 그 적의 선택·체력 정보·위치를 공개하거나 시야를 제공하지 않는다.
- 최초 발견 안내 문구의 렌더링은 `HUD`의 소비자가 맡는다.
- 미니맵의 명령 입력(이동, 공격 이동, 예약, 무장 조준)을 해석하는 일은 `Controls`가 맡는다.

## 공용 테스트 맵

해당 없음.

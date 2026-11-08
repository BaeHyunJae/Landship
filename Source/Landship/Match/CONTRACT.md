# 승패 판정 계약

## 제공하는 것

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

# TASK-39 프로젝트 골격

## 적용 범위

빨간색 담당 범위인 기능 폴더, 게임 모드/게임 상태/컨트롤러 기반, 인구수, 충돌 채널만 구현한다. 전투 대상, 진영, 피해, 기본 공격, 쿨타임과 이동은 다른 담당 구현과 연결한다.

## 기반 클래스

- `Match/LSGameMode.h`: `ALSGameMode`. `ALSGameState`와 `ALSPlayerController`를 기본 클래스로 연결한다.
- `Match/LSGameState.h`: `ALSGameState`. 현재 인구수와 상한을 관리한다.
- `Controls/LSPlayerController.h`: `ALSPlayerController`. 입력/선택/카메라 기능은 아직 없다.

기본 맵과 템플릿 게임 모드를 덮어쓰지 않는다. 개인 테스트 맵의 World Settings에서 GameMode Override를 `LSGameMode`로 지정하여 확인한다. 컨트롤러에 RTS 입력이나 카메라를 구현한 상태는 아니다.

## 인구수 계약

- 초기 상태는 현재 `0`, 상한 `0`이다. 전투 설정 측에서 `SetPopulation(현재, 상한)`으로 설정한다. 밸런스 수치를 임의로 고정하지 않는다.
- `GetCurrentPopulation`, `GetPopulationLimit`으로 조회한다.
- `SetCurrentPopulation`, `SetPopulationLimit`, `ChangePopulation`으로 변경한다. 성공은 `true`, 잘못된 값은 `false`이며 실패 시 상태와 알림은 바뀌지 않는다.
- 두 값을 동시에 변경할 때는 `SetPopulation`을 사용한다.
- 음수, 상한 초과, 현재 값 미만으로 상한 변경을 거부한다. 상한 초과를 유지하는 기획이 필요하면 이 계약을 팀과 변경한다.
- 실제 값이 바뀌면 `OnPopulationChanged(현재, 상한)`이 한 번 발생한다. 같은 값 지정은 성공하지만 알림을 보내지 않는다.
- C++에서는 `AddDynamic`, 위젯 블루프린트에서는 이벤트 바인딩을 사용한다. 구독 직후 최초 표시는 조회 함수로 채운다. 동기 이벤트 콜백에서 인구수를 다시 변경하면 후속 이벤트가 발생할 수 있으므로 UI 콜백은 표시 갱신에 사용한다.
- 실제 병력 생성/사망 연결, 인구수의 소비 의미, 쿨타임 처리는 이번 변경에 포함하지 않는다. 싱글플레이 기준이며 네트워크 복제는 구현하지 않는다.

## 충돌 채널

`Combat/LSCollisionChannels.h`와 `Config/DefaultEngine.ini`의 번호를 함께 관리한다.

- `Unit`: `LSUnit`, GameTraceChannel1
- `Building`: `LSBuilding`, GameTraceChannel2
- `PathObstacle`: `LSPathObstacle`, GameTraceChannel3
- `Projectile`: `LSProjectile`, GameTraceChannel4
- `Ground`: `LSGround`, GameTraceChannel5

다섯 채널은 Object Channel이다. Ground는 정적 오브젝트로 등록한다. Projectile의 기본 응답은 Ignore, 나머지는 Block이다. 각 담당자는 실제 컴포넌트의 Object Type과 채널 응답을 지정해야 한다. 기존 템플릿 액터의 충돌 종류를 자동 변경하지 않는다. 지면 선택에는 Ground Object Query를 사용할 수 있다. 아군/적 판정은 충돌 채널이 아니라 별도 진영 규칙에서 처리한다.

## 검증

에디터 자동화 테스트의 `Landship.Skeleton`을 실행한다. 인구수의 정상 증감, 음수/상한 초과/오버플로 거부와 게임 모드 클래스 연결, 채널 등록을 검사한다.

게임 모드 실제 적용과 UI 이벤트 연동은 개인 테스트 맵에서 확인한다. GameMode Override를 적용하고 게임 상태에서 인구수를 설정한 뒤 이벤트를 바인딩하여 정상 변경, 동일 값, 실패 입력을 비교한다. 실제 유닛과 투사체의 충돌 및 공동 전투 AC는 담당 기능 통합 후 검증한다.

두 사람 각각의 빌드/실행과 TASK-39 공동 기능 검증 전에는 Task를 Done으로 처리하지 않는다.

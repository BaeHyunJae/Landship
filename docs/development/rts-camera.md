# RTS 카메라

## HMH에서 사용

`ALSGameMode`의 기본 Pawn은 `ALSRTSCameraPawn`이다. 성과 카메라는 서로 다른 Pawn이며 성을 선택하거나 이동시켜도 카메라에 빙의하지 않는다. HMH의 기존 고정 카메라는 삭제하지 않고 자동 활성화만 해제했다.

- 방향키 또는 화면 가장자리 12px: 화면 기준 상하좌우 이동.
- 마우스 가운데 버튼을 누르고 드래그: 지면을 끄는 방향으로 이동.
- 마우스 휠 위·아래: 확대·축소.
- 대상 선택 후 Home: 선택 대상들의 중심으로 카메라 초점 이동.
- 공통 선택·명령은 rts-controls.md를 따른다.

마우스가 게임 뷰포트 안에 있고, 뷰포트가 입력 포커스를 가지며, 전면 창인 경우에만 입력을 처리한다. 실제 마우스 적중 위젯이 현재 게임 뷰포트 객체가 아니면 카메라와 월드 클릭 입력을 차단한다. 위젯의 타입 이름은 비교하지 않으므로 새 PIE 창의 SPIEViewport도 허용한다. 드래그 중 창이나 UI로 벗어나면 이전 마우스 위치를 버려 복귀 시 화면이 튀는 것을 방지한다.

## 조정할 값

카메라 Pawn Blueprint에서 PanSpeed, DragUnitsPerPixel, ZoomStep, MinZoomDistance, MaxZoomDistance, FocusHeight, BoundsMin, BoundsMax를 조정할 수 있다. 기본 이동 속도는 1800cm/s, 줌 거리는 700~4500cm, 기본 거리는 3600cm다. 다중 병력과 HUD를 함께 볼 수 있도록 초기 거리를 넓혔다. 대각선 이동이 더 빠르지 않도록 방향을 정규화한다.

CameraBoom 컴포넌트의 기본 기울기는 -55도, Camera 컴포넌트 FOV는 55도다. 원근 카메라를 사용하며 자유 회전과 자동 대상 추적은 없다. 장애물 충돌로 카메라가 갑자기 확대되지 않도록 SpringArm 충돌 검사는 끈다.

BoundsMin/Max는 카메라가 바라보는 초점의 XY 경계다. 현재 기본 경계는 HMH 시험용이다. 화면 네 모서리까지 맵 밖 배경이 보이지 않도록 하는 제한은 아니며, 실제 전장 크기와 최대 줌에 맞춰 범위를 조정해야 한다.

다른 맵에서 기존 CameraActor에 Auto Activate for Player가 설정되어 있으면 해제한다. 전용 GameMode Blueprint가 DefaultPawnClass를 재정의한 경우 카메라 Pawn으로 지정해야 한다.

## 검증 결과와 남은 확인

- Development Editor 빌드 성공.
- 공통 전투 코드 교체 후 `Landship` 자동 검사 31개 통과. 카메라 검사 2개도 포함된다. 최신 결과는 `Saved/Automation/CommonCombatReplacement/index.json`을 따른다.
- BoundsAndMovement: 기본 GameMode 연결, 화면 기준 이동, 대각선 속도, 초점 경계·높이, 최소·최대 줌, 잘못된 입력 거부, 드래그 방향 검사.
- EdgeInput: 중앙·양 모서리·창 밖·최소화·가장자리 이동 비활성 설정 검사.
- HMH 실제 실행에서 새 카메라 시점과 마우스 휠 줌 확인.
- 새 PIE 창 가장자리 이동 오류 수정 후 별도 Preview 창에서 좌우 가장자리 스크롤을 직접 확인했다. 기존 타입 이름 비교가 SPIEViewport를 차단하던 문제를 현재 게임 뷰포트 객체 비교로 교체했다. 수정 후 카메라 자동 검사 2개도 통과했다.
- 실제 입력으로 방향키·가운데 버튼 드래그·Home 복귀·UI 차단을 모두 검증한 것은 아니다. 최초 자동 UI 조작 중 사용자 입력이 감지되어 추가 입력을 중단했고, 이후 새 PIE 창 버그 수정 과정에서는 좌우 가장자리 스크롤을 추가 검증했다. 특히 HUD를 붙인 뒤 위젯 위 차단 동작을 다시 확인해야 한다.

보고서: `Saved/Automation/RTSCamera/index.json`, 실행 로그: `Saved/Logs/RTSCameraInteractive.log`.

후속 직접 조종 구현과 검증 내용은 rts-controls.md를 따른다.

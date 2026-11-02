# HUD 계약

## 제공하는 것

- `/Game/Landship/HUD/UI/WBP_BattleHUD`는 선택 정보, 명령 격자, 적 발견 알림의 디자인을 소유한다. `ALSRTSHUD`가 전투 시작 시 이 WBP를 화면에 추가한다. 부모 클래스 `ULSBattleHUDWidget`은 데이터를 표시하고 요청을 전달할 뿐이며 비용·인구수·쿨타임을 바꾸지 않는다.
- `ULSBattleHUDWidget::RefreshState()`는 매 틱 선택 설명, 명령 격자 15칸(제목·단축키·레벨·상태 문구·진행 막대·활성 여부·툴팁), 명령 카드 제목, `인구 현재 / 상한`, 안내 문구를 갱신한다. 선택이 없으면 '대상을 선택하세요'를 보인다. 선택 상세는 첫 대상 기준이고 여러 대상이면 '첫 대상'이라고 표시한다.
- `ULSBattleHUDWidget::ExecuteCommandCell(Index)`: 셀 번호를 넘기면 표시 중인 칸과 같은 칸인지 `ULSCommandCardComponent::ExecuteCell()`이 다시 검사한 뒤 실행하고 성공 여부를 반환한다. 버튼 클릭(`ULSCommandButton`)과 `ALSRTSHUD`의 클릭 처리가 부른다. 단축키도 같은 `ExecuteCell()` 경로를 쓴다.
- 레벨이 없는 칸은 레벨 줄을 비우고, 쿨타임이 있는 칸은 남은 시간과 진행 막대를 보인다. 비어 있는 칸도 자리를 유지한다.
- 전투 결과가 확정되면 안내 문구를 '전투 종료'로 바꾸고 발견 알림을 숨긴다.
- 적 발견 알림은 `Vision`의 대기 알림이 있을 때 `DiscoveryPanel`을 보이고, 화면에 실제로 그려진 뒤에만 `MarkDiscoveryCaptionDisplayed()`로 표시 완료를 기록한다. 표시 시간은 `DiscoveryDuration`으로 조정한다.
- HUD 패널(정보·명령 카드) 위의 마우스 클릭은 전장 선택이나 명령으로 전달되지 않는다.
- `ULSBattleHUDWidget::SetPanelLayout()`은 `InfoPanel`과 `CommandPanel`의 위치·크기를 화면 배치에 맞춘다. 배치 계산은 `Vision`의 `FLSBattlefieldHUDLayout`을 따른다.

## 동작 조건

- WBP는 `ULSBattleHUDWidget`을 부모로 하고 아래 이름의 위젯을 가져야 한다. 이름으로 찾으며 빠지면 해당 표시만 생략된다. `Command0`~`Command14`는 `ULSCommandButton`이어야 클릭이 연결된다.
  - 패널: `InfoPanel`, `CommandPanel`, `DiscoveryPanel`
  - 텍스트: `SelectionText`, `FailureText`, `CommandGroupText`, `PopulationText`, `CommandHint`
  - 셀 i(0~14): `Command{i}`, `CommandKey{i}`, `CommandTitle{i}`, `CommandLevel{i}`, `CommandState{i}`, `CommandProgress{i}`(`UProgressBar`)
- 명령 카드는 5열 3행이고 셀 번호는 왼쪽 위부터 0~14다. WBP의 셀 위치는 `CommandPanel` 420×336 기준으로 왼쪽 12 + 열 × 80, 위쪽 36 + 행 × 82, 크기 76×78이어야 한다. `ALSRTSHUD::GetCommandCellRect()`가 같은 값으로 클릭 영역을 계산하기 때문이다. 정보 패널 기준 크기는 340×125다. 이 값과 다르게 디자인하면 보이는 칸과 클릭되는 칸이 어긋난다.
- 최종 아트와 배치는 WBP 디자이너에서 조정하되 위 이름과 위젯 종류를 유지한다.
- 글꼴은 직렬화되는 폰트 에셋을 참조해야 저장 후에도 유지된다.

## 하지 않는 일

- 명령 격자의 구성(어떤 칸에 어떤 명령이 오는지), 활성 병종 전환, 단축키 충돌 검사, 예약 입력은 `Controls`가 맡는다. 이 폴더는 만들어진 칸을 표시한다.
- 성 무장·생산 칸의 내용은 `Castle`의 명령 제공자가 만든다.
- 인구수와 승패 상태는 `Match`가 보관한다. 이 폴더는 읽어서 표시한다.
- 미니맵과 결과 화면은 `Vision`과 `Match`가 맡는다.
- Shipping 모바일 터치 입력과 최종 아이콘 아트는 제공하지 않는다.

## 공용 테스트 맵

해당 없음.

# HUD 계약

## 제공하는 것

- 자동 전투 준비의 삭제 버튼은 선택 제거 대신 삭제 브러시를 전환하고 활성 상태를 색·문구로 표시한다. 병종 버튼은 생성 브러시로 돌아간다.

### 자동 전투 전용 UI

- 상단 ForceComparisonPanel은 양측 배치 인구수와 병종 비용을 합친 명목 코스트를 나란히 표시한다. AllyForcePopulation·AllyForceCost·EnemyForcePopulation·EnemyForceCost가 값이다. 성은 제외하며 전투 중에도 배치 기준을 유지한다. 적 배치는 예산을 차감하지 않지만 같은 병종 비용으로 비교한다. 편성 변경·초기화·프리셋 로드 시 갱신하며 패널 위 클릭은 전장으로 전달하지 않는다.

- 오른쪽 EditorPanel은 EditAllies·EditEnemies·ApplyLimits·SaveNewPreset·OverwritePreset·LoadSelectedPreset·ReloadPresetList 버튼과 BudgetInput·PopulationInput·PresetName·PresetList 입력을 제공한다. 전투 중 편집을 막으며 현재 편집 진영과 파일 오류·결과를 표시한다. 파일 목록은 최초 표시와 명시적 새로고침·저장 때만 읽는다.
- `UpgradeAutoBattleWidget`은 기존 개인 WBP의 원래 패널을 유지하고 오른쪽 시나리오 편집 패널이 없을 때 추가한다. 기존 편집 패널에서는 버튼 줄바꿈과 입력 글꼴·대비를 보정한다. 별도 개인 절차의 upgrade-runtime-editor.py로 실행한다.


- 개인 `/Game/Developers/HMH/TABS/WBP_AutoBattle`은 `ULSAutoBattleWidget`을 부모로 하며 기존 RTS WBP와 분리된다. 위젯은 준비 요청만 전달하고 예산·인구수·단계를 직접 변경하지 않는다.
- 버튼 이름은 Spear·Archer·Cavalry·Castle·Cannon·Shotgun·Ballista·Start·Retry·Reset·Remove다. 텍스트 Phase·Economy·Health·Weapon·Message와 각 버튼 이름 뒤 Label을 사용한다. Panel의 실제 hover로 월드 클릭을 차단한다.
- 준비 중 편성과 무장을 바꿀 수 있고, 전투 중 변경 버튼은 비활성이다. 결과에서 Retry를 켠다. GameMode 상태 변경과 제한된 표시 주기로 내구도·경제·단계·실패 이유를 갱신한다.
- `ULSAutoBattleEditorLibrary::CreateAutoBattleWidget`은 최초 개인 WBP만 만들고 기존 에셋을 덮어쓰지 않는다. 개인 절차 `docs/procedures/Developers/HMH/tabs-auto-battle.md`에서 실행한다.

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

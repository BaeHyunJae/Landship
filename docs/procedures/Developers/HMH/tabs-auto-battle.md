# TABS 자동 전투 맵 준비와 검사

## 언제 쓰는가

개인 맵에서 병력 배치·자동 전투·자유 관전·편성 복원을 확인할 때 쓴다. 기존 RTS 맵과 공용 전투 에셋은 변경하지 않는다.

## 준비

- UE 5.8의 `LandshipEditor Win64 Development`를 빌드한다. C++ 파일을 추가하거나 제거했으면 같은 빌드를 두 번 성공시킨다.
- 원본 개인 맵 `/Game/Developers/HMH/HMH_TroopTypes`와 그 맵의 NavMesh 볼륨이 필요하다.
- 스크립트는 `Scripts/Developers/HMH/tabs-auto-battle/setup-tabs-map.py`다. 실행 전 에디터를 저장 후 닫는다.
- `<엔진 경로>`, `<프로젝트 경로>`, Git이 추적하지 않는 `<로그 파일 경로>`를 준비한다.

## 순서

1. 개인 TABS 맵을 준비한다. 맵이 이미 있으면 그 맵의 전투 액터와 바닥을 다시 만든다. WBP가 이미 있으면 디자인을 보존한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' -run=pythonscript '-script=<프로젝트 경로>/Scripts/Developers/HMH/tabs-auto-battle/setup-tabs-map.py' -unattended -nop4 -nosound -NullRHI
   ```

2. `/Game/Developers/HMH/TABS/L_TABS`를 열고 PIE 새 창에서 실행한다. 병종 버튼을 고르고 청록색 배치 구역에서 좌클릭 드래그하면 일정 간격으로 연속 생성한다. Shift+클릭으로 병력을 선택한 뒤 지면을 클릭하면 개별 이동한다. Delete 또는 삭제 브러시 버튼을 눌러 삭제 모드로 바꾸고 좌클릭 드래그하면 닿은 병력이 삭제된다. 병종 버튼을 다시 누르면 새 병력 배치로 돌아간다.

   준비 화면 오른쪽 상단의 전장 목록에서 평지, 다리 1개, 다리 3개, 높낮이 전장을 선택한다. `L_TABS` 안에서 지형이 교체되고 양측 병력 편성이 비워진다. 성이 다시 배치된 뒤 새 지형에서 병력을 놓는다. 다리 3개에서 1개로 바꿨을 때 사라진 다리 경로를 사용하지 않는지, 높낮이 전장에서 경사로와 낮은 우회로를 통과하는지 확인한다.
3. 성 위치와 초기 무장을 선택하고 전투를 시작한다. 전투 중 아군 성은 W/S로 전진·후진하고 A/D로 차체를 회전한다. 카메라는 성 뒤를 따라간다. 1/2/3 또는 무장 버튼으로 대포·산탄포·발리스타를 선택하고 지면을 좌클릭해 발사한다. 우클릭이나 Esc는 조준을 취소한다. 병력은 자동으로 전투한다. 준비와 결과에서는 기존 자유 카메라를 사용한다.
4. 전투 중 또는 결과 화면에서 편성 복원을 눌러 이전 편성으로 준비 화면에 돌아오고 성 내구도가 초기화되는지 확인한다.
5. 실제 맵 회귀 검사를 실행한다. 화면 캡처와 WBP를 포함하므로 렌더링을 켠 실행 파일을 사용한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor.exe' '<프로젝트 경로>/Landship.uproject' /Game/Developers/HMH/TABS/L_TABS -game -windowed -ResX=1600 -ResY=900 -unattended -nop4 -nosound -TABSAutoBattleTestExit '-abslog=<로그 파일 경로>'
   ```

6. 성의 공격을 제외한 동수 일대일 교전과 돌격·대비 연결을 검사하려면 다음 명령을 쓴다. 기존 맵 데이터는 저장하지 않고 실행 중 편성만 바꾼다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' /Game/Developers/HMH/TABS/L_TABS -game -unattended -nop4 -nosound -NullRHI -TABSMatchupTestExit '-abslog=<로그 파일 경로>'
   ```

   `Landship.Troops.LanceCharge` 자동화 검사는 순간 이동 우회·방어 소비·공격 재시작 우회를 함께 검사한다. 일대일 검사는 동일 비용의 편성 균형을 보장하지 않으므로 예산과 배치를 바꾸어 별도로 플레이한다.

### 인게임 시나리오 편집

에디터를 닫고 다음 명령으로 기존 개인 WBP에 오른쪽 편집 패널을 추가한다. 기존 왼쪽 패널과 맵 편성을 덮어쓰지 않는다.

```powershell
& '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' -run=pythonscript '-script=<프로젝트 경로>/Scripts/Developers/HMH/tabs-auto-battle/upgrade-runtime-editor.py' -unattended -nop4 -nosound -NullRHI
```

1. PIE 준비 화면의 오른쪽에서 아군 배치 또는 적 배치를 고른다. 왼쪽 병종 선택 뒤 좌클릭 드래그로 연속 배치한다. Delete로 삭제 모드를 켜고 좌클릭 드래그로 지운다. 병종 버튼은 생성 모드, Esc는 삭제 모드 취소다. Shift+클릭으로 병력을 고르면 지면 클릭으로 개별 이동할 수 있다. 편성 초기화는 현재 선택 진영만 비운다.
2. 왼쪽 성 위치와 무장 선택은 현재 선택 진영의 성에 적용한다. 적은 전장 전체의 유효한 지면에 배치한다. 아군은 코스트와 인구 상한 없이 배치한다. 아군 배치 구역은 그대로다.
4. 프리셋 이름을 입력하고 다른 이름으로 저장한다. 같은 이름은 거부하며 교체하려면 입력 이름 덮어쓰기를 명시적으로 누른다.
5. 프리셋 목록에서 골라 불러온다. 목록 새로고침으로 외부에서 복사한 파일도 표시한다. 형식·버전·수치·배치가 잘못되면 오류를 표시하고 기존 편성을 유지한다.
6. 공유할 JSON 파일은 프로젝트의 `Saved/AutoBattlePresets` 폴더에 있다. 다른 사용자는 같은 폴더에 복사하고 새로고침한다. 액터와 전투 진행 상태는 저장하지 않으며 불러오기는 준비 단계에서만 가능하다.
7. 전투를 시작한다. 전투 중 편집·저장·불러오기는 비활성이다. 결과에서 편성을 복원하면 수정한 양측 편성과 경제 설정으로 준비한다.

`Landship.AutoBattle.Preset.SerializationAndFiles`는 형식 검사와 파일 저장을 검증한다. 실제 WBP 버튼·배치·재로드 검사는 전용 맵을 `-TABSScenarioEditorTestExit -ScenarioPresetName=<검사용 고유 이름>`으로 실행한다. 같은 이름과 `-TABSScenarioReadOnly`를 더해 새 프로세스에서 저장 파일 복원과 전투 시작을 확인한다. 검사용 이름의 JSON은 확인 후 지우며 사용자 프리셋과 구분한다.

### 전장과 배치 구역 확장

기존 편성·경제·성 시작 위치를 유지하면서 개인 전장, 배치 경계, 지면과 NavMesh 범위를 넓히려면 에디터를 저장 후 닫고 다음 명령을 실행한다. 확장 후 카메라 이동 경계는 시나리오 범위를 자동으로 따른다. 기존 프리셋의 좌표는 그대로 사용할 수 있다.

```powershell
& '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' -run=pythonscript '-script=<프로젝트 경로>/Scripts/Developers/HMH/tabs-auto-battle/expand-battlefield.py' -unattended -nop4 -nosound -NullRHI
```

로그에 `TABS_MAP_EXPANDED_OK`가 있어야 한다. 플레이 때 확장된 볼륨으로 NavMesh를 다시 만든다. 전용 맵을 `-TABSExpandedMapTestExit`로 실행하면 확장 구역의 배치, 반대 모서리 두 경로의 연결, 실제 병력 이동을 검사한다. 로그의 `TABS_PROBE_FINISHED failures=0`으로 확인한다. 최초 맵 준비 스크립트로 맵을 다시 생성한 경우 이 확장 명령을 다시 실행한다.

### 다리와 높낮이 지형 편집

콘텐츠 브라우저의 `Developers/HMH/TABS`에서 `L_TABS_Bridge`, `L_TABS_ThreeBridges`, `L_TABS_Heights` 중 편집할 레벨을 연다. 아웃라이너에서 지형 메쉬를 선택해 이동·회전·스케일을 조정하거나 장애물을 배치하고 레벨을 저장한다. 지형 전용 레벨은 조명이 없으므로 필요하면 뷰포트의 조명 모드를 Unlit으로 전환한다.

테스트할 때는 `L_TABS`를 다시 열어 플레이하고 준비 화면에서 편집한 지형을 고른다. 보행 바닥 메쉬에는 `LSGround` 오브젝트 타입과 NavMesh에 반영되는 충돌을 사용한다. 지형 레벨은 공용 조명과 NavMesh 볼륨을 `L_TABS`에서 사용한다.

## 성공 확인

- 준비 스크립트 로그에 `TABS_MAP_SETUP_OK`가 있고 Python 예외가 없다.
- 회귀 로그에 `TABS_PROBE_FINISHED failures=0`이 있고 `TABS_PROBE FAIL`이 없다. 종료 코드만으로 성공을 판정하지 않는다.
- 준비 중에는 병력과 성이 전투하지 않으며, 전투 시작 뒤에는 양측 병력과 선택한 성 무장이 자동으로 싸운다.
- 재도전은 액터를 새로 생성하며 이전 전투의 포탄과 사망 상태를 재사용하지 않는다.

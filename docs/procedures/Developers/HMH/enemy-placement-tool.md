# 에디터에서 적 편성 배치하기

## 언제 쓰는가

개인 자동 전투 맵의 적 편성을 화면에서 배치하고 맵에 저장할 때 사용한다.

## 준비

- 새 네이티브 클래스를 빌드한 후 Unreal Editor에서 개인 맵 `/Game/Developers/HMH/TABS/L_TABS`를 연다.
- PIE를 종료한다. 이 도구는 에디터 월드에서만 작동한다.
- 자동 검증 스크립트는 `Scripts/Developers/HMH/enemy-placement-tool/verify-enemy-placement-tool.py`다.

## 순서

1. World Outliner에서 `TABS_Scenario`를 선택하고 Details의 `Enemy Placement Tool`을 펼친다.
2. `1. Load Saved Formation`을 누른다. 저장된 적들이 빨간 도형 마커로 표시된다. 기존 마커가 있으면 중복 로드를 거부한다.
3. 새 적을 추가하려면 시나리오의 `Placement Troop Type`을 고르고 `2. Add Enemy`를 누른다. `Placement Ground` 주변의 빈 지면에 추가하고 자동 선택한다.
4. 마커를 선택해 W 이동 도구로 옮긴다. Alt+드래그로 복제하고 Delete로 삭제한다. Details의 `Troop Type`으로 병종을 바꾼다. 회전과 스케일은 전투 수치에 반영하지 않으며 스케일 변경은 적용을 거부한다. 마커의 피벗은 지면 위치다.
5. 시나리오를 다시 선택하고 `3. Apply Formation`을 누른다. `Placement Tool Status`에서 성공 또는 실패 원인을 확인한다. 지면·전장 경계·성/장애물 겹침·병력 간 겹침을 검사하고 성공한 경우에만 편성 전체를 반영한다. 마커의 높이는 지면으로 보정된다. NavMesh의 실제 연결 상태는 PIE 초기화에서도 검사한다.
6. Ctrl+S로 맵을 저장하고 PIE를 실행한다. 마커는 전투에 참여하지 않고 저장된 편성에서 실제 적 병력을 생성한다.
7. 시나리오의 Budget·Population Limit·Unit Costs로 아군 편성 조건을 조정한다.
8. 마커를 정리하려면 `4. Hide Markers`를 누른다. 저장된 편성은 유지하고, 적용 전 마커 편집은 버린다. 추가·적용·마커 정리는 Ctrl+Z로 되돌릴 수 있다. 원래 편성을 다시 불러오려면 Hide 다음 Load를 사용한다.

### 자동 검증

에디터를 닫은 후 아래 쓰기·읽기 명령을 순서대로 실행한다. 개인 맵의 검증용 복사본을 생성하며 원본 맵은 저장하지 않는다. 읽기 검증이 끝나면 복사본을 제거한다. 쓰기 단계 후 중단했다면 읽기 명령으로 검증과 정리를 마친다.

```powershell
& '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' -run=pythonscript '-script=<프로젝트 경로>/Scripts/Developers/HMH/enemy-placement-tool/verify-enemy-placement-tool.py' -unattended -nop4 -nosound -NullRHI
& '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' -run=pythonscript '-script=<프로젝트 경로>/Scripts/Developers/HMH/enemy-placement-tool/verify-enemy-placement-tool.py' -EnemyPlacementVerifyRead -unattended -nop4 -nosound -NullRHI
```

자동화 테스트 `Landship.AutoBattle.EnemyPlacementTool`은 잘못된 배치의 원자적 거부와 Undo·Redo를 검사한다.

## 성공 확인

- 적용 성공 문구가 나오고 다시 실행한 PIE에 변경한 병종과 적 수가 반영된다.
- 겹침 또는 범위 밖 배치는 실패 이유를 표시하고 기존 편성을 바꾸지 않는다.
- 자동 검증 로그에 각각 `ENEMY_PLACEMENT_WRITE_OK`, `ENEMY_PLACEMENT_RELOAD_OK`가 있고 Python 예외가 없다.
- 자동화 테스트가 성공하고 Undo·Redo 후 편성과 마커가 복원된다.

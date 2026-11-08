# 포탄 조합 실험 전장 저작과 실행

## 언제 쓰는가

포탄의 효과 순서와 물리 연쇄를 비교하는 독립 실험 전장을 플레이하거나 저작할 때 쓴다.

## 준비

### Arena 플레이 수락 기준

기술 검사는 재미 판정을 대신하지 않는다. 전달 전에 다음 실행 사실을 확인한다.

- 전격↔추진, 냉각↔추진, 중력↔폭발에서 물체 위치·생존자·잔여 전하·다음 발의 기회가 달라진다. 색이나 피해량만 다른 것으로 통과시키지 않는다.
- 실제 몸체 이동→접촉→방전·기폭이 적 무리나 기지에 영향을 준다.
- 같은 후보에서 코어 집중·최소 대응·기물 활용·초반 폭약통 정리 뒤 반복 사격을 비교한다. 위협을 계속 무시해도 여유롭게 이기면 전장의 선택 기준에 실패한 것이다.

사용자는 약 5분 동안 2~3판을 자유롭게 플레이하고 다음 다섯 질문에 한 번에 답한다. 역할이나 인과를 읽을 수 있는지는 실행 로그로 대신 판정하지 않는다.

- 별도 해설 없이 전격의 저장·접촉 방전, 냉각의 제어, 중력의 집결을 구분할 수 있었는가?
- 순서를 바꾼 차이가 실제 공략의 차이로 느껴졌는가?
- 연쇄의 원인을 따라갈 수 있었는가?
- 묵직하고 크게 터지는 한 방의 쾌감이 있었는가?
- 다른 조합이나 공략으로 한 판 더 해보고 싶은가?

이 다섯 사용자 수락 조건을 충족해야 재미 검증을 통과한 것으로 기록한다. 후보 전달→피드백 한 묶음→원인에 맞춘 수정 한 차례→재평가를 한 주기로 삼는다. 재평가에서도 핵심 재미가 부족하면 성공 처리하거나 기능을 더 붙이지 않고 실패한 가설과 유지할 부분을 정리해 방향을 다시 결정한다.

### 조합과 연쇄를 위한 별도 전장

`/Game/Landship/ShellExperiment/Maps/L_ShellChainArena`는 전격을 저장한 물체의 추진·접촉 방전, 범위 냉각, 중력 집결과 후행 효과를 비교하는 별도 전장이다. 기존 두 실험 맵의 에셋과 튜닝은 유지한다. 저장된 맵은 저작 스크립트를 재실행하지 않고 열어 플레이한다.

처음 저작할 때는 네이티브 코드를 빌드하고 PIE를 끝낸다. 대상 폴더의 실제 에셋 사본과 SHA-256 목록을 먼저 보존한다. 에디터 Python 콘솔에서 아래 명령을 실행한다. `tracer`는 작은 최초 경로를 만들며, 실제 입력·충전·접촉·기폭을 관찰한 뒤 `full`로 바꾸어 보급과 증원을 추가한다. 기존 단계의 이름·타입·배치가 예상과 다르면 스크립트는 중단한다.

```python
import pathlib, sys, unreal
script = pathlib.Path(unreal.Paths.project_dir()) / 'Scripts/shell-experiment/setup-shell-chain-arena.py'
previous_args = list(sys.argv)
try:
    sys.argv = [str(script), '--phase', 'tracer']
    exec(compile(script.read_text(encoding='utf-8'), str(script), 'exec'))
finally:
    sys.argv = previous_args
```

이 변경의 자동화 검사는 냉각·집결·접촉 반응과 직접 영향을 받는 기존 동작만 개별 이름으로 선택한다. 전체 Landship 또는 전체 실험 테스트를 실행하지 않는다. 실제 조합의 차이와 재미는 자동화 통과와 별도로 플레이에서 판단한다.

중앙 Heavy가 코어 반복 사격에 함께 제거되던 초기 Arena 배치를 보정하려면 `--phase full --revise-heavy-lane`을 명시한다. 이 경로는 기존 Heavy의 정확한 초기 위치와 전개 지점을 확인한 뒤 남쪽 공성 경로로 옮긴다. 보정 태그가 있는 맵은 새 위치를 확인하고 보존하며, 사용자가 바꾼 배치는 덮어쓰지 않는다. 새로 만든 full 전장은 남쪽 경로를 바로 사용한다.

Arena의 기존 초기 저작본에 추가 보정을 반영할 때는 `--phase full --revise-siege-pressure`로 전용 Heavy 공격력을 45에서 90으로 갱신하고, `--phase full --revise-rim-height`로 높이가 240cm였던 여섯 경계 턱을 120cm로 바로잡는다. 각 옵션은 해당 원본 상태나 이미 보정된 상태만 허용하며 다른 수동 변경은 덮어쓰지 않는다. 턱 보정 뒤에는 내비게이션 생성이 끝난 다음 맵을 저장한다.

### 별도 개선판의 작은 물리 전장

`/Game/Landship/ShellExperiment/Maps/L_ShellSupplyExperiment`는 현재 폭발·추진과 철구·폭약통을 다루는 첫 물리 단계다. 기존 3종 전장과 구분한다. 저장된 맵은 다시 저작하지 않고 열어 PIE로 실행한다. F로 두 슬롯과 공용 모듈 선택지를 열고, R로 교환한 뒤 F로 확정한다. Space 안내가 있을 때만 후행 효과를 조기에 발동할 수 있다.

처음 저작할 때는 네이티브 클래스를 빌드하고 PIE를 끝낸다. 실행 전 `Content/Landship/ShellExperiment/` 파일의 실제 바이트 사본과 경로·SHA-256 목록을 프로젝트 밖에 보관하고, 대상 맵과 전용 에셋이 이미 있었는지도 기록한다. 그런 다음 아래 스크립트를 실행한다.

```python
import pathlib, unreal
script = pathlib.Path(unreal.Paths.project_dir()) / 'Scripts/shell-experiment/setup-shell-supply-experiment.py'
exec(compile(script.read_text(encoding='utf-8'), str(script), 'exec'))
```

새 맵은 기본 `--stage physics`로만 만들 수 있다. 기존 개선 맵은 아래 순서로 한 단계씩 추가한다: `physics` → `charge` → `transport` → `materials` → `complete`. 각 실행 전에 해당 단계의 네이티브 클래스를 빌드하고 PIE를 끝낸다. 스크립트는 단계별 액터·모듈·참조를 더하며 기존 단계의 배치와 튜닝은 보존한다. 이름·타입·단계 표시·경로·소유 필드가 예상과 다르면 쓰기 전에 중단한다. 같은 단계로 실행하면 기존 맵을 검사하고 보존한다. 코드가 Complete를 지원하는 것만으로 맵이 그 단계의 배치까지 저작했다고 간주하지 않는다.

다음 단계 하나를 추가할 때는 기존 Python 콘솔 실행 예제에서 `exec` 전에 다음 인수를 붙이고, 그 단계에 해당하는 네이티브 클래스를 빌드한 뒤 실행한다.

```python
import pathlib, sys, unreal
script = pathlib.Path(unreal.Paths.project_dir()) / 'Scripts/shell-experiment/setup-shell-supply-experiment.py'
previous_args = list(sys.argv)
try:
    sys.argv.extend(['--stage', 'charge'])
    exec(compile(script.read_text(encoding='utf-8'), str(script), 'exec'))
finally:
    sys.argv = previous_args
```

예제의 `charge`를 현재 단계 다음 이름으로 바꾼다. 새 맵 생성 때는 `--stage physics` 기본 동작을 사용한다.

처음 `complete` 단계 저작을 마친 뒤에는 `--stage complete --revise-complete-layout`으로 배치 보정을 반드시 한 번 실행한다. 이 경로는 Complete 맵의 액터와 타입, 수정 전 또는 이미 수정된 배치 상태, 보급 경로와 랙 설정을 먼저 확인하고, 초기 물자와 겹치는 하역 후보 및 빈차 출구의 지지면 연결을 보정한다. 기대한 상태와 다르면 수동 변경을 덮지 않고 중단한다. 다시 실행하면 완료된 보정은 유지하며, 옵션을 생략하면 기존 Complete 배치를 보존한다.

Complete 배치 수정을 실행할 때는 Python 콘솔 예제의 인수 목록을 다음처럼 설정한다.

```python
import pathlib, sys, unreal
script = pathlib.Path(unreal.Paths.project_dir()) / 'Scripts/shell-experiment/setup-shell-supply-experiment.py'
previous_args = list(sys.argv)
try:
    sys.argv.extend(['--stage', 'complete', '--revise-complete-layout'])
    exec(compile(script.read_text(encoding='utf-8'), str(script), 'exec'))
finally:
    sys.argv = previous_args
```

첫 저작이 도중에 실패해 재실행도 중단되면 자동 초기화나 재개 인수를 사용하지 않는다. 먼저 PIE와 저작 실행을 끝내고 첫 오류, 실행 전 대상 파일의 존재 여부, 이번 실행이 만든 파일 목록을 확인한다. 기존 원본 전장과 에셋의 실행 전 백업을 유지하고, 실패한 맵과 에셋의 현재 파일 바이트도 프로젝트 밖의 별도 폴더에 복사해 보존한다. 저장하지 않은 상태가 있다면 원본 경로에 덮어쓰지 말고 별도 사본으로 보존한다. 실행 전 기록과 대조해 이번 실행이 새로 만든 불완전한 결과라고 확인한 파일만 작업자가 에디터를 닫은 상태에서 프로젝트 밖으로 격리한다. 기존 파일이나 다른 맵이 참조하는 파일은 격리하지 않는다. 새로 만든 결과인지 확인할 수 없으면 그 상태에서 멈추고 파일의 소유자와 복구 범위를 정한다. 오류 원인을 고친 뒤 기본 스크립트를 다시 실행하며, 남아 있는 유효한 전용 에셋은 검사 후 재사용한다. 이 절차는 수동 배치를 초기화하거나 원본을 복원하는 자동 기능이 아니다.

`SUPPLY_PHYSICS_AUTHORED`는 저작 완료 표시다. 내비게이션 빌드가 끝난 뒤 저장하고, `SUPPLY_PHYSICS_PRESERVED`는 기존 대상이 보존됐다는 표시다. 두 표시 모두 실제 사격·접촉·화면·재시작 검증을 대신하지 않는다. 기존 맵용 `--populate-p1`이나 `--resume-authoring`을 개선 맵에 사용하지 않는다.

철구에 추진을 적용했을 때 실제 폭약통과 부딪히는지, 후행 폭발이 물체의 이동·회전을 따르는지 관찰한다. 범위 안이어도 다른 몸체에 가려지면 폭발 피해를 받지 않을 수 있고, 그 접점에 짧은 방패 표시가 나온다. 이 단계의 한 발 관찰과 사용자 자유 플레이를 마친 뒤 다음 조합·수송차 전장으로 확장한다.

### 저장된 물리 전장 튜닝

`L_ShellSupplyExperiment`의 저장된 맵은 플레이테스트용 튜닝을 보유할 수 있으며, 네이티브 새 액터 기본값과 달라도 된다. 기존 전장 저작 경로는 맵의 액터 Transform과 튜닝을 보존하며 값을 초기화하지 않는다. 튜닝할 때는 PIE를 먼저 끝내고 맵에서 `Supply_Ball`의 `IntrinsicMassKg`와 `Restitution`, `Supply_Coordinator`의 `SupplyTuning` 아래 `ThrustMomentum`과 `BlastMomentum`을 조정한다. 질량 단위는 kg이고 운동량 단위는 kg·cm/s다. 맵만 저장한 뒤 PIE를 다시 시작한다. 물리 몸체는 `BeginPlay`에서 설정값으로 시뮬레이션을 시작한다. 조정은 PIE 밖에서 하고 새 실행의 실제 질량과 반응을 확인한다.

`Thrust`는 포탄 입사 방향을 XY 평면에 투영한 수평 방향으로, 포탄이 맞은 한 몸체에 충격을 적용한다. 수평 방향이 없으면 추진은 실패한다. 기록된 충돌은 추진된 몸체와 접촉한 몸체 사이의 사건이다. 후행 `Thrust`는 예약된 최초 착탄점에서 발동해 `ThrustRadius` 안의 몸체를 포탄의 수평 입사 방향으로 함께 민다. 범위는 실제 3D 몸체 표면 사이 거리로 판정하고 기존 폭발 차폐를 적용한다. 충격은 거리 가중치에 따라 총 운동량을 나누어 적용하며, 몸체별 속도 변화 상한과 기존 속도 누적을 따른다. 상한으로 적용하지 못한 운동량은 다른 몸체에 재분배하지 않는다. 후행 효과의 타이머와 `Space` 수동 발동은 유지된다. 첫 대상이 남아 있거나 물리 몸체가 존속하는 기준점이면 기존 단일 대상 적용을 사용하고, 기준점이 사라지면 최초 착탄점으로 돌아간다. 지면 최초 착탄점에서도 후행 효과를 허용하고 대상이 없어도 착탄점 연출을 표시하되, 살아 있는 고정 핵심 대상으로 예약된 후행 효과는 기존처럼 취소한다. `Blast`는 피해·반경·차폐를 유지하며 총충격 예산 안에서 수직 상향 충격을 적용한다. SupplyStageOne에서 Thrust가 첫 슬롯인 포탄은 직격 체력 피해를 주지 않지만, 직격 충격·밀려난 물체의 충돌 피해·후행 Blast 피해는 유지하며 Blast가 첫 슬롯인 포탄과 Legacy의 직격 피해는 바뀌지 않는다. `SupplyTuning`의 `ModuleMaximumDeltaSpeed`는 Thrust와 모듈 Blast 충격 각각에 대상 질량 기준 속도 변화 상한을 적용하며, 포탄 직격 충격과 환경 기폭에는 적용하지 않는다. 환경 기폭은 기존의 방사형 충격과 상향 비율을 유지한다. 두 효과를 비교할 때는 노출된 대상과 실제 엄폐 뒤 대상을 각각 관찰한다. 기존 `Legacy` 전장의 규칙은 바뀌지 않는다. 현재 맵의 선택 모듈은 저작 단계에 따르며, 수치 조정만으로 다음 단계가 열리지는 않는다. 타격감과 조정 결과는 플레이로 판단한다.

- [자동화 테스트 절차의 빌드 준비](run-automation-tests.md)에 따라 새 네이티브 클래스를 빌드한다. 에디터에서 `LSShellExperimentGameMode`, `LSShellExperimentController`, `LSShellExperimentWidget`, `LSShellExperimentEditorLibrary`, `LSShellExperimentCoordinator`, `LSShellExperimentEnemy`, `LSShellWeakPoint`, `LSShellExplosive`를 읽을 수 있어야 한다.
- Python Editor Script Plugin과 Editor Scripting Utilities가 활성화된 에디터에서 프로젝트를 연다.
- 변경한 맵을 저장하고 PIE를 끝낸다. 저작 스크립트가 다른 맵을 연다.
- 스크립트는 `Scripts/shell-experiment/setup-shell-experiment.py`다. DataTable 원본은 `Data/ShellExperiment/DT_ShellEnemyStats.csv`와 `Data/ShellExperiment/DT_ShellCoreStats.csv`다.
- Combat의 Developer Settings에서 오폭 허용과 자기 피해 허용을 모두 켠다. 이 설정은 프로젝트 공통이므로 다른 전장을 확인하기 전에는 원래 값으로 되돌린다. 스크립트와 실험 모드는 설정을 자동으로 바꾸지 않는다.

## 순서

### 저장된 전장 플레이

1. `/Game/Landship/ShellExperiment/Maps/L_ShellExperiment`를 열고 PIE를 시작한다. 저장된 맵을 플레이할 때는 저작 스크립트를 다시 실행하지 않는다.
2. 좌클릭으로 지형이나 실제 몸체를 조준해 발사한다. `F`로 조합을 열어 슬롯별 모듈 버튼을 선택하고 `R`로 순서를 교환한다. 다시 `F`를 누르면 선택이 확정되고 정상 속도로 돌아온다. 창의 포커스를 잃으면 미확정 선택을 취소한다.
3. 후행 효과 대기 안내가 있을 때 `Space`로 즉시 발동한다. 가운데 버튼 드래그와 휠로 카메라를 조절한다.
4. 바퀴 달린 `CARRIER`는 이동·자폭하는 운반차다. `FIXED` 탱크와 `FACILITY` 시설은 고정되어 중력으로 움직이지 않는다. 중장갑 병기 위의 청록색 `WEAK`가 별도 약점이며, 병기를 격파하면 분열이 해금된다.
5. 승리·패배 후 `다시 시작`으로 초기 배치를 다시 불러온다. 구성 오류가 표시되면 안내한 피해 정책을 확인하고 PIE를 다시 시작한다.

초기 전장은 돌격병 8기·운반차 2기·중장갑 병기 1기, 보조 시설 2개·고정 폭발물 4개로 구성된다. 첫 연쇄로 동력로를 약화시키는 공략과 병기 약점부터 격파해 분열을 얻는 공략을 비교한다. 북쪽 진격 경로는 폭발물 줄의 서쪽 끝을 거쳐 성에 접근한다. 3~5분은 튜닝 목표이며, 실제 타격감·원인 전달·재시도 의욕은 플레이로 판단한다.

### 최초 전장 저작

1. 처음 저작할 때만 에디터의 Python 콘솔에서 다음을 실행한다.

   ```python
   import pathlib, unreal
   script = pathlib.Path(unreal.Paths.project_dir()) / 'Scripts/shell-experiment/setup-shell-experiment.py'
   exec(compile(script.read_text(encoding='utf-8'), str(script), 'exec'))
   ```

2. Output Log에서 `SHELL_EXPERIMENT_AUTHORED`를 확인한다. 스크립트는 `LevelEditorSubsystem.new_level_from_template`로 원본 전장에서 새 맵을 만든 뒤 새 맵의 게임플레이 액터와 지형을 교체한다. 조명, 내비게이션 시스템 설정, 실제 NavMeshBoundsVolume 브러시는 새 맵에서 보존한다. 원본 전장이나 공용 성·병력 에셋을 저장하지 않는다. 맵을 일반 에셋의 `EditorAssetLibrary.duplicate_asset`으로 복제하지 않는다. 그 경로는 독립 UWorld 에셋을 만들어 맵 로드의 전제 조건을 깨뜨린다.
3. 내비게이션 빌드가 끝나면 현재 맵을 저장한다. 빌드가 진행되지 않으면 Build 메뉴의 Build Paths를 실행하고 저장한다.
4. `/Game/Landship/ShellExperiment/Maps/L_ShellExperiment`를 연다.
5. World Settings의 GameMode Override가 `BP_ShellGameMode`인지 확인한다. `Shell_BattlefieldSetup`의 PlayerCastle과 CoreOutpost가 각각 `Shell_PlayerCastle`, `Shell_Core`를 가리키는지 확인한다.
6. PIE를 시작한다. 좌클릭으로 발사하고 `F`로 조합을 열어 바꾼다. 화면의 순서 바꾸기 버튼으로 중력→폭발과 폭발→중력을 비교하고, 다시 `F`를 눌러 확정한다. 다시 시작 버튼으로 같은 배치를 다시 플레이한다.
7. 첫 한 발로 적의 반응과 폭발물 연쇄가 보이는지 직접 확인한 뒤 다른 조합을 비교한다. 첫 플레이에서 파괴의 느낌과 원인 구분이 충분한지 사용자에게 확인받은 뒤 병력 수나 사건을 늘린다.
8. [자동화 테스트 절차](run-automation-tests.md)에 따라 P0 플레이 가능 단계의 회귀 검사를 실행한다. 이후 관련 코드·에셋을 바꾸면 그 절차의 재실행 조건을 따른다.

### 기존 저작 결과 보존

기본 실행에서 대상 맵이 이미 있으면 스크립트는 `SHELL_EXPERIMENT_PRESERVED`를 출력하고 에셋·액터를 바꾸지 않는다. DataTable 원본 CSV를 수정해도 기존 맵을 보존하는 실행에서는 재가져오지 않는다. 기존 전장은 에디터에서 수정하고, CSV를 바꿨다면 대응하는 실험 DataTable에 재가져온다.

첫 실행 도중 오류가 나면 로그의 첫 오류를 확인한다. 이미 만든 대상 맵과 에셋을 에디터에서 확인한 뒤 원인을 수정한다. 부분 저작된 대상 맵을 다시 구성하기로 결정했다면 백업한 뒤 `--resume-authoring` 인수를 명시한다. 이 실행은 실험 대상 맵만 열어 게임플레이 배치와 지형을 다시 만들며, 원본 맵이나 다른 맵을 재구성하지 않는다. 실험 전용 DataTable과 블루프린트의 스크립트 지정 기본값도 다시 적용하므로 수동으로 조정한 값을 보존하는 실행으로 쓰지 않는다.

에디터 Python 콘솔에서 부분 저작을 재개하려면 다음을 실행한다.

```python
import pathlib, sys, unreal
script = pathlib.Path(unreal.Paths.project_dir()) / 'Scripts/shell-experiment/setup-shell-experiment.py'
previous_args = list(sys.argv)
try:
    sys.argv.append('--resume-authoring')
    exec(compile(script.read_text(encoding='utf-8'), str(script), 'exec'))
finally:
    sys.argv = previous_args
```

명령줄에서 Python 스크립트로 실행할 때도 같은 인수를 명시해야 부분 저작을 재개한다. 정상 실행 뒤에는 에디터를 유지하므로 열린 맵과 내비게이션 빌드를 확인하고 저장한다. 스크립트 완료 로그는 에디터 종료나 PIE 검증 완료를 뜻하지 않는다.

### 기존 전장의 피드백만 갱신

맵 배치를 보존하면서 연쇄 반응의 설명과 소리를 갱신할 때는 PIE를 끝내고 맵을 저장한 뒤 `--update-feedback` 인수로 실행한다. 위 콘솔 예제의 `sys.argv.append('--resume-authoring')`를 `sys.argv.append('--update-feedback')`로 바꾸면 된다. 두 인수를 함께 쓰지 않는다.

이 실행은 정확히 실험 대상 맵을 열어 경로를 검사한 뒤 실험 전용 링 머티리얼을 만들고, 발사·폭발·명중 소리 에셋만 다시 생성해 덮어 가져온다. 기존 위젯에는 반응 설명 TextBlock이 없을 때만 추가하고 글자 그림자를 적용한다. 기존 컨트롤의 이름과 배치는 유지한다. 실험 폭발물 중 액터 라벨이 `Shell_Auxiliary_`로 시작하는 대상만 보조 시설 표시로 바꾼다. 액터 Transform, 피격 몸체, 체력, 카메라와 전장 배치는 바꾸지 않는다.

Output Log의 `SHELL_EXPERIMENT_FEEDBACK_UPDATED`와 저장된 맵을 확인한다. 다음 PIE에서 `FIXED`는 고정 폭발물, `FACILITY`는 보조 시설로 읽히는지, 반응 설명이 실제 명중·충돌·추가 기폭의 원인을 구분해 주는지 확인한다. 새 소리의 크기와 폭발 표현이 충분한지는 플레이로 판단한다.

### 작은 전장에 P1 구성을 추가

작은 전장에서 첫 플레이를 확인한 뒤 병력·대형 병기·사건과 슬롯별 모듈 선택을 추가할 때는 PIE를 끝내고 맵을 저장한 뒤 `--populate-p1` 인수로 실행한다. 위 콘솔 예제의 인수를 `sys.argv.append('--populate-p1')`로 바꾸면 된다. 다른 저작 인수와 함께 쓰지 않는다.

스크립트는 실험 대상 맵의 액터 이름·타입·역할을 먼저 검사하고 맵을 저장한다. 지정한 이름의 병력·운반차·대형 병기·폭발물·엄폐물이 없을 때만 추가한다. 이미 배치한 액터와 Transform은 유지한다. 지정하지 않은 실험 병력이나 폭발물이 있거나 이름과 타입·역할이 다르면 배치를 시작하지 않는다. 같은 인수로 다시 실행해도 기존 이름의 액터를 추가 생성하거나 이동하지 않는다.

적은 실험 전용 DataTable의 병종별 행을 사용한다. 대형 병기의 약점은 해당 병기의 초기화가 생성하므로 맵에 따로 배치하지 않는다. 엄폐물은 포탄과 폭발 차폐 채널을 모두 막도록 설정하고 전장 지형 목록에 넣는다. 기존 조정자에 운반차 클래스와 수송대 진입점·접근점을 연결하고 사건을 활성화한다.

`UpgradeP1WidgetAsset`은 기존 WBP의 배치를 보존하며 없는 컨트롤만 추가한다. 슬롯별 중력·폭발·분열 버튼과 분열 해금 상태 텍스트가 추가된다. 모든 선택 버튼은 키보드 포커스를 받지 않는다. 이미 있는 컨트롤의 타입이나 포커스 설정이 잘못되면 위젯을 덮어쓰지 않고 오류를 알린다.

Output Log에서 `SHELL_EXPERIMENT_P1_POPULATED`를 확인하고 내비게이션 빌드가 끝나면 맵을 다시 저장한다. 다음 PIE에서 이동 경로, 대형 병기의 실제 사격 통로, 약점 피격, 분열 해금과 슬롯 선택을 확인한다. 적이 조금 진격한 뒤에도 밀어낸 적을 기존 폭발물에 부딪칠 기회가 있는지 확인한다. 엄폐 뒤의 포탄·폭발 차단과 노출된 동력로 부분의 직접 피격을 각각 확인한다. 사건의 경고와 실행, 수송대 경로는 별도로 확인한다. 저작 완료 로그가 이 플레이 검증을 대신하지 않는다.

현재 배치는 고정 폭발물 네 개를 북쪽 행군 경로에서 보조 시설로 이어 둔다. 초기 좌표는 `(150,1000)`, `(550,1000)`, `(950,1000)`, `(1350,800)`이며, 두 보조 시설은 동력로 옆에 남는다. 첫 돌격병은 `(-120,860)`에서 시작한다. 한 번의 초기 연쇄가 전투 대부분을 생략하지 않으면서, 진격한 적을 폭발물에 부딪칠 기회를 만드는 배치다. 이 좌표는 새 액터 저작의 기본값이며, 기존 맵의 수동 배치를 자동으로 덮어쓰지 않는다.

### 위젯과 소리

스크립트는 `ULSShellExperimentEditorLibrary::BuildWidgetAsset`을 호출해 `ULSShellExperimentWidget`을 부모로 한 `WBP_ShellExperiment`를 만든다. 이 에디터 전용 도우미가 WidgetTree의 CanvasPanel 아래에 `HealthText`, `CoreText`, `LoadoutText`, `StatusText`, `ReactionText`, `HelpText`, `ReloadProgress`, `RestartButton`, `SwapButton`을 배치하고 블루프린트를 컴파일한다. 배치는 에셋에 저장되며 런타임에 새로 만들지 않는다. 기존 위젯에 배치가 있으면 이름·타입·버튼 포커스 설정을 검사하고 그대로 보존한다. 비어 있는 위젯만 채운다. 기존 배치에 선택 항목인 `ReactionText`를 추가할 때는 `--update-feedback`으로 `UpgradeFeedbackWidgetAsset`을 호출한다. 유효하지 않은 기존 배치는 UMG 디자이너에서 수정한 뒤 다시 실행한다.

P1 슬롯 선택 컨트롤 이름은 `Slot1GravityButton`, `Slot1BlastButton`, `Slot1SplitButton`, `Slot2GravityButton`, `Slot2BlastButton`, `Slot2SplitButton`이며, 해금 상태는 `SplitStatusText`다. 기존 위젯에는 `--populate-p1`으로 이 컨트롤을 추가한다.

루트의 Visibility는 Self Hit Test Invisible, 텍스트와 진행 막대는 Hit Test Invisible로 둔다. 두 버튼은 Visible로 두고 Is Focusable을 끈다. 버튼을 클릭한 뒤에도 키보드 조합 입력이 작동하고, 버튼이 아닌 화면 영역이 월드 조준 입력을 받는지 PIE에서 확인한다.

발사·폭발·명중 소리는 스크립트가 감쇠하는 저음·고정 시드 잡음·잔향을 합쳐 WAV로 생성한다. 파형의 최댓값을 제한해 PCM 범위를 넘기지 않는다. 임시 WAV는 `Intermediate/ShellExperimentAudio/`에 생성하고 에셋은 `/Game/Landship/ShellExperiment/Audio/`에 저장한다. 외부 음원을 내려받지 않는다. 기본 저작에서는 에셋이 있으면 재사용하고, `--update-feedback`에서만 기존 소리를 덮어 가져온다. 소리 에셋을 바꿀 때는 해당 실험 에셋만 교체하고 Coordinator의 Sound 참조를 연결한다.

`M_ShellEffect`는 채워진 섬광과 탄도 표시에, `M_ShellRing`은 링과 충격파 표시에 쓴다. 링 머티리얼은 Tint와 Opacity 파라미터를 제공하고 Fresnel을 불투명도에 곱한다. 두 머티리얼은 실험 전용이며, 반응 연출은 실제 피해 판정을 추가하지 않는다.

## 성공 확인

- Outliner에 BattlefieldSetup, BattlefieldSettings, ShellExperimentCoordinator가 각각 하나 있고, 이전 전장의 배치 프리셋·지역 출격 조정자·병력·시설·장애물이 남아 있지 않다.
- 작은 전장에는 실제 성, 핵심 거점, 일반 돌격병 하나, 고정 폭발물과 보조 시설이 보인다. 고정 폭발물의 연속 배치와 보조 시설의 연결 거리는 스크립트가 실제 상자 몸체 외곽으로 검사한다. 차폐·피해 감쇠를 포함한 실제 기폭 전파는 PIE에서 관찰한다.
- 전장에 안개로 가려지는 실물이 없고, 공개 핵심 위치 플래그는 핵심 거점에만 켜져 있다.
- 적이 요새까지 접근하고 공격한다. 포탄 명중, 살아 있는 적의 넉백, 별도 폭발물의 추가 기폭을 구분할 수 있다. 재장전 상태와 두 시설의 HP가 HUD에 표시된다.
- 핵심 거점 파괴는 승리로, 요새 파괴는 패배로 이어지고 다시 시작할 수 있다. 핵심 거점은 함락 뒤에도 액터가 유지된다.
- 스크립트 완료 로그는 저작 단계의 완료 표시다. 입력, 위젯 배치, 내비게이션 경로, 연쇄 기폭과 종료 결과의 성공은 PIE 관찰로 확인한다.

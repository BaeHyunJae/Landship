# HMH 맵 기반 기능 검사

## 언제 쓰는가

프로젝트 골격(인구수, 충돌 채널, 게임 모드), 성 이동, 직접 조종(선택·명령·부대·예약·교전·위치 사수)을 고친 뒤 개인 테스트 맵 `/Game/Developers/HMH/HMH`에서 회귀를 확인할 때 쓴다. 공통 이동 코드(`Controls/LSOrderComponent`, `Castle/LSCastle`)를 고치기 전후에는 세 검사를 모두 실행한다. 병종 맵의 검사는 [`hmh-troop-types-map-check`](hmh-troop-types-map-check.md)에, 적 분대 맵의 검사는 [`enemy-combat-check`](../BHJ/enemy-combat-check.md)에 있다.

## 준비

- `LandshipEditor`(Win64 Development Editor)를 빌드해 둔다. 검사하는 프로브 클래스는 이 빌드의 Landship 모듈에 있다.
- 로그를 저장할 파일 경로를 정해 아래 명령의 `<로그 파일 경로>`에 쓴다. Git이 추적하지 않는 위치로 한다. `<엔진 경로>`는 UE 5.8 설치 폴더, `<프로젝트 경로>`는 이 저장소의 클론 폴더다.
- 맵 `/Game/Developers/HMH/HMH`에 다음 액터가 있어야 한다: `HMH_SkeletonProbe`, `HMH_MovementCastle`, `HMH_MovementObstacle`, `HMH_MovementNavBounds`, `HMH_RTS_Probe`, `HMH_RTS_Ally_0`~`3`, `HMH_RTS_Enemy_0`~`1`, `HMH_TestCamera`. 없으면 아래 '맵을 다시 만들 때'를 먼저 한다.
- 이동 검사와 직접 조종 검사는 같은 맵의 내비게이션을 쓴다. 이동 성(`HMH_MovementCastle`)이 시작할 때 맵의 NavMesh 에이전트를 반경 142cm, 높이 200cm로 맞춰 다시 만들므로, 이 성을 맵에서 지우지 않는다.
- 실행할 스크립트: `Scripts/Developers/HMH/hmh-map-check/setup-hmh-tests.py`, `Scripts/Developers/HMH/hmh-map-check/setup-hmh-movement.py`, `Scripts/Developers/HMH/hmh-map-check/setup-hmh-rts-controls.py`

## 순서

각 단계는 명령 하나를 실행한다. 한 명령이 끝나면 '성공 확인'의 표식을 로그에서 확인하고 다음으로 넘어간다.

1. 프로젝트 골격 검사를 실행한다. 인구수 이벤트, 다섯 Object Channel의 Object Line Trace, Projectile 채널 구체 Sweep을 검사한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' /Game/Developers/HMH/HMH -game -unattended -nop4 -nosound -NullRHI -HMHSkeletonTestExit '-abslog=<로그 파일 경로>'
   ```

2. 성 이동 검사를 실행한다. 초기 정지, 행의 이동 속도 반영, 경로 요청과 이동 상태, 목적지 변경, 맵 밖 명령 거부, 장애물 우회 후 도착, 복귀 명령, 명시적 정지를 검사한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' /Game/Developers/HMH/HMH -game -unattended -nop4 -nosound -NullRHI -HMHMovementTestExit '-abslog=<로그 파일 경로>'
   ```

3. 직접 조종 검사를 실행한다. 장애물 우회, 예약 이동, 추적, 공격 취소, 위치 사수, 전투 뒤 이동 재개, 경계 목적지, 공통 피해·사망·제거를 검사한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' /Game/Developers/HMH/HMH -game -unattended -nop4 -nosound -NullRHI -HMHRTSTestExit '-abslog=<로그 파일 경로>'
   ```

### 맵을 다시 만들 때

맵에 위 액터가 없거나 지워졌을 때 쓴다. 세 스크립트는 `/Game/Developers/HMH/HMH`를 불러와 없는 액터만 이름으로 찾아 만들고(이미 있으면 재사용) 저장한다. 맵 자체를 만들지는 않으므로 `HMH` 맵이 있어야 한다.

1. 에디터를 닫는다.

2. 게임 모드, 골격 프로브, 관찰 카메라를 만든다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' -run=pythonscript '-script=<프로젝트 경로>/Scripts/Developers/HMH/hmh-map-check/setup-hmh-tests.py' -unattended -nop4 -nosound -NullRHI
   ```

3. 이동 성, 장애물, NavMesh 볼륨을 만들고 내비게이션을 다시 빌드한다. 관찰 카메라는 이 단계에서 이동 검사 위치로 옮겨진다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' -run=pythonscript '-script=<프로젝트 경로>/Scripts/Developers/HMH/hmh-map-check/setup-hmh-movement.py' -unattended -nop4 -nosound -NullRHI
   ```

4. 아군 병력 4기, 적 병력 2기, 직접 조종 프로브를 만든다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' -run=pythonscript '-script=<프로젝트 경로>/Scripts/Developers/HMH/hmh-map-check/setup-hmh-rts-controls.py' -unattended -nop4 -nosound -NullRHI
   ```

5. `git status`로 `Content/Developers/HMH/HMH.umap`의 변경을 확인한다. 의도한 변경이 아니면 되돌린다.

## 성공 확인

- 단계 1: 로그에 `[HMH Skeleton] HMH Skeleton: N/N PASS`가 있다. 앞뒤 숫자가 같고 `[HMH Skeleton] FAIL` 줄이 없다.
- 단계 2: 로그에 `HMH_MOVE_FINISHED failures=0`이 있다.
- 단계 3: 로그에 `HMH_RTS_FINISHED failures=0`이 있다.
- 표식 줄은 `Select-String -Path '<로그 파일 경로>' -Pattern 'FINISHED|HMH Skeleton:'`로 찾는다. 실패한 항목은 같은 로그의 `HMH_MOVE FAIL`, `HMH_RTS FAIL`, `[HMH Skeleton] FAIL` 줄에 나온다.
- '맵을 다시 만들 때'의 세 명령은 로그에 각각 `HMH_SETUP_OK`, `HMH_MOVEMENT_SETUP_OK`, `HMH_RTS_CONTROLS_SETUP_OK`가 있고 Python 오류 메시지가 없다.

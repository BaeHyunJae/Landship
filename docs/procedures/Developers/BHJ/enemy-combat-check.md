# 적 거점·분대 전장 검사

## 언제 쓰는가

적 거점·분대(`Source/Landship/Enemies/`)를 고치거나, 적과 아군이 함께 쓰는 이동·공격 코드(`Controls/LSOrderComponent`, `Combat/LSBasicAttackComponent`)를 고친 뒤, 개인 테스트 맵 `L_EnemyCombat`에서 실제 내비게이션으로 회귀를 확인할 때 쓴다. 적 이동 실행부(`LSEnemyBehaviorComponent`)와 공용 명령 실행부(`LSOrderComponent`)에는 같은 이동 조건이 중복돼 있으므로, 공통 이동 결함을 고쳤다면 이 절차와 [`hmh-map-check`](../HMH/hmh-map-check.md)를 함께 실행한다.

## 준비

- `LandshipEditor`(Win64 Development Editor)를 빌드해 둔다. 검사하는 프로브 클래스는 이 빌드의 Landship 모듈에 있다.
- 로그를 저장할 파일 경로를 정해 아래 명령의 `<로그 파일 경로>`에 쓴다. Git이 추적하지 않는 위치로 한다. `<엔진 경로>`는 UE 5.8 설치 폴더, `<프로젝트 경로>`는 이 저장소의 클론 폴더다.
- 검사 대상 맵은 `/Game/Developers/BHJ/EnemyCombat/L_EnemyCombat`이다. 이 맵에는 프로브 액터(`LSEnemyCombatProbe`, `LSEnemyBoundaryProbe`, `LSEnemySupportProbe`, `LSEnemyRegressionProbe`, `LSEnemyVisionRuntimeProbe`)가 놓여 있다. 프로브는 해당 실행 인수가 없으면 스스로 제거되므로 일반 플레이에는 영향이 없다.
- 맵이 없거나 배치가 달라졌으면 아래 '맵을 다시 만들 때'를 먼저 한다.
- 아래 명령의 `--%`는 그 뒤를 PowerShell이 해석하지 않고 그대로 넘기라는 뜻이다. `-ExecCmds` 값의 따옴표를 지키려고 쓴다.
- 실행할 스크립트: `Scripts/Developers/BHJ/enemy-combat-check/setup-enemy-combat.py`, `Scripts/Developers/BHJ/enemy-combat-check/prepare-enemy-command-map.py`

## 순서

각 단계는 명령 하나를 실행한다. 한 명령이 끝나면 '성공 확인'의 표식을 로그에서 확인하고 다음으로 넘어간다. 프로세스 종료 코드만으로 통과를 판정하지 않는다.

1. 기본 시나리오(적 분대의 진격·방어·복귀·함락 후 중단)를 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemyCombatTestExit -abslog="<로그 파일 경로>"
   ```

2. 더 빠른 근접병이 계속 이동하는 성을 추격해 이동 중 피해를 주는지 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemyCombatTestExit -LSEnemyMovingSolo -abslog="<로그 파일 경로>"
   ```

3. 경계 프로브(작은 이탈 보정, 한계 초과 시 복귀, 구역 밖 상대 공격)를 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemyBoundaryTestExit -abslog="<로그 파일 경로>"
   ```

4. 밀집한 네 병력이 모두 정지한 성을 타격하는지 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemyDenseMarch -abslog="<로그 파일 경로>"
   ```

5. 네 병력이 전진해 이동하는 성을 근접·원거리로 타격하고, 과거 성 지원 위치로 진격하다 이탈하지 않는지 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemyMovingCastle -abslog="<로그 파일 경로>"
   ```

6. 지원 요청(거점 우선, 가까운 새 요청, 현재 지원 유지, 보고 위치 갱신)을 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemySupportTestExit -abslog="<로그 파일 경로>"
   ```

7. 구역 밖 우회 경로 거부(`Detour`)를 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemyRegressionTestExit -LSEnemyRegressionCase=Detour -abslog="<로그 파일 경로>"
   ```

8. 복귀 중 타격 금지와 점유 해제(`Return`)를 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemyRegressionTestExit -LSEnemyRegressionCase=Return -abslog="<로그 파일 경로>"
   ```

9. 경로 중단 뒤 재시도 대기 동안 지원 유지와 재개(`Retry`)를 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemyRegressionTestExit -LSEnemyRegressionCase=Retry -abslog="<로그 파일 경로>"
   ```

10. 저프레임에서의 경계 동작을 실행한다. `t.MaxFPS 5`로 프레임 간격을 0.2초로 늘린다.

    ```powershell
    & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemyBoundaryTestExit -LSEnemyBoundaryLowFPS -ExecCmds="t.MaxFPS 5" -abslog="<로그 파일 경로>"
    ```

11. 저프레임에서의 회귀 프로브를 실행한다. `Return`, `Retry`도 `-LSEnemyRegressionCase` 값만 바꿔 같은 방식으로 실행한다.

    ```powershell
    & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemyRegressionTestExit -LSEnemyRegressionCase=Detour -LSEnemyRegressionLowFPS -ExecCmds="t.MaxFPS 5" -abslog="<로그 파일 경로>"
    ```

### 시야를 켠 공개 공격 명령 검사

아군의 공개 공격 명령(지정 공격, 공격 이동)이 적 거점에 닿는지 확인한다. 시야가 켜진 맵에서는 명령을 내리기 전에 아군 시야가 거점을 포함해야 하므로, 성을 거점 근처에 임시로 놓고 실행한다. 위 단계 1~11을 먼저 끝낸 뒤에 한다.

1. 에디터를 닫고 성을 거점에서 1000cm 떨어진 곳에 놓는다. 시야 설정, 시야 표현, 시야 프로브, 핵심 거점의 공개 위치 플래그도 함께 맞추고 맵을 저장한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' -run=pythonscript '-script=<프로젝트 경로>/Scripts/Developers/BHJ/enemy-combat-check/prepare-enemy-command-map.py' -unattended -nop4 -nosound -NullRHI
   ```

2. 아군 근접병의 지정 공격을 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemyCombatTestExit -LSEnemyCoreOrderRunExit -LSEnemyOrderMelee -LSEnemyRequireVision -abslog="<로그 파일 경로>"
   ```

3. 대각선 공격 이동에서 자동 교전 뒤에도 원래 명령이 유지되는지 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" /Game/Developers/BHJ/EnemyCombat/L_EnemyCombat -game -unattended -nop4 -nosound -NullRHI -LSEnemyCombatTestExit -LSEnemyCoreOrderRunExit -LSEnemyOrderMelee -LSEnemyOrderAttackMove -LSEnemyOrderCorner -LSEnemyRequireVision -abslog="<로그 파일 경로>"
   ```

4. 임시 배치를 되돌리려고 '맵을 다시 만들 때'의 명령을 실행한다. 이 스크립트가 성을 원래 위치 `(-1800, -1200, 142)`에 다시 놓는다.

5. `git status`로 `Content/Developers/BHJ/EnemyCombat/L_EnemyCombat.umap`의 변경을 확인한다. 의도한 변경이 아니면 되돌린다.

### 맵을 다시 만들 때

`L_EnemyCombat`을 병종 맵의 복사본으로 다시 저작한다. 맵 안의 `Landship` 네이티브 액터와 정적 메시 액터를 모두 지우고 바닥, 이동 성, 적 거점, 시야 설정, 전장 설정, 적 병력 네 기, 프로브 다섯 개를 새로 놓은 뒤 저장한다. NavMesh 볼륨과 `LSNavigationSetup`은 보존하므로 NavMesh 볼륨 한 개 이상과 `LSNavigationSetup` 정확히 한 개가 맵에 이미 있어야 한다. `L_EnemyCombat`이 없으면 `/Game/Developers/HMH/HMH_TroopTypes`를 복제해 시작하므로 그 맵이 있어야 한다.

1. 에디터를 닫는다.

2. 아래 명령을 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' -run=pythonscript '-script=<프로젝트 경로>/Scripts/Developers/BHJ/enemy-combat-check/setup-enemy-combat.py' -unattended -nop4 -nosound -NullRHI
   ```

3. 단계 1~11 중 하나를 실행해 내비게이션이 실제로 도달 가능한지 확인한다. 이 스크립트는 맵을 저작할 뿐 NavMesh에서의 도달성은 검사하지 않는다.

## 성공 확인

- 모든 명령의 로그에 아래 표식이 있고 `failures=0`이다. 표식이 없으면 프로브가 중간에 끝난 것이므로 실패로 본다.

  | 단계 | 로그 표식 |
  | --- | --- |
  | 1, 2, '시야를 켠 공개 공격 명령 검사'의 2, 3 | `ENEMY_COMBAT_FINISHED failures=0` |
  | 3, 10 | `ENEMY_BOUNDARY_FINISHED failures=0` |
  | 4 | `ENEMY_DENSE_FINISHED failures=0` |
  | 5 | `ENEMY_MOVING_FINISHED failures=0` |
  | 6 | `ENEMY_SUPPORT_FINISHED failures=0` |
  | 7, 8, 9, 11 | `ENEMY_REGRESSION_FINISHED case=<Case> failures=0` |

- 로그에서 표식 줄은 `Select-String -Path '<로그 파일 경로>' -Pattern 'FINISHED'`로 찾는다. 실패한 항목은 같은 로그의 `FAIL` 줄에 나온다.
- 시야 검사에서는 `ENEMY_VISION_SUBMIT` 줄이 명령 제출 때마다 나오고 `enabled`, `targetVisible`, `passes`가 모두 참이다. 하나라도 거짓이면 프로브가 종료 코드 1로 끝나므로 `ENEMY_COMBAT_FINISHED` 줄이 없다.
- 스크립트 명령은 로그에 `ENEMY_COMBAT_SETUP_OK`(맵 다시 만들기), `ENEMY_VISION_COMBAT_MAP_SAVED`(시야 검사 준비)가 있고 Python 오류 메시지가 없다.

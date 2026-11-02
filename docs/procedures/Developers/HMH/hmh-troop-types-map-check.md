# HMH 병종 맵 검사

## 언제 쓰는가

병종(창병·궁병·기마병), 이동 성, 병력 생산, 성 무장, 교전 명령, 근접 집단 교전의 회피 정책을 고친 뒤 개인 테스트 맵 `/Game/Developers/HMH/HMH_TroopTypes`에서 실제 내비게이션으로 회귀를 확인할 때 쓴다. 공통 이동·교전 코드를 고칠 때는 이동·교전 검사(단계 1, 4, 5)와 [`hmh-map-check`](hmh-map-check.md)를 함께 실행한다. 병종 수치(`Data/Troops/DT_TroopStats.csv`)만 DataTable에 반영하려면 [`import-troop-stats`](../../import-troop-stats.md)를 쓴다.

## 준비

- `LandshipEditor`(Win64 Development Editor)를 빌드해 둔다. 검사하는 프로브 클래스는 이 빌드의 Landship 모듈에 있다.
- 로그를 저장할 파일 경로를 정해 아래 명령의 `<로그 파일 경로>`에 쓴다. Git이 추적하지 않는 위치로 한다. `<엔진 경로>`는 UE 5.8 설치 폴더, `<프로젝트 경로>`는 이 저장소의 클론 폴더다.
- 맵 `/Game/Developers/HMH/HMH_TroopTypes`에 프로브 액터 `HMH_Troop_Probe`, `HMH_Production_Probe`, `HMH_Weapons_Probe`, `HMH_Orders_Probe`와 양 진영의 이동 성(`HMH_MobileCastle_Ally`, `HMH_MobileCastle_Enemy`), 병종별 병력(`HMH_Troop_<번호>_Ally`, `HMH_Troop_<번호>_Enemy`)이 있어야 한다. 프로브는 해당 실행 인수가 없으면 스스로 제거되므로 일반 플레이에는 영향이 없다. 없으면 아래 '맵을 다시 만들 때'를 먼저 한다.
- 실행할 스크립트: `Scripts/Developers/HMH/hmh-troop-types-map-check/setup-hmh-troop-types.py` (이 스크립트가 같은 폴더의 `setup-castle-weapon-data.py`를 실행한다)
- 단계 1·3의 프로브는 이동·무장 동작을 검증하는 동안 오폭 금지(`bAllowFriendlyFire=false`)·자기 피해 허용(`bAllowSelfDamage=true`)을 임시 지정하고 종료 시 원래 설정을 복원한다. 양쪽 오폭 설정의 피해·넉백 조합은 `Landship` 자동화 테스트에서 확인한다.

## 순서

각 단계는 명령 하나를 실행한다. 한 명령이 끝나면 '성공 확인'의 표식을 로그에서 확인하고 다음으로 넘어간다.

1. 병종 검사를 실행한다. 세 병종의 이동 속도 관계, 원거리에서 궁병만 공격, 근접 병종의 접근 후 공격, 이동 성의 벽 우회, 성의 기본 지정 공격 거부와 수동 대포 시전을 검사한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' /Game/Developers/HMH/HMH_TroopTypes -game -unattended -nop4 -nosound -NullRHI -HMHTroopTypesTestExit '-abslog=<로그 파일 경로>'
   ```

2. 병력 생산 검사를 실행한다. 버튼 클릭, 세 병종 생성, 성 선택 조건, 빈 공간 탐색, 병종별 쿨타임, 인구수 상한, 사망·제거 시 중복 반환 방지, 공간 부족 실패와 재시도를 검사한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' /Game/Developers/HMH/HMH_TroopTypes -game -unattended -nop4 -nosound -NullRHI -HMHProductionTestExit '-abslog=<로그 파일 경로>'
   ```

3. 성 무장 검사를 실행한다. 실제 지면에서 세 무장과 무장·생산 버튼을 검사한다. 명령 카드 좌표와 발견 알림의 실제 그리기도 확인하므로 `-NullRHI` 대신 화면 밖 렌더링을 사용한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' /Game/Developers/HMH/HMH_TroopTypes -game -RenderOffscreen -windowed -ResX=1280 -ResY=720 -ForceRes -unattended -nop4 -nosound -HMHCastleWeaponsTestExit '-abslog=<로그 파일 경로>'
   ```

4. 교전 명령 검사를 실행한다. 세 병종의 동일 성 공격, 이동한 표적 재추적, 표적 사망 후 예약 이동, 위치 사수와 일반 이동의 공격 중단을 검사한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' /Game/Developers/HMH/HMH_TroopTypes -game -unattended -nop4 -nosound -NullRHI -HMHCombatOrdersTestExit '-abslog=<로그 파일 경로>'
   ```

5. 근접 집단 교전 검사를 실행한다. 아군과 적 근접 병력을 각 5기씩 생성해 모든 표적에 지속적인 피해가 들어가는지 검사한다. 이 프로브는 시험 전용 병력을 만들며 데이터 에셋을 수정하지 않는다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' /Game/Developers/HMH/HMH_TroopTypes -game -unattended -nop4 -nosound -NullRHI -HMHMeleeAvoidanceTestExit '-abslog=<로그 파일 경로>'
   ```

6. 필요하면 단계 5의 명령에 인수를 덧붙여 조건을 바꾼다. 인수는 함께 쓸 수 있다.

   - `-MeleeProbeStaticTargets`: 적 병력의 명령 컴포넌트를 꺼 추적하지 않게 한다.
   - `-MeleeProbeCount=1`: 한 쌍(1대1)으로 줄인다. 값은 1~5로 제한된다.
   - `-MeleeProbeRange=250`: 시험 사거리(cm)를 정한다. 지정하지 않으면 250이다.
   - `-MeleeProbeNoRVO`: 비교용으로 모든 시험 병력의 회피 고려 반경을 0으로 만든다. 통과 여부가 아니라 로그의 `MELEE_PROBE` 줄(위치, 속도, 표적과의 거리 `gap`, 표적 체력 `targetHP`)을 이 인수가 없는 실행과 비교한다.

### 맵을 다시 만들 때

맵이나 프로브가 없거나 지워졌을 때 쓴다. 이 스크립트는 다음을 한다.

- `/Game/Developers/HMH/HMH`를 `HMH_TroopTypes`로 복제한다. 이미 있으면 복제하지 않는다. 복제본에서 기존 HMH 검증 액터(이동 성 포함)를 치운다.
- 성 무장 시험용 Data Asset `DA_Cannon`, `DA_Shotgun`, `DA_Ballista`를 `/Game/Landship/Castle/Data`에 없을 때만 만든다. 이미 있는 에셋의 수치는 덮어쓰지 않는다.
- `Data/Troops/DT_TroopStats.csv`로 `/Game/Landship/Troops/Data/DT_TroopStats`를 채운다. 에디터에서 직접 고친 행 값은 CSV 값으로 덮어써진다.
- `M_TroopPrototype` 머티리얼이 없으면 만든다.
- 양 진영의 이동 성과 병종별 병력, 프로브 네 개, 내비게이션 설정(반경 142cm, 높이 284cm), 전장 설정을 배치하고 저장한다.

1. `DT_TroopStats`를 에디터에서 직접 고친 값이 있으면 먼저 `Data/Troops/DT_TroopStats.csv`에 옮겨 적는다.

2. 에디터를 닫는다.

3. 아래 명령을 실행한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' -run=pythonscript '-script=<프로젝트 경로>/Scripts/Developers/HMH/hmh-troop-types-map-check/setup-hmh-troop-types.py' -unattended -nop4 -nosound -NullRHI
   ```

4. `git status`로 바뀐 파일을 확인한다. `Content/Developers/HMH/HMH_TroopTypes.umap` 외에 `Content/Landship/Troops/Data/DT_TroopStats.uasset`이 바뀌었으면 의도한 변경인지 확인하고, 아니면 되돌린다.

## 성공 확인

- 각 단계의 로그에 아래 표식이 있고 `failures=0`이다. 표식이 없으면 프로브가 중간에 끝난 것이므로 실패로 본다.

  | 단계 | 로그 표식 |
  | --- | --- |
  | 1 | `HMH_TROOPS_FINISHED failures=0` |
  | 2 | `HMH_PRODUCTION_FINISHED failures=0` |
  | 3 | `HMH_WEAPONS_FINISHED failures=0` |
  | 4, 5 (`-MeleeProbeNoRVO` 제외) | `HMH_ORDERS_FINISHED failures=0` |

- 표식 줄은 `Select-String -Path '<로그 파일 경로>' -Pattern 'FINISHED'`로 찾는다. 실패한 항목은 같은 로그의 `FAIL` 줄에 나온다.
- 단계 5에서는 각 표적에 지속적인 피해가 들어갔다는 항목이 `HMH_ORDERS PASS`로 나온다.
- '맵을 다시 만들 때'의 명령은 로그에 `CASTLE_WEAPON_DATA_SETUP_OK`, `HMH_TROOP_TYPES_SETUP_OK`가 있고 `Troop stats CSV import failed` 등 Python 오류 메시지가 없다.

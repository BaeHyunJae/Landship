# 자동화 테스트 실행

## 언제 쓰는가

C++ 코드, 데이터, 에셋을 고친 뒤, PR을 열기 전이나 리뷰하기 전에 이 프로젝트의 자동화 테스트(이름이 `Landship.`으로 시작하는 테스트)를 맵 없이 명령줄로 실행해 통과 여부를 확인할 때 쓴다.

## 준비

- `<엔진 경로>`는 UE 5.8 설치 폴더, `<프로젝트 경로>`는 이 저장소의 클론 폴더다.
- 로그를 저장할 파일 경로와 보고서를 저장할 폴더를 정해 아래 명령의 `<로그 파일 경로>`, `<보고서 폴더>`에 쓴다. 둘 다 Git이 추적하지 않는 위치로 한다.
- 아래 명령의 `--%`는 그 뒤를 PowerShell이 해석하지 않고 그대로 넘기라는 뜻이다. `-ExecCmds` 값의 따옴표와 `;`를 지키려고 쓴다.

## 순서

1. `LandshipEditor`(Win64 Development Editor)를 빌드한다. 테스트는 이 빌드의 Landship 모듈에 들어 있으므로, 고친 코드를 빌드하지 않으면 고치기 전 코드를 검사한다.

   ```powershell
   & '<엔진 경로>/Engine/Build/BatchFiles/Build.bat' LandshipEditor Win64 Development '-Project=<프로젝트 경로>/Landship.uproject' -WaitMutex
   ```

   출력 끝에 `Result: Succeeded`가 있고 바로 실행한 `$LASTEXITCODE`가 `0`일 때만 다음 단계로 넘어간다. 빌드가 실패해도 PowerShell은 멈추지 않으므로, 실패한 채 2단계를 실행하면 이전 빌드의 코드를 검사하게 된다. C++ 파일을 추가하거나 지웠으면 같은 명령을 한 번 더 실행해 두 번째도 성공인지 확인한다. 첫 빌드가 파일 추가·삭제를 반영하지 못한 채 끝날 수 있기 때문이다.

2. 테스트를 실행한다. `<필터>` 자리에는 아래 '실행할 단계 고르기'에서 고른 필터를 넣는다. 테스트가 끝나면 `Quit`이 에디터를 종료한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" -ExecCmds="Automation RunTests <필터>;Quit" -unattended -nop4 -nosound -NullRHI -nosplash -ReportExportPath="<보고서 폴더>" -abslog="<로그 파일 경로>"
   ```

3. 명령이 끝나면 바로 종료 코드를 본다.

   ```powershell
   $LASTEXITCODE
   ```

4. 로그에서 결과 줄을 찾는다. `-CaseSensitive`를 빼면 `TEST COMPLETE`가 테스트마다 찍히는 `Test Completed` 줄에도 걸린다.

   ```powershell
   Select-String -Path '<로그 파일 경로>' -CaseSensitive -Pattern 'automation tests based on', 'Result=\{Fail', 'No automation tests matched', 'TEST COMPLETE'
   ```

### 실행할 단계 고르기

바뀐 파일이 깨뜨릴 수 있는 테스트만 실행하려고 단계를 고른다. 아래 표에서 바뀐 파일에 맞는 줄의 필터를 순서 2의 `<필터>` 자리에 넣는다. 바뀐 파일이 여러 줄에 걸치면 가장 넓은 단계를 고른다. 같은 단계의 줄에 여러 폴더가 걸치면 `+StartsWith:Landship.<폴더>`를 폴더마다 이어 붙인다.

| 바뀐 것 | 단계 | 필터 |
| --- | --- | --- |
| 문서(`docs/`, `*.md`)만 | 실행하지 않음 | 없음 |
| 폴더 하나의 수치(DataTable·데이터 에셋의 값, `Data/<폴더>/`의 CSV)나, 모양만 바꾼 아트 에셋(메시, 머티리얼, 텍스처, 애니메이션, 사운드, 위젯의 모양) | P0과 그 폴더의 테스트 | `Group:P0+StartsWith:Landship.<폴더>` |
| 기능 폴더 하나의 C++ 코드나 블루프린트(그래프, 기본값, 충돌 설정, 다른 에셋 참조). 그 폴더의 CONTRACT.md에 적힌 것(다른 폴더가 기대는 것)은 바꾸지 않은 경우 | P0과 그 폴더의 테스트 | `Group:P0+StartsWith:Landship.<폴더>` |
| 공용 폴더(`Combat/`, `Navigation/`)의 C++ 코드나 블루프린트, CONTRACT.md에 적힌 것을 바꾸는 변경, 맵(`.umap`), `Config/`의 설정, `*.Build.cs` | P1 | `StartsWith:Landship` |
| 위 어느 줄에도 맞지 않는 파일. 예: `Scripts/`의 스크립트 | P1 | `StartsWith:Landship` |

- P0은 전투 한 판을 진행하는 데 꼭 필요한 기능을 검사한다. 프로젝트 설정, 체력·피해, 기본 공격, 전투 수치, 성 이동과 무장, 병종, 선택과 명령, 적의 공격과 거점 함락, 승패 판정, 전투 HUD가 그 기능이다. P0에 넣은 테스트 목록은 `Config/DefaultEngine.ini`의 `[/Script/AutomationController.AutomationControllerSettings]` 절에 있는 `Name="P0"` 줄들이다.
- P1은 이 프로젝트의 자동화 테스트 전체다.
- `<폴더>` 자리에는 그 폴더 테스트 이름의 두 번째 마디를 쓴다. 이름이 폴더 이름과 다른 테스트도 있으므로 '일부 테스트만 실행할 때'의 방법으로 확인한다.
- 바뀐 에셋이 `Content/Landship/<폴더>/` 밖에 있으면(예: `Content/ReflectionLab/`), 그 에셋을 참조하는 코드나 에셋이 있는 폴더를 `<폴더>`로 쓴다. 참조하는 곳은 에디터의 Reference Viewer나 `Source/`에서 에셋 경로를 검색해 찾는다. 참조하는 폴더를 찾지 못하거나, 찾은 폴더에 이름이 `Landship.<폴더>.`로 시작하는 테스트가 없으면 P1을 실행한다.
- 표가 고른 단계는 P1이라도 따로 묻지 않고 실행한다. 표가 고른 것보다 넓은 단계는 작업을 요청한 사람이 요구할 때만 실행한다.
- 한 단계는, 통과한 뒤 표에 해당하는 파일이 다시 바뀌었을 때만 다시 실행한다. PR을 열기 전이나 리뷰하기 전에도 같다.
- 테스트가 실패하면, 고친 뒤 먼저 실패한 테스트만 실행해 통과를 확인한다. 필터 자리에 실패한 테스트의 전체 이름을 `StartsWith:` 없이 넣는다. 그다음 고른 단계를 한 번 실행한다.
- `Name="P0"` 줄은 둘 중 하나다. `MatchFromEnd=True`가 있는 줄은 이름이 같은 테스트 하나를 넣는다. 없는 줄은 `.`으로 끝나는 이름 앞부분으로 시작하는 테스트를 모두 넣는다.
- 새 테스트는 이름이 P0 줄의 앞부분으로 시작하면 P0에도 들어가고, 아니면 P1에만 들어간다. 테스트를 추가하는 PR에서 새 테스트가 위 P0 기준에 맞게 들어갔는지 확인한다. 기준에 맞는데 빠졌으면 `Name="P0"` 줄을 추가한다. 기준에 맞지 않는데 앞부분 때문에 들어갔으면, 그 앞부분 줄을 테스트마다 한 줄씩으로 나누고 새 테스트를 뺀다.

### 일부 테스트만 실행할 때

순서 2의 `<필터>` 자리에 `StartsWith:`와 `Landship.`보다 긴 이름 앞부분을 넣는다. 예: `StartsWith:Landship.Combat`. 엔진이 넣은 값 끝에 `.`을 붙여 비교하므로, `StartsWith:Landship.Comb`처럼 마디 중간에서 끊으면 아무 테스트도 고르지 않는다. 테스트 이름은 로그의 `Test Started` 줄에서 `Path={...}`로 확인한다. 이름의 두 번째 마디가 늘 소스 폴더 이름과 같지는 않으므로, 폴더 하나의 테스트를 모두 고르려면 그 폴더의 `Tests/` 아래 파일에서 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`의 두 번째 인수를 확인한다.

## 성공 확인

- 순서 3의 종료 코드가 `0`이다. 테스트가 하나라도 실패하거나 이름 앞부분에 맞는 테스트가 없으면 `0`이 아닌 값(PowerShell에서 `255`)이 나온다.
- 순서 4의 결과가 다음과 같다.
  - `Found <개수> automation tests based on '<필터>'` 줄이 있다. `<필터>`는 순서 2에 넣은 값이고, 이 줄의 개수가 실행한 테스트 수다.
  - `**** TEST COMPLETE. EXIT CODE: 0 ****` 줄이 있다. 실패가 있으면 `EXIT CODE: -1`이 나온다.
  - `Result={Fail}` 줄과 `No automation tests matched` 줄이 없다.
- 실패한 테스트는 로그의 `Result={Fail} ... Path={<테스트 이름>}` 줄로 찾는다. 바로 다음 `LogAutomationController: Error:` 줄에 실패한 검사 내용과 소스 위치가 나온다.
- `<보고서 폴더>/index.json`의 `failed`가 `0`이다. `succeededWithWarnings`에 세어진 테스트는 통과다. 경고 내용은 `index.json`의 각 테스트 `entries`나 로그의 `LogAutomationController: Warning:` 줄에서 확인한다.

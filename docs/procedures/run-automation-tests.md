# 자동화 테스트 실행

## 언제 쓰는가

`Source/Landship/` 아래 C++ 코드를 고친 뒤, PR을 열기 전이나 리뷰하기 전에 이 프로젝트의 자동화 테스트(이름이 `Landship.`으로 시작하는 테스트)를 맵 없이 명령줄로 실행해 통과 여부를 확인할 때 쓴다.

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

2. 테스트를 실행한다. `StartsWith:Landship`은 이름이 `Landship.`으로 시작하는 테스트만 고른다. 테스트가 끝나면 `Quit`이 에디터를 종료한다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' --% "<프로젝트 경로>/Landship.uproject" -ExecCmds="Automation RunTests StartsWith:Landship;Quit" -unattended -nop4 -nosound -NullRHI -nosplash -ReportExportPath="<보고서 폴더>" -abslog="<로그 파일 경로>"
   ```

3. 명령이 끝나면 바로 종료 코드를 본다.

   ```powershell
   $LASTEXITCODE
   ```

4. 로그에서 결과 줄을 찾는다. `-CaseSensitive`를 빼면 `TEST COMPLETE`가 테스트마다 찍히는 `Test Completed` 줄에도 걸린다.

   ```powershell
   Select-String -Path '<로그 파일 경로>' -CaseSensitive -Pattern 'automation tests based on', 'Result=\{Fail', 'No automation tests matched', 'TEST COMPLETE'
   ```

### 일부 테스트만 실행할 때

순서 2의 `StartsWith:Landship`을 더 긴 이름 앞부분으로 바꾼다. 예: `StartsWith:Landship.Combat`. 엔진이 넣은 값 끝에 `.`을 붙여 비교하므로, `StartsWith:Landship.Comb`처럼 마디 중간에서 끊으면 아무 테스트도 고르지 않는다. 테스트 이름은 로그의 `Test Started` 줄에서 `Path={...}`로 확인한다. 이름의 두 번째 마디가 늘 소스 폴더 이름과 같지는 않으므로, 폴더 하나의 테스트를 모두 고르려면 그 폴더의 `Tests/` 아래 파일에서 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`의 두 번째 인수를 확인한다.

## 성공 확인

- 순서 3의 종료 코드가 `0`이다. 테스트가 하나라도 실패하거나 이름 앞부분에 맞는 테스트가 없으면 `0`이 아닌 값(PowerShell에서 `255`)이 나온다.
- 순서 4의 결과가 다음과 같다.
  - `Found <개수> automation tests based on '<필터>'` 줄이 있다. `<필터>`는 순서 2에 넣은 값(`StartsWith:Landship` 또는 '일부 테스트만 실행할 때'에서 바꾼 값)이고, 이 줄의 개수가 실행한 테스트 수다.
  - `**** TEST COMPLETE. EXIT CODE: 0 ****` 줄이 있다. 실패가 있으면 `EXIT CODE: -1`이 나온다.
  - `Result={Fail}` 줄과 `No automation tests matched` 줄이 없다.
- 실패한 테스트는 로그의 `Result={Fail} ... Path={<테스트 이름>}` 줄로 찾는다. 바로 다음 `LogAutomationController: Error:` 줄에 실패한 검사 내용과 소스 위치가 나온다.
- `<보고서 폴더>/index.json`의 `failed`가 `0`이다. `succeededWithWarnings`에 세어진 테스트는 통과다. 경고 내용은 `index.json`의 각 테스트 `entries`나 로그의 `LogAutomationController: Warning:` 줄에서 확인한다.

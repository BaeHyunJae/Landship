# 병종 수치 CSV 가져오기

## 언제 쓰는가

`Data/Troops/DT_TroopStats.csv`를 고친 뒤, 그 값을 `Content/Landship/Troops/Data/DT_TroopStats` DataTable에 반영할 때 쓴다. DataTable이 없으면 새로 만든다.

## 준비

- 에디터를 닫는다. 명령줄 에디터가 같은 에셋을 저장하기 때문이다.
- DataTable에서 에디터로 직접 고친 값이 있으면 먼저 CSV에 옮겨 적는다. 이 절차는 DataTable의 모든 행을 CSV 값으로 덮어쓴다.
- 실행할 스크립트: `Scripts/import-troop-stats/import_troop_stats.py`

## 순서

1. 프로젝트 폴더에서 아래 명령을 실행한다. `<엔진 경로>`는 UE 5.8 설치 폴더, `<프로젝트 경로>`는 이 저장소의 클론 폴더다.

   ```powershell
   & '<엔진 경로>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<프로젝트 경로>/Landship.uproject' -run=pythonscript '-script=<프로젝트 경로>/Scripts/import-troop-stats/import_troop_stats.py' -unattended -nop4 -nosound -NullRHI
   ```

2. 바뀐 `DT_TroopStats.uasset`과 `DT_TroopStats.csv`를 같은 PR에 넣는다.

## 성공 확인

- 명령 출력에 `TROOP_STATS_IMPORTED`가 있고 `Troop stats CSV import failed`가 없다.
- 에디터에서 `DT_TroopStats`를 열면 행과 값이 CSV와 같다.

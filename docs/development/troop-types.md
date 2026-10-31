# 창병·궁병·기마병

## 실행

`/Game/Developers/Minhyeok/HMH_TroopTypes`를 열고 Play한다. 아래쪽 파란 진영과 위쪽 붉은 진영에 각각 창병·궁병·기마병 한 기씩 배치되어 있다. 좌클릭 선택, 우클릭 이동·지정 공격, A 공격 이동, S 정지, H 위치 사수는 기존 RTS 조작을 사용한다. 기존 HMH와 BHJ 테스트 맵은 변경하지 않았다.

## 초기 병종 정의

- 창병 Spearman: 기본 Manny 아바타와 머리 위 사각형 표식. 근접 사거리 150cm, 이동 속도 300cm/s.
- 궁병 Archer: 기본 Manny 아바타와 머리 위 원뿔 표식. 원거리 사거리 800cm, 이동 속도 300cm/s로 창병과 같다.
- 기마병 Cavalry: 기본 Manny 아바타와 머리 위 구 표식. 근접 사거리 150cm, 이동 속도 600cm/s로 창병의 두 배다.

세 병종의 초기 체력 120, 공격력 12, 공격 간격 1초는 비교를 위한 공통 시험값이다. 특수 상성·돌진 피해는 추가하지 않았다. 궁병은 화살 모양의 직선 비유도 투사체를 발사하며 실제 명중 시 공통 피해를 적용한다. 기본 속도는 1400cm/s, 수명 3초, 충돌 반경 6cm다. 화살은 목표 외 모든 유닛·건물·지형을 통과하고 발사 시 지정한 목표의 충돌 형상에만 실제 스윕 명중을 검사한다. 중력·곡사·유도 및 엄폐물을 우회하는 사격 위치 탐색은 추가하지 않았다.

세 병종 모두 기존 인간형 테스트와 같은 Manny Simple 아바타를 사용한다. 머리 위 도형과 병종 이름으로 종류를 구분하고 표식의 파랑·주황으로 진영을 표시한다. 표식에는 이동·피격 충돌이 없으며 사망하면 표식과 이름을 숨긴다. 아바타는 공통 인간형 랙돌과 지연 제거를 사용한다. 기존 아바타와 동일하게 애니메이션 Blueprint는 연결하지 않았으며 기마병도 현재는 말을 타지 않는 이동 속도 시험용 인간형이다.

## 코드와 데이터

아바타 변경 후 Editor 빌드와 전체 자동 테스트 38개가 통과했다. 동일 아바타·세 표식·머리 위 배치·표식 충돌 제외·사망 시 숨김과 랙돌을 확인했다. 실제 맵 실행 검사도 실패 없이 완료했다. 결과는 `Saved/Automation/TroopAvatar/index.json`, `Saved/Logs/HMHTroopAvatar.log`에 있다. 표식 그림자는 끄고 아바타와 겹치는 불필요한 그림자를 제거했다.

`ALSTroopUnit`은 기존 ALSUnit을 상속하며 체력·진영·피해·선택·명령·길 찾기를 다시 구현하지 않는다. TroopType에 맞는 데이터 행과 머리 위 표식을 선택한다. 같은 클래스도 공통 Faction으로 아군·적에 배치할 수 있다.

- 소스 데이터: `Data/Troops/DT_TroopStats.csv`
- 게임 데이터: `/Game/Landship/Troops/Data/DT_TroopStats`
- 표시 머티리얼: `/Game/Landship/Troops/Materials/M_TroopPrototype`
- 테스트 맵: `/Game/Developers/Minhyeok/HMH_TroopTypes`

TroopType은 에디터에서 지정하거나 지연 생성 중 SetInitialTroopType으로 설정한다. 부모 BeginPlay 전에 데이터 행을 확정하며, 전투 준비 후 변경·잘못된 병종은 거부한다. 같은 클래스의 세 병종을 더블클릭할 때 섞이지 않도록 SelectionType을 데이터 행 이름으로 구분한다.

전투 수치는 DataTable에서 조정한다. 속도 관계를 유지하려면 창병·궁병은 같은 값, 기마병은 그 두 배로 설정한다. 자동 검사가 이 관계를 확인한다. 수치·데이터가 잘못되면 부모의 공통 초기화 실패 정책을 따르며 대체 수치를 넣지 않는다.

## 에셋 재생성

`setup-hmh-troop-types.py`는 CSV를 DataTable로 가져오고 머티리얼·전용 맵·양 진영 세 병종을 준비한다. CSV를 다시 가져오므로 재실행 전에 보존할 데이터 변경을 CSV에도 반영해야 한다. 기존 HMH를 처음 한 번 복제하며, 이후에는 전용 맵만 다시 설정한다.

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Report/Projects/LandShip/Landship.uproject' -run=pythonscript '-script=C:/Report/Projects/LandShip/docs/development/setup-hmh-troop-types.py' -unattended -nop4 -nosound -NullRHI
```

## 검증

- Win64 Development Editor 빌드 성공.
- 전체 Landship 자동 테스트 36개 통과. TypesAndCombat은 세 데이터 행·도형·속도 관계·공통 체력과 진영·병종 변경 잠금·발사 직후 피해 없음과 명중 후 피해를 확인한다. 투사체 검사 4개는 지연 명중·중복 피해 방지·장애물 통과 후 실제 대상 명중·발사 시 진영 보존·오폭 설정·회피·수명 종료를 확인한다.
- 전용 맵 검사 24개 통과. 실제 이동, 같은 시간 창병·궁병의 동일 거리 이동, 기마병의 더 빠른 이동, 원거리에서 궁병만 공격, 두 근접 병종의 접근 후 공격을 확인한다.
- 가속 때문에 같은 시간 이동 거리가 정확히 두 배는 아니다. 검증 당시 창병·궁병은 각각 약 428cm, 기마병은 약 812cm를 이동했다. 최대 이동 속도 값은 정확히 300/300/600이다.

자동 테스트 결과: `Saved/Automation/Projectiles/index.json`.
장애물 차단 제거 후 재검증: `Saved/Automation/ProjectileNoObstaclesVerified/index.json`에서 전체 36개 통과. `IgnoresObstacles`는 지형·이동 장애물·정적/동적 장식물과 체력 없는 건물 장식물을 통과해 뒤의 적에게 한 번 명중하는지 확인한다.
투사체 적용 후 맵 실행 로그: `Saved/Logs/HMHProjectileRuntime.log`.
성공 기준: `HMH_TROOPS_FINISHED failures=0`.

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Report/Projects/LandShip/Landship.uproject' /Game/Developers/Minhyeok/HMH_TroopTypes -game -unattended -nop4 -nosound -NullRHI -HMHTroopTypesTestExit '-abslog=C:/Report/Projects/LandShip/Saved/Logs/HMHTroopTypesRuntime.log'
```

검사는 NullRHI 실행을 포함한다. 엔진 자체 스크린샷 `Saved/Screenshots/WindowsEditor/HighresScreenshot00001.png`에서 세 도형과 진영 색, 양 진영 초기 배치가 모두 화면에 보이는 것을 확인했다. 기존 종료 시 내비게이션 CleanUp 문제는 별개로 남아 있으며, 이번 병종 구현은 해당 엔진 종료 경로를 변경하지 않았다.


## 궁병의 화살

`RangedAttack` 설정에서 발사체 클래스·속도·수명·반경을 조정할 수 있다. 창병·기마병은 발사체 클래스를 비워 기존 근접 즉시 피해를 사용한다. 궁병은 발사 당시 대상 위치를 향해 직진하므로 옆으로 이동하면 피할 수 있다. 명령을 중단해도 이미 발사한 화살은 유지된다.

화살은 아군·다른 적·다른 건물·지형·장식물을 모두 통과하고, 발사 시 지정한 살아 있는 목표에만 명중해 사라진다. 목표가 이동해 빗나가거나 제거되면 다른 유닛으로 표적을 바꾸지 않고 직진하다 수명 종료 시 제거된다.

발사 시 `MakeDamageRequest`로 피해량과 진영을 저장하며, 명중 시 실제 맞은 대상의 `ApplyDamage`에 전달한다. 발사자가 제거돼도 비행과 저장된 진영은 유지된다. 화살 모양은 기본 도형으로 만든 축과 화살촉이며 진영 색을 사용한다.

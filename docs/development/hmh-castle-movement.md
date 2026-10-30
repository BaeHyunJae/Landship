# HMH 성 클릭 이동 확인 (TASK-40)

## 직접 실행

1. LandshipEditor를 빌드하고 `/Game/Developers/Minhyeok/HMH`를 연다.
2. Play 후 화면을 클릭하여 입력을 잡는다.
3. 작은 정육면체가 테스트용 성이고, 긴 직육면체는 장애물이다.
4. 성을 좌클릭하고 바닥을 우클릭한다. 장애물을 돌아 이동해야 한다.
5. 이동 중 다른 바닥을 우클릭하면 목적지가 바뀐다. 빈 곳 좌클릭은 선택을 해제한다.

테스트용 성은 이동 속도 400cm/s의 전용 임시 테이블을 사용한다. 공용 전투 테이블은 변경하지 않는다. HMH의 기존 인구수·충돌 검사 액터도 유지한다.

## 다른 맵에 적용할 조건

- GameMode의 PlayerControllerClass를 `ALSPlayerController`로 지정한다.
- `ALSCastle` 파생 성에 유효한 `StatsRow`를 지정한다. MovementSpeed가 0이거나 수치 초기화가 실패하면 이동 요청을 거부한다.
- 성은 AIController가 소유하고, 플레이어 컨트롤러는 성에 직접 빙의하지 않는다.
- 지면의 Object Type은 `LSGround`로, 장애물은 이동을 막는 충돌과 내비게이션 영향이 있는 형태로 설정한다. 클릭 Trace는 Visibility를 사용한다.
- NavMeshBoundsVolume과 성 크기에 맞는 NavMesh가 필요하다. 현재 기본 성의 충돌 상자는 반길이 100cm이므로 내비게이션 반경은 142cm, 높이는 200cm를 기준으로 한다. 기본 보행자용 반경 34cm로 생성하면 모서리에 걸릴 수 있다.
- HMH의 테스트용 성만 시작 시 해당 맵의 내비게이션 설정을 맞춰 재생성한다. 정식 맵에서는 지원 에이전트와 내비게이션 데이터를 별도로 설정해야 한다.
- 현재는 수평 지면에서의 이동 검증이다. 중력, 경사면 높이 추종, 시각적 차체 회전, 선택 강조 UI는 포함하지 않는다.

## 외부 기능용 API

- `ALSCastle::RequestMove(Destination)`: 명령 수락 여부. 잘못된 좌표, 내비게이션 밖, 불완전 경로는 거부한다. 수락은 도착 보장이 아니며 이후 장애물 변경으로 중단될 수 있다.
- `ALSCastle::IsMoving()`: 경로 추종의 진행·대기 상태를 조회한다. 도착·취소·경로 실패 시 false다. 공성포 등의 이동 제약 판단에 사용할 수 있다.
- `ALSCastle::StopMoving()`: 경로와 잔여 속도를 모두 정지한다.
- `ALSPlayerController::GetSelectedCastle()`: 현재 선택한 성. 대상이 파괴되면 null이다.

도착 판정에는 `AcceptanceRadius`와 성의 충돌 반경이 함께 적용된다. 현재 기본값에서는 목적지 중심으로부터 약 162cm 이내에서 정지할 수 있다.

진영에 따른 선택 제한은 이번 범위에 포함하지 않는다. 진영 기능 통합 시 적 성 선택 차단 정책을 연결해야 한다.

## 자동 재현

HMH 환경 재생성은 에디터를 닫고 다음 명령으로 실행한다. 기존 액터를 삭제하지 않고 이름으로 재사용한다.

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Report/Projects/LandShip/Landship.uproject' -run=pythonscript '-script=C:/Report/Projects/LandShip/docs/development/setup-hmh-movement.py' -EnablePlugins=PythonScriptPlugin -unattended -nop4 -nosound -NullRHI
```

이동 검사는 아래 명령으로 실행한다. `HMH_MOVE_FINISHED failures=0`을 확인한다. 해당 옵션이 없으면 테스트용 성은 자동 이동하지 않는다.

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Report/Projects/LandShip/Landship.uproject' /Game/Developers/Minhyeok/HMH -game -unattended -nop4 -nosound -NullRHI -HMHMovementTestExit '-abslog=C:/Report/Projects/LandShip/Saved/Logs/HMHMovementRuntime.log'
```

검사 항목은 초기 정지, 행의 이동 속도 반영, 경로 요청, 이동 상태, 실제 위치 변화, 이동 중 상태 조회, 목적지 변경, 맵 밖 명령 거부, 기존 이동 유지, 장애물 우회 후 도착, 복귀 명령, 명시적 정지다.

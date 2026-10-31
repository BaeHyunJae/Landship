# RTS 직접 조종

## HMH 실행

`/Game/Developers/Minhyeok/HMH`에는 아군 인간형 병력 4기, 적 인간형 병력 2기와 이동 성이 있다. HMH도 PR #12의 공통 전투 코드를 그대로 사용한다. 별도 TestHealth·TestTeam과 공격 참여 인터페이스는 제거했다.

병력은 PR의 `DT_UnitStats.PrototypeTest`, 성은 `DT_CastleStats.PrototypeTest`를 읽는다. HMH 표시용 클래스는 메시·이름표·공격선만 추가한다. 인간형 메시와 Physics Asset은 BHJ의 BP_TestHumanoid와 같은 설정을 사용하며, 사망·랙돌·지면 충돌·기본 10초 제거는 부모 유닛과 공통 체력 코드가 수행한다. 별도 애니메이션 그래프를 추가한 것은 아니다.

진영은 저장된 액터의 공통 Faction 속성을 사용한다. 부모 BeginPlay에서 진영을 잠그고 체력을 준비한 뒤, Ally는 조작 가능하고 Enemy는 정보 확인만 가능하도록 연결한다. 체력바와 패널도 실제 공통 체력 스냅샷을 표시한다.

## 조작

- 화면 가장자리·방향키: 카메라 이동. 가운데 버튼 드래그: 화면 이동. 휠: 확대·축소.
- 좌클릭: 단일 선택. 적은 정보만 확인한다.
- 좌클릭 드래그: 아군 다중 선택. 성과 병력을 함께 선택한다. 성·병력이 포함되면 고정 건물은 제외한다. 투영한 액터 중심으로 판정한다.
- Shift 클릭·드래그: 선택 추가·제외.
- 더블클릭: 화면 안의 같은 병종 선택. 기본은 클래스이며 SelectionType으로 구분할 수 있다.
- 지면 우클릭 또는 M 후 지면 클릭: 일반 이동. 이동 중 공격하지 않는다.
- 적 우클릭 또는 A 후 적 클릭: 지정 공격. 사거리 밖이면 접근한다.
- A 후 지면 클릭: 공격 이동. 교전 뒤 원래 목적지로 이동을 재개한다.
- S: 현재 명령과 예약을 취소하고 대기한다. 이후 감지 범위의 적을 자동 교전할 수 있다.
- H: 위치 사수. 이동 없이 사거리 안에서만 공격한다.
- Esc 또는 명령 입력 대기 중 우클릭: 입력 취소.
- Shift 명령: 최대 64개 예약. 위치 사수·대기는 즉시 교체한다.
- Ctrl+0~9: 부대 지정. 숫자: 호출. 빠르게 두 번: 화면 복귀.
- Home: 선택 대상 중심으로 화면 복귀.
- Q/W/E: 선택한 아군 이동 성의 대포·산탄·발리스타 조준. 좌클릭 시전, 우클릭·Esc 취소. 생산 키는 F1/F2/F3이다.

기존 조작 안내와 M/A/S/H 화면 버튼은 제거했다. 해당 단축키는 유지하며 우측 하단에는 병력 생산 버튼을 표시한다. 아군 성 선택 시 F1 창병·F2 궁병·F3 기마병을 생산하며 병종별 쿨타임은 10초, 인구수 상한은 100이다. UI 클릭은 전장 선택·명령으로 전달하지 않는다. 일반 다중 이동은 모든 대상이 클릭한 공통 목적지로 집결한다. 공격 이동만 기존 격자 목적지 배정을 유지한다. 병력과 대형 성은 CharacterMovement의 RVO 근거리 회피를 사용한다. 모든 대상을 한 좌표에 겹쳐 세우지는 않으며 실제 충돌 크기만큼 주변에 모인다. 복잡한 병목 통과와 대형 유지는 별도다.

## 구현과 공통 코드의 경계

- `ULSHealthComponent`: 체력 초기화, 피해 정책과 결과, 피격·사망 알림, 제거 정책.
- `ILSFactionInterface`: 준비된 진영 조회. 플레이 중 진영 잠금은 실제 전투 액터가 담당한다.
- `ULSCombatTargetLibrary`: 양측 생존 및 상대 진영 판정. 사거리·시야와 명중 피해 정책은 별도다.
- `ULSBasicAttackComponent`: 공격 간격·사거리·즉시 피해 요청. 공통 체력을 직접 호출한다.
- `ULSOrderComponent`: 이동·교전 상태와 예약 명령. 유효한 공격 대상을 유지한다.
- `ULSSelectableComponent`: 선택 분류·조작 권한. 공통 체력이 실패하거나 사망한 전투 대상은 선택하지 않는다.
- `ALSPlayerController`: 입력 해석, 선택·부대, 명령 전달과 대상별 목적지 배정.
- `ALSRTSHUD`: 공통 체력 표시, 선택 원, 입력 대기와 명령 피드백.

체력·수치 초기화 실패 시 이동·공격을 활성화하지 않는다. 이동 성에는 Q/W/E 수동 무장을 연결했고 정적인 거점은 이동할 수 없다. 클릭용 유닛 캡슐은 Visibility를 막는다. Blueprint에서 충돌 프로필을 바꾸면 이 응답을 유지해야 한다.

## 이동 결과

이동 중 새 이동·공격 이동 명령을 받으면 현재 속도를 유지한 채 경로를 교체한다. 지정 공격도 사거리 밖에서는 정지 없이 추적으로 교체한다. 정지·위치 사수 명령, 공격 사거리 진입과 이동 요청 실패에서는 실제 정지한다. 이전 경로의 교체 알림은 새 이동의 완료로 처리하지 않는다.

이동·공격 이동은 NavMesh에 보정한 목적지를 저장한다. 이미 보정 목적지에 도착한 명령도 허용한다. 요청이 유효하지 않으면 기존 명령을 교체하지 않는다. 예약 명령은 실행 전에 다시 검사한다.

경로 추적의 완료 이벤트에서 성공과 실패·외부 중단을 구분하며 이전 요청 ID의 이벤트를 무시한다. 실패·중단된 명령은 종료하고 유효한 다음 예약을 시도한다. 이동 불가능한 건물은 대형 계산에서 제외한다. HUD 버튼과 입력은 같은 능력 판정을 사용하고 이동 목적지 표시는 디버그 드로잉에 의존하지 않는다.

## 검증

일반 Move의 격자 목적지 배정을 제거하고 공통 클릭 지점으로 집결하도록 변경했다. 병력은 이동 중 RVO 회피를 사용하고, 공격 추적에서는 이를 꺼서 공격 대상 밖에서 멈추는 것을 방지한다. 성은 회피 우선순위를 높여 진행 방향을 유지하고 병력이 성을 회피한다. 공격 이동은 교전 종료 후 회피를 다시 활성화한다.

Editor 빌드와 전체 자동 테스트 38개, 기존 HMH RTS 검사가 통과했다. 전용 맵에서 병력 3기와 성의 실제 집결을 확인했다. 병력은 클릭 지점에서 약 76~102cm, 성은 약 243cm 거리에 모여 속도 0으로 정지했다. 결과는 `Saved/Automation/GatherMoveFinal/index.json`, `Saved/Logs/HMHGatherMoveVerified.log`, `Saved/Logs/HMHGatherMoveRTSFinal.log`에 있다. 병종 기본 속도 비교에서는 회피에 의한 감속을 분리하고 집결 검사에서는 실제 회피를 활성화한다. RVO는 NavMesh 경계 준수나 대규모 병목 해소까지 보장하는 기능은 아니다.

연속 이동 수정 검증: Editor 빌드와 전체 자동 테스트 36개가 통과했다. `HMH_TroopTypes`에서 이동·공격 이동 반복 교체 직전과 직후의 속도 보존, 요청 교체 알림 구분, 도달 불가 명령 거부와 위치 사수 정지를 실제 NavMesh 이동으로 검사했다. 병종 실행 검사 285개와 기존 HMH RTS 실행 검사도 실패 없이 완료했다. 시각적인 조작감 확인은 별도로 필요하다.

- 자동 테스트: `Saved/Automation/ContinuousMove/index.json`
- 연속 이동 검사: `Saved/Logs/HMHContinuousMove.log`
- 기존 RTS 회귀 검사: `Saved/Logs/HMHContinuousMoveRTS.log`

Win64 Development Editor 빌드와 Landship 자동 테스트 31개, HMH 실행 검사 43개가 통과했다. HMH 검사는 장애물 우회·예약·추적·공격 취소·위치 사수·전투 후 이동 재개·경계 목적지·요청 ID 구분에 더해 공통 피해·사망 선택 차단·랙돌 활성화·제거 타이머·실제 지연 제거를 확인한다. Blocked 검사는 경로 추적 실패 결과를 주입하는 방식이다.

- 자동 테스트: `Saved/Automation/CommonCombatReplacement/index.json`
- HMH 로그: `Saved/Logs/HMHCommonCombat.log`
- 성공 기준: `HMH_RTS_FINISHED failures=0`
- 맵 준비: `docs/development/setup-hmh-rts-controls.py`. 기존 액터를 재사용하고 공통 Faction을 설정한다.
- 입력 진단: `LS.RTS.InputDebug 1`. 기본값은 0이다.

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Report/Projects/LandShip/Landship.uproject' /Game/Developers/Minhyeok/HMH -game -unattended -nop4 -nosound -NullRHI -HMHRTSTestExit '-abslog=C:/Report/Projects/LandShip/Saved/Logs/HMHCommonCombat.log'
```

실제 화면에서 인간형 메시, 적 정보 전용 선택과 HP 111/111, 아군 4기 선택과 같은 공통 체력 표시를 확인했다. 자동 검증은 NullRHI 실행을 포함한다. 모든 입력 조합·해상도·팀원 PC·Shipping 패키지를 검증한 것은 아니다.

HMH 종료 시 내비게이션 CleanUp 오류는 이전 병합 전에도 존재한 문제로 남아 있다. HUD의 UUserWidget/WBP 이전, 선택·부대 클래스 분리, 성능 최적화와 테스트 모듈의 배포 분리는 아직 적용하지 않았다. 안개·네트워크·타격 애니메이션은 포함하지 않는다. 병종 전용 맵의 궁병에는 별도로 직선 투사체를 연결했다. 기존 HMH의 인간형 검증 병력은 즉시 피해 모드를 유지한다.

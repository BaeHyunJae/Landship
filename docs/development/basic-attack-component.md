# 공통 기본 공격 컴포넌트

## 구현 범위

`ULSBasicAttackComponent`를 유닛과 적 거점에 기본 부착했다. 공격력·공격 간격·사거리는 기존 `TryGetStats`로 초기화하는 행에서 전달한다. 기존 진영·체력 기능은 구현하거나 교체하지 않는다.

직접 조종 확장 이후 유닛·거점의 교전 및 추적은 ULSOrderComponent에서 관리한다. 선택·명령과 이동의 연결은 rts-controls.md를 따른다. 아래 API는 다른 전투 대상에서 단독 재사용할 때도 사용할 수 있다.

- 자동 모드: `LSUnit`, `LSBuilding` 오브젝트 중 사거리 안의 가장 가까운 유효한 적을 선택한다. 사망·범위 이탈·진영 변경을 검사하며 대상을 다시 선택한다.
- 지정 모드: `StartAttack(Target)`으로 대상을 고정한다. 범위 밖에서는 대기하며 직접 추적 이동하지 않는다. 대상이 무효가 되면 명령을 해제한다.
- 사거리는 두 액터 원점 사이의 3차원 거리다. 외형의 경계 거리나 공격자의 감지 범위를 대신 사용하지 않는다.
- 첫 공격은 준비되면 즉시 적용하고, 이후 공격 간격을 지킨다. 중단·대상 변경·설정 변경으로 기존 쿨타임을 초기화하지 않는다.
- 긴 프레임에서 밀린 공격을 한꺼번에 적용하지 않는다. 실제 공격 시점부터 다음 간격을 계산한다. 검사 주기와 프레임에 따라 판정 시점에 지연이 생길 수 있다.
- 피해 적용 결과가 양수일 때만 `OnAttackPerformed(Target, AppliedDamage)`를 알린다. 피해 요청이 거부돼도 시도한 공격의 쿨타임은 소모한다.
- `StopAttack`, 컴포넌트 비활성화, EndPlay 시 타이머와 대상 참조를 정리한다.

투사체·애니메이션 타격 시점·시야 차단·공격 대상 추적 이동·멀티플레이 복제는 이번 구현에 포함하지 않는다. 현재 공격은 즉시 피해 방식이다.

## 공통 진영·체력 구현과의 연결

유닛·성·거점은 `ILSBasicAttackParticipant`와 팀원의 `ILSFactionInterface`를 함께 구현한다. 기본 공격 인터페이스의 기본 구현은 별도 체력·진영을 소유하지 않고 PR #12의 공통 구현에 위임한다. 생존·HUD 체력은 `ULSHealthComponent::TryGetHealth`, 표적 판정은 `ULSCombatTargetLibrary::CanAutoAttackTarget`, 실제 피해는 `MakeDamageRequest`와 `ApplyDamage`를 사용한다. 인터페이스만 있고 준비된 공통 체력·진영이 없는 액터는 공격을 허용하지 않는다. 단위 테스트의 대역은 해당 인터페이스를 별도로 override한다.

1. `IsCombatAlive`: 체력 기능에서 생존 여부를 조회한다. 공격자와 대상 양쪽에 사용한다.
2. `CanReceiveBasicAttack(Attacker)`: 서로 다른 진영인지, 피해를 허용하는 대상인지 검사한다. 조회만 수행하며 상태를 변경하지 않는다.
3. `ReceiveBasicAttack(Attacker, Damage)`: 팀원의 피해 적용 함수를 호출하고 실제 감소한 체력량을 반환한다. 체력 기능이 피격·사망 알림과 제거 여부를 처리한다. 호출 직전에도 진영·생존 조건을 재검사해야 한다.

공격 컴포넌트가 체력을 별도로 차감하거나 사망 처리를 중복 수행하지 않는다. `OnAttackPerformed`는 피격·사망 알림의 대체가 아니라 공격자 측 연출 연결용이다.

소유 액터의 수치는 컴포넌트 BeginPlay 전에 준비한다. 기본 자동 공격의 첫 검사는 타이머에서 수행하여 다른 컴포넌트의 BeginPlay가 끝날 시간을 확보한다. 그보다 늦게 초기화하는 경우 준비 완료 후 `StartAutoAttack()`을 호출한다.

## 다른 전투 대상에 재사용

1. 액터에 `ULSBasicAttackComponent`를 부착한다.
2. 액터에 위 인터페이스를 연결한다.
3. 수치 초기화 시 `ConfigureAttack(Settings)`에 Damage, Interval, Range를 전달한다. 잘못된 수치는 반영하지 않으며 기존 설정을 유지한다.
4. 자동 탐색 대상의 충돌 컴포넌트는 Query가 활성화돼 있고 Object Type이 `LSUnit` 또는 `LSBuilding`이어야 한다.

단독으로 사용하고 BeginPlay 전에 설정된 컴포넌트는 기본적으로 자동 공격을 시작한다. 유닛·거점은 생성자에서 자동 시작을 끄고 명령 컴포넌트가 교전을 제어한다. 런타임에 생성·설정한 경우 `ConfigureAttack` 후 `StartAutoAttack`을 호출한다. `ConfigureAttack` 자체는 공격 명령을 내리지 않는다.

`GetAttackTarget`, `GetAttackSettings`, `GetCooldownRemaining`은 다른 기능이나 UI에서 조회할 수 있다. `StartAttack`과 `StartAutoAttack`의 성공은 명령 수락을 뜻하며 피해 적용 성공을 보장하지 않는다.

## 검증

`Landship.Combat.BasicAttack`의 Acquisition·Lifecycle·Settings는 독립적인 테스트 대역을 사용하고, StatConnection과 RealHealthIntegration은 실제 BHJ 테스트 BP와 공통 체력 구현을 사용한다.

- Acquisition: 가장 가까운 적 선택, 가까운 아군 제외, 사망 후 대상 변경, 범위 밖 제외, 재진입 시 재획득, 비활성화.
- Lifecycle: 즉시 피해, 공격 알림, 쿨타임, 사거리, 알림 중 재진입 방지, 진영 변경, 대상·공격자 사망, 대상 파괴와 타이머 정리.
- Settings: 0 간격, 음수, NaN, 무한대 거부와 실패 시 기존 수치 유지.
- StatConnection: 실제 테스트 BP를 생성하여 유닛·거점의 공격력·간격·사거리가 데이터 행에서 컴포넌트로 전달되는지 확인.
- RealHealthIntegration: 양 진영의 실제 BP 유닛, 성, 거점을 생성해 조작 권한·표적 판정·공통 체력 감소·HUD 조회·사망 후 선택 및 명령 차단·사망 공격자 타이머 정리를 확인한다. 명령 컴포넌트의 Tick이 없어도 독립 자동 공격 타이머가 켜지지 않는지 검사한다.

기존 검사까지 실행하려면 에디터를 종료한 뒤 아래 명령을 사용한다.

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Report/Projects/LandShip/Landship.uproject' /Engine/Maps/Entry -unattended -nop4 -nosound -NullRHI '-ExecCmds=Automation RunTests Landship' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=C:/Report/Projects/LandShip/Saved/Automation/BasicAttack' '-abslog=C:/Report/Projects/LandShip/Saved/Logs/BasicAttackTests.log'
```

# 전투 공통 계약

## 제공하는 것

- `ULSBasicAttackComponent`는 공격 간격 관리와 즉시 피해 또는 투사체 발사를 맡는다. 접근 이동은 맡지 않는다.
- `ConfigureAttack(FLSBasicAttackSettings)`는 피해·간격·사거리를 받아 유효한 값만 적용하고 성공 여부를 반환한다. 받는 간격은 원본이며 `GetAttackSettings()`도 원본을 반환한다. 실제 간격은 월드의 공격 속도 배율로 한 번 나눈 값이고 `GetEffectiveAttackInterval()`로 조회한다. 같은 설정을 다시 적용해도 진행 중인 쿨타임과 다음 검사 시각을 초기화하지 않는다. 소유자의 수치 읽기와 체력 준비에 성공한 뒤 호출한다.
- 플레이 시작 전의 `ConfigureAttack`은 원본만 준비하므로 성공해도 실행 준비 완료가 아니다. `BeginPlay`에서 월드 배율을 조회한 뒤 `IsConfigured()`가 정해진다. 플레이 중 호출에서 배율을 얻지 못하거나 계산 결과가 0 이하이거나 유한하지 않으면 실패를 반환한다. 1배 대체값이나 자동 재시도는 없다.
- `ConfigureProjectile(FLSProjectileAttackSettings)`는 `ProjectileClass`가 비어 있으면 즉시 피해를, 지정하면 `ALSBasicProjectile`을 생성하는 공격을 쓰게 한다. 속도·수명·반경이 양수가 아니거나 클래스가 추상이면 실패한다.
- `StartAttack(AActor*)`는 지정 대상을 유지하고 사거리 안에 들어오면 공격한다. 사거리 밖 대상도 지정할 수 있다.
- `StartAutoAttack()`은 사거리 안의 가장 가까운 상대 진영 대상을 자동으로 고른다.
- `StopAttack()`은 타이머와 대상을 정리한다. 남은 쿨타임은 초기화하지 않는다.
- 사거리는 양측 충돌 원통 외곽 사이의 거리다(`ULSCombatTargetLibrary::GetAttackDistance`). 첫 공격은 준비되면 즉시 적용하고 이후 실제 간격을 지킨다. 중단·대상 변경·설정 변경으로 쿨타임을 우회하지 않는다. 긴 프레임에서 밀린 공격을 몰아 적용하지 않으며, 프레임보다 빠른 공격 빈도는 보장하지 않는다.
- `OnAttackPerformed(Target, AppliedDamage)`는 실제 감소량이 양수이고 공격자 수명이 유효할 때 공격자 측 연출용으로 발생한다. 투사체 공격에서는 발사가 아니라 명중 뒤에 발생한다. 공통 피격·사망 알림을 대신하지 않는다. `OnProjectileLaunched(Projectile)`는 투사체 발사 직후 발생한다.
- `ULSBasicAttackTuningSubsystem::TryGetBasicAttackSpeedMultiplier(float&)`는 플레이 시작 때 확정한 배율을 반환한다. 배율의 원본은 프로젝트 설정 '기본 공격 속도 배율'(`ULSCombatDeveloperSettings`)이며 유한한 양수만 쓴다. 플레이 중에 바꾼 값은 다음 플레이부터 반영한다. 플레이 중에 생성한 병력·적·거점도 같은 월드의 배율을 쓴다.
- `ALSBasicProjectile::Launch(...)`는 발사 시 지정한 목표만 맞히는 직선 비유도 투사체를 만든다. 이전 위치에서 다음 위치까지 구체를 목표의 질의 충돌 컴포넌트에 스윕하므로 아군·다른 적·다른 건물·지형·장식물에 막히지 않고, 목표의 충돌 형상에 닿아야 명중한다. 피해는 지정 목표에만 공통 체력 컴포넌트로 전달하며 오폭 허용 설정과 관계없이 다른 유닛에는 주지 않는다. 빗나가거나 목표가 이동·제거되면 계속 직진하고 수명이 끝나면 제거된다. 중력은 없다.
- `ALSBasicProjectile::LaunchFirstEnemy(...)`는 목표를 고정하지 않고 이전 위치에서 새 위치까지 `Unit`·`Building`을 스윕해 처음 만난 살아 있는 상대 진영 한 곳에만 피해를 주고 사라진다. 아군·시체·장식물은 무시하고 뒤의 적을 계속 검사한다.
- 발사 시 진영과 피해량을 복사하고 공격자는 약한 참조로 보관하므로, 발사 뒤 공격자가 사라져도 비행과 피해 판정은 계속된다.
- `LSCollisionChannels`(`Combat/LSCollisionChannels.h`)와 `Config/DefaultEngine.ini`의 채널 번호는 함께 관리한다. 모두 Object 채널이다. 기본 응답은 `LSProjectile`만 Ignore이고 나머지는 Block이다.
  - `Unit`: `LSUnit`, GameTraceChannel1
  - `Building`: `LSBuilding`, GameTraceChannel2
  - `PathObstacle`: `LSPathObstacle`, GameTraceChannel3
  - `Projectile`: `LSProjectile`, GameTraceChannel4
  - `Ground`: `LSGround`, GameTraceChannel5, 정적 오브젝트로 등록한다. 지면 선택에 Ground Object Query를 쓸 수 있다.
- 아군·적 판정은 채널이 아니라 진영 규칙(`ILSFactionInterface`, `ULSCombatTargetLibrary::CanAutoAttackTarget`)으로 한다. 자동 표적은 오폭·자기 피해 설정과 관계없이 살아 있는 상대 진영만 고른다.
- `ALSBasicProjectile::Launch()`는 `SpawnActorDeferred` 뒤 `FinishSpawning` 전에 한 번만 부른다. 공격 명령을 중단해도 이미 발사한 투사체는 유지된다.
- `ULSBasicAttackComponent::OnAttackExecutedEvent`는 공격 확정 시점의 기록(`FLSAttackExecution`)을 한 번 전달한다. 근접은 피해 처리 직전(피해 0 포함), 투사체는 발사 성공 직후에 발생한다. 명중은 두 번째 실행이 아니다. 실제 피해를 보장하지 않으므로 소비자는 후속 작업만 큐에 넣고, 처리할 때 약한 참조와 전투 상태를 다시 검증한다.
- `SetAttackTargetResolver(Resolver)`는 준비된 타격 직전에 대상을 새로 조회하게 한다. 콜백은 후보 조회만 하고 이동·공격·지원을 실행하지 않는다. 반환한 대상은 다시 검증하고, null이거나 유효하지 않으면 그 타격의 대기시간을 소비하지 않는다. `StopAttack()`은 바인딩을 유지하고 `EndPlay`가 해제한다. 바인딩하지 않은 컴포넌트의 기존 동작은 유지한다.
- 자동 공격과 신규 공격 시작은 `ULSVisionSubsystem::PassesFriendlySight()`를 통과한 대상에만 한다.

## 동작 조건

- 공격하는 쪽과 맞는 쪽 모두 공통 체력 컴포넌트가 준비되어 있고 진영 인터페이스를 구현해야 한다. 준비되지 않은 대상은 공격 대상이 되지 않는다.
- 명령 컴포넌트가 있는 유닛·거점은 독립 자동 시작을 끄고(`SetAutoAttackOnBeginPlay(false)`) 명령 상태가 교전을 제어한다.
- 프로젝트 설정의 기본 공격 속도 배율이 유한한 양수가 아니면 해당 플레이의 모든 기본 공격이 준비되지 않는다.
- 각 담당자는 자기 컴포넌트의 Object Type과 위 채널 응답을 직접 지정한다. 기존 템플릿 액터의 충돌 종류는 자동으로 바뀌지 않는다.

## 하지 않는 일

- 체력 차감, 사망·랙돌·제거 정책은 `ULSHealthComponent`가 맡는다. 기본 공격 컴포넌트와 투사체는 체력을 직접 깎지 않는다.
- 사거리 밖 대상으로의 접근 이동과 명령 상태는 `Controls`가 맡는다.
- 성의 수동 무장과 그 쿨타임은 `Castle`이 맡으며 공격 속도 배율의 대상이 아니다.
- 투사체는 유도와 중력을 쓰지 않고, 멀티플레이 복제, 애니메이션 타격 시점, 사격 위치 탐색은 제공하지 않는다.

## 공용 테스트 맵

해당 없음.

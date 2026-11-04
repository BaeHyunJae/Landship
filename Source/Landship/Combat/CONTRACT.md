# 전투 공통 계약

## 제공하는 것

- 지속 치유는 `CanClaimHealing`과 `TryClaimHealing`으로 환자별 독점 권한을 확인·획득한다. 다른 컴포넌트가 보유한 활성 권한은 공유하지 않는다. `RestoreClaimedHealth(Healer, Serial, Amount)`는 해당 권한의 실제 회복량을 반환하고 `ReleaseHealingClaim`은 발급 세대가 일치할 때만 해제한다. 풀 초기화는 활성 권한을 지우되 발급 번호를 되돌리지 않아 이전 생명의 해제가 새 권한에 적용되지 않는다. 일반 `RestoreHealth`는 별도 즉시 회복용으로 유지한다.

- `ULSHealthComponent::RestoreHealth(Amount)`는 준비된 생존 대상의 최대 체력 이내 실제 회복량을 반환한다. 잘못된 입력·사망·체력 알림 처리 중에는 0이다. `OnHealedEvent(AppliedHealing, CurrentHealth)`는 회복량이 양수일 때만 발생하며 회복은 피격·사망 알림을 발생시키지 않는다. 풀 정리에서는 회복 구독도 해제한다.

- 풀 병력은 숨김·충돌 중지 상태에서만 `ULSHealthComponent::ResetForPool`과 `ULSBasicAttackComponent::ResetForPool`을 사용한다. 체력과 외부 구독·기본 공격 표적/쿨타임/배율을 새 생명용으로 정리하며 피해/타격 콜백 도중 재초기화는 거부한다. 일반 초기화의 중복 호출 금지는 유지한다.
- 기본 공격 컴포넌트의 생명 세대는 풀 초기화 때 증가한다. 발사체는 발사자의 당시 생명 세대도 보관하므로 같은 액터가 새 병력으로 재사용되면 이전 발사자를 현재 생명으로 해석하지 않는다. 이전 발사체의 공격자 측 명중 알림도 새 생명에 전달하지 않으며 저장된 발사 시 진영과 피해 판정은 유지한다.

- `SetAttackIntervalMultiplier(Source, Multiplier)`는 병력별 출처를 구분해 공격 간격 배율을 적용한다. 1은 해당 출처 제거이며 같은 출처의 재적용은 중첩하지 않는다. 월드 공격속도 배율과 함께 계산하고 대기 중 공격 진행률을 보존한다. `DelayNextAttack`의 경직 보류는 별도로 유지해 배율 해제로 단축하지 않는다.

- `ULSBasicAttackComponent`는 공격 간격 관리와 즉시 피해 또는 투사체 발사를 맡는다. 접근 이동은 맡지 않는다.
- `ConfigureAttack(FLSBasicAttackSettings)`는 피해·간격·사거리를 받아 유효한 값만 적용하고 성공 여부를 반환한다. 받는 간격은 원본이며 `GetAttackSettings()`도 원본을 반환한다. 실제 간격은 월드의 공격 속도 배율로 한 번 나눈 값이고 `GetEffectiveAttackInterval()`로 조회한다. 같은 설정을 다시 적용해도 진행 중인 쿨타임과 다음 검사 시각을 초기화하지 않는다. 소유자의 수치 읽기와 체력 준비에 성공한 뒤 호출한다.
- 플레이 시작 전의 `ConfigureAttack`은 원본만 준비하므로 성공해도 실행 준비 완료가 아니다. `BeginPlay`에서 월드 배율을 조회한 뒤 `IsConfigured()`가 정해진다. 플레이 중 호출에서 배율을 얻지 못하거나 계산 결과가 0 이하이거나 유한하지 않으면 실패를 반환한다. 1배 대체값이나 자동 재시도는 없다.
- `ConfigureProjectile(FLSProjectileAttackSettings)`는 `ProjectileClass`가 비어 있으면 즉시 피해를, 지정하면 `ALSBasicProjectile`을 생성하는 공격을 쓰게 한다. 속도·수명·반경이 양수가 아니거나 클래스가 추상이면 실패한다.
- `StartAttack(AActor*)`는 지정 대상을 유지하고 사거리 안에 들어오면 공격한다. 사거리 밖 대상도 지정할 수 있다.
- `StartAutoAttack()`은 사거리 안의 가장 가까운 상대 진영 대상을 자동으로 고른다.
- `StopAttack()`은 타이머와 대상을 정리한다. 남은 쿨타임은 초기화하지 않는다.
- `DelayNextAttack(Seconds)`는 현재 시각부터 지정한 초 동안 다음 공격을 보류하며 기존 대기시간을 줄이지 않는다. 음수·유한하지 않은 입력·월드 부재·전투 종료는 false이며 변경하지 않는다. 공격 중단·재시작으로 보류를 우회하지 않는다.
- 사거리는 양측 충돌 원통 외곽 사이의 거리다(`ULSCombatTargetLibrary::GetAttackDistance`). 첫 공격은 준비되면 즉시 적용하고 이후 실제 간격을 지킨다. 중단·대상 변경·설정 변경으로 쿨타임을 우회하지 않는다. 긴 프레임에서 밀린 공격을 몰아 적용하지 않으며, 프레임보다 빠른 공격 빈도는 보장하지 않는다.
- `OnAttackPerformed(Target, AppliedDamage)`는 실제 감소량이 양수이고 공격자 수명이 유효할 때 공격자 측 연출용으로 발생한다. 투사체 공격에서는 발사가 아니라 명중 뒤에 발생하며 `Target`은 실제 피해를 받은 대상으로, 같은 진영이나 최초 지정 목표 외의 적도 포함한다. 공통 피격·사망 알림을 대신하지 않는다. `OnProjectileLaunched(Projectile)`는 투사체 발사 직후 발생한다.
- `ULSBasicAttackTuningSubsystem::TryGetBasicAttackSpeedMultiplier(float&)`는 플레이 시작 때 확정한 배율을 반환한다. 배율의 원본은 프로젝트 설정 '기본 공격 속도 배율'(`ULSCombatDeveloperSettings`)이며 유한한 양수만 쓴다. 플레이 중에 바꾼 값은 다음 플레이부터 반영한다. 플레이 중에 생성한 병력·적·거점도 같은 월드의 배율을 쓴다.
- `ALSBasicProjectile::Launch(...)`는 발사 시 목표의 생존 여부를 검사하지만 비행 중에는 목표를 고정하지 않는다. 이전 위치에서 다음 위치까지 구체로 검사해 첫 피해 허용 몸체와 첫 실제 지형 중 먼저 만난 곳에서 멈춘다. 전투 몸체는 `Unit`·`Building`에서 찾으며, 최초 목표 외의 적도 가로막는다. 같은 진영은 오폭 허용 시 명중하고 금지 시 통과한다. 실제 벽·지형은 `LSObstacleProjectile`의 Block 응답으로 막고, Ignore로 설정한 보조 표시·접촉 구는 통과한다. 발사자·시체는 통과하고 중력은 없다. 목표가 이동·제거되어도 계속 직진하며 명중하거나 수명이 끝나면 제거된다. 화살과 터렛 탄은 같은 규칙을 쓴다.
- `ALSBasicProjectile::LaunchInDirection(...)`는 발리스타의 방향 사격이다. 발사자·시체와 피해 정책이 거부한 전투 몸체는 통과하며, 경로의 첫 피해 허용 전투 몸체와 `LSObstacleProjectile`을 막는 실제 지형 중 먼저 만난 곳에서 멈춘다. 투사체 반경·피해 정책·수명은 지정한 값을 유지한다. `LaunchFirstBlocking(...)`는 발사자를 제외하고 지형·같은 진영을 포함해 `LSObstacleProjectile`을 막는 첫 실제 몸체에서 멈추며, 그 대상의 피해는 공통 체력 정책이 결정한다.
- `ULSHealthComponent::EvaluateDamagePolicy(Request, Receiver)`는 현재 오폭·자기 피해 설정만 판정하는 부작용 없는 C++ 함수다. `Applied`는 정책상 허용이라는 뜻이며 실제 피해·체력 준비·입력 유효성을 보장하지 않는다. 거부 시 `RejectedSelfDamageDisabled` 또는 `RejectedFriendlyFireDisabled`를 반환한다. 투사체의 통과 판정과 `ApplyDamage`가 이 규칙을 공유한다.
- `bAllowFriendlyFire`와 `bAllowSelfDamage`의 기본값은 모두 true이며, 질의·피해 시점의 현재 값을 사용한다. 자기 피해는 오폭 설정과 독립적이다. `EnvironmentalHazard`는 같은 진영의 오폭 차단만 우회하며 자기 피해 설정은 유지한다.
- 발사 시 진영과 피해량을 복사하고 공격자는 약한 참조로 보관하므로, 발사 뒤 공격자가 사라져도 비행과 피해 판정은 계속된다.
- `OnDamageApplied(Target, AppliedDamage)`는 실제 감소량이 양수일 때 실제 피격 대상을 전달한다. 같은 진영이나 최초 지정 목표 외의 적도 포함하며 발사자의 생존 여부와 별개다.
- `FLSProjectileAttackSettings::VisualStyle`은 `Arrow`, `TurretBolt`, `BallistaBolt` 중 시각 형태를 정한다. `ALSBasicProjectile::SetVisualStyle`은 Deferred 생성 중에만 적용한다. 형태는 충돌·속도·피해를 바꾸지 않으며 시각 메시를 내비게이션에 등록하지 않는다. `IsInFlight`, `GetFlightOrigin`, `GetFlightDirection`, `GetRemainingFlightTime`과 액터 위치로 실제 비행을 조회한다.
- `FLSDamageRequest`의 `AttackOrigin`, `ImpactLocation`, `IncomingDirection`은 각각의 `bHas...`가 참일 때만 알려진 값이다. 공격 생산자는 실행·명중 시점의 실제 값을 복사한다. 체력 컴포넌트는 모르는 공격 원점을 현재 주체 위치나 월드 원점으로 대체하지 않는다.
- `ULSHealthComponent::HasTakenDamage()`는 양수 피해가 확정된 뒤 참이다. 0 피해와 거부된 요청은 이 값을 바꾸거나 최근 기록을 만들지 않는다. `TryGetLastDamage`는 확정된 최근 피해를 복사하며 기록이 없으면 false를 반환하고 출력을 보존한다. 기록에는 이전·현재·최대 체력, 실제 감소량, 진영, 명시된 공격 기하, 월드 시각과 수신자의 약한 참조가 들어 있다.
- `GetDamageReceiverId()`는 체력 컴포넌트별로 생성 시 부여하는 프로세스 안의 고유한 `int64` 키다. 저장 데이터 식별자가 아니며 액터가 제거된 뒤에도 `FLSDamageResult::ReceiverId`로 같은 피해 수신자의 기록을 연결할 수 있다.
- `FLSDamageResult::bReceiverHiddenAtCommit`은 양수 피해가 확정됐을 때 수신 액터의 `IsHidden()` 값이다. 기본값은 false이며, 이후 액터가 보이거나 숨겨져도 기록을 바꾸지 않는다. 시야·연출 소비자는 이 값과 소비 시점의 시야 상태를 함께 판정한다.
- 네이티브 `OnDamageCommitted(const FLSDamageResult&)`는 양수 피해를 치명 여부와 관계없이 다음 타이머 틱에 알린다. 같은 틱에 전달되는 기록 사이의 순서는 보장하지 않는다. 기존 비치명 `OnDamagedEvent`, 필수 `PrepareDeath`, 공개 `OnDiedEvent`의 순서를 유지하며 치명 기록은 필수 사망 훅이 반환한 뒤 큐에 넣는다. 기록과 확정 시점의 구독 목록을 복사하므로 공격자·피격 대상이 제거되어도 살아 있는 소비자는 기록을 받는다. 소비자는 `AddUObject` 또는 `AddWeakLambda`로 수명에 묶어 구독한다. `RemoveAll`을 호출해도 이미 복사한 구독 목록은 바뀌지 않으므로 살아 있는 소비자는 이미 확정한 기록을 받을 수 있다. 소비자는 콜백에서 자신의 종료 상태를 검사한다. 월드 종료는 남은 타이머를 종료한다.
- 최근 기록과 `OnDamageCommitted`는 `Source`·`Causer`의 약한 참조만 보관한다. 기존 동기 알림과의 호환을 위한 `SourceActor`·`DamageCauser`는 지연 기록에서 비어 있다. 공격자·투사체 제거 후 연출은 보관한 진영과 기하를 사용한다.
- `LSCollisionChannels`(`Combat/LSCollisionChannels.h`)와 `Config/DefaultEngine.ini`의 채널 번호는 함께 관리한다. 1~5는 Object 채널이며 기본 응답은 `LSProjectile`만 Ignore이고 나머지는 Block이다. 6~7은 Trace 채널이다.
  - `Unit`: `LSUnit`, GameTraceChannel1
  - `Building`: `LSBuilding`, GameTraceChannel2
  - `PathObstacle`: `LSPathObstacle`, GameTraceChannel3
  - `Projectile`: `LSProjectile`, GameTraceChannel4
  - `Ground`: `LSGround`, GameTraceChannel5, 정적 오브젝트로 등록한다. 지면 선택에 Ground Object Query를 쓸 수 있다.
  - `ExplosionOcclusion`: `LSExplosionOcclusion`, GameTraceChannel6, 기본 응답 Ignore. 지뢰 폭발을 막는 벽은 Block으로 설정한다.
  - `ObstacleProjectile`: `LSObstacleProjectile`, GameTraceChannel7, 기본 응답 Block. 장애물 지정 탄을 막는 실제 몸체에 사용하며 보조 표시·접촉 구는 Ignore로 설정한다.
- 아군·적 판정은 채널이 아니라 진영 규칙(`ILSFactionInterface`, `ULSCombatTargetLibrary::CanAutoAttackTarget`)으로 한다. 자동 표적은 오폭·자기 피해 설정과 관계없이 살아 있는 상대 진영만 고른다.
- 풀을 쓰지 않는 `ALSBasicProjectile::Launch()`는 `SpawnActorDeferred` 뒤 `FinishSpawning` 전에 한 번만 부른다. 풀에서 대여한 화살은 생성 완료 상태에서 대여당 한 번 호출한다. 공격 명령을 중단해도 이미 발사한 투사체는 유지된다.
- `FLSProjectileAttackSettings::bUsePool`은 `SupportsPooling()`을 명시적으로 지원하는 클래스에 적용한다. 기본 화살은 지원하며 커스텀 파생 클래스와 성 무장은 기본적으로 기존 생성·제거를 유지한다. 파생 클래스가 지원하려면 추가 상태와 비동기 작업을 lifecycle에서 초기화해야 한다. 궁병 기본 화살은 풀을 사용한다.
- `ULSProjectilePoolSubsystem`은 Game·PIE 월드마다 클래스별 비활성 버킷과 GC 소유 참조를 유지한다. `AcquireProjectile(Class, Transform, Owner, Instigator)`는 객체와 발급 세대가 든 `FLSProjectileLease`를 반환한다. 아직 발사는 아니며 호출자는 `Launch` 후 실패하면 같은 핸들을 `ReleaseProjectile(Lease)`에 전달한다. 소유자·Instigator를 지정하면 같은 월드의 유효 객체여야 한다. 기본 공격의 발사자는 기존처럼 피해 요청의 약한 참조로만 보관한다.
- 일반 반환은 같은 월드에서 발급된 활성 대여와 세대 번호가 일치할 때만 성공한다. 대여를 먼저 소비하고 비활성화하므로 중복·재진입·오래된 핸들은 거부한다. 직접 생성·맵 배치 객체를 일반 반환으로 편입하지 않는다. 취소 불가능한 외부 콜백은 획득 당시 핸들을 보관해야 하며 현재 세대를 다시 읽어 반환하면 안 된다.
- 액터의 `PrepareForPool`은 deferred 생성 중에만, `ActivateFromPool`은 대여 발급 후에, `DeactivateForPool`은 대여 소비 후에 호출한다. 기본 화살은 피해 요청·발사자·명중 구독·수명 타이머·속도·특수탄 상태·부착·Owner·Instigator·기하 변환을 초기화하고 Tick·충돌·표시를 끈다. 수명 콜백과 명중 후속 처리는 발사 세대 번호도 확인한다. 머티리얼 인스턴스는 유지하고 새 발사 시 진영과 재질을 갱신한다.
- `PrewarmPool(Class, DesiredInactiveCount)`는 클래스의 비활성 개수를 목표로 한다. `ls.Pool.MaxInactivePerClass`는 클래스별 반환 보관 상한이며 0은 무제한이다. 부족 시 새 액터를 만들고 활성 발사 수는 제한하지 않는다. 반환 초과분과 외부 파괴 객체는 소유·대여 목록에서도 제거한다. 상한 변경 즉시 축소하거나 유휴 trim하지는 않는다. 월드 종료 시 활성 대여를 무효화하고 모든 소유 액터를 제거한다.
- 개발 콘솔 `ls.Pool.Snapshot`은 클래스별 활성·비활성·최고 활성 및 생성·재사용·획득·반환·제거 횟수를 기록한다. `ls.Pool.ResetCounters`는 객체·대여를 유지하면서 현재 활성 수를 기준으로 카운터를 다시 시작한다. 진단 명령과 기록은 Shipping에서 제외한다. 실행 방법은 [개인 화살 풀 점검 절차](../../../docs/procedures/Developers/HMH/projectile-pool.md)를 따른다.
- 자동 전투의 편성 복원은 `ReleaseAll()`로 비행 중인 풀 화살도 반환한다. 외부 코드는 발사 알림의 화살 포인터를 영구적인 사격 ID로 사용하면 안 된다. 반환 뒤 같은 액터가 다른 발사자·진영의 화살로 재사용될 수 있다. 풀 반환은 `OnDestroyed`가 아니므로 명중 연출은 기존 `OnDamageApplied`를 사용한다.
- `ULSBasicAttackComponent::OnAttackExecutedEvent`는 공격 확정 시점의 기록(`FLSAttackExecution`)을 한 번 전달한다. 근접은 피해 처리 직전(피해 0 포함), 투사체는 발사 성공 직후에 발생한다. 명중은 두 번째 실행이 아니다. 실제 피해를 보장하지 않으므로 소비자는 후속 작업만 큐에 넣고, 처리할 때 약한 참조와 전투 상태를 다시 검증한다.
- `FLSAttackExecution`은 주체·목표의 약한 참조, 실행 위치·조준 위치·방향·시각과 주체 진영을 실행 시점에 고정한다. 위치는 이후 이동이나 제거에 따라 갱신되지 않는다.
- `ULSBasicAttackComponent::HasClearAttackPathFromLocation(SourceLocation, Target)`는 후보 액터 중심에서 목표 액터 중심까지 발사 경로를 검사한다. 준비되지 않았거나 입력이 유효하지 않으면 false이며, 준비된 즉시 피해 공격은 유효한 입력에 true다. 투사체 공격은 설정한 충돌 반경으로 `LSObstacleProjectile`을 검사한다. 발사자는 제외하며 목표에 먼저 닿으면 경로가 열려 있다. 체력·진영을 가진 다른 몸체는 기존 `MakeDamageRequest`·`EvaluateDamagePolicy`와 전투 생존 판정을 사용한다. 준비되지 않았거나 죽은 몸체와 피해 정책이 거부한 몸체는 통과한다. 피해를 허용하는 Pawn이 아닌 시설은 경로를 막는다. 상대 `APawn`은 이동 계획의 장애물에서 제외하지만, 오폭을 허용하는 같은 진영 Pawn은 경로를 막는다. 체력·진영이 없는 다른 Pawn도 이동 계획에서 제외하며, Pawn이 아닌 실제 벽·지형은 경로를 막는다. 사거리·목표 자격·시야·쿨타임을 판정하거나 상태를 바꾸지 않는다. 실제 발사체는 이후의 모든 피해 허용 몸체에 명중할 수 있으므로 열린 경로가 명중을 보장하지 않는다.
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

- 풀 반환 시 피격 이력을 비우고 피해 수신 식별자를 새로 부여한다. `OnDamageCommitted` 구독은 컴포넌트 수명 동안 유지하며, 지연된 피해 표시는 수신 식별자가 현재 생명과 일치할 때만 반영한다.

# 공통 기본 공격 컴포넌트

## 공통 전투 코드 사용

PR #12의 체력·진영·사망 처리를 사용한다. 별도 공격 참여 인터페이스와 테스트 전용 체력 판정은 제거했다.

- 표적 판정: `ULSCombatTargetLibrary::CanAutoAttackTarget(Source, Target)`
- 생존 조회: `ULSCombatTargetLibrary::IsCombatAlive(Target)`와 공통 체력 스냅샷
- 피해 요청: `ULSHealthComponent::MakeDamageRequest`
- 실제 피해: 대상의 `ULSHealthComponent::ApplyDamage`
- HUD 조회: `ULSHealthComponent::TryGetHealth`

`ULSBasicAttackComponent`와 `ALSBasicProjectile`은 체력을 직접 차감하거나 사망·랙돌·제거 정책을 다시 구현하지 않는다. 공통 피해 결과의 AppliedDamage를 사용하며, 팀원의 비치명 피격·치명 사망 알림과 필수 사망 훅을 유지한다. 개발용 자기 피해·오폭 설정은 명중 피해 처리의 정책이며 자동 표적은 설정과 관계없이 살아 있는 상대 진영만 선택한다.

## 공격과 명령의 역할

유닛·거점은 DataTable 초기화와 공통 체력 준비에 성공한 뒤 공격력·간격·사거리를 공격 컴포넌트에 전달한다. 명령 컴포넌트가 있는 유닛·거점은 독립 자동 시작을 끄고 명령 상태가 교전을 제어한다. 성에는 기본 공격 무장을 추가하지 않았다.

- `ConfigureAttack`: 유효한 수치만 적용한다. 설정 자체는 공격 명령이 아니다.
- `StartAttack`: 지정 대상을 유지하고 사거리 안에 들어오면 공격한다. 접근 이동은 명령 컴포넌트가 담당한다.
- `StartAutoAttack`: 사거리 안의 가장 가까운 적을 획득하며, 유효한 기존 대상은 유지한다.
- `StopAttack`: 타이머와 대상을 정리한다. 기존 공격 쿨타임은 초기화하지 않는다.
- `OnAttackPerformed`: 실제 감소량이 양수이고 공격자 수명이 유효할 때 공격자 측 연출에 알린다. 투사체 공격은 발사가 아니라 명중 후에 발생한다. 공통 피격·사망 알림의 대체가 아니다.

사거리는 액터 원점 사이의 3차원 거리다. 첫 공격은 준비되면 즉시 적용하고 이후 간격을 지킨다. 중단·대상 변경·설정 변경으로 쿨타임을 우회하지 않는다. 긴 프레임에서 밀린 공격을 몰아 적용하지 않는다. 사망 필수 처리에서 공격 타이머를 해제한다.

발사체 클래스가 비어 있으면 기존 즉시 피해를 사용한다. ConfigureProjectile로 클래스를 지정하면 실제 투사체를 생성한다. 발사 시 피해 요청을 보관하고 명중 시 공통 ApplyDamage로 전달한다. OnProjectileLaunched는 발사 연출, OnAttackPerformed는 실제 피해 연출에 사용한다. 애니메이션 타격 시점·멀티플레이 복제와 사격 위치 탐색은 포함하지 않는다.

## 검증

기본 공격의 자동 테스트 액터도 공통 체력 컴포넌트와 진영 인터페이스를 사용한다. 직접 체력 쓰기, 사망자 부활, 실행 중 진영 덮어쓰기는 제거했다. 시나리오 준비는 피해 API로 수행하고 사망 이후 검사는 새 대상에서 진행한다.

`Landship.Combat.BasicAttack`은 획득·쿨타임·재진입·설정 검증·수치 연결과 실제 BP 통합을 검사한다. 기존 PR의 체력·피해·래그돌·사망·진영 잠금 검사는 그대로 포함된다.

최종 확인: Win64 Development Editor 빌드 성공, Landship 자동 테스트 36개 통과. 결과는 `Saved/Automation/Projectiles/index.json`과 `Saved/Logs/ProjectileTests.log`에 기록한다.


## 투사체 충돌과 수명

ALSBasicProjectile은 발사 시 지정한 목표를 약한 참조로 저장한다. 이전 위치에서 다음 위치까지 구체를 목표의 Query 충돌 컴포넌트에 직접 스윕하므로 아군·다른 적·다른 건물·지형·장식물에 막히지 않는다. 목표의 실제 충돌 형상에 닿아야 명중하며 Visibility 광선을 대신 사용하지 않는다.

지정 목표에만 공통 피해 정책을 적용하고 화살을 제거한다. 오폭 허용 여부와 관계없이 목표 외 유닛에는 피해를 주지 않는다. 공격자 참조는 약하게 보관하고 진영·피해량은 발사 시 복사한다. 명중 전 bResolved를 확정해 재진입·중복 피해를 차단한다. 빗나가거나 대상이 이동·제거되면 계속 직진하고 수명 종료 시 제거된다.

목표 외 유닛 차단 제거 후 Editor 빌드와 전체 자동 테스트 38개가 통과했다. `PassesOtherCombatants`는 앞의 아군·다른 적·전투 건물을 통과해 지정 목표만 타격하는지, 공격자/목표 제거와 오폭 설정 변경에도 의도한 대상만 처리하는지 검사한다. 결과는 `Saved/Automation/ProjectileTargetOnly/index.json`에 있다.

투사체 자동 검사는 실제 이동·구체 스윕·공통 피해 경로를 실행한다. 고정 목표점 직선 방식이며 중력과 유도는 없다.

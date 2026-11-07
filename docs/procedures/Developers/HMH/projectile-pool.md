# 화살 풀 상태 확인

## 언제 쓰는가

자동 전투에서 화살 재사용과 전투 복원 후 수명을 확인하거나 보관 상한을 조정할 때 사용한다.

## 준비

- 변경된 C++ 모듈을 빌드한 에디터와 `Content/Developers/HMH/TABS/L_TABS` 맵.
- 개발 빌드에서 콘솔과 Output Log를 연다. 진단 명령은 Shipping에서 제공하지 않는다.

## 순서

1. 양측에 궁병을 배치하고 전투를 시작한다. 시작 시 첫 일제 사격 수의 비활성 화살을 준비한다.
2. 콘솔에서 `ls.Pool.Snapshot`을 실행하고 Output Log의 `ArrowPool` 행을 확인한다.
3. `ls.Pool.ResetCounters`를 실행해 재사용 관찰 구간을 시작한다.
4. 화살이 명중하거나 빗나가도록 전투를 진행하고 다시 `ls.Pool.Snapshot`을 실행한다.
5. 전투가 끝나면 편성 복원을 실행하고 다시 snapshot을 확인한다.
6. 보관 상한을 조정하려면 `ls.Pool.MaxInactivePerClass <개수>`를 입력한다. 0은 무제한이다. 기존 비활성 목록이 즉시 줄어드는 설정은 아니다.
7. 에디터 플레이를 종료한 후 다시 시작해 이전 플레이의 화살이나 알림이 남지 않는지 확인한다.

### C++ 호출

`AcquireProjectile`은 아직 비행하지 않는 대여를 반환한다. 성공한 핸들에 대해 `Launch`를 한 번 호출한다. 발사 실패 시에도 반환해야 하며 자동 명중·수명 종료는 화살이 직접 반환한다.

```cpp
ULSProjectilePoolSubsystem* Pool = World->GetSubsystem<ULSProjectilePoolSubsystem>();
Pool->PrewarmPool(ALSBasicProjectile::StaticClass(), DesiredInactiveCount);
const FLSProjectileLease Lease = Pool->AcquireProjectile(ALSBasicProjectile::StaticClass(), Transform);
if (ALSBasicProjectile* Arrow = Lease.Projectile.Get())
{
    if (!Arrow->Launch(Request, Target, Direction, Speed, Radius, Lifetime, Material))
        Pool->ReleaseProjectile(Lease);
}
```

필요한 헤더는 `Combat/LSProjectilePoolSubsystem.h`와 `Combat/LSBasicProjectile.h`다. 수동 반환·비동기 작업은 `Lease` 전체를 보관한다. 오래된 객체 포인터에서 현재 세대 번호를 다시 읽으면 이전 작업을 구분할 수 없다. Actor Owner/Instigator가 필요하면 `AcquireProjectile`의 선택 인자로 전달하며, 기본 궁병 공격은 발사자 제거 후에도 비행하도록 기존 약한 피해 출처만 사용한다.

## 성공 확인

- 반복 사격에서 `Reused`와 `Released`가 증가한다. 풀이 비면 `Created`도 증가하며 필요한 발사를 누락하지 않는다.
- 편성 복원 뒤 활성 화살은 없고 이전 전투의 피해가 발생하지 않는다. 대기 화살은 숨김·충돌 없음·Tick 중지 상태다.
- 제한된 보관량을 초과해도 비행 중인 화살은 유지되고, 반환된 초과분만 `Retired`에 반영된다.
- 진단 수치는 실제 생성·대여·반환 횟수다. CPU 시간·GC 회수 시간·FPS·메모리 개선량을 측정한 값이 아니다.

### 수명 검사 항목

네이티브 테스트를 추가하거나 디버거로 확인할 때에는 같은 객체의 반복 획득·반환, 중복 반환, 이전 세대 핸들 반환, 다른 월드·직접 생성 객체 반환 거부, 비활성화 중 재진입, 외부 파괴, 사전 생성 목표, 보관 상한, 플레이 종료·재시작을 구분해서 확인한다. 실제 명중·빗나감·진영 판정도 별도로 확인한다.

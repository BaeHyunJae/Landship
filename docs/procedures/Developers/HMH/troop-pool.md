# 자동 전투 병력 풀 확인

## 언제 쓰는가

TABS 편성의 창병·궁병·기병 재사용과 사망 후 새 생명의 상태를 확인할 때 사용한다.

## 준비

- 변경된 C++ 모듈을 빌드한 에디터와 `Content/Developers/HMH/TABS/L_TABS` 맵.
- 개발 빌드 콘솔과 Output Log. `ls.TroopPool.Snapshot`은 Shipping에서 제공하지 않는다.

## 순서

1. 창병·궁병·기병을 배치하고 `ls.TroopPool.Snapshot`을 실행한다.
2. 편성에서 각 병종을 삭제한 뒤 같은 병종을 다시 배치하고 snapshot을 비교한다.
3. 병력을 싸우게 해 기병 돌격·창병 방어·궁병 피격 디버프와 사망 랙돌이 발생하게 한다.
4. 편성 복원을 실행하고 snapshot 및 병력의 초기 체력·이동·외형을 확인한다.
5. 다시 전투를 시작해 각 병종의 일회성 행동을 새로 사용할 수 있는지 확인한다.
6. 반복 배치·삭제·복원을 진행하고 클래스·병종별 보관량을 확인한다.
7. 필요하면 `ls.TroopPool.MaxInactivePerBucket <개수>`로 반환 보관 상한을 조정한다. 0은 무제한이며 기존 보관 목록을 즉시 축소하지는 않는다.
8. 플레이를 종료하고 다시 시작해 이전 플레이의 병력·시체·AI·타이머가 남지 않는지 확인한다.

### C++ 호출

```cpp
ULSTroopPoolSubsystem* Pool = World->GetSubsystem<ULSTroopPoolSubsystem>();
Pool->PrewarmPool(ALSTroopUnit::StaticClass(), Type, DesiredInactiveCount);
const FLSTroopLease Lease = Pool->AcquireUnit(
    ALSTroopUnit::StaticClass(), Type, Faction, Transform, Owner);
if (ALSTroopUnit* Unit = Lease.Unit.Get())
{
    // 새 생명의 초기화가 완료된 병력이다. 편성에서 제거할 때 당시 핸들을 사용한다.
    Pool->ReleaseUnit(Lease);
}
```

필요한 헤더는 `Troops/LSTroopPoolSubsystem.h`다. 같은 액터가 다시 대여되어도 과거 핸들의 반환은 실패한다. 일반 맵 배치 액터와 RTS 생산은 기존 수명을 유지하며 자동 편입 API는 제공하지 않는다. 성과 커스텀 병력/AI 클래스도 이번 적용 범위에 포함하지 않는다.

## 성공 확인

- 같은 병종을 반복 삭제·배치하면 `Reused`가 증가하고 필요한 수량이 비활성 보관량보다 많으면 `Created`가 증가한다.
- 비활성 병력은 숨김·충돌 없음·이동/AI/메시 Tick 중지 상태이며 시야 참여 목록에서 제외된다.
- 재대여 시 초기 체력과 병종 수치를 사용하고, 랙돌 메시가 캡슐에 다시 붙으며 이전 표적·쿨타임·디버프·일회성 행동 사용 여부가 남지 않는다.
- 사망 제거는 시체 유지 시간 후 풀 반환으로 처리된다. 즉시 제거 설정도 현재 피해/사망 콜백을 빠져나온 뒤 반환한다.
- 중복 반환, 과거 세대 반환, 다른 월드·미발급 액터 반환은 거부한다. 피해/타격/명령 업데이트 중 반환도 거부하므로 해당 호출 스택을 빠져나온 후 같은 핸들로 다시 요청해야 한다.
- 상한 초과분은 반환될 때 병력과 AI를 제거하며 활성 편성을 제한하지 않는다.
- 진단은 객체 생성·대여·반환 횟수다. CPU 시간·GC 시간·FPS·메모리 절감량을 측정한 값은 아니다.

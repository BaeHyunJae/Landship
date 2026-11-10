# Landship 코딩 규칙

[Epic C++ 코딩 표준](https://dev.epicgames.com/documentation/en-us/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)을 기본으로 따른다. 폴더 구조와 담당은 [`CONTRIBUTING.md`](../CONTRIBUTING.md)를 따른다.

## 이름

- 이 프로젝트에서 만드는 모든 C++ 타입에 `LS` 접두사를 붙인다. 언리얼 타입 접두사(`A`, `U`, `F`, `E`, `I`, `T`) 바로 뒤에 밑줄 없이 이어 쓴다.
  - 예: `ALSCastle`, `ULSHealthComponent`, `FLSDamageInfo`, `ELSFaction`, `ILSDamageable`
- 파일 이름은 언리얼 타입 접두사를 뺀 이름으로 한다. 예: `LSCastle.h`, `LSHealthComponent.cpp`
- `LS`는 C++ 타입에만 붙인다. 에셋은 종류 접두사만 쓴다. 예: `ALSCastle`을 부모로 한 블루프린트는 `BP_Castle`
- 에셋 종류 접두사는 [Epic의 에셋 이름 규칙 문서](https://dev.epicgames.com/documentation/unreal-engine/recommended-asset-naming-conventions-in-unreal-engine-projects)를 따른다. 예: `BP_`, `WBP_`, `M_`, `IA_`

## 소스 폴더와 include

- 새 폴더를 만들면 `Landship.Build.cs`의 `PublicIncludePaths`에 경로를 추가한다.

## C++과 블루프린트의 경계

판단 기준: 여러 기능 폴더가 호출하거나 두 사람이 함께 고치는 로직은 병합할 수 있는 C++에 두고, 한 기능 안에서만 쓰는 연출과 에셋 연결은 블루프린트에 둔다. 블루프린트는 바이너리라 충돌이 나면 한쪽을 버려야 하기 때문이다.

- UI는 위젯이 쓰는 데이터와 동작을 C++ 위젯 클래스(`UUserWidget` 파생)에, 배치와 디자인을 위젯 블루프린트(`WBP_`)에 둔다.
- 나머지는 담당자가 위 기준으로 고른다.

## 조정할 수치

- 사거리, 쿨타임, 체력처럼 플레이하며 조정할 값은 코드에 직접 쓰지 않고 `UPROPERTY(EditDefaultsOnly)`로 노출한다.
- 같은 형식의 값이 여러 개인 데이터(병종 스탯 등)는 DataTable로 모은다. 행 구조체는 `FTableRowBase`를 상속하고 이름 끝에 `Row`를 붙인다. 예: `FLSTroopStatRow`

## 주석

- 주석은 한국어로 쓴다.
- 주석에는 코드만 읽어서는 알 수 없는 것을 쓴다. 분명하지 않은 판단, 제약, 순서 의존, 객체의 수명과 소유, 코드가 기대는 엔진 동작, 의도한 예외가 그렇다. 주석을 지운 뒤 코드만 읽어도 같은 내용을 알 수 있으면 그 주석은 코드를 되풀이한 것이다. 이름이나 조건이 분명하지 않아서 주석이 필요하면 이름을 고치거나 조건을 함수로 뺀다.
  - 나쁜 예: `if (Health <= 0) { Die(); }` 위의 `// 체력이 0 이하면 사망 처리한다.`
- 클래스와 함수의 `/** */` 주석에는 호출하는 쪽이 알아야 할 계약을 쓴다. 범위, 전제 조건, 반환값과 실패할 때의 동작, 부작용, 단위와 유효 범위가 계약이다. 이 항목 중 쓸 것이 하나도 없는 함수에는 쓰지 않는다. 계약은 선언(`.h`)에, 구현한 이유는 정의(`.cpp`)에 둔다.
  - 예: `RequestMove`의 `@return`에는 "요청을 받아들였으면 true. 이동이 끝났다는 뜻은 아니다"라고 쓴다.
- `UPROPERTY`와 `UFUNCTION` 위의 `/** */` 주석은 에디터 툴팁이 된다. 에디터에서 값을 조정하는 사람이 읽을 설명으로 쓴다.
- 코드를 고치면 그 코드를 설명하는 주석도 고치거나 지운다. 성능, 메모리, 스레드 안전성은 확인한 것만 쓴다.
- 주석에는 지금 코드의 사실만 쓴다. 누가 언제 왜 고쳤는지는 Git 기록(`git blame`)에서 확인한다.
- 할 일 주석에는 그 일을 추적하는 Task ID를 붙인다. 예: `// @TODO(TASK-42) 유닛 수가 늘면 전체 순회 대신 공간 분할 검색으로 바꾼다.`
- 이유를 설명하면서 ADR 번호로 인용할 수 있다(형식은 CONTRIBUTING.md의 'ADR'을 따른다). 번호를 빼고 읽어도 주석만으로 이유가 이해되게 쓴다. 주석에서 무엇을 참조할 수 있는지는 CONTRIBUTING.md의 '팀원이 읽는 글'을 따른다.

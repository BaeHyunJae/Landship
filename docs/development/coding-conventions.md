# Landship 코딩 규칙

[Epic C++ 코딩 표준](https://dev.epicgames.com/documentation/en-us/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)을 기본으로 따른다. 이 문서에는 표준이 정해 주지 않아 팀이 따로 정한 것만 적는다. 폴더 구조와 담당은 [`CONTRIBUTING.md`](../../CONTRIBUTING.md)를 따른다.

## 타입 이름

- 이 프로젝트에서 만드는 모든 C++ 타입에 `LS` 접두사를 붙인다. 언리얼 타입 접두사(`A`, `U`, `F`, `E`, `I`, `T`) 바로 뒤에 밑줄 없이 이어 쓴다.
  - 예: `ALSCastle`, `ULSHealthComponent`, `FLSDamageInfo`, `ELSFaction`, `ILSDamageable`
- 파일 이름은 언리얼 타입 접두사를 뺀 이름으로 한다. 예: `LSCastle.h`, `LSHealthComponent.cpp`
- `LS`는 C++ 타입에만 붙인다. 에셋은 종류 접두사만 쓴다. 예: `ALSCastle`을 부모로 한 블루프린트는 `BP_Castle`
- 템플릿이 만든 클래스(`Landship*`, `Strategy*`, `TwinStick*`)는 이름을 바꾸지 않는다.

## 소스 폴더와 include

- 코드는 담당 기능 폴더(`Source/Landship/<기능>/`)에 둔다.
- 새 폴더를 만들면 `Landship.Build.cs`의 `PublicIncludePaths`에 경로를 추가한다. `Build.cs`는 여러 기능이 함께 기대는 파일이므로 고치기 전에 상대에게 알린다.

## C++과 블루프린트의 경계

판단 기준: 여러 기능이 함께 쓰거나 자주 바뀔 로직은 병합할 수 있는 C++에 두고, 한 기능 안에서만 쓰는 연출과 에셋 연결은 블루프린트에 둔다. 블루프린트는 바이너리라 충돌이 나면 한쪽을 버려야 하기 때문이다.

- 전투 공통 규칙(`Combat/`)은 C++로 만든다.
- UI는 위젯이 쓰는 데이터와 동작을 C++ 위젯 클래스(`UUserWidget` 파생)에, 배치와 디자인을 위젯 블루프린트(`WBP_`)에 둔다.
- 나머지는 담당자가 위 기준으로 고른다.

## 조정할 수치

- 사거리, 쿨타임, 체력처럼 플레이하며 조정할 값은 코드에 직접 쓰지 않고 `UPROPERTY(EditDefaultsOnly)`로 노출한다.
- 같은 형식의 값이 여러 개인 데이터(병종 스탯 등)는 DataTable로 모은다. 행 구조체는 `FTableRowBase`를 상속하고 이름 끝에 `Row`를 붙인다. 예: `FLSTroopStatRow`

## 주석

- 주석은 한국어로 쓴다.
- 코드가 무엇을 하는지보다 왜 그렇게 했는지를 쓴다.
- 누가 언제 고쳤는지는 Git 기록(`git blame`)으로 확인한다.
- `UPROPERTY`와 `UFUNCTION` 위의 `/** */` 주석은 에디터 툴팁이 된다. 에디터에서 값을 조정하는 사람이 읽을 설명으로 쓴다.

# Landship 협업 규칙

두 사람이 이 저장소 하나에서 브랜치와 PR로 작업한다. 이 문서가 코드 협업 규칙의 원본이다.

## 저장소

- 이 저장소 하나에서 작업하고 fork는 쓰지 않는다.
- 저장소는 퍼블릭으로 운영한다. 상업 전환 전에 비공개로 바꾼다.
- `Content/` 아래 파일은 모두 Git LFS로 관리한다(`.gitattributes`). 처음 클론하기 전에 `git lfs install`을 한 번 실행한다.

## 브랜치와 병합

| 브랜치 | 용도 |
| --- | --- |
| `main` | 단계를 마친 결과를 모으는 브랜치. 통합 브랜치의 단계가 끝날 때 한 번 병합받는다 |
| `prototype` | 현재 통합 브랜치(첫 전투 프로토타입). 저장소 기본 브랜치다. 문서·설정을 포함한 모든 작업 브랜치는 여기서 나와 여기로 병합한다 |
| `기능/작업` | 작업 브랜치. 앞부분은 주로 고치는 기능 폴더 이름이다. 예: `castle/move`, `vision/fog-of-war`, `combat/damage` |

- 기능 폴더 밖(문서, 설정)을 고치는 브랜치는 앞부분에 커밋 타입을 쓴다. 예: `docs/coding-conventions`, `chore/build-settings`
- `main`과 `prototype`에는 직접 push하지 않고 PR로만 병합한다.
- 통합 브랜치를 새로 열면 저장소 기본 브랜치를 그 브랜치로 바꾸고, 단계가 끝나 `main`에 병합하면 기본 브랜치를 `main`으로 되돌린다. 작업은 늘 기본 브랜치에서 시작하므로 `main`을 통합 브랜치로 옮겨 오는 동기화 PR은 열지 않는다.
- 병합 방식은 스쿼시 병합만 쓴다. 통합 브랜치에는 PR 하나가 커밋 하나로 들어가므로, 문제가 생기면 그 커밋만 되돌려 확인할 수 있다. 작업 브랜치의 중간 커밋은 그대로 남는다.

## Task와 PR

- 개발 Task 하나에 PR 하나를 연다.
- PR 제목 끝에 Notion Task ID를 괄호로 붙인다. 예: `feat(castle): 성 클릭 이동 (TASK-40)`
- PR 주소는 Notion Task의 ‘산출물 링크’ 속성에 넣는다.
- PR 병합은 Task 완료와 같지 않다. Task 담당자가 Notion Task의 완료 기준을 확인한 뒤 직접 Done으로 바꾼다. 상태 변경은 자동화에 맡기지 않는다. Done 조건은 Notion의 프로젝트 사용 및 운영 가이드를 따른다.
- 구현 범위와 완료 기준은 Notion의 해당 Task가 원본이다.

## 커밋과 PR 제목

- PR 제목은 [Conventional Commits](https://www.conventionalcommits.org/ko/v1.0.0/) 형식으로 쓰고, 범위(scope)에 주로 고친 기능 폴더 이름을 적는다. 기능 폴더 밖만 고쳤으면 범위를 생략한다.
  - 형식: `타입(기능): 요약 (TASK-번호)`. 예: `docs: 코딩 규칙 추가 (TASK-38)`
  - 타입: `feat`(기능), `fix`(버그 수정), `refactor`(동작 변화 없는 구조 변경), `docs`(문서), `chore`(설정·빌드), `test`(테스트)
- 스쿼시 병합할 때 커밋 메시지 입력창에 PR 제목과 같은 형식으로 쓴다. 이 메시지가 통합 브랜치에 남는 커밋이다.
- 작업 브랜치 안의 중간 커밋은 형식을 강제하지 않는다.

## 폴더와 담당

언리얼 에셋(`.uasset`, `.umap`)은 병합할 수 없다. 같은 파일을 여러 사람이 동시에 고치지 않도록 폴더마다 담당을 정한다.

- 폴더는 Notion의 Feature 단위로 나눈다. 여러 Feature가 함께 쓰는 전투 규칙만 `Combat/`에 둔다. 콘텐츠와 소스에 같은 이름의 폴더를 둔다.
- 폴더별 담당은 폴더 이름이 아니라 [`.github/CODEOWNERS`](.github/CODEOWNERS)에 적는다. 업무 분장이 바뀌면 이 파일만 고친다. 담당 폴더를 건드린 PR에는 담당자가 리뷰어로 자동 지정된다.

```
Content/Landship/          Source/Landship/
├── Combat/     전투 공통 규칙: 진영, 체력·피해, 기본 공격, 전투 수치, 쿨타임
├── Castle/     성 이동, 무장 조준·발사
├── Troops/     병력 투입, 병종, 교전 반응
├── Controls/   카메라, 선택·명령
├── Vision/     전장의 안개, 미니맵
├── Enemies/    적 거점, 적 분대
├── Obstacles/  경로 장애물: 터렛, 지뢰, 바리케이드
├── Maps/       전장 맵, 적 배치안
├── Match/      승패 판정
└── HUD/        HUD, 전투 알림

Content/Developers/(이니셜)/   개인 테스트 맵과 에셋. 예: BHJ
```

- 담당이 아닌 폴더의 파일을 바꿔야 하면 직접 고치지 않고 담당자에게 요청한다. 담당자가 고쳐서 PR을 올린다.
- 담당이 여러 명인 폴더(`Combat/` 등)와 여러 기능이 함께 기대는 파일(`Config/*.ini`, `*.uproject`, `Source/**/*.Build.cs`, `Source/*.Target.cs`)은 고치기 전에 팀원에게 알려 동시에 같은 파일을 열지 않게 한다.
- 새 기능이 생겨 폴더를 추가할 때는 `CODEOWNERS`에 담당을 함께 적는다.
- 템플릿이 만든 폴더와 코드(`TopDown/`, `Variant_*/`, `Characters/`, `LevelPrototyping/`, `Landship*` 클래스 등)는 참고용으로 두고 고치지 않는다. 쓰려면 담당 기능 폴더로 복사한다.
- 에셋 이름은 언리얼 커뮤니티 표준 접두사를 따른다. 예: `BP_`, `WBP_`, `M_`, `IA_`. 코딩 규칙은 [`docs/development/coding-conventions.md`](docs/development/coding-conventions.md)를 따른다.

## PR 리뷰

- PR을 열면 팀원를 리뷰어로 지정한다. `CODEOWNERS`로 자동 지정됐으면 따로 지정하지 않아도 된다.
- 충돌을 해결했으면 작성자가 바로 병합한다. 프로토타입 기간에는 빠른 병합을 우선한다.
- 코드 리뷰 의견은 PR에 남긴다.

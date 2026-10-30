# HMH 기반 기능 실험

## 실행

`/Game/Developers/Minhyeok/HMH`를 열고 Play를 누른다. 이 레벨에만 `LSGameMode`, `HMH_SkeletonProbe`, `HMH_TestCamera`를 적용한다. 공용 맵과 기본 실행 설정은 바꾸지 않는다.

테스트 액터가 시작 다음 프레임에 한 번 검사한다. 화면에 `HMH Skeleton: 21/21 PASS`가 표시되면 모두 통과다. 세부 항목은 Output Log에서 `HMH Skeleton`으로 필터링한다. 다시 실행하려면 Stop 후 Play한다.

## 검사 내용

- 실제 레벨의 게임 상태와 플레이어 컨트롤러가 LS 기반 클래스인지 확인한다.
- 테스트 액터가 `OnPopulationChanged`를 직접 구독한다. 초기화, 증가, 감소와 상한 변경에서 이벤트 횟수와 값을 확인한다.
- 같은 값 지정, 상한 초과, 음수와 유효하지 않은 상한 변경에서는 추가 이벤트가 없는지 확인한다. 검사가 끝나면 원래 인구수를 복원한다.
- 다섯 Object Channel을 각각 설정한 QueryOnly 박스에 Object Line Trace가 맞는지 확인한다.
- 구체 Sweep을 Projectile 채널로 수행하여 각 박스의 Block 반응을 확인한다. Unit 박스를 Ignore로 전환하면 통과하는지도 확인하고 원복한다.
- 청록 박스는 검사 대상, 노란 선은 검사 경로다. 관찰용 카메라에서 전체 배치를 볼 수 있다.

이는 기반 API와 실제 쿼리 응답 실험이다. 유닛 소환, 실제 투사체 이동, 물리 밀어내기, 진영/피해 판정, UI 위젯 연결까지 검증한 것은 아니다. 유닛이나 전투 구현 없이 실행할 수 있다.

## 재설정 스크립트

`setup-hmh-tests.py`는 언리얼 Python commandlet에서 실행한다. PythonScriptPlugin은 실행 인자로만 활성화하며 프로젝트 플러그인 설정은 변경하지 않는다. 레벨을 수정하기 전에 에디터에서 저장하고 닫는다. 기존 테스트 액터와 카메라가 있으면 중복 생성하지 않는다.

실험 전 원본 백업은 `Saved/HMHBackup/HMH-before-tests.umap`에 있다. 이 경로는 Git에 포함되지 않는다.

## 확인 결과

261030: UE 5.8.4 에디터 빌드 성공. 저장된 HMH를 `-game -NullRHI -HMHSkeletonTestExit`로 실제 게임 월드에서 실행하여 21/21 PASS와 종료 코드 0을 확인했다. 기록은 `Saved/Logs/HMHSkeletonRuntime.log`의 `[HMH Skeleton]` 항목에 있다. 이후 에디터 PIE에서도 21/21 PASS 화면과 다섯 박스/검사 경로 표시를 확인했다. 증빙은 `Saved/HMHSkeleton-PIE.png`다. 실제 병력 전투 AC의 완료를 의미하지 않는다.

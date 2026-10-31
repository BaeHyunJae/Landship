# 이동 성 프로토타입

`ALSMobileCastle`은 `ALSUnit`을 상속하는 대형 전투 유닛이다. 기존 `ALSCastle`과 공용 성 데이터는 변경하지 않는다. 시설·무장 조합을 구현하기 전에 병력과 같은 입력으로 성의 이동·교전을 시험하는 목적이다.

## 조작과 외형

좌클릭 선택, 지면 우클릭 이동, 적 우클릭 지정 공격, A 공격 이동, S 정지, H 위치 사수, Shift 명령 예약과 부대 지정을 공통으로 사용한다. 선택 분류는 성이며 병력과 함께 드래그해 동시에 조종할 수 있다. 일반 혼합 이동은 성과 병력이 같은 클릭 지점으로 집결하고 충돌 크기와 RVO 회피에 따라 주변에 모인다. 적 성은 정보 확인만 가능하다.

몸통과 두 탑을 큐브로 표현한다. 아군은 파랑, 적군은 주황이다. 시각 메시에는 충돌을 두지 않고 반경 142cm·높이 284cm 캡슐로 이동·피격·선택을 처리한다.

## 시험 수치

`DT_TroopStats`의 `MobileCastle` 행을 사용한다. 체력 1000, 시야 1600cm, 이동 속도 200cm/s다. 기존 기본 공격·감지 수치는 0이며 수동 무장 설정은 Weapons 컴포넌트에서 관리한다. 사거리는 기존 공격 계약과 동일하게 액터 중심 간 거리다. 수치는 밸런스 확정값이 아니다.

기본 공격은 제거했으며 Q 일반 대포·W 산탄총·E 발리스타의 수동 조준 무장을 사용한다. 상세 규칙은 castle-weapons.md를 따른다. 체력·진영·사망은 기존 공통 컴포넌트를 그대로 사용한다. 파괴 시 이동·공격·충돌을 차단하고 성 객체는 보존한다.

## 이동과 검증 맵

`HMH_TroopTypes`에 아군·적 성을 배치한다. 기존 이동 실험 성은 이 복사본에서만 제거한다. 기존 HMH와 팀원 테스트 맵은 수정하지 않는다.

전용 맵의 NavMesh는 반경 142cm·높이 284cm로 준비한다. 현재는 성과 병력이 이 넓은 통로 기준을 공유한다. 병종별 좁은 길 사용, 이동하는 성 주변 군중 회피와 진형 밀집 해소는 별도 작업이다. 성 캡슐로 NavMesh를 매 프레임 깎지는 않는다.

맵과 데이터 준비는 `setup-hmh-troop-types.py`로 수행한다. `Landship.Castle.MobileUnit`은 초기 수치·대형 충돌·아군/적 조작·공통 근접 피해·사망 후 보존을 검사한다. `-HMHTroopTypesTestExit`는 성의 실제 벽 우회·반복 이동 속도 보존·추적 후 공격을 병종 검사와 함께 실행한다.

Editor 빌드 성공, 전체 자동 테스트 37개 통과. 실행 검사도 `HMH_TROOPS_FINISHED failures=0`으로 완료했다. 자동 테스트 결과는 `Saved/Automation/MobileCastle/index.json`, 실제 맵 실행 로그는 `Saved/Logs/HMHMobileCastleFinal.log`에 있다. 내비게이션 초기화는 일반 코드인 Navigation/LSNavigationSetup이 담당한다. 전용 맵에 이 액터를 배치하고 반경 142cm·높이 284cm를 설정한다. Probe는 경로 생성이나 게임 초기화에 관여하지 않는다. 다른 맵에는 해당 맵에 맞는 내비게이션을 별도로 준비해야 한다.

혼합 드래그 변경 후 빌드와 `Landship.Controls.SelectionAndGroups`가 통과했다. Shift 성 선택·해제와 혼합 부대 복원을 확인했으며, 실제 맵에서 성·병력 양쪽의 이동 명령 수신과 성 크기에 맞춘 목적지 간격도 검사했다. 결과는 `Saved/Automation/MixedCastleSelection/index.json`, `Saved/Logs/HMHMixedCastleSelection.log`에 기록한다.

## 게임 코드와 검증 코드 구분

성·병력·공격·명령·내비게이션 준비는 각각 일반 기능 폴더에 둔다. `Troops/Tests/LSHMHTroopProbe`에는 실행 검사만 남긴다. `HMHTroopTypesTestExit` 옵션이 없는 일반 플레이에서는 Probe가 즉시 제거된다. Probe는 `IsEditorOnly()`를 통해 쿠킹 제외 대상으로 지정하며 Shipping에서는 검사 옵션을 활성화하지 않는다. 클래스 자체를 별도 테스트 모듈로 분리한 것은 아니다.

분리 후 Editor 빌드와 전체 자동 테스트 38개가 통과했다. `Landship.Navigation.ProbeIsolation`은 일반 실행에서 Probe 제거와 쿠킹 제외 지정을 확인한다. 실제 맵 실행 검사도 실패 없이 완료했다. 결과는 `Saved/Automation/NavigationSeparation/index.json`과 `Saved/Logs/HMHNavigationSeparation.log`에 있다. 실제 Shipping 패키징은 검증하지 않았다.

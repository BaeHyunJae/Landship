#if WITH_DEV_AUTOMATION_TESTS
#include "HUD/LSBattleHUDWidget.h"
#include "HUD/Tests/LSCommandTestProvider.h"
#include "Controls/LSSelectableComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Castle/LSCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSHealthComponent.h"
#include "Components/ProgressBar.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Controls/LSPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleHUDContractTest, "Landship.HUD.BattleWidget.StatesAndRequests",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSBattleHUDContractTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = Play.GetTestWorld();
	auto* PC = World->SpawnActor<ALSPlayerController>();
	auto* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 144), FRotator::ZeroRotator);
	if (!Play.BeginPlayInTestWorld())
		return false;
	auto* Player = NewObject<ULocalPlayer>(GEngine);
	Player->PlayerController = PC;
	PC->Player = Player;
	UClass* Class = LoadClass<ULSBattleHUDWidget>(nullptr, TEXT("/Game/Landship/HUD/UI/WBP_BattleHUD.WBP_BattleHUD_C"));
	if (!TestNotNull(TEXT("실제 WBP 로드"), Class))
		return false;
	auto* Widget = CreateWidget<ULSBattleHUDWidget>(PC, Class);
	if (!TestNotNull(TEXT("위젯 생성"), Widget))
		return false;
	const auto* SelectionLabel = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("SelectionText")));
	TestTrue(TEXT("저장·재로드 후에도 폰트 에셋 유지"),
	         SelectionLabel && SelectionLabel->GetFont().FontObject != nullptr);
	Widget->RefreshState();
	TestTrue(TEXT("선택 없을 때 안내"), Widget->GetSelectionDescription().ToString().Contains(TEXT("선택하세요")));
	UWidget* CastleStatusPanel = Widget->GetWidgetFromName(TEXT("CastleStatusPanel"));
	if (!TestNotNull(TEXT("상시 성 상태 패널"), CastleStatusPanel)) return false;
	TestTrue(TEXT("전장 설정이 없는 기존 맵에서는 상시 성 패널 숨김"),
	    CastleStatusPanel->GetVisibility() == ESlateVisibility::Collapsed);
	PC->ApplySelection({Castle});
	Widget->RefreshState();
	TestTrue(TEXT("기존 맵에서 성을 선택해도 상시 성 패널을 만들지 않음"),
	    CastleStatusPanel->GetVisibility() == ESlateVisibility::Collapsed);
	TestTrue(TEXT("성의 선택·체력 정보"), Widget->GetSelectionDescription().ToString().Contains(TEXT("1000 / 1000")));
	auto* Button = Cast<UButton>(Widget->GetWidgetFromName(TEXT("Command5")));
	if (!TestNotNull(TEXT("WBP의 실제 무장 버튼"), Button))
		return false;
	TestTrue(TEXT("준비된 무장 버튼 활성"), Button->GetIsEnabled());
	Button->OnClicked.Broadcast();
	TestTrue(TEXT("버튼은 컨트롤러의 조준 요청 호출"), PC->GetPendingInput() == ELSCommandInput::CastleWeapon);
	TestEqual(TEXT("조준만으로 쿨타임 차감 없음"), Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon),
	          0.0f);
	TestTrue(TEXT("기존 시전 요청으로 발사"),
	         PC->ConfirmCastleWeapon(FVector(200, 0, 0)) == ELSWeaponCastResult::Ready);
	const float Duration = Castle->Weapons->GetCooldownDuration(ELSCastleWeapon::Cannon);
	Castle->Weapons->UpgradeWeapon(ELSCastleWeapon::Cannon);
	TestEqual(TEXT("강화 중 쿨타임 표시 분모 보존"), Castle->Weapons->GetCooldownDuration(ELSCastleWeapon::Cannon),
	          Duration);
	Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Cannon, false);
	Widget->RefreshState();
	TestFalse(TEXT("비활성 무장 버튼 차단"), Button->GetIsEnabled());
	const FString Description =
	    Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("CommandState5")))->GetText().ToString();
	TestTrue(TEXT("비활성과 남은 쿨타임 모두 표시"),
	         Description.Contains(TEXT("비활성")) && Description.Contains(TEXT("초")));
	Castle->Weapons->SetWeaponUnlocked(ELSCastleWeapon::Cannon, false);
	Widget->RefreshState();
	TestTrue(TEXT("잠금 표시"), Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("CommandState5")))
	                                ->GetText()
	                                .ToString()
	                                .Contains(TEXT("잠김")));
	TestNotNull(TEXT("생산 진행 막대"), Widget->GetWidgetFromName(TEXT("CommandProgress10")));
	TestNotNull(TEXT("발견 알림 위젯"), Widget->GetWidgetFromName(TEXT("DiscoveryPanel")));
	TestNotNull(TEXT("상시 성 상태 패널"), Widget->GetWidgetFromName(TEXT("CastleStatusPanel")));
	TestNotNull(TEXT("상시 성 체력 진행 막대"), Widget->GetWidgetFromName(TEXT("CastleHealthProgress")));
	PC->ApplySelection({});
	Widget->RefreshState();
	TestTrue(TEXT("선택 해제 시 고정 격자를 비움"), PC->CommandCard->BuildCard()[5].Entry.Id.IsNone());
	int32 ButtonCount = 0;
	for (int32 Index = 0; Index < 15; ++Index)
		ButtonCount += Widget->GetWidgetFromName(*FString::Printf(TEXT("Command%d"), Index)) != nullptr;
	TestEqual(TEXT("항상 5열 3행 15칸 존재"), ButtonCount, 15);
	PC->ApplySelection({Castle});
	Widget->RefreshState();
	const auto Stale = PC->CommandCard->BuildCard()[6];
	auto* Unit = World->SpawnActor<ALSTroopUnit>(FVector(700, 0, 100), FRotator::ZeroRotator);
	PC->ApplySelection({Unit});
	TestFalse(TEXT("이전 선택의 스킬 클릭을 새 선택에서 실행하지 않음"), PC->CommandCard->ExecuteCell(6, Stale));
	Widget->RefreshState();
	TestFalse(TEXT("병력 선택 시 성 무장 버튼 차단"), Button->GetIsEnabled());
	TestTrue(TEXT("병력의 공통 이동 명령"), !PC->CommandCard->BuildCard()[0].Entry.Id.IsNone());
	auto* Provider = NewObject<ULSCommandTestProvider>(Unit);
	Unit->AddInstanceComponent(Provider);
	Provider->RegisterComponent();
	FLSCommandEntry Skill;
	Skill.Id = TEXT("TestSkill");
	Skill.Title = FText::FromString(TEXT("시험 스킬"));
	Skill.Cell = 8;
	Skill.Hotkey = EKeys::R;
	Skill.bEnabled = true;
	Provider->Entries.Add(Skill);
	Widget->RefreshState();
	TestTrue(TEXT("성 외 유닛의 확장 스킬 버튼 실행"), Widget->ExecuteCommandCell(8));
	TestEqual(TEXT("제공자가 실행을 소유"), Provider->Executions, 1);
	TestTrue(TEXT("단축키와 버튼이 같은 경로 사용"), PC->CommandCard->HandleHotkey(EKeys::R));
	TestEqual(TEXT("단축키 실행 횟수"), Provider->Executions, 2);
	Provider->Entries[0].bEnabled = false;
	TestFalse(TEXT("표시 이후 비활성화되면 실행 거부"), Widget->ExecuteCommandCell(8));
	Provider->Entries[0].bEnabled = true;
	Provider->Entries[0].Hotkey = EKeys::A;
	TestFalse(TEXT("공통 명령 단축키 충돌 차단"), PC->CommandCard->BuildCard()[8].Entry.bEnabled);
	Provider->Entries[0].Hotkey = EKeys::Escape;
	TestFalse(TEXT("잘못된 스킬 단축키가 취소 입력을 가로채지 않음"), PC->CommandCard->HandleHotkey(EKeys::Escape));
	Provider->Entries[0].Hotkey = EKeys::One;
	TestFalse(TEXT("잘못된 스킬 단축키가 부대 선택을 가로채지 않음"), PC->CommandCard->HandleHotkey(EKeys::One));
	Provider->Entries[0].Hotkey = EKeys::R;
	Provider->Entries.Add(Skill);
	TestFalse(TEXT("중복 셀 정의 차단"), PC->CommandCard->BuildCard()[8].Entry.bEnabled);
	Provider->Entries.SetNum(1);
	TestFalse(TEXT("범위를 벗어난 셀 요청 거부"), Widget->ExecuteCommandCell(15));
	PC->ApplySelection({Castle, Unit});
	TestTrue(TEXT("혼합 선택에서 활성 병종 유지"), !PC->CommandCard->BuildCard()[8].Entry.Id.IsNone());
	PC->CommandCard->CycleGroup();
	TestTrue(TEXT("Tab으로 성 카드 전환"), !PC->CommandCard->BuildCard()[6].Entry.Id.IsNone());
	TestTrue(TEXT("다른 병종 스킬 숨김"), PC->CommandCard->BuildCard()[8].Entry.Id.IsNone());
	PC->CommandCard->CycleGroup();
	Widget->RefreshState();
	Provider->DestroyComponent();
	TestFalse(TEXT("표시 이후 제공자 제거 시 실행 거부"), Widget->ExecuteCommandCell(8));
	Unit->Selection->SetPlayerControlled(false);
	PC->ApplySelection({Unit});
	TestTrue(TEXT("적 정보 선택은 명령 제공하지 않음"), PC->CommandCard->BuildCard()[0].Entry.Id.IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleHUDPersistentCastleTest,
	"Landship.HUD.BattleWidget.PersistentConfiguredCastle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSBattleHUDPersistentCastleTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* PC = World->SpawnActor<ALSPlayerController>();
	auto* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 144), FRotator::ZeroRotator);
	auto* Unit = World->SpawnActor<ALSTroopUnit>(FVector(-900, 0, 96), FRotator::ZeroRotator);
	auto* Setup = World->SpawnActor<ALSBattlefieldSetup>();
	Setup->PlayerCastle = Castle;
	Setup->CoreOutpost = World->SpawnActor<ALSEnemyOutpost>(FVector(10000, 0, 100), FRotator::ZeroRotator);
	Setup->CoreOutpost->Vision->bPublicCorePosition = true;
	if (!Fixture.BeginPlayInTestWorld()) return false;
	TestTrue(TEXT("핵심 거점 수치 초기화"), Setup->CoreOutpost->IsStatsInitialized());
	auto* Player = NewObject<ULocalPlayer>(GEngine);
	Player->PlayerController = PC;
	PC->Player = Player;
	UClass* Class = LoadClass<ULSBattleHUDWidget>(nullptr, TEXT("/Game/Landship/HUD/UI/WBP_BattleHUD.WBP_BattleHUD_C"));
	if (!TestNotNull(TEXT("상시 상태를 표시하는 실제 WBP"), Class)) return false;
	auto* Widget = CreateWidget<ULSBattleHUDWidget>(PC, Class);
	if (!TestNotNull(TEXT("상시 상태 위젯"), Widget)) return false;
	UWidget* CastleStatusPanel = Widget->GetWidgetFromName(TEXT("CastleStatusPanel"));
	if (!TestNotNull(TEXT("설정된 성 상태 패널"), CastleStatusPanel)) return false;
	auto* HealthLabel = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("CastleHealthText")));
	auto* HealthBar = Cast<UProgressBar>(Widget->GetWidgetFromName(TEXT("CastleHealthProgress")));
	auto* WeaponLabel = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("CastleWeaponState0")));
	auto* WeaponBar = Cast<UProgressBar>(Widget->GetWidgetFromName(TEXT("CastleWeaponProgress0")));
	if (!TestNotNull(TEXT("성 체력 텍스트"), HealthLabel) || !TestNotNull(TEXT("성 체력 막대"), HealthBar) ||
	    !TestNotNull(TEXT("무장 상태 텍스트"), WeaponLabel) || !TestNotNull(TEXT("무장 쿨타임 막대"), WeaponBar))
		return false;
	for (const TArray<AActor*>& Selection : {TArray<AActor*>(), TArray<AActor*>({Unit}), TArray<AActor*>({Unit, Castle})})
	{
		PC->ApplySelection(Selection);
		Widget->RefreshState();
		TestTrue(TEXT("설정된 성의 패널은 선택과 무관하게 표시"),
		    CastleStatusPanel->GetVisibility() == ESlateVisibility::Visible);
		TestEqual(TEXT("무선택·병력·혼합 선택에서 성 체력 막대 유지"), HealthBar->GetPercent(), 1.0f);
		TestTrue(TEXT("선택에 관계없이 성 무장 준비 상태"), Widget->GetCastleWeaponStatus(ELSCastleWeapon::Cannon).bAvailable);
		TestTrue(TEXT("WBP에 사용 가능 상태 표시"), WeaponLabel->GetText().ToString().Contains(TEXT("사용 가능")));
	}
	PC->ApplySelection({Unit});
	Castle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(100, Setup->CoreOutpost, Setup->CoreOutpost));
	Widget->RefreshState();
	TestEqual(TEXT("병력 선택 중 피해가 성 체력 막대에 반영"), HealthBar->GetPercent(), 0.9f);
	TestTrue(TEXT("성 체력 텍스트 갱신"), HealthLabel->GetText().ToString().Contains(TEXT("900 / 1000")));
	TestTrue(TEXT("병력 선택 중 대포 조준"), PC->ArmCastleWeapon(ELSCastleWeapon::Cannon));
	TestTrue(TEXT("병력 선택 중 대포 발사"), PC->ConfirmCastleWeapon(FVector(300, 0, 0)) == ELSWeaponCastResult::Ready);
	Widget->RefreshState();
	const auto Cooldown = Widget->GetCastleWeaponStatus(ELSCastleWeapon::Cannon);
	TestTrue(TEXT("독립 상태 조회에 실제 쿨타임 반영"), Cooldown.Remaining > 0 && Cooldown.Duration > 0 && !Cooldown.bAvailable);
	TestTrue(TEXT("WBP에 남은 시간 표시"), WeaponLabel->GetText().ToString().Contains(TEXT("초")));
	Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Cannon, false);
	Widget->RefreshState();
	TestTrue(TEXT("비활성과 쿨타임 함께 표시"), WeaponLabel->GetText().ToString().Contains(TEXT("비활성")) &&
	    WeaponLabel->GetText().ToString().Contains(TEXT("초")));
	Setup->CoreOutpost->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(10000, Castle, Castle));
	++GFrameCounter;
	World->Tick(LEVELTICK_All, 0.01f);
	Widget->RefreshState();
	TestTrue(TEXT("전투 종료 무장 표시"), WeaponLabel->GetText().ToString().Contains(TEXT("전투 종료")));
	TestEqual(TEXT("전투 종료 시 쿨타임 막대 해제"), WeaponBar->GetPercent(), 0.0f);
	TestFalse(TEXT("전투 종료 시 무장 사용 차단"), Widget->GetCastleWeaponStatus(ELSCastleWeapon::Ballista).bAvailable);
	Castle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(10000, Setup->CoreOutpost, Setup->CoreOutpost));
	Widget->RefreshState();
	TestTrue(TEXT("사망한 지정 성의 상태 패널 유지"),
	    CastleStatusPanel->GetVisibility() == ESlateVisibility::Visible);
	TestEqual(TEXT("사망한 지정 성의 체력 0 표시"), HealthBar->GetPercent(), 0.0f);
	TestTrue(TEXT("사망한 지정 성의 체력 문구 유지"), HealthLabel->GetText().ToString().Contains(TEXT("0 / 1000")));
	Fixture.ForwardErrorMessages(this);
	return true;
}
#endif

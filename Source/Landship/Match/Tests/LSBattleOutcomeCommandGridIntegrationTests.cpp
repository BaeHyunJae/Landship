#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSCastleCommandProviderComponent.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Castle/LSCastle.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Controls/LSCommandCardComponent.h"
#include "Controls/LSPlayerController.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HUD/LSBattleHUDWidget.h"
#include "HUD/Tests/LSCommandTestProvider.h"
#include "Match/LSGameMode.h"
#include "Match/LSGameState.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Troops/LSTroopProductionComponent.h"
#include "Troops/LSTroopUnit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeCommandGridTest,
                               "Landship.Match.Outcome.CommandGridRejectsFinalizedRequests",
                               EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeCommandGridTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = Play.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ALSGameMode::StaticClass();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Setup = World->SpawnActor<ALSBattlefieldSetup>();
	auto* Castle = LSCastleTestFixture::Spawn(World, FVector(10000, 0, 200), FRotator::ZeroRotator, Spawn);
	auto* Core = World->SpawnActor<ALSEnemyOutpost>(FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
	Core->BasicAttack->SetAutoAttackOnBeginPlay(false);
	Setup->PlayerCastle = Castle;
	Setup->CoreOutpost = Core;
	auto* PC = World->SpawnActor<ALSPlayerController>();
	auto* Unit = World->SpawnActor<ALSTroopUnit>(FVector(20000, 0, 200), FRotator::ZeroRotator, Spawn);
	if (!Play.BeginPlayInTestWorld())
		return false;
	auto* State = World->GetGameState<ALSGameState>();
	if (!TestNotNull(TEXT("Managed game state"), State))
		return false;
	TestTrue(TEXT("Outcome enabled by actual setup"), State->IsBattleOutcomeEnabled());
	auto* Player = NewObject<ULocalPlayer>(GEngine);
	Player->PlayerController = PC;
	PC->Player = Player;
	// 맵 배치 병력 집계는 다음 틱에 실행되므로 기준값을 읽기 전에 완료한다.
	++GFrameCounter;
	World->Tick(LEVELTICK_All, 0.01f);
	TestEqual(TEXT("Placed unit population initialized"), State->GetCurrentPopulation(), 1);
	UClass* WidgetClass = LoadClass<ULSBattleHUDWidget>(nullptr,
	    TEXT("/Game/Landship/HUD/UI/WBP_BattleHUD.WBP_BattleHUD_C"));
	if (!TestNotNull(TEXT("Battle HUD asset"), WidgetClass))
		return false;
	auto* Widget = CreateWidget<ULSBattleHUDWidget>(PC, WidgetClass);
	if (!TestNotNull(TEXT("Battle HUD"), Widget))
		return false;
	auto* Provider = NewObject<ULSCommandTestProvider>(Unit);
	Unit->AddInstanceComponent(Provider);
	Provider->RegisterComponent();
	FLSCommandEntry Skill;
	Skill.Id = TEXT("OutcomeTestSkill");
	Skill.Title = FText::FromString(TEXT("Test skill"));
	Skill.Cell = 8;
	Skill.Hotkey = EKeys::R;
	Skill.bEnabled = true;
	Skill.Level = 2;
	Skill.Remaining = 3;
	Skill.Duration = 5;
	Provider->Entries.Add(Skill);
	PC->ApplySelection({Unit, Castle});
	Widget->RefreshState();
	TestTrue(TEXT("Provider executes while managed battle is in progress"), Widget->ExecuteCommandCell(8));
	const auto Stale = PC->CommandCard->BuildCard();
	const int32 PopulationBefore = State->GetCurrentPopulation();
	const float WeaponCooldownBefore = Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon);
	const float ProductionCooldownBefore = PC->Production->GetRemainingCooldown(ELSTroopType::Spearman);
	FLSHealthSnapshot Health;
	auto* CoreHealth = Core->FindComponentByClass<ULSHealthComponent>();
	if (!TestNotNull(TEXT("Core health"), CoreHealth) || !CoreHealth->TryGetHealth(Health))
		return false;
	CoreHealth->ApplyDamage(ULSHealthComponent::MakeDamageRequest(Health.CurrentHealth, nullptr, nullptr));
	++GFrameCounter;
	World->Tick(LEVELTICK_All, 0.01f);
	TestTrue(TEXT("Outcome finalized by actual world tick"), State->HasBattleEnded());
	TestFalse(TEXT("Stale widget request rejected"), Widget->ExecuteCommandCell(8));
	TestFalse(TEXT("Stale cell request rejected"), PC->CommandCard->ExecuteCell(8, Stale[8]));
	TestFalse(TEXT("Hotkey request rejected"), PC->CommandCard->HandleHotkey(EKeys::R));
	TestEqual(TEXT("Finalization did not execute provider again"), Provider->Executions, 1);
	Widget->RefreshState();
	const auto* SkillButton = Cast<UButton>(Widget->GetWidgetFromName(TEXT("Command8")));
	TestTrue(TEXT("Actual widget skill button disabled"), SkillButton && !SkillButton->GetIsEnabled());
	const auto* Hint = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("CommandHint")));
	TestTrue(TEXT("HUD reports ended battle"), Hint && Hint->GetText().ToString() == TEXT("전투 종료"));
	const auto* Discovery = Widget->GetWidgetFromName(TEXT("DiscoveryPanel"));
	TestTrue(TEXT("Discovery caption suppressed"), Discovery && Discovery->GetVisibility() == ESlateVisibility::Collapsed);
	for (int32 Index = 0; Index < 4; ++Index)
		TestFalse(TEXT("Common command rejected"), PC->CommandCard->ExecuteCell(Index, Stale[Index]));
	TestEqual(TEXT("No pending targeting input"), PC->GetPendingInput(), ELSCommandInput::None);
	PC->CommandCard->CycleGroup();
	const auto FinalCells = PC->CommandCard->BuildCard();
	TestEqual(TEXT("All fifteen grid cells remain"), FinalCells.Num(), 15);
	TestEqual(TEXT("Cycling leaves active unit provider in place"), FinalCells[8].Entry.Id, Skill.Id);
	TestEqual(TEXT("Provider level retained"), FinalCells[8].Entry.Level, Skill.Level);
	TestTrue(TEXT("Provider title retained"), FinalCells[8].Entry.Title.EqualTo(Skill.Title));
	for (const auto& Cell : FinalCells)
		if (!Cell.Entry.Id.IsNone())
		{
			TestFalse(TEXT("Occupied cell disabled"), Cell.Entry.bEnabled);
			TestEqual(TEXT("No readiness countdown after battle"), Cell.Entry.Remaining, 0.0f);
		}
	auto* CastleProvider = Castle->FindComponentByClass<ULSCastleCommandProviderComponent>();
	if (!TestNotNull(TEXT("Castle provider"), CastleProvider))
		return false;
	TestFalse(TEXT("Direct weapon provider request rejected"), CastleProvider->ExecuteCommand(TEXT("CastleWeapon0"), PC));
	TestFalse(TEXT("Direct production provider request rejected"), CastleProvider->ExecuteCommand(TEXT("CastleProduce0"), PC));
	TestEqual(TEXT("No population consumed"), State->GetCurrentPopulation(), PopulationBefore);
	TestEqual(TEXT("No weapon cooldown consumed"), Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon), WeaponCooldownBefore);
	TestEqual(TEXT("No production cooldown consumed"), PC->Production->GetRemainingCooldown(ELSTroopType::Spearman), ProductionCooldownBefore);
	Play.ForwardErrorMessages(this);
	return true;
}
#endif

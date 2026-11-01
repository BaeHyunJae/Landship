#if WITH_DEV_AUTOMATION_TESTS
#include "HUD/LSBattleHUDWidget.h"
#include "HUD/Tests/LSCommandTestProvider.h"
#include "Controls/LSSelectableComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Castle/LSMobileCastle.h"
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
	auto* Castle = World->SpawnActor<ALSMobileCastle>(FVector(0, 0, 144), FRotator::ZeroRotator);
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
	PC->ApplySelection({Castle});
	Widget->RefreshState();
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
#endif

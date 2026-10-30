#include "Controls/Tests/LSHMHRTSUnit.h"
#include "Controls/LSSelectableComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"

ALSHMHRTSUnit::ALSHMHRTSUnit()
{
	UStaticMeshComponent* Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TestBody"));
	Body->SetupAttachment(RootComponent);
	Body->SetRelativeScale3D(FVector(0.65f, 0.65f, 1.2f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Body->SetStaticMesh(Cube.Object);
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);
	UTextRenderComponent* Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("TestLabel"));
	Label->SetupAttachment(RootComponent);
	Label->SetRelativeLocation(FVector(0, 0, 100));
	Label->SetRelativeRotation(FRotator(0, 180, 0));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(28.0f);
}

void ALSHMHRTSUnit::BeginPlay()
{
	UDataTable* Table = NewObject<UDataTable>(this);
	Table->RowStruct = FLSUnitStatRow::StaticStruct();
	FLSUnitStatRow Row;
	Row.Health = 200;
	Row.AttackPower = 20;
	Row.AttackInterval = 0.4f;
	Row.AttackRange = 260;
	Row.DetectionRange = 650;
	Row.VisionRadius = 1800;
	Row.MovementSpeed = 400;
	Table->AddRow(TEXT("RTSTest"), Row);
	StatsRow.DataTable = Table;
	StatsRow.RowName = TEXT("RTSTest");
	SetInitialFaction(TestTeam == 0 ? ELSFaction::Ally : ELSFaction::Enemy);
	Selection->SetPlayerControlled(TestTeam == 0);
	if (UTextRenderComponent* Label = FindComponentByClass<UTextRenderComponent>())
	{
		Label->SetText(FText::FromString(TestTeam == 0 ? TEXT("ALLY TEST") : TEXT("ENEMY TEST")));
		Label->SetTextRenderColor(TestTeam == 0 ? FColor::Cyan : FColor::Red);
	}
	Super::BeginPlay();
	BasicAttack->OnAttackPerformed.AddDynamic(this, &ALSHMHRTSUnit::ObserveAttack);
}

bool ALSHMHRTSUnit::IsCombatAlive_Implementation() const { return TestHealth > 0; }

bool ALSHMHRTSUnit::CanReceiveBasicAttack_Implementation(AActor* Attacker) const
{
	const ALSHMHRTSUnit* Source = Cast<ALSHMHRTSUnit>(Attacker);
	return Source && Source->TestTeam != TestTeam && Source->TestHealth > 0 && TestHealth > 0;
}

float ALSHMHRTSUnit::ReceiveBasicAttack_Implementation(AActor* Attacker, float Damage)
{
	if (!CanReceiveBasicAttack_Implementation(Attacker) || !FMath::IsFinite(Damage) || Damage <= 0) { return 0; }
	const float Applied = FMath::Min(TestHealth, Damage);
	TestHealth -= Applied;
	if (TestHealth <= 0)
	{
		SetActorEnableCollision(false);
		if (UTextRenderComponent* Label = FindComponentByClass<UTextRenderComponent>()) { Label->SetText(FText::FromString(TEXT("DEAD TEST"))); }
	}
	return Applied;
}

bool ALSHMHRTSUnit::GetCombatHealth_Implementation(float& Current, float& Maximum) const
{
	Current = TestHealth;
	Maximum = 200;
	return true;
}

void ALSHMHRTSUnit::ObserveAttack(AActor* Target, float AppliedDamage)
{
	if (IsValid(Target))
	{
		DrawDebugLine(GetWorld(), GetActorLocation(), Target->GetActorLocation(), TestTeam == 0 ? FColor::Cyan : FColor::Red, false, 0.15f, 0, 3);
	}
}

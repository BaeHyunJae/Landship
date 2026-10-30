#include "Controls/Tests/LSHMHRTSUnit.h"

#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"

ALSHMHRTSUnit::ALSHMHRTSUnit()
{
	static ConstructorHelpers::FObjectFinder<UDataTable> UnitStats(
	    TEXT("/Game/Landship/Combat/Data/DT_UnitStats.DT_UnitStats"));
	StatsRow.DataTable = UnitStats.Object;
	StatsRow.RowName = TEXT("PrototypeTest");
	// PR의 인간형 예시와 같은 메시·Physics Asset을 사용한다. 사망 물리는 부모가 수행한다.
	static ConstructorHelpers::FClassFinder<ALSUnit> Humanoid(TEXT("/Game/Developers/BHJ/CombatStats/BP_TestHumanoid"));
	if (Humanoid.Class)
	{
		const ALSUnit* Template = Humanoid.Class->GetDefaultObject<ALSUnit>();
		const USkeletalMeshComponent* TemplateMesh = Template->GetMesh();
		GetMesh()->SetSkeletalMeshAsset(TemplateMesh->GetSkeletalMeshAsset());
		GetMesh()->SetPhysicsAsset(TemplateMesh->GetPhysicsAsset());
		GetMesh()->SetRelativeTransform(TemplateMesh->GetRelativeTransform());
		for (int32 Index = 0; Index < TemplateMesh->GetNumMaterials(); ++Index)
		{
			GetMesh()->SetMaterial(Index, TemplateMesh->GetMaterial(Index));
		}
	}
	bUseRagdoll = true;
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);
	GetMesh()->SetCanEverAffectNavigation(false);
	UTextRenderComponent* Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("TestLabel"));
	Label->SetupAttachment(RootComponent);
	Label->SetRelativeLocation(FVector(0, 0, 100));
	Label->SetRelativeRotation(FRotator(0, 180, 0));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(28.0f);
}

void ALSHMHRTSUnit::BeginPlay()
{
	Super::BeginPlay();
	if (UTextRenderComponent* Label = FindComponentByClass<UTextRenderComponent>())
	{
		const bool bAlly = GetFaction() == ELSFaction::Ally;
		Label->SetText(FText::FromString(bAlly ? TEXT("ALLY") : TEXT("ENEMY")));
		Label->SetTextRenderColor(bAlly ? FColor::Cyan : FColor::Red);
	}
	BasicAttack->OnAttackPerformed.AddDynamic(this, &ALSHMHRTSUnit::ObserveAttack);
	Health->OnDiedEvent.AddDynamic(this, &ALSHMHRTSUnit::ObserveDeath);
}

void ALSHMHRTSUnit::ObserveDeath(const FLSDamageResult& Damage)
{
	// 체력이나 충돌은 수정하지 않고 공통 사망 알림의 표시만 담당한다.
	if (UTextRenderComponent* Label = FindComponentByClass<UTextRenderComponent>())
	{
		Label->SetText(FText::FromString(TEXT("DEAD")));
	}
}

void ALSHMHRTSUnit::ObserveAttack(AActor* Target, float AppliedDamage)
{
	if (IsValid(Target))
	{
		DrawDebugLine(GetWorld(), GetActorLocation(), Target->GetActorLocation(),
		              GetFaction() == ELSFaction::Ally ? FColor::Cyan : FColor::Red, false, 0.15f, 0, 3);
	}
}

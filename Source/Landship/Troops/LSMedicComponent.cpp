#include "Troops/LSMedicComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ULSMedicComponent::ULSMedicComponent()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	BeamMesh = Cylinder.Object;
	BeamMaterial = TSoftObjectPtr<UMaterialInterface>(
	    FSoftObjectPath(TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype")));
}

bool ULSMedicComponent::IsConfigurationValid() const
{
	return Super::IsConfigurationValid() && GetTroop()->GetTroopType() == ELSTroopType::Medic && BeamMesh &&
	    FMath::IsFinite(HealingPerSecond) && HealingPerSecond > 0 && FMath::IsFinite(HealingRange) &&
	    HealingRange > 0 && FMath::IsFinite(HealingInterval) && HealingInterval > 0 &&
	    FMath::IsFinite(HealingPerSecond * HealingInterval * 2) && FMath::IsFinite(BeamRadius) && BeamRadius > 0;
}

void ULSMedicComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!IsConfigurationValid())
	{
		UE_LOG(LogTemp, Error, TEXT("%s: 의무병의 소유자 또는 치유 설정이 유효하지 않습니다."), *GetPathName());
		return;
	}
	Beam = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("MedicHealingBeam"), RF_Transient);
	GetOwner()->AddInstanceComponent(Beam);
	Beam->SetupAttachment(GetOwner()->GetRootComponent());
	Beam->SetStaticMesh(BeamMesh);
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->SetCanEverAffectNavigation(false);
	Beam->SetCastShadow(false);
	Beam->SetVisibility(false);
	Beam->RegisterComponent();
	if (UMaterialInterface* Material = BeamMaterial.LoadSynchronous())
	{
		BeamMaterialInstance = UMaterialInstanceDynamic::Create(Material, this);
		BeamMaterialInstance->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(0.12f, 1.0f, 0.35f));
		Beam->SetMaterial(0, BeamMaterialInstance);
	}
}

bool ULSMedicComponent::IsPatientValid() const
{
	ALSTroopUnit* Unit = Patient.Get();
	const ALSTroopUnit* Medic = GetTroop();
	return Medic && Unit && Unit != Medic && ULSCombatTargetLibrary::IsCombatAlive(Unit) &&
	    Unit->GetFaction() == Medic->GetFaction() &&
	    (!bPooledPatient || (Unit->IsPoolManaged() && Unit->GetActivationSerial() == PatientSerial));
}

void ULSMedicComponent::SetPatient(ALSTroopUnit* Unit)
{
	if (Patient.Get() == Unit && IsPatientValid())
		return;
	HideBeam();
	Patient = Unit;
	PatientSerial = Unit ? Unit->GetActivationSerial() : 0;
	bPooledPatient = Unit && Unit->IsPoolManaged();
	AccumulatedHealingTime = 0;
	RefreshTick();
}

FLSCombatDirective ULSMedicComponent::EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Allies)
{
	FLSCombatDirective Result;
	Result.bSuppressBasicAttack = true;
	Result.bHoldPosition = IsStaggered();
	Result.ApproachRange = HealingRange * 0.8f;
	ALSTroopUnit* Medic = GetTroop();
	if (!IsBattleEnabled() || !ULSCombatTargetLibrary::IsCombatAlive(Medic) || IsStaggered())
	{
		CancelSpecialAction();
		return Result;
	}
	FLSUnitStatRow Stats;
	if (!Medic->TryGetStats(Stats))
		return Result;
	// 현재 부상자를 완치하거나 잃기 전까지 연결을 유지해 매 검사마다 빔이 튀지 않게 한다.
	FLSHealthSnapshot Health;
	ALSTroopUnit* Chosen = nullptr;
	if (IsPatientValid() && Patient->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health) &&
	    Health.CurrentHealth < Health.MaxHealth &&
	    ULSCombatTargetLibrary::GetAttackDistance(Medic, Patient.Get()) <= Stats.DetectionRange)
		Chosen = Patient.Get();
	double BestRatio = 2;
	const bool bKeepPatient = Chosen != nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	ALSTroopUnit* Follow = nullptr;
	for (ALSUnit* Ally : Allies)
	{
		ALSTroopUnit* Candidate = Cast<ALSTroopUnit>(Ally);
		if (!Candidate || Candidate == Medic || Candidate->GetFaction() != Medic->GetFaction() ||
		    !ULSCombatTargetLibrary::IsCombatAlive(Candidate))
			continue;
		const double Distance = ULSCombatTargetLibrary::GetAttackDistance(Medic, Candidate);
		if (Candidate->GetTroopType() != ELSTroopType::Medic && Distance < BestDistance)
		{
			BestDistance = Distance;
			Follow = Candidate;
		}
		if (!bKeepPatient && Distance <= Stats.DetectionRange &&

		    Candidate->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health) &&
		    Health.CurrentHealth < Health.MaxHealth)
		{
			const double Ratio = Health.CurrentHealth / Health.MaxHealth;
			if (Ratio < BestRatio)
			{
				BestRatio = Ratio;
				Chosen = Candidate;
			}
		}
	}
	SetPatient(Chosen);
	Result.Target = Chosen ? Chosen : Follow;
	if (Chosen)
	{
		Result.ApproachRange = HealingRange;
		Result.bHoldPosition = ULSCombatTargetLibrary::GetAttackDistance(Medic, Chosen) <= HealingRange;
	}
	return Result;
}

void ULSMedicComponent::TickSpecialAction(float DeltaTime)
{
	ALSTroopUnit* Medic = GetTroop();
	if (!Beam || !IsPatientValid() || IsStaggered() || Medic->GetCharacterMovement()->IsFalling() ||
	    Medic->Orders->IsKnockedBack() || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0 ||
	    ULSCombatTargetLibrary::GetAttackDistance(Medic, Patient.Get()) > HealingRange)
	{
		HideBeam();
		AccumulatedHealingTime = 0;
		return;
	}
	ULSHealthComponent* Health = Patient->FindComponentByClass<ULSHealthComponent>();
	FLSHealthSnapshot State;
	if (!Health || !Health->TryGetHealth(State) || State.CurrentHealth >= State.MaxHealth)
	{
		HideBeam();
		AccumulatedHealingTime = 0;
		return;
	}
	const FVector End = Patient->GetActorLocation() + FVector(0, 0, 30);
	const FVector Start = Medic->GetActorLocation() + FVector(0, 0, 30);
	const FVector Segment = End - Start;
	if (!Segment.IsNearlyZero())
		Medic->SetActorRotation(Segment.GetSafeNormal2D().Rotation());
	Beam->SetWorldLocationAndRotation((Start + End) * 0.5f,
	    FQuat::FindBetweenNormals(FVector::UpVector, Segment.GetSafeNormal()));
	Beam->SetWorldScale3D(FVector(BeamRadius / 50, BeamRadius / 50, Segment.Size() / 100));
	Beam->SetVisibility(true);
	if (!bBeamVisible)
		Medic->SetTacticalLabel(TEXT("HEALING"));
	bBeamVisible = true;
	AccumulatedHealingTime += DeltaTime;
	if (AccumulatedHealingTime >= HealingInterval)
	{
		const float Amount = HealingPerSecond * FMath::Min(AccumulatedHealingTime, HealingInterval * 2);
		AccumulatedHealingTime = 0;
		Health->RestoreHealth(Amount);
	}
}

void ULSMedicComponent::HideBeam()
{
	if (Beam)
		Beam->SetVisibility(false);
	if (bBeamVisible && GetTroop())
		GetTroop()->SetTacticalLabel(TEXT(""));
	bBeamVisible = false;
}

void ULSMedicComponent::CancelSpecialAction()
{
	HideBeam();
	Patient.Reset();
	PatientSerial = 0;
	bPooledPatient = false;
	AccumulatedHealingTime = 0;
}



void ULSMedicComponent::ResetForNewLife()
{
	Super::ResetForNewLife();
	CancelSpecialAction();
}

FString ULSMedicComponent::GetRestingLabel() const
{
	return bBeamVisible ? TEXT("HEALING") : FString();
}

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
	       FMath::IsFinite(PatientDetectionRange) && PatientDetectionRange >= HealingRange &&
	       FMath::IsFinite(HealingPerSecond * HealingInterval * 2) && FMath::IsFinite(BeamRadius) && BeamRadius > 0 &&
	       FMath::IsFinite(MaxMana) && MaxMana > 0 && FMath::IsFinite(StartingMana) && StartingMana >= 0 &&
	       StartingMana <= MaxMana && FMath::IsFinite(ManaRegenerationPerSecond) && ManaRegenerationPerSecond >= 0 &&
	       FMath::IsFinite(ManaPerHealth) && ManaPerHealth > 0 && FMath::IsFinite(ManaToStartHealing) &&
	       ManaToStartHealing > 0 && ManaToStartHealing <= MaxMana;
}

void ULSMedicComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!IsConfigurationValid())
	{
		UE_LOG(LogTemp, Error, TEXT("%s: 의무병의 소유자 또는 치유 설정이 유효하지 않습니다."), *GetPathName());
		return;
	}
	CurrentMana = StartingMana;
	++ManaLifeSerial;
	UpdateManaLabel();
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
	       (!bPooledPatient || (Unit->IsPoolManaged() && Unit->GetActivationSerial() == PatientSerial)) &&
	       Unit->FindComponentByClass<ULSHealthComponent>()->CanClaimHealing(this) &&
	       (HealingClaimSerial == 0 ||
	        Unit->FindComponentByClass<ULSHealthComponent>()->HasHealingClaim(this, HealingClaimSerial));
}

void ULSMedicComponent::SetPatient(ALSTroopUnit* Unit)
{
	if (Patient.Get() == Unit && IsPatientValid())
		return;
	ReleasePatient();
	Patient = Unit;
	PatientSerial = Unit ? Unit->GetActivationSerial() : 0;
	bPooledPatient = Unit && Unit->IsPoolManaged();
	if (Unit && CanConnectHealing() && ULSCombatTargetLibrary::GetAttackDistance(GetTroop(), Unit) <= HealingRange)
	{
		HealingClaimSerial = Unit->FindComponentByClass<ULSHealthComponent>()->TryClaimHealing(this);
		if (HealingClaimSerial == 0)
			ReleasePatient();
	}
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
	const ALSTroopUnit* CurrentPatient = IsPatientValid() ? Patient.Get() : nullptr;
	const bool bCanStartHealing = CanConnectHealing();
	const bool bCanContinueHealing = HealingClaimSerial != 0 && CurrentMana > UE_SMALL_NUMBER;
	ALSTroopUnit* Chosen = nullptr;
	float LowestHealth = TNumericLimits<float>::Max();
	double PatientDistance = TNumericLimits<double>::Max();
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
		const bool bCurrent = Candidate == CurrentPatient;
		const float DetectionRange = bCurrent ? FMath::Max(PatientDetectionRange, Stats.DetectionRange)
		                                      : PatientDetectionRange;
		if ((!bCanStartHealing && !(bCurrent && bCanContinueHealing)) || Distance > DetectionRange)
			continue;

		const auto* CandidateHealth = Candidate->FindComponentByClass<ULSHealthComponent>();
		FLSHealthSnapshot Health;
		if (!CandidateHealth || !CandidateHealth->CanClaimHealing(this) ||
		    !CandidateHealth->TryGetHealth(Health) || Health.CurrentHealth >= Health.MaxHealth)
			continue;

		// 같은 체력이면 현재 연결을 우선 유지하고, 새 환자끼리는 가까운 쪽을 고른다.
		const bool bWinsTie = Health.CurrentHealth == LowestHealth &&
		    (bCurrent || (Chosen != CurrentPatient && Distance < PatientDistance));
		if (Health.CurrentHealth < LowestHealth || bWinsTie)
		{
			LowestHealth = Health.CurrentHealth;
			PatientDistance = Distance;
			Chosen = Candidate;
		}
	}
	SetPatient(Chosen);
	Chosen = Patient.Get();
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
	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0)
		return;
	CurrentMana = FMath::Min(MaxMana, CurrentMana + ManaRegenerationPerSecond * DeltaTime);
	UpdateManaLabel();
	if (!Beam || !IsPatientValid() || CurrentMana <= UE_SMALL_NUMBER)
	{
		ReleasePatient();
		return;
	}
	if (IsStaggered() || Medic->GetCharacterMovement()->IsFalling() || Medic->Orders->IsKnockedBack() ||
	    ULSCombatTargetLibrary::GetAttackDistance(Medic, Patient.Get()) > HealingRange)
	{
		ReleaseHealingConnection();
		HideBeam();
		AccumulatedHealingTime = 0;
		return;
	}
	ULSHealthComponent* Health = Patient->FindComponentByClass<ULSHealthComponent>();
	FLSHealthSnapshot State;
	if (!Health || !Health->TryGetHealth(State) || State.CurrentHealth >= State.MaxHealth)
	{
		ReleasePatient();
		return;
	}
	if (HealingClaimSerial == 0)
	{
		if (!CanConnectHealing())
		{
			ReleasePatient();
			return;
		}
		HealingClaimSerial = Health->TryClaimHealing(this);
		if (HealingClaimSerial == 0)
		{
			ReleasePatient();
			return;
		}
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
	bBeamVisible = true;
	UpdateManaLabel();
	AccumulatedHealingTime += DeltaTime;
	if (AccumulatedHealingTime >= HealingInterval)
	{
		// 긴 프레임 뒤 밀린 치유를 무제한으로 몰아 적용하지 않는다.
		const float Amount = FMath::Min3(HealingPerSecond * FMath::Min(AccumulatedHealingTime, HealingInterval * 2),
		                                 CurrentMana / ManaPerHealth, State.MaxHealth - State.CurrentHealth);
		AccumulatedHealingTime = 0;
		const float ReservedMana = Amount * ManaPerHealth;
		const uint64 LifeSerial = ManaLifeSerial;
		// 먼저 비용을 예약한다. 회복 알림에서 재진입해도 같은 마나를 두 번 쓸 수 없다.
		CurrentMana = FMath::Max(0.0f, CurrentMana - ReservedMana);
		const float Applied = Health->RestoreClaimedHealth(this, HealingClaimSerial, Amount);
		if (IsValid(this) && ManaLifeSerial == LifeSerial)
		{
			CurrentMana = FMath::Clamp(CurrentMana + (Amount - Applied) * ManaPerHealth, 0.0f, MaxMana);
			if (CurrentMana <= UE_SMALL_NUMBER)
				ReleasePatient();
			UpdateManaLabel();
		}
	}
}

void ULSMedicComponent::HideBeam()
{
	if (Beam)
		Beam->SetVisibility(false);
	bBeamVisible = false;
	UpdateManaLabel();
}

void ULSMedicComponent::CancelSpecialAction()
{
	ReleasePatient();
}

void ULSMedicComponent::ReleaseHealingConnection()
{
	if (ALSTroopUnit* Unit = Patient.Get())
		if (ULSHealthComponent* Health = Unit->FindComponentByClass<ULSHealthComponent>())
			Health->ReleaseHealingClaim(this, HealingClaimSerial);
	HealingClaimSerial = 0;
}

void ULSMedicComponent::ReleasePatient()
{
	ReleaseHealingConnection();
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
	++ManaLifeSerial;
	CurrentMana = StartingMana;
	LastDisplayedMana = -1;
	UpdateManaLabel();
}

FString ULSMedicComponent::GetRestingLabel() const
{
	return FString::Printf(TEXT("%sMP %.0f/%.0f"), bBeamVisible ? TEXT("HEALING / ") : TEXT(""),
	                       FMath::FloorToFloat(CurrentMana), MaxMana);
}

bool ULSMedicComponent::CanConnectHealing() const
{
	return CurrentMana >= ManaToStartHealing;
}

void ULSMedicComponent::UpdateManaLabel()
{
	if (IsStaggered())
	{
		LastDisplayedMana = -1;
		return;
	}
	const float DisplayMana = FMath::FloorToFloat(CurrentMana);
	if (LastDisplayedMana == DisplayMana && bLastDisplayedBeam == bBeamVisible)
		return;
	LastDisplayedMana = DisplayMana;
	bLastDisplayedBeam = bBeamVisible;
	if (ULSCombatTargetLibrary::IsCombatAlive(GetTroop()))
		GetTroop()->SetTacticalLabel(GetRestingLabel());
}

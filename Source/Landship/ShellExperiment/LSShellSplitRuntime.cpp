#include "LSShellSplitRuntime.h"

#include "LSShellEffectInstance.h"
#include "LSShellExperimentCoordinator.h"
#include "LSShellField.h"
#include "LSShellMaterialStateComponent.h"
#include "LSShellPhysicalProp.h"
#include "LSShellProjectile.h"
#include "LSShellReactiveComponent.h"
#include "LSShellSplitBudget.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Maps/LSBattlefieldSettings.h"

void FLSShellSplitRuntime::Execute(ALSShellExperimentCoordinator& Coordinator, FVector Point, const FLSShellShot& Shot,
	const FHitResult& Hit, bool bCombatImpact, ALSShellEffectInstance* Instance)
{
	if (!Coordinator.IsBattleActive() || Shot.RuleProfile != ELSShellRuleProfile::SupplyStageOne || Shot.bChild) return;
	const bool bSplitFirst = Shot.First == ELSShellModule::Split;
	if (!bSplitFirst && Shot.Second != ELSShellModule::Split) return;
	const ELSShellModule PairedModule = bSplitFirst ? Shot.Second : Shot.First;
	const FLSShellSplitAllocation Allocation = FLSShellSplitBudget::Allocate(PairedModule,
		bSplitFirst ? ELSShellSplitOrder::SplitFirst : ELSShellSplitOrder::SplitSecond, Shot.BaseTuning.DirectDamage);
	if (!Allocation.bValid) { Coordinator.SetTargetFailure(TEXT("분열 예산이 유효하지 않다")); return; }

	FLSShellShot Children[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FLSShellShot& Child = Children[Index];
		Child = Shot;
		Child.bChild = true;
		Child.bInheritedModule = true;
		Child.First = Child.Second = PairedModule;
		Child.Damage = Allocation.Children[Index].DirectDamage;
		Child.EffectScale = Allocation.Children[Index].EffectScale;
		Child.AdhesionSlots = Allocation.Children[Index].AdhesionSlots;
		Child.ColdSeconds = Allocation.Children[Index].ColdSeconds;
		Child.SupplyTuning.DirectMomentum = Shot.SupplyTuning.DirectMomentum * (bSplitFirst ? 1.f / 3 : 1.f / 6);
		Child.ChargePayload.Reset();
		if (LSShellUsesArena(Shot) && PairedModule == ELSShellModule::Gravity && bSplitFirst)
			Child.GatherRadiusOverride = Shot.BaseTuning.GravityRadius / FMath::Sqrt(3.f);
		Child.Origin = Point;
	}
	auto AppendFragments = [&Children](const FLSShellChargePacket& Packet, uint64& NextPacketId)
	{
		const auto Fragments = FLSShellSplitBudget::FragmentChargePacket(Packet, [&NextPacketId]() { return ++NextPacketId; });
		if (Fragments.Num() != 3) return false;
		for (int32 Index = 0; Index < 3; ++Index) Children[Index].ChargePayload.Add(Fragments[Index]);
		return true;
	};
	AActor* Anchor = Coordinator.ResolveBodyTarget(Instance ? Instance->GetAnchorActor() : Hit.GetActor());
	auto* Reactive = IsValid(Anchor) ? Anchor->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;

	if (Allocation.bSplitExistingGeneratedMass)
	{
		auto* Sphere = Reactive ? Cast<USphereComponent>(Reactive->GetPhysicalBody()) : nullptr;
		if (!Sphere || !Coordinator.GeneratedMaterials.IsGenerated(Anchor))
		{ Coordinator.SetTargetFailure(TEXT("분할할 생성 철구가 없다")); return; }
		const TArray<FLSShellChargePacket> ParentPackets = Reactive->GetChargeStore().GetPackets();
		uint64 PreparedNextPacketId = Coordinator.NextChargePacketId;
		for (const FLSShellChargePacket& Packet : ParentPackets)
		{
			if (!AppendFragments(Packet, PreparedNextPacketId))
			{ Coordinator.SetTargetFailure(TEXT("분할 전하를 준비할 수 없다")); return; }
		}
		const float ChildRadius = Sphere->GetScaledSphereRadius() * FMath::Pow(1.f / 3, 1.f / 3);
		const float Offset = 2 * ChildRadius / FMath::Sqrt(3.f) + 1;
		const FVector Center = Sphere->GetCenterOfMass();
		TArray<FTransform> Poses;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FVector Radial = FVector::ForwardVector.RotateAngleAxis(Index * 120.f, FVector::UpVector);
			Poses.Add(FTransform(Anchor->GetActorQuat(), Center + Radial * Offset));
		}
		auto* ParentMaterial = Anchor->FindComponentByClass<ULSShellMaterialStateComponent>();
		TArray<AActor*> ReplacementActors;
		auto Prepare = [&Children, &ReplacementActors, ParentMaterial](const AActor*, const TArray<ALSShellMetalBall*>& Bodies, FString& Error)
		{
			for (int32 Index = 0; Index < Bodies.Num(); ++Index)
			{
				auto* ChildReactive = Bodies[Index]->Reactive.Get();
				if (!ChildReactive || !ChildReactive->bCanStoreCharge || !ChildReactive->GetChargeStore().IsEmpty())
				{ Error = TEXT("분할 몸체의 전하 저장소가 준비되지 않았다"); return false; }
				double TotalCharge = 0;
				for (const auto& Packet : Children[Index].ChargePayload) TotalCharge += Packet.Amount;
				const float Capacity = Children[Index].SupplyTuning.ChargeCapacity;
				if (!FMath::IsFinite(Capacity) || Capacity <= 0 || TotalCharge > Capacity)
				{ Error = TEXT("분할 전하가 자식 저장 용량을 넘는다"); return false; }
				ReplacementActors.Add(Bodies[Index]);
				if (ParentMaterial && !ULSShellMaterialStateComponent::GetOrCreate(Bodies[Index]))
				{ Error = TEXT("분할 재질 상태를 준비할 수 없다"); return false; }
			}
			return !ParentMaterial || ParentMaterial->PrepareSplitTransfer(ReplacementActors, Error);
		};
		auto Commit = [&Coordinator, &Children, &ReplacementActors, ParentMaterial, Reactive, PreparedNextPacketId, Instance]
			(AActor* Parent, const TArray<ALSShellMetalBall*>& Bodies)
		{
			Coordinator.NextChargePacketId = PreparedNextPacketId;
			for (int32 Index = 0; Index < Bodies.Num(); ++Index)
			{
				for (FLSShellChargePacket Packet : Children[Index].ChargePayload)
				{
					Packet.ActivationSequence = ++Coordinator.ChargeContactSequence;
					Bodies[Index]->Reactive->AddCharge(Packet, Children[Index].SupplyTuning.ChargeCapacity);
				}
			}
			Reactive->ClearCharge();
			if (ParentMaterial) ParentMaterial->CommitSplitTransfer(ReplacementActors);
			for (TActorIterator<ALSShellEffectInstance> It(Coordinator.GetWorld()); It; ++It)
				if (*It != Instance) It->CancelForBodyRemoval(Parent);
		};
		TArray<ALSShellMetalBall*> Replacements;
		FString Error;
		if (!Coordinator.GeneratedMaterials.TryReplaceThree(Anchor, Poses, Prepare, Commit, Replacements, Error))
			Coordinator.SetTargetFailure(Error);
		return;
	}

	if (Allocation.bExportRemainingCharge && Reactive && Reactive->IsPhysicalBodyPresent())
	{
		TArray<FLSShellChargePacket> MatchingPackets;
		for (const auto& Packet : Reactive->GetChargeStore().GetPackets())
			if (Packet.ShotId == Shot.ShotId) MatchingPackets.Add(Packet);
		for (const auto& Packet : MatchingPackets)
		{
			const FLSShellChargePacket Remaining = Reactive->GetChargeStore().ConsumePacket(Packet.PacketId, Packet.Amount);
			if (Remaining.Amount > 0 && !AppendFragments(Remaining, Coordinator.NextChargePacketId))
			{ Coordinator.SetTargetFailure(TEXT("자탄 전하를 준비할 수 없다")); return; }
		}
	}
	if (Allocation.bExportRemainingGravity)
	{
		auto* Field = Instance ? Instance->GetSourceGravityField() : nullptr;
		if (LSShellUsesArena(Shot))
		{
			const FLSShellGatherBudget Budget = IsValid(Field) ? Field->ReleaseRemainingGather() : FLSShellGatherBudget();
			for (FLSShellShot& Child : Children)
			{
				Child.GatherRadiusOverride = Budget.Radius / FMath::Sqrt(3.f);
				Child.GatherSecondsOverride = Budget.Seconds;
			}
		}
		else
		{
			const float Remaining = IsValid(Field) ? Field->ReleaseRemainingSupplyMomentum() : 0;
			for (FLSShellShot& Child : Children)
			{
				Child.SupplyTuning.GravityIntegratedMomentum = Remaining / 3;
				Child.EffectScale = 1;
			}
		}
	}

	FVector Basis = Shot.Direction;
	if (!bCombatImpact) Basis = FMath::GetReflectionVector(Basis, Hit.ImpactNormal);
	Basis = Basis.GetSafeNormal2D();
	if (Basis.IsNearlyZero()) Basis = Shot.Direction.GetSafeNormal2D();
	if (Basis.IsNearlyZero()) Basis = FVector::ForwardVector;
	const auto* Settings = ALSBattlefieldSettings::Find(Coordinator.GetWorld());
	const float Ground = Settings ? Settings->GetGroundHeight() : Point.Z;
	const FVector Outward = bCombatImpact ? -Basis : Hit.ImpactNormal.GetSafeNormal();
	const FVector Start = Point + Outward * 14;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FLSShellShot& Child = Children[Index];
		const FVector Planar = Basis.RotateAngleAxis(-40.f + Index * 40.f, FVector::UpVector);
		const FVector GroundTarget(Point.X + Planar.X * 600, Point.Y + Planar.Y * 600, Ground + LSShellProjectileRadius - .1f);
		Child.Direction = (GroundTarget - Start).GetSafeNormal();
		FHitResult SpawnHit;
		if (ALSShellProjectile::SweepFlight(Coordinator.GetWorld(), Point, Start, Shot.Source.Get(), SpawnHit, Hit.GetComponent()))
			Coordinator.QueueShellHit(Child, SpawnHit);
		else if (auto* Projectile = Coordinator.GetWorld()->SpawnActor<ALSShellProjectile>(Start, Child.Direction.Rotation()))
			Projectile->Launch(&Coordinator, Child, Start, Child.Direction);
		else Coordinator.SetTargetFailure(TEXT("자탄을 생성할 수 없다"));
	}
}

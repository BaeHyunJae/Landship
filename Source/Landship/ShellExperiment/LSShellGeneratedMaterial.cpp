#include "LSShellGeneratedMaterial.h"
#include "LSShellPhysicalProp.h"
#include "LSShellReactiveComponent.h"
#include "LSShellExperimentCoordinator.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"

namespace
{
ULSShellReactiveComponent* GeneratedReactive(const AActor* Actor)
{
	return IsValid(Actor) ? Actor->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
}

bool IsLiveGeneratedActor(const TWeakObjectPtr<AActor>& Actor)
{
	return Actor.IsValid() && !Actor->IsActorBeingDestroyed();
}

bool IsGeneratedPoseClear(UWorld* World, FVector Point, float Radius, const AActor* Ignore)
{
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellGeneratedPose), false, Ignore);
	return !World->OverlapBlockingTestByChannel(Point, FQuat::Identity, LSCollisionChannels::ObstacleProjectile,
		FCollisionShape::MakeSphere(Radius), Query);
}

bool ResolveGeneratedFloor(UWorld* World, FVector& Point, float Radius)
{
	if (IsGeneratedPoseClear(World, Point, Radius, nullptr)) return true;
	const FVector Upper = Point + FVector::UpVector * Radius;
	if (!IsGeneratedPoseClear(World, Upper, Radius, nullptr)) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellGeneratedFloor), false);
	FHitResult Floor;
	if (!World->SweepSingleByChannel(Floor, Upper, Point, FQuat::Identity,
		LSCollisionChannels::ObstacleProjectile, FCollisionShape::MakeSphere(Radius), Query) ||
		Floor.bStartPenetrating || Floor.ImpactNormal.Z <= UE_KINDA_SMALL_NUMBER) return false;
	const double NewZ = FMath::Min(Upper.Z, Floor.Location.Z + UE_KINDA_SMALL_NUMBER);
	if (NewZ < Point.Z || NewZ - Point.Z > Radius) return false;
	Point.Z = NewZ;
	return IsGeneratedPoseClear(World, Point, Radius, nullptr);
}

ALSShellMetalBall* PrepareGeneratedBall(UWorld* World, const FTransform& Pose, float Mass, float Radius, float Health = 0)
{
	auto* Ball = World->SpawnActorDeferred<ALSShellMetalBall>(ALSShellMetalBall::StaticClass(), Pose, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Ball) return nullptr;
	Ball->IntrinsicMassKg = Mass;
	Ball->Radius = Radius;
	Ball->bDeferPhysicsActivation = true;
	if (Health > 0 && !Ball->SetGeneratedHealthBeforeBeginPlay(Health)) { Ball->Destroy(); return nullptr; }
	Ball->FinishSpawning(Pose);
	return Ball;
}
}

void FLSShellGeneratedMaterialRegistry::Prune()
{
	for (auto It = Entries.CreateIterator(); It; ++It)
		if (!IsLiveGeneratedActor(It.Key())) It.RemoveCurrent();
}

bool FLSShellGeneratedMaterialRegistry::IsGenerated(const AActor* Actor) const
{
	return IsValid(Actor) && !Actor->IsActorBeingDestroyed() && Entries.Contains(const_cast<AActor*>(Actor));
}

const FLSShellShot* FLSShellGeneratedMaterialRegistry::GetOrigin(const AActor* Actor) const
{
	if (!IsGenerated(Actor)) return nullptr;
	const FLSEntry* Entry = Entries.Find(const_cast<AActor*>(Actor));
	return Entry ? &Entry->Origin : nullptr;
}

float FLSShellGeneratedMaterialRegistry::GetLiveMassKg() const
{
	float Mass = 0;
	for (const auto& Entry : Entries)
		if (IsLiveGeneratedActor(Entry.Key)) Mass += Entry.Value.MassKg;
	return Mass;
}

int32 FLSShellGeneratedMaterialRegistry::GetLiveBodyCount() const
{
	int32 Count = 0;
	for (const auto& Entry : Entries) Count += IsLiveGeneratedActor(Entry.Key) ? 1 : 0;
	return Count;
}

ALSShellMetalBall* FLSShellGeneratedMaterialRegistry::GenerateIron(UWorld* World, const FHitResult& Hit,
	const FLSShellShot& Shot, FString& OutError)
{
	OutError.Reset();
	Prune();
	const auto* Coordinator = World ? ALSShellExperimentCoordinator::Find(World) : nullptr;
	if (!Coordinator || !Coordinator->IsBattleActive() || Shot.RuleProfile != ELSShellRuleProfile::SupplyStageOne)
	{ OutError = TEXT("생성 가능한 전투 상태가 아니다"); return nullptr; }
	const float Mass = Shot.SupplyTuning.GeneratedBallMassKg * Shot.EffectScale;
	const float Radius = 70 * FMath::Pow(Shot.EffectScale, 1.f / 3);
	if (!FMath::IsFinite(Mass) || Mass <= 0 || !FMath::IsFinite(Shot.SupplyTuning.GeneratedMassCapKg) ||
		Shot.SupplyTuning.GeneratedMassCapKg <= 0 || Shot.SupplyTuning.GeneratedBodyCap <= 0 ||
		!FMath::IsFinite(Shot.SupplyTuning.GeneratedSurfaceGap) || Shot.SupplyTuning.GeneratedSurfaceGap < 0 ||
		!FMath::IsFinite(Shot.SupplyTuning.GeneratedLaunchMomentum) || Shot.SupplyTuning.GeneratedLaunchMomentum <= 0 ||
		!FMath::IsFinite(Shot.EffectScale) || Shot.EffectScale <= 0 || GetLiveMassKg() + Mass > Shot.SupplyTuning.GeneratedMassCapKg ||
		GetLiveBodyCount() + 1 > Shot.SupplyTuning.GeneratedBodyCap)
	{ OutError = TEXT("생성 물질 한도 초과"); return nullptr; }
	const FVector Direction = Shot.Direction.GetSafeNormal();
	const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	// 후행 효과의 실제 접점과 법선은 몸체 회전을 따른다. 입사 방향과의 각도는 빈 공간 여부가 아니다.
	if (Direction.IsNearlyZero() || Normal.IsNearlyZero() ||
		Hit.ImpactPoint.ContainsNaN() || Shot.Origin.ContainsNaN())
	{ OutError = TEXT("생성 위치의 입사 방향이 없다"); return nullptr; }
	FVector Point = Hit.ImpactPoint + Normal * (Radius + Shot.SupplyTuning.GeneratedSurfaceGap);
	FVector OutwardPoint = Hit.ImpactPoint + Normal * (Radius + Shot.SupplyTuning.SurfaceOffset);
	if (!ResolveGeneratedFloor(World, Point, Radius) || !ResolveGeneratedFloor(World, OutwardPoint, Radius))
	{ OutError = TEXT("철구가 들어갈 빈 공간이 없다"); return nullptr; }
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellGeneratedApproach), false, Shot.Source.Get());
	Query.bFindInitialOverlaps = true;
	FHitResult Blocker;
	// 최초 표면에서 바깥으로 나온 몸체만 짧게 검사한다. 포탄이 통과한 전 구간에 큰 구를 요구하지 않는다.
	if (World->SweepSingleByChannel(Blocker, OutwardPoint, Point, FQuat::Identity,
		LSCollisionChannels::ObstacleProjectile, FCollisionShape::MakeSphere(Radius), Query))
	{ OutError = TEXT("철구의 생성 경로가 막혔다"); return nullptr; }
	auto* Ball = PrepareGeneratedBall(World, FTransform(Point), Mass, Radius);
	if (!Ball || !Ball->ActivatePhysicalBody())
	{
		if (Ball) Ball->Destroy();
		OutError = TEXT("철구 몸체를 준비하지 못했다"); return nullptr;
	}
	Entries.Add(Ball, {Shot, Mass, Radius});
	Ball->Reactive->ApplyMomentum(Direction * Shot.SupplyTuning.GeneratedLaunchMomentum * Shot.EffectScale);
	Ball->Reactive->NotifyExternalForce();
	return Ball;
}

bool FLSShellGeneratedMaterialRegistry::Recall(AActor* Actor)
{
	if (!IsGenerated(Actor)) return false;
	if (auto* Reactive = GeneratedReactive(Actor)) Reactive->ClearCharge();
	return Actor->Destroy();
}

bool FLSShellGeneratedMaterialRegistry::Remap(AActor* Original, AActor* Replacement)
{
	Prune();
	const FLSEntry* Entry = Entries.Find(Original);
	auto* Reactive = GeneratedReactive(Replacement);
	if (!Entry || !Reactive || !Reactive->IsPhysicalBodyPresent() || Entries.Contains(Replacement) ||
		!FMath::IsNearlyEqual(Reactive->GetPhysicsMassKg(), Entry->MassKg, .1f)) return false;
	const FLSEntry Copy = *Entry;
	Entries.Remove(Original);
	Entries.Add(Replacement, Copy);
	return true;
}

bool FLSShellGeneratedMaterialRegistry::TryReplaceThree(AActor* Parent, const TArray<FTransform>& Poses,
	const FPrepareReplacement& Prepare, const FCommitReplacement& Commit,
	TArray<ALSShellMetalBall*>& OutChildren, FString& OutError)
{
	OutChildren.Reset(); OutError.Reset();
	Prune();
	const FLSEntry* ParentEntry = Entries.Find(Parent);
	auto* ParentReactive = GeneratedReactive(Parent);
	if (!ParentEntry || !ParentReactive || !ParentReactive->CanReceiveMomentum() || Poses.Num() != 3 || !Commit)
	{ OutError = TEXT("분할할 생성 몸체가 없다"); return false; }
	FLSHealthSnapshot ParentHealth;
	const auto* ParentBall = Cast<ALSShellMetalBall>(Parent);
	if (!ParentBall || !ParentBall->GetHealthComponent()->TryGetHealth(ParentHealth) || ParentHealth.CurrentHealth <= 0)
	{ OutError = TEXT("현재 생성 철구와 같은 재질로 분할할 수 없다"); return false; }
	const FLSEntry Origin = *ParentEntry;
	UWorld* World = Parent->GetWorld();
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(World);
	if (!Coordinator || !Coordinator->IsBattleActive() || GetLiveBodyCount() + 2 > Origin.Origin.SupplyTuning.GeneratedBodyCap)
	{ OutError = TEXT("분할 몸체 수 한도 초과"); return false; }
	UPrimitiveComponent* ParentBody = ParentReactive->GetPhysicalBody();
	const float ParentMass = ParentReactive->GetPhysicsMassKg(), ChildMass = ParentMass / 3;
	if (ParentMass <= 0 || !FMath::IsNearlyEqual(ParentMass, Origin.MassKg, .1f))
	{ OutError = TEXT("생성 몸체의 실제 질량과 등록 질량이 다르다"); return false; }
	const float ChildRadius = Origin.BallRadius * FMath::Pow(ChildMass / Origin.MassKg, 1.f / 3);
	const FVector Center = ParentBody->GetCenterOfMass();
	FVector ChildCenter = FVector::ZeroVector;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FTransform& Pose = Poses[Index];
		if (Pose.ContainsNaN() || !Pose.GetScale3D().Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER) ||
			!IsGeneratedPoseClear(World, Pose.GetLocation(), ChildRadius, Parent))
		{ OutError = TEXT("분할 몸체의 배치가 막혔다"); return false; }
		FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellGeneratedSplitSweep), false, Parent);
		FHitResult Blocker;
		if (World->SweepSingleByChannel(Blocker, Center, Pose.GetLocation(), FQuat::Identity,
			LSCollisionChannels::ObstacleProjectile, FCollisionShape::MakeSphere(ChildRadius), Query))
		{ OutError = TEXT("분할 몸체의 이동 경로가 막혔다"); return false; }
		for (int32 Other = 0; Other < Index; ++Other)
			if (FVector::Distance(Pose.GetLocation(), Poses[Other].GetLocation()) < 2 * ChildRadius)
			{ OutError = TEXT("분할 몸체가 서로 겹친다"); return false; }
		ChildCenter += Pose.GetLocation() / 3;
	}
	if (!ChildCenter.Equals(Center, .1)) { OutError = TEXT("분할 질량중심이 원본과 다르다"); return false; }
	const FVector Velocity = ParentReactive->GetMomentumAdjustedLinearVelocity();
	const FVector AngularMomentum = ParentBody->ScaleByMomentOfInertia(ParentBody->GetPhysicsAngularVelocityInRadians());
	TArray<ALSShellMetalBall*> Children;
	auto RollBack = [&Children]() { for (auto* Child : Children) if (IsValid(Child)) Child->Destroy(); };
	for (const FTransform& Pose : Poses)
	{
		auto* Child = PrepareGeneratedBall(World, Pose, ChildMass, ChildRadius, ParentHealth.CurrentHealth / 3);
		if (!Child) { RollBack(); OutError = TEXT("분할 몸체 생성 실패"); return false; }
		Children.Add(Child);
		if (!Child->ActivatePhysicalBody()) { RollBack(); OutError = TEXT("분할 몸체 활성화 실패"); return false; }
	}
	if (Prepare && !Prepare(Parent, Children, OutError)) { RollBack(); return false; }
	// 자식들의 회전·공전 관성을 함께 써 원본의 선운동량과 각운동량을 한 번만 나눈다.
	FMatrix Inertia = FMatrix::Identity;
	for (int32 Row = 0; Row < 3; ++Row)
		for (int32 Column = 0; Column < 3; ++Column) Inertia.M[Row][Column] = 0;
	for (auto* Child : Children)
	{
		const FVector R = Child->Body->GetCenterOfMass() - Center;
		const float Mass = Child->Body->GetMass();
		const FVector LocalInertia = Child->Body->GetInertiaTensor();
		for (int32 Row = 0; Row < 3; ++Row)
			for (int32 Column = 0; Column < 3; ++Column)
				Inertia.M[Row][Column] += (Row == Column ? LocalInertia[Row] + Mass * R.SizeSquared() : 0) - Mass * R[Row] * R[Column];
	}
	if (!FMath::IsFinite(Inertia.Determinant()) || FMath::Abs(Inertia.Determinant()) < UE_SMALL_NUMBER)
	{ RollBack(); OutError = TEXT("분할 관성을 계산할 수 없다"); return false; }
	const FVector AngularVelocity(Inertia.Inverse().TransformVector(AngularMomentum));
	if (AngularVelocity.ContainsNaN()) { RollBack(); OutError = TEXT("분할 각속도가 유효하지 않다"); return false; }
	for (auto* Child : Children)
	{
		Child->Body->SetPhysicsLinearVelocity(Velocity + FVector::CrossProduct(AngularVelocity, Child->Body->GetCenterOfMass() - Center));
		Child->Body->SetPhysicsAngularVelocityInRadians(AngularVelocity);
	}
	Commit(Parent, Children);
	for (auto* Child : Children) Entries.Add(Child, {Origin.Origin, ChildMass, ChildRadius});
	Parent->Destroy();
	Entries.Remove(Parent);
	OutChildren = MoveTemp(Children);
	return true;
}

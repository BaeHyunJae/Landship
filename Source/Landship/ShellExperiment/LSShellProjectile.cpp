#include "LSShellProjectile.h"
#include "LSShellExperimentCoordinator.h"
#include "LSShellReactiveComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"

ALSShellProjectile::ALSShellProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shell"));
	SetRootComponent(Visual);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Visual->SetStaticMesh(Sphere.Object);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
	Visual->SetRelativeScale3D(FVector(0.18));
	Trail = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Trail"));
	Trail->SetupAttachment(Visual);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Trail->SetStaticMesh(Cylinder.Object);
	Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Trail->SetCanEverAffectNavigation(false);
	Trail->SetCastShadow(false);
}

void ALSShellProjectile::Launch(ALSShellExperimentCoordinator* OwnerCoordinator, const FLSShellShot& InShot, FVector Start, FVector Direction)
{
	Coordinator = OwnerCoordinator;
	Shot = InShot;
	SetActorLocation(Start);
	Velocity = Direction.GetSafeNormal() * (Shot.bChild ? 2500 : OwnerCoordinator->Tuning.ShellSpeed);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Landship/ShellExperiment/Materials/M_ShellEffect.M_ShellEffect")))
	{
		auto* Material = UMaterialInstanceDynamic::Create(Base, this);
		Material->SetVectorParameterValue(TEXT("Tint"), FLinearColor(4, 2.2f, 0.5f));
		Material->SetScalarParameterValue(TEXT("Opacity"), 0.85f);
		Visual->SetMaterial(0, Material); Trail->SetMaterial(0, Material);
	}
	Trail->SetWorldLocation(Start - Direction.GetSafeNormal() * 60);
	Trail->SetWorldRotation(FRotationMatrix::MakeFromZ(Direction).Rotator());
	Trail->SetWorldScale3D(FVector(0.06, 0.06, 1.2));
	bLaunched = true;
}

bool ALSShellProjectile::SweepFlight(UWorld* World, const FVector& Start, const FVector& End, AActor* Source, FHitResult& Hit, const UPrimitiveComponent* InitialSurface)
{
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellFlight), false, Source);
	Query.bFindInitialOverlaps = true;
	// 생성 여유 검사에서 원래 면의 시작 겹침만 제외한다. 실제 비행에는 이 예외를 넘기지 않는다.
	while (World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, LSCollisionChannels::ObstacleProjectile,
		FCollisionShape::MakeSphere(LSShellProjectileRadius), Query))
	{
		UPrimitiveComponent* Body = Hit.GetComponent();
		AActor* Target = Hit.GetActor();
		if (Body == InitialSurface && Hit.bStartPenetrating)
		{
			// 여유 구간 전체를 다시 검사한다. 끝점 검사만 하면 중간의 얇은 몸체를 건너뛴다.
			Query.AddIgnoredComponent(Body);
			InitialSurface = nullptr;
			continue;
		}
		if (!Body) return false;
		if (Target && Target->FindComponentByClass<ULSHealthComponent>() && !ULSCombatTargetLibrary::IsCombatAlive(Target))
		{
			const auto* Coordinator = ALSShellExperimentCoordinator::Find(World);
			const auto* Reactive = Target->FindComponentByClass<ULSShellReactiveComponent>();
			if (!Coordinator || !Coordinator->IsSupplyExperiment() || !Reactive || !Reactive->IsPhysicalBodyPresent())
			{
				Query.AddIgnoredActor(Target);
				continue;
			}
		}
		if (Body->GetCollisionEnabled() == ECollisionEnabled::NoCollision || Body->GetCollisionObjectType() == LSCollisionChannels::Projectile)
		{
			Query.AddIgnoredComponent(Body);
			continue;
		}
		return true;
	}
	return false;
}

void ALSShellProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bLaunched) return;
	if (!Coordinator.IsValid() || !Coordinator->IsBattleActive()) { Destroy(); return; }
	const FVector Start = GetActorLocation();
	float Step = DeltaSeconds;
	if (Shot.bChild) Step = FMath::Min3<double>(Step, FMath::Max(0.f, 3 - Age), FMath::Max(0.f, 3000 - DistanceTravelled) / Velocity.Size());
	if (Step <= 0) { Destroy(); return; }
	const ALSBattlefieldSettings* Settings = ALSBattlefieldSettings::Find(GetWorld());
	if (Settings)
	{
		const FBox2D Bounds = Settings->GetWorldBounds();
		if (!Bounds.IsInside(FVector2D(Start))) { Destroy(); return; }
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			if (Velocity[Axis] > 0) Step = FMath::Min<double>(Step, (Bounds.Max[Axis] - Start[Axis]) / Velocity[Axis]);
			if (Velocity[Axis] < 0) Step = FMath::Min<double>(Step, (Bounds.Min[Axis] - Start[Axis]) / Velocity[Axis]);
		}
	}
	const FVector End = Start + Velocity * Step;
	FHitResult Hit;
	if (SweepFlight(GetWorld(), Start, End, Shot.Source.Get(), Hit))
	{
		Coordinator->QueueShellHit(Shot, Hit);
		Destroy();
		return;
	}
	SetActorLocation(End);
	Trail->SetWorldLocation(End - Velocity.GetSafeNormal() * 60);
	Trail->SetWorldRotation(FRotationMatrix::MakeFromZ(Velocity).Rotator());
	Trail->SetWorldScale3D(FVector(0.06, 0.06, 1.2));
	Age += Step;
	DistanceTravelled += FVector::Distance(Start, End);
	const bool bOutside = Settings && !Settings->GetWorldBounds().IsInside(FVector2D(End));
	if (bOutside || Step < DeltaSeconds || (Shot.bChild && (Age >= 3 || DistanceTravelled >= 3000))) Destroy();
}

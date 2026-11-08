#include "Controls/LSFreeCameraPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Match/LSAutoBattleScenario.h"
#include "EngineUtils.h"
#include "Engine/World.h"

ALSFreeCameraPawn::ALSFreeCameraPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	Body = CreateDefaultSubobject<USphereComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->InitSphereRadius(20);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionObjectType(ECC_Camera);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::Ground, ECR_Block);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::Building, ECR_Block);
	Body->SetCanEverAffectNavigation(false);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Body);
	Camera->FieldOfView = 80;
	Camera->bConstrainAspectRatio = false;
}

void ALSFreeCameraPawn::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<ALSAutoBattleScenario> It(GetWorld()); It; ++It)
	{
		BoundsMin = FVector(It->BoundsMin.X - 300, It->BoundsMin.Y - 300, 40);
		BoundsMax = FVector(It->BoundsMax.X + 300, It->BoundsMax.Y + 300, 3500);
		HomeLocation = It->CameraLocation;
		HomeRotation = It->CameraRotation;
		break;
	}
	ResetView();
}

void ALSFreeCameraPawn::MoveCamera(const FVector& LocalDirection, float DeltaSeconds, bool bFast)
{
	if (LocalDirection.ContainsNaN() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0)
		return;
	const FVector Direction = GetActorForwardVector() * LocalDirection.X + GetActorRightVector() * LocalDirection.Y +
	                          FVector::UpVector * LocalDirection.Z;
	const FVector Goal =
	    GetActorLocation() + Direction.GetClampedToMaxSize(1) * MoveSpeed * (bFast ? FastMultiplier : 1) * DeltaSeconds;
	SetActorLocation(Goal.BoundToBox(BoundsMin, BoundsMax), true);
}

void ALSFreeCameraPawn::Look(const FVector2D& MouseDelta)
{
	if (MouseDelta.ContainsNaN())
		return;
	FRotator Rotation = GetActorRotation();
	Rotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + MouseDelta.X * LookSensitivity);
	// 게임 뷰포트의 MouseY는 위쪽이 양수다. 에디터 기본 마우스 보기와 같은 방향을 쓴다.
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + MouseDelta.Y * LookSensitivity, -85.0f, 85.0f);
	Rotation.Roll = 0;
	SetActorRotation(Rotation);
}

void ALSFreeCameraPawn::Pan(const FVector2D& MouseDelta)
{
	if (MouseDelta.ContainsNaN()) return;
	const FVector Delta = (GetActorRightVector() * MouseDelta.X + GetActorUpVector() * MouseDelta.Y) * PanUnitsPerPixel;
	SetActorLocation((GetActorLocation() + Delta).BoundToBox(BoundsMin, BoundsMax), true);
}

void ALSFreeCameraPawn::Dolly(float Steps)
{
	if (!FMath::IsFinite(Steps)) return;
	SetActorLocation((GetActorLocation() + GetActorForwardVector() * Steps * DollyStep).BoundToBox(BoundsMin, BoundsMax), true);
}

void ALSFreeCameraPawn::ChangeSpeed(float Steps)
{
	if (FMath::IsFinite(Steps))
		MoveSpeed = FMath::Clamp(MoveSpeed * FMath::Pow(1.2f, Steps), MinSpeed, MaxSpeed);
}

void ALSFreeCameraPawn::ResetView()
{
	bTacticalView = false;
	bBlendingView = false;
	Camera->SetProjectionMode(ECameraProjectionMode::Perspective);
	SetActorLocationAndRotation(HomeLocation.BoundToBox(BoundsMin, BoundsMax), HomeRotation);
}

void ALSFreeCameraPawn::SetHomeView(const FVector& Location, const FRotator& Rotation)
{
	HomeLocation = Location;
	HomeRotation = Rotation;
	ResetView();
}

void ALSFreeCameraPawn::FollowCastle(const AActor& Castle, float DeltaSeconds, bool bSnap)
{
	if (bTacticalView)
	{
		bTacticalView = false;
		bBlendingView = false;
		bSnap = true;
	}
	const FVector Pivot = Castle.GetActorLocation() + FVector(0, 0, 120);
	const FVector Desired = Pivot - Castle.GetActorForwardVector() * CastleCameraDistance +
	                        FVector(0, 0, CastleCameraHeight);
	FVector Position = bSnap ? Desired : FMath::VInterpTo(GetActorLocation(), Desired, DeltaSeconds, 8.0f);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::PathObstacle);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSCastleFollowCamera), false, this);
	Query.AddIgnoredActor(&Castle);
	FHitResult Hit;
	if (GetWorld()->SweepSingleByObjectType(Hit, Pivot, Position, FQuat::Identity, Objects,
	                                      FCollisionShape::MakeSphere(Body->GetScaledSphereRadius()), Query))
	{
		Position = Hit.Location;
	}
	const FVector Look = Pivot - Position;
	const FQuat Rotation = Look.IsNearlyZero() ? GetActorQuat() : Look.Rotation().Quaternion();
	SetActorLocationAndRotation(Position, Rotation);
}

void ALSFreeCameraPawn::SetTacticalView(const FVector2D& BattlefieldSize, float AspectRatio)
{
	// 원근 투영을 유지해 카메라가 올라가는 동안 화면 투영이 갑자기 바뀌지 않게 한다.
	const double Width = FMath::Max(BattlefieldSize.X * 1.45, BattlefieldSize.Y * AspectRatio * 1.12) * 0.5;
	const double HalfFov = FMath::DegreesToRadians(static_cast<double>(Camera->FieldOfView) * 0.5);
	TacticalHeight = Width / (2.0 * FMath::Tan(HalfFov));
	BlendStartLocation = GetActorLocation();
	BlendStartPitch = GetActorRotation().Pitch;
	ViewBlendElapsed = 0;
	bBlendingView = true;
	bTacticalView = true;
}

void ALSFreeCameraPawn::UpdateTacticalView(const AActor& Castle, float DeltaSeconds)
{
	if (!bTacticalView)
		return;
	FVector Location = Castle.GetActorLocation() + FVector(0, 0, TacticalHeight);
	double Pitch = -90.0;
	if (bBlendingView)
	{
		ViewBlendElapsed += DeltaSeconds;
		const float Progress = FMath::Clamp(ViewBlendElapsed / FMath::Max(ViewBlendDuration, 0.01f), 0.0f, 1.0f);
		const float Alpha = FMath::SmoothStep(0.0f, 1.0f, Progress);
		Location = FMath::Lerp(BlendStartLocation, Location, Alpha);
		Pitch = FMath::Lerp(BlendStartPitch, Pitch, Alpha);
		bBlendingView = Progress < 1.0f;
	}
	SetActorLocationAndRotation(Location, FRotator(Pitch, Castle.GetActorRotation().Yaw, 0.0));
}

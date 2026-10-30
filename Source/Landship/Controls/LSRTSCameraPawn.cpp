#include "Controls/LSRTSCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/SpringArmComponent.h"

ALSRTSCameraPawn::ALSRTSCameraPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetRelativeRotation(FRotator(-55, 0, 0));
	CameraBoom->TargetArmLength = 3600.0f;
	// 전장 장애물이 카메라의 줌 거리를 임의로 바꾸지 않도록 한다.
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bUsePawnControlRotation = false;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	Camera->FieldOfView = 55.0f;
	Camera->bConstrainAspectRatio = false;
}

void ALSRTSCameraPawn::BeginPlay()
{
	Super::BeginPlay();
	FocusLocation(GetActorLocation());
	Zoom(0.0f);
}

FVector ALSRTSCameraPawn::ScreenToWorldDirection(const FVector2D& Direction) const
{
	const FRotator Heading(0, CameraBoom->GetComponentRotation().Yaw, 0);
	return Heading.Vector() * Direction.Y + FRotationMatrix(Heading).GetUnitAxis(EAxis::Y) * Direction.X;
}

void ALSRTSCameraPawn::Pan(const FVector2D& ScreenDirection, float DeltaSeconds)
{
	if (ScreenDirection.ContainsNaN() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0) { return; }
	FocusLocation(GetActorLocation() + ScreenToWorldDirection(ScreenDirection.GetClampedToMaxSize(1.0f)) * PanSpeed * DeltaSeconds);
}

void ALSRTSCameraPawn::Drag(const FVector2D& MouseDelta)
{
	if (MouseDelta.ContainsNaN()) { return; }
	// 지면을 잡아 끄는 방향으로 움직이고 줌 거리에 비례해 감도를 조절한다.
	const FVector2D Direction(-MouseDelta.X, MouseDelta.Y);
	FocusLocation(GetActorLocation() + ScreenToWorldDirection(Direction) * DragUnitsPerPixel * GetZoomDistance() / 2400.0f);
}

void ALSRTSCameraPawn::Zoom(float WheelDelta)
{
	if (!FMath::IsFinite(WheelDelta)) { return; }
	const float Min = FMath::Max(1.0f, MinZoomDistance);
	const float Max = FMath::Max(Min, MaxZoomDistance);
	CameraBoom->TargetArmLength = FMath::Clamp(CameraBoom->TargetArmLength - WheelDelta * ZoomStep, Min, Max);
}

float ALSRTSCameraPawn::GetZoomDistance() const
{
	return CameraBoom->TargetArmLength;
}

void ALSRTSCameraPawn::FocusLocation(const FVector& Location)
{
	if (Location.ContainsNaN()) { return; }
	const double X = FMath::Clamp(Location.X, FMath::Min(BoundsMin.X, BoundsMax.X), FMath::Max(BoundsMin.X, BoundsMax.X));
	const double Y = FMath::Clamp(Location.Y, FMath::Min(BoundsMin.Y, BoundsMax.Y), FMath::Max(BoundsMin.Y, BoundsMax.Y));
	SetActorLocation(FVector(X, Y, FocusHeight));
}

FVector2D ALSRTSCameraPawn::CalculateEdgeDirection(const FVector2D& Mouse, const FVector2D& Size, float Margin)
{
	if (Mouse.ContainsNaN() || Size.ContainsNaN() || Size.X <= 0 || Size.Y <= 0 || !FMath::IsFinite(Margin)
		|| Margin <= 0 || Mouse.X < 0 || Mouse.Y < 0 || Mouse.X >= Size.X || Mouse.Y >= Size.Y) { return FVector2D::ZeroVector; }
	const double Band = FMath::Min(static_cast<double>(Margin), FMath::Min(Size.X, Size.Y) * 0.5);
	return FVector2D(Mouse.X < Band ? -1 : (Mouse.X >= Size.X - Band ? 1 : 0),
		Mouse.Y < Band ? 1 : (Mouse.Y >= Size.Y - Band ? -1 : 0));
}

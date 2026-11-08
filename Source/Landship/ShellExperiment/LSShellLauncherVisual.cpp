#include "LSShellLauncherVisual.h"
#include "Castle/LSCastle.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

ULSShellLauncherVisualComponent::ULSShellLauncherVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void ULSShellLauncherVisualComponent::Initialize(ALSCastle* InCastle)
{
	Castle = InCastle;
	if (Base || !IsValid(InCastle) || !GetOwner() || !GetOwner()->GetRootComponent()) return;
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* MetalBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype"));
	UMaterialInstanceDynamic* Metal = MetalBase ? UMaterialInstanceDynamic::Create(MetalBase, this) : nullptr;
	if (Metal) Metal->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(0.035f, 0.045f, 0.06f));
	Base = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("ShellLauncherBase"));
	Barrel = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("ShellLauncherBarrel"));
	MuzzleRim = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("ShellLauncherMuzzleRim"));
	MuzzleFlash = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("ShellLauncherMuzzleFlash"));
	for (UStaticMeshComponent* Mesh : {Base.Get(), Barrel.Get(), MuzzleRim.Get(), MuzzleFlash.Get()})
	{
		GetOwner()->AddInstanceComponent(Mesh);
		Mesh->SetupAttachment(GetOwner()->GetRootComponent());
		// 메시 지정이 nav 등록을 요청하기 전에 표현용 설정을 적용한다.
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetStaticMesh(Mesh == MuzzleFlash ? Sphere : Cylinder);
		Mesh->SetCastShadow(false);
		if (Metal) Mesh->SetMaterial(0, Metal);
		Mesh->RegisterComponent();
	}
	UMaterialInterface* EffectBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Landship/ShellExperiment/Materials/M_ShellEffect.M_ShellEffect"));
	if (EffectBase)
	{
		FlashMaterial = UMaterialInstanceDynamic::Create(EffectBase, this);
		FlashMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(4, 2.2f, 0.5f));
		MuzzleFlash->SetMaterial(0, FlashMaterial);
	}
	MuzzleFlash->SetVisibility(false);
}

void ULSShellLauncherVisualComponent::SetPose(FVector Origin, FVector Muzzle, FVector Direction)
{
	PoseOrigin = Origin;
	PoseMuzzle = Muzzle;
	PoseDirection = Direction.GetSafeNormal();
	UpdateMeshes();
}

void ULSShellLauncherVisualComponent::UpdateMeshes()
{
	if (!Base) return;
	const float ReturnT = FMath::Clamp(RecoilAge / (0.25f * FMath::Sqrt(RecoilScale)), 0.f, 1.f);
	const float Retraction = 60 * RecoilScale * (1 - ReturnT) * (1 - ReturnT);
	const FRotator Rotation = FRotationMatrix::MakeFromZ(PoseDirection).Rotator();
	Base->SetWorldLocation(PoseOrigin);
	Base->SetWorldRotation(Rotation);
	Base->SetWorldScale3D(FVector(1.5f, 1.5f, 0.7f));
	Barrel->SetWorldLocation(PoseMuzzle - PoseDirection * (90 + Retraction));
	Barrel->SetWorldRotation(Rotation);
	Barrel->SetWorldScale3D(FVector(0.7f, 0.7f, 1.8f));
	MuzzleRim->SetWorldLocation(PoseMuzzle - PoseDirection * (8 + Retraction));
	MuzzleRim->SetWorldRotation(Rotation);
	MuzzleRim->SetWorldScale3D(FVector(0.95f, 0.95f, 0.16f));
	MuzzleFlash->SetWorldLocation(PoseMuzzle);
	MuzzleFlash->SetWorldScale3D(FVector(1.25f * FMath::Sqrt(RecoilScale) * (1 - ReturnT)));
	MuzzleFlash->SetVisibility(RecoilAge < 0.07f * FMath::Sqrt(RecoilScale));
	if (FlashMaterial) FlashMaterial->SetScalarParameterValue(TEXT("Opacity"), 1 - ReturnT);
}

void ULSShellLauncherVisualComponent::Fire(float Strength)
{
	if (!Base || !Castle.IsValid()) return;
	RecoilScale = FMath::Clamp(Strength, .1f, 3.f);
	RecoilAge = 0;
	SetComponentTickEnabled(true);
	UpdateMeshes();
}

void ULSShellLauncherVisualComponent::Hide()
{
	SetComponentTickEnabled(false);
	for (UStaticMeshComponent* Mesh : {Base.Get(), Barrel.Get(), MuzzleRim.Get(), MuzzleFlash.Get()})
		if (Mesh) Mesh->SetVisibility(false);
}

void ULSShellLauncherVisualComponent::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, TickFunction);
	RecoilAge += DeltaSeconds;
	UpdateMeshes();
	if (RecoilAge >= 0.25f * FMath::Sqrt(RecoilScale)) SetComponentTickEnabled(false);
}

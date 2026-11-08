#pragma once

#include "CoreMinimal.h"
#include "ShellExperiment/LSShellPhysicalProp.h"
#include "LSShellMaterials.generated.h"

class UBoxComponent;
class UPhysicalMaterial;
class UPhysicsConstraintComponent;
class UShapeComponent;
class ULSShellMaterialStateComponent;
class ULSShellNavigationBodyComponent;

/** 충돌·질량·재질 설정을 공유한다. 시뮬레이션은 끈 상태로 반환하며 호출자가 활성화한다. */
LANDSHIP_API void LSShellConfigureMaterialBody(UPrimitiveComponent* Body, UPhysicalMaterial* Material,
	float MassKg, float Friction, float Restitution, float LinearDamping, float AngularDamping);

enum class ELSShellBodyShape : uint8 { Capsule, Sphere, Box };

/** 사망 훅이 몸체를 중단하기 전에 캡처한다. 풀이 뒤 속도에 아직 소비하지 않은 충격을 더한다. */
struct LANDSHIP_API FLSShellRigidBodyState
{
	FTransform Transform = FTransform::Identity;
	FVector LinearVelocity = FVector::ZeroVector;
	FVector AngularVelocity = FVector::ZeroVector;
	/** 실제 원본 관성으로 캡처한 값이다. 없으면 기존 각속도를 그대로 복원한다. */
	FVector AngularMomentum = FVector::ZeroVector;
	bool bHasAngularMomentum = false;
	FVector ShapeExtent = FVector(42, 42, 88);
	float MassKg = 100;
	ELSShellBodyShape Shape = ELSShellBodyShape::Capsule;
	bool bConductiveRelay = false;
	bool bCanStoreCharge = true;
	bool bHasSurvivingBonds = false;
	bool bThrustPropelled = false;
	TWeakObjectPtr<AActor> OriginalCollisionOwner;
	bool bValid = false;
};

UCLASS(Abstract)
class LANDSHIP_API ALSShellInertMaterial : public AActor
{
	GENERATED_BODY()
public:
	ALSShellInertMaterial();
	/** Deferred 생성 중 한 번 준비한다. 체력·공격·기폭 페이로드를 만들지 않는다. */
	virtual bool InitializeBody(const FLSShellRigidBodyState& State);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<ULSShellReactiveComponent> Reactive;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<ULSShellMaterialStateComponent> MaterialState;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<ULSShellNavigationBodyComponent> NavigationBody;
	UPrimitiveComponent* GetPhysicalBody() const;
protected:
	virtual void BeginPlay() override;
	UPROPERTY() TObjectPtr<UShapeComponent> Body;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY() TObjectPtr<UPhysicalMaterial> BodyMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> VisualMaterialInstance;
	FLinearColor MaterialColor = FLinearColor(.35f, .7f, 1);
	float InertFriction = .35f;
	FLSShellRigidBodyState InitialState;
};

UCLASS()
class LANDSHIP_API ALSShellInertIce : public ALSShellInertMaterial
{
	GENERATED_BODY()
public:
	ALSShellInertIce();
};

UCLASS()
class LANDSHIP_API ALSShellSiegeCarriage : public ALSShellInertMaterial
{
	GENERATED_BODY()
public:
	ALSShellSiegeCarriage();
	virtual bool InitializeBody(const FLSShellRigidBodyState& State) override;
	/** Heavy 캡슐140/140과 몸통 시각형상120/120/110 반크기의 비율이다. */
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Carriage")
	FVector CapturedExtentScale = FVector(120.f / 140, 120.f / 140, 110.f / 140);
};

UCLASS(Blueprintable)
class LANDSHIP_API ALSShellPlate : public ALSShellPhysicalProp
{
	GENERATED_BODY()
public:
	ALSShellPlate();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UBoxComponent> Body;
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Plate") FVector HalfExtent = FVector(15, 170, 120);
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Plate") bool bInitiallySupported = true;
	UFUNCTION(BlueprintPure) bool IsSupported() const;
protected:
	virtual void BeginPlay() override;
	virtual void RefreshGeometry() override;
	virtual void PrepareForDeath() override;
	virtual bool RetainsPhysicalBodyOnDeath() const override { return true; }
private:
	UPROPERTY() TObjectPtr<UPhysicsConstraintComponent> Support;
};

UCLASS(Blueprintable)
class LANDSHIP_API ALSShellBattery : public ALSShellPhysicalProp
{
	GENERATED_BODY()
public:
	ALSShellBattery();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UBoxComponent> Body;
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Battery") FVector HalfExtent = FVector(40, 45, 55);
	/** 조정자가 고유 출처 패킷으로 초기 전하를 넣은 뒤에만 호출한다. 사망·재사용으로 다시 열리지 않는다. */
	bool CommitInitialCharge();
	bool NeedsInitialCharge() const { return !bInitialChargeCommitted; }
protected:
	virtual void RefreshGeometry() override;
	virtual bool RetainsPhysicalBodyOnDeath() const override { return true; }
private:
	bool bInitialChargeCommitted = false;
};

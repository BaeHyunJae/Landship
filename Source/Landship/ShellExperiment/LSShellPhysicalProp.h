#pragma once

#include "CoreMinimal.h"
#include "Obstacles/LSPathObstacle.h"
#include "LSShellExperimentTypes.h"
#include "LSShellPhysicalProp.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPhysicalMaterial;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;
class ULSShellReactiveComponent;
class ULSShellMaterialStateComponent;
class ULSShellNavigationBodyComponent;
struct FLSDamageResult;

UCLASS(Abstract, Blueprintable)
class LANDSHIP_API ALSShellPhysicalProp : public ALSPathObstacle
{
	GENERATED_BODY()
public:
	ALSShellPhysicalProp();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual FString GetConfigurationError() const override;
	virtual bool IsObstacleActive() const override;
	UPrimitiveComponent* GetPhysicalBody() const { return GetObstacleBody(); }
	bool DoesRetainPhysicalBodyOnDeath() const { return RetainsPhysicalBodyOnDeath(); }
	/** Deferred 생성 중 양수 체력 예산만 설정한다. BeginPlay 이후에는 체력이나 상태를 바꾸지 않는다. */
	bool SetGeneratedHealthBeforeBeginPlay(float Value);
	/** 결속·접촉 준비가 끝난 뒤 시뮬레이션을 명시적으로 시작한다. */
	bool ActivatePhysicalBody();
	/** 속도와 몸체를 바꾸지 않고 현재 마찰·반발·감쇠 값만 솔버에 반영한다. */
	void UpdatePhysicalMaterial();
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Physics") bool bDeferPhysicsActivation = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment")
	TObjectPtr<ULSShellReactiveComponent> Reactive;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment")
	TObjectPtr<ULSShellMaterialStateComponent> MaterialState;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment")
	TObjectPtr<ULSShellNavigationBodyComponent> NavigationBody;
	/** 물리 솔버가 사용하는 질량(kg)이다. Reactive의 무차원 Mass와 별개다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shell Experiment|Physics", meta = (ClampMin = "0.01", Units = "kg"))
	float IntrinsicMassKg = 300;
	/** 정지 마찰에도 같은 값을 사용한다. 결합 방식은 Min으로 고정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shell Experiment|Physics", meta = (ClampMin = "0"))
	float Friction = .35f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shell Experiment|Physics", meta = (ClampMin = "0", ClampMax = "1"))
	float Restitution = .2f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shell Experiment|Physics", meta = (ClampMin = "0"))
	float LinearDamping = .01f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shell Experiment|Physics", meta = (ClampMin = "0"))
	float AngularDamping = .01f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shell Experiment|Visual")
	FLinearColor BodyColor = FLinearColor(.28f, .33f, .4f);

protected:
	virtual void BeginPlay() override;
	virtual void PrepareForDeath() override;
	virtual void RefreshGeometry();
	virtual bool RetainsPhysicalBodyOnDeath() const { return false; }
	void ConfigurePhysicalBody();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment|Physics")
	TObjectPtr<UPhysicalMaterial> BodyMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> VisualMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BodyVisualMaterial;
private:
	UFUNCTION() void OnMaterialDied(const FLSDamageResult& Damage);
};

UCLASS(Blueprintable)
class LANDSHIP_API ALSShellMetalBall : public ALSShellPhysicalProp
{
	GENERATED_BODY()
public:
	ALSShellMetalBall();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment")
	TObjectPtr<USphereComponent> Body;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shell Experiment|Physics", meta = (ClampMin = "0.01", Units = "cm"))
	float Radius = 70;
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback")
	FLSShellFeedbackTuning ShatterFeedback = FLSShellFeedbackTuning(.4f, .35f, .07f, .65f);
protected:
	virtual void RefreshGeometry() override;
	virtual void PrepareForDeath() override;
private:
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SpinMarks;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SpinMarkMaterial;
};

UCLASS(Blueprintable)
class LANDSHIP_API ALSShellBarrel : public ALSShellPhysicalProp
{
	GENERATED_BODY()
public:
	ALSShellBarrel();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment")
	TObjectPtr<UBoxComponent> Body;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shell Experiment|Physics", meta = (ClampMin = "0.01", Units = "cm"))
	FVector HalfExtent = FVector(45, 45, 60);
protected:
	virtual void RefreshGeometry() override;
private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> UpperBand;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> LowerBand;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BandVisualMaterial;
};

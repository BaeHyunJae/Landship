#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthTypes.h"
#include "LSRTSHUD.generated.h"

enum class ELSCastleWeapon : uint8;
class ULSMinimapWidget;
class ULSBattleHUDWidget;
class SWidget;
class UFont;
class ULSHealthComponent;
class UMeshComponent;
class UMaterialInstanceDynamic;
class ALSBasicProjectile;
class ALSCastleProjectile;
class ULSCastleWeaponComponent;

USTRUCT()
struct FLSHitOverlay
{
	GENERATED_BODY()
	TWeakObjectPtr<UMeshComponent> Mesh;
	TWeakObjectPtr<AActor> Actor;
	UPROPERTY() TObjectPtr<UMaterialInterface> Previous;
	double ExpiresAt = 0;
	uint64 StartedFrame = 0;
};

struct FLSCombatHealthDisplay
{
	float BeforeFraction = 1;
	float AfterFraction = 1;
	double PresentationTime = -1;
	float GetGhostFraction(double Now) const
	{
		return PresentationTime < 0 ? AfterFraction : FMath::Lerp(BeforeFraction, AfterFraction,
			FMath::Clamp(static_cast<float>((Now - PresentationTime - 0.2) / 0.35), 0.0f, 1.0f));
	}
};

struct FLSCombatAttackPresentation
{
	FLSAttackExecution Execution;
	double ReceivedAt = 0;
	uint64 StartedFrame = 0;
};

struct FLSCombatDamagePresentation
{
	FLSDamageResult Damage;
	double ReceivedAt = 0;
};

/** 선택과 명령 상태를 확인하기 위한 기본 HUD. 최종 아트·위젯과 분리한다. */
UCLASS()
class LANDSHIP_API ALSRTSHUD : public AHUD
{
	GENERATED_BODY()
  public:
	ALSRTSHUD();
	virtual void DrawHUD() override;
	virtual void Tick(float DeltaSeconds) override;
	static bool UsesUnifiedCombatFeedback(UWorld* World);
	void EndBattleActions();
	bool IsOverPanel(const FVector2D& Mouse) const;
	bool IsOverResultPanel(const FVector2D& Mouse) const;
	FBox2D ResultPanel() const;
	bool HandleClick(const FVector2D& Mouse);
	FBox2D GetCommandCellRect(int32 Index) const;
	/** 지정된 전장 성이 없으면 유효하지 않은 사각형을 반환한다. */
	FBox2D GetCastleStatusRect() const;
	bool GetMinimapWorldPoint(FVector& OutLocation) const;
	bool IsOverMinimap(const FVector2D& Mouse) const;
	bool IsMinimapPointerCaptured() const;
	bool IsMinimapWidget(const TSharedRef<SWidget>& Widget) const;
	bool IsBattleWidget(const TSharedRef<SWidget>& Widget) const;
	ULSBattleHUDWidget* GetBattleWidget() const { return BattleWidget; }

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Vision")
	TSubclassOf<ULSMinimapWidget> MinimapWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<ULSMinimapWidget> MinimapWidget;
	UPROPERTY(Transient)
	TObjectPtr<UFont> ResultFont;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|HUD")
	TSoftClassPtr<ULSBattleHUDWidget> BattleWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<ULSBattleHUDWidget> BattleWidget;

  private:
	friend class FLSUnifiedCombatFeedbackTest;
	void BindCombatActor(AActor* Actor);
	void OnDamageCommitted(const FLSDamageResult& Damage);
	UFUNCTION() void OnAttackExecuted(const FLSAttackExecution& Execution);
	void DrawCombatFeedback(const TArray<AActor*>& Selection);
	void RestoreOverlay(int32 Index);
	void FlashHit(AActor* Actor, double ExpiresAt);
	bool IsFeedbackVisible(const AActor* Actor) const;
	bool IsFeedbackPointVisible(const FVector& Point) const;
	FBox GetVisualBodyBounds(AActor* Actor) const;
	void DrawClippedProjectileTail(const FVector& Head, const FVector& Direction, float Length, const FLinearColor& Color);
	void DrawBattleResult();
	FBox2D InfoPanel() const;
	FBox2D CommandPanel() const;
	void DrawSelectionRing(AActor* Actor, const FLinearColor& Color);
	void DrawWeaponShape(class ALSCastle* Castle, ELSCastleWeapon Weapon, const FVector& Aim,
	                     const FLinearColor& Color, bool bPreview,
	                     const struct FLSCastleWeaponSettings* Snapshot = nullptr);
	bool bCombatFeedbackActive = false;
	FDelegateHandle ActorSpawnedHandle;
	TArray<TWeakObjectPtr<ULSHealthComponent>> BoundHealth;
	TArray<TWeakObjectPtr<ULSBasicAttackComponent>> BoundAttacks;
	TArray<TWeakObjectPtr<ALSBasicProjectile>> Projectiles;
	TArray<TWeakObjectPtr<ALSCastleProjectile>> CastleProjectiles;
	TArray<TWeakObjectPtr<ULSCastleWeaponComponent>> BoundWeapons;
	TMap<TWeakObjectPtr<AActor>, FLSCombatHealthDisplay> DamagedActors;
	TArray<FLSCombatAttackPresentation> AttackFeedback;
	TArray<FLSCombatDamagePresentation> DamageFeedback;
	TArray<FLSAttackExecution> PendingAttacks;
	TArray<FLSDamageResult> PendingDamage;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HitMaterial;
	UPROPERTY(Transient) TArray<FLSHitOverlay> HitOverlays;
	void DrawWorldLine(const FVector& Start, const FVector& End, const FLinearColor& Color, float Thickness = 2);
};

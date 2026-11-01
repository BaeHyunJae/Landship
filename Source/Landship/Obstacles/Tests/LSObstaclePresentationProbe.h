#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSObstaclePresentationProbe.generated.h"
class ALSMine;
class ALSTroopUnit;
class ALSRTSCameraPawn;
UCLASS()
class LANDSHIP_API ALSObstaclePresentationProbe : public AActor
{
	GENERATED_BODY()
  public:
	ALSObstaclePresentationProbe();

  protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

  private:
	void Finish(bool bPassed, const TCHAR* Reason);
	TWeakObjectPtr<ALSMine> Mine;
	TWeakObjectPtr<ALSTroopUnit> Scout;
	TWeakObjectPtr<ALSRTSCameraPawn> Camera;
	FString ScreenshotPath;
	float Elapsed = 0;
	float AcceptedAt = -1;
	bool bInitialized = false;
	bool bSubmitted = false;
	bool bScreenshotRequested = false;
	bool bFinished = false;
};

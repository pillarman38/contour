#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "Components/SplineMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "BallTrailSplineActor.generated.h"

class AUDPGolfReceiver;

/**
 * Draws a trail (spline mesh segments) following the ball's path.
 * Uses M_Trail material by default. Persist and fade duration are configurable in the editor.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Ball Trail Spline"))
class GOLFSIMUE_API ABallTrailSplineActor : public AActor
{
	GENERATED_BODY()

public:
	ABallTrailSplineActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallTrail|Actors")
	bool bEnableTrail = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "BallTrail|Actors", meta = (AllowAbstract = "false"))
	AUDPGolfReceiver* GolfReceiver = nullptr;

	/** Ball this trail follows (required for correct multi-ball trails; legacy "ball" / BallData is tracker balls[0] only). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "BallTrail|Actors", meta = (AllowAbstract = "false"))
	AActor* BallActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallTrail|Rendering")
	UStaticMesh* TrailMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallTrail|Rendering")
	UMaterialInterface* TrailMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallTrail|Behaviour", meta = (ClampMin = "0.0", Tooltip = "Seconds the trail stays visible after ball stops. 0 = hide immediately."))
	float TrailPersistSeconds = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallTrail|Behaviour", meta = (ClampMin = "0.0", Tooltip = "How long the trail takes to fade out. 0 = instant. Material needs 'Opacity' param."))
	float FadeOutDurationSeconds = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallTrail|Behaviour", meta = (ClampMin = "2", ClampMax = "256", Tooltip = "Max trail points (segments = points - 1)."))
	int32 MaxTrailPoints = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallTrail|Behaviour", meta = (ClampMin = "0.1", Tooltip = "Min distance (Unreal units) between trail samples."))
	float SampleDistanceThreshold = 2.f;

	/** If the ball jumps farther than this between samples (e.g. tracker slot reassignment when a new ball appears), clear the trail so we do not draw a long connector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallTrail|Behaviour", meta = (ClampMin = "10.0"))
	float TeleportClearThreshold = 120.f;

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(Transient)
	USceneComponent* RootScene = nullptr;

	TArray<USplineMeshComponent*> SplineMeshPool;
	TArray<FVector> TrailPoints;
	float StoppedElapsedSeconds = 0.f;
	int32 LastPuttNumber = -1;
	/** Last BallsData.Num() from receiver; used to clear trail when balls are added/removed. */
	int32 LastBallsDataCount = -1;
	bool bWasInMotion = false;
	bool bIsFadingOut = false;
	float FadeOutElapsedSeconds = 0.f;

	UPROPERTY(Transient)
	TArray<UMaterialInstanceDynamic*> TrailDynamicMaterials;

	void RebuildSplineMeshes();
	void ClearTrail();
	void SetTrailOpacity(float Opacity);
	void EnsureTrailMaterials();
};

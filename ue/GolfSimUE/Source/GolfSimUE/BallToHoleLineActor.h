#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "BallToHoleLineActor.generated.h"

class AUDPGolfReceiver;

/**
 * Draws a line between the ball and hole that follows the green contour.
 * Samples points between ball and hole, line-traces down to the surface,
 * and builds a spline (with optional spline mesh segments) for rendering.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Ball to Hole Line"))
class GOLFSIMUE_API ABallToHoleLineActor : public AActor
{
	GENERATED_BODY()

public:
	ABallToHoleLineActor();

	/** When false, the line does nothing (no segments, no updates). Set true to enable. Reduces load-time work when opening the project. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Actors")
	bool bEnableLine = false;

	/** If set, ball and hole are taken from this receiver (no need to set Ball/Hole below). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "BallToHole|Actors", meta = (AllowAbstract = "false"))
	AUDPGolfReceiver* GolfReceiver = nullptr;

	/** Ball actor. Ignored if Golf Receiver is set. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "BallToHole|Actors", meta = (AllowAbstract = "false"))
	AActor* BallActor = nullptr;

	/** Hole actor. Ignored if Golf Receiver is set. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "BallToHole|Actors", meta = (AllowAbstract = "false"))
	AActor* HoleActor = nullptr;

	/**
	 * Which entry in GolfReceiver->BallsData drives this line (0 = first ball, 1 = second, ...).
	 * Duplicate this actor in the level for multiple players; set 0 / 1 / ... on each instance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Actors", meta = (ClampMin = "0"))
	int32 AimLineBallSortedIndex = 0;

	/** Number of sample points between ball and hole (including endpoints). Min 2, max 32. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Trace", meta = (ClampMin = "2", ClampMax = "32"))
	int32 NumSamplePoints = 16;

	/** Height above start point to begin the downward trace (world units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Trace")
	float TraceHeightAbove = 500.f;

	/** Length of the downward trace (world units). Total trace length = 2 * TraceHalfLength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Trace")
	float TraceHalfLength = 1000.f;

	/** Collision channel for the line trace (e.g. Visibility, WorldStatic). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/** Static mesh used for the straight line between ball and hole (aligned along X in the mesh). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Rendering")
	UStaticMesh* LineMesh = nullptr;

	/** Length of the line mesh along its X axis, in Unreal units (used to scale it to the ball‑hole distance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Rendering", meta = (ClampMin = "1.0"))
	float LineMeshBaseLength = 100.f;

	/** When true, displays the distance to the hole in inches on the line. Requires a TextRenderComponent (created by default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Rendering", meta = (Tooltip = "Show distance to hole in inches at the midpoint of the line."))
	bool bShowDistanceInches = true;

	/** When using Golf Receiver: line only updates when putt state is "idle" (ball stationary, putt reset). When not using a receiver, line is frozen if ball moves more than this (Unreal units) per frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Freeze", meta = (ClampMin = "0.0"))
	float RollingThreshold = 5.f;

	/** How long the ball-to-hole line stays visible after the ball starts moving (seconds). 0 = never hide (line stays until ball stops). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Freeze", meta = (ClampMin = "0.0", Tooltip = "Seconds the line remains visible after ball enters in_motion. 0 = keep visible until ball stops."))
	float TracePersistSeconds = 20.f;

	/** Minimum seconds of continuous idle/stopped before the persist timer resets. Prevents state flicker from resetting the timer during a putt. Increase if line disappears too soon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Freeze", meta = (ClampMin = "1.0", ClampMax = "60.0", Tooltip = "Require this many seconds of idle/stopped before resetting the persist timer. Use 15–30 to avoid resets during normal play."))
	float IdleStoppedDebounceSeconds = 15.f;

	/** Seconds of ball+hole both not detected before line is hidden. When neither is visible (e.g. camera moved off green), the line fades/hides so pointers don't stay forever. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Freeze", meta = (ClampMin = "0.0", Tooltip = "When both ball and hole are not detected, hide line after this delay."))
	float BothLostHideDelaySeconds = 2.f;

	/** Duration of the fade-out when the line is hidden (seconds). 0 = instant hide. Requires line mesh material to have an "Opacity" scalar parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Rendering", meta = (ClampMin = "0.0", Tooltip = "How long the line takes to fade out when hiding. 0 = instant. Material needs 'Opacity' param."))
	float FadeOutDurationSeconds = 2.f;

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Created when line is first enabled; not in constructor to avoid load-time work. */
	UPROPERTY(Transient, VisibleAnywhere, Category = "BallToHole")
	USplineComponent* SplineComponent;

	/** Simple static mesh line between ball and hole (visual only). */
	UPROPERTY(VisibleAnywhere, Category = "BallToHole")
	UStaticMeshComponent* LineMeshComponent = nullptr;

	/** Text component showing distance to hole in inches. */
	UPROPERTY(VisibleAnywhere, Category = "BallToHole")
	UTextRenderComponent* DistanceTextComponent = nullptr;

	/** Height above the line midpoint to display the distance text (Unreal units). Lower = closer to line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Rendering", meta = (ClampMin = "0.0", Tooltip = "Height offset above the line. Use a small value (e.g. 5–15) for text close to the line."))
	float DistanceTextHeightOffset = 8.f;

	/** Pitch offset (degrees) applied to distance text rotation. Tune in editor if text faces wrong way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Rendering", meta = (Tooltip = "Add to pitch to fix text orientation. Try 90, -90, 180 if text is tilted wrong."))
	float DistanceTextPitchOffset = 90.f;

	/** Roll offset (degrees) applied to distance text rotation. Tune in editor if text is upside down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BallToHole|Rendering", meta = (Tooltip = "Add to roll to flip text. Try 180 if upside down."))
	float DistanceTextRollOffset = 180.f;

	FVector LastBallLocation = FVector::ZeroVector;
	bool bHasLastBallLocation = false;

	/** When ball enters in_motion, we count elapsed time. After TracePersistSeconds, we hide the line. */
	float InMotionPersistElapsed = 0.f;
	bool bWasInMotion = false;

	/** Debounce: accumulated seconds of idle/stopped before we reset persist timer. */
	float IdleStoppedElapsed = 0.f;

	/** Putt number when we last reset (avoids reset during same-putt flicker). */
	int32 LastPuttNumberWhenReset = -1;

	/** Seconds both ball and hole have been not detected; when >= BothLostHideDelaySeconds, hide line. */
	float BothLostElapsedSeconds = 0.f;

	/** True when we are fading out (opacity 1->0). */
	bool bIsFadingOut = false;
	float FadeOutElapsedSeconds = 0.f;

	/** Dynamic materials for LineMeshComponent (for opacity fade). Material must have "Opacity" scalar param. */
	UPROPERTY(Transient)
	TArray<UMaterialInstanceDynamic*> LineMeshDynamicMaterials;

	void UpdateLine();
	void SetLineVisibility(bool bVisible);
	void SetLineOpacity(float Opacity);
	void StartFadeOut();
	void EnsureLineMeshDynamicMaterials();

	/** Resolve target hole from receiver: position-based when TargetHoleX/Y set, else index-based. */
	AActor* ResolveTargetHole(AUDPGolfReceiver* ValidReceiver) const;
};

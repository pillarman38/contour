#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PuttMarkerActor.generated.h"

class AUDPGolfReceiver;
struct FGolfPuttStats;

// ─────────────────────────────────────────────────────────────────────────────
// APuttMarkerWidget — Blueprint-friendly single marker (circle + text).
//
// Subclass this as a Blueprint, arrange the mesh and text visually in the
// editor viewport, assign materials / glow, etc.  The parent
// APuttMarkerActor only calls SetMarkerText / SetMarkerVisible at runtime.
// ─────────────────────────────────────────────────────────────────────────────
UCLASS(Blueprintable, meta = (DisplayName = "Putt Marker Widget"))
class GOLFSIMUE_API APuttMarkerWidget : public AActor
{
	GENERATED_BODY()

public:
	APuttMarkerWidget();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Marker")
	USceneComponent* SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Marker")
	UStaticMeshComponent* MarkerMesh = nullptr;

	/** Updates any Text Render component on this actor (Blueprint-added or inherited). */
	UFUNCTION(BlueprintCallable, Category = "Marker")
	void SetMarkerText(const FText& Text);

	UFUNCTION(BlueprintCallable, Category = "Marker")
	void SetMarkerVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "Marker")
	void SetMarkerScale(FVector Scale);

	/** Set opacity 0-1 for text and mesh (mesh requires material with "Opacity" param). */
	UFUNCTION(BlueprintCallable, Category = "Marker")
	void SetMarkerOpacity(float Opacity);

	/** For debug: number of mesh material slots we update with opacity. */
	int32 GetNumMeshDynamicMaterials() const { return MeshDynamicMaterials.Num(); }
	/** For debug: blend mode of first mesh material (0=Opaque, 1=Masked, 2=Translucent); -1 if none. */
	int32 GetMeshBlendMode() const;

protected:
	virtual void BeginPlay() override;

	/** Text color for fade-out (set in Blueprint if you want non-white). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker")
	FColor DefaultTextColor = FColor::White;

	/** Cached dynamic materials for mesh opacity (one per slot; created in BeginPlay). */
	UPROPERTY() TArray<UMaterialInstanceDynamic*> MeshDynamicMaterials;

	/** Cached mesh scales when at full opacity (Blueprint default); used so we multiply by opacity during fade without overwriting intended size. */
	UPROPERTY(Transient) TArray<FVector> CachedMeshBaseScales;
};

// ─────────────────────────────────────────────────────────────────────────────
// APuttMarkerActor — Spawns a single stats marker at the putt start.
//
// Assign a Blueprint subclass of APuttMarkerWidget to MarkerClass.
// Displays "L:launch P:peak D:distance" at the putt start. Positioning/layout
// can be customized in Unreal via the MarkerClass Blueprint.
// ─────────────────────────────────────────────────────────────────────────────
UCLASS(Blueprintable, meta = (DisplayName = "Putt Marker Actor"))
class GOLFSIMUE_API APuttMarkerActor : public AActor
{
	GENERATED_BODY()

public:
	APuttMarkerActor();

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "PuttMarkers|Source", meta = (AllowAbstract = "false"))
	AUDPGolfReceiver* GolfReceiver = nullptr;

	/** Blueprint class to spawn for the stats marker. Must be a subclass of APuttMarkerWidget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PuttMarkers|Rendering")
	TSubclassOf<APuttMarkerWidget> MarkerClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PuttMarkers|Behaviour")
	bool bShowDuringMotion = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PuttMarkers|Behaviour")
	bool bShowWhenStopped = true;

	/** Seconds after putt stops before markers start fading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PuttMarkers|Behaviour", meta = (ClampMin = "0.0"))
	float FadeDelaySeconds = 10.f;

	/** Shorter delay when ball is not detected (e.g. went in hole, tracker lost sight). Use this so markers fade even when UDP stops or ball visibility is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PuttMarkers|Behaviour", meta = (ClampMin = "0.0", Tooltip = "When ball not visible + stopped, use this delay instead of FadeDelaySeconds so markers fade when ball is lost."))
	float FadeDelayWhenBallLostSeconds = 2.f;

	/** Duration of the fade-out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PuttMarkers|Behaviour", meta = (ClampMin = "0.1"))
	float FadeDurationSeconds = 1.5f;

	/** Duration of the fade-in when markers first appear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PuttMarkers|Behaviour", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FadeInDurationSeconds = 0.4f;

	/** Consecutive frames of idle/empty state required before hiding markers (prevents flicker from UDP packet glitches). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PuttMarkers|Behaviour", meta = (ClampMin = "1", ClampMax = "60"))
	int32 HideDebounceFrames = 10;

	/** Consecutive frames of in_motion required before resetting display (legacy; use InMotionDebounceSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PuttMarkers|Behaviour", meta = (ClampMin = "1", ClampMax = "30"))
	int32 InMotionDebounceFrames = 10;

	/** Seconds of continuous in_motion required before resetting fade timer (prevents UDP flicker from blocking fade). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PuttMarkers|Behaviour", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float InMotionDebounceSeconds = 2.f;

	/** Scale factor for speed display (in/s). Use if C++ calibration yields wrong values (e.g. 0.01 if values are 100x too large). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PuttMarkers|Display", meta = (ClampMin = "0.001"))
	float SpeedScale = 1.f;

	/** Scale factor for distance display (inches). Same usage as SpeedScale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PuttMarkers|Display", meta = (ClampMin = "0.001"))
	float DistanceScale = 1.f;

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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY() APuttMarkerWidget* StatsWidget = nullptr;

	/** Accumulated seconds in "stopped" state; used for fade delay. */
	float StoppedTimeSeconds = 0.f;
	/** True once we start fading (past FadeDelaySeconds). */
	bool bIsFading = false;
	/** True after fade completes; prevents re-showing until next putt (in_motion) even if UDP keeps sending "stopped". */
	bool bFadeCompleted = false;
	/** Elapsed seconds during fade; keeps counting even if state changes (next putt starts). */
	float FadeElapsedSeconds = 0.f;

	/** True when markers are fading in (0 -> 1 opacity). */
	bool bIsFadingIn = false;
	/** Elapsed seconds during fade-in. */
	float FadeInElapsedSeconds = 0.f;

	/** Consecutive frames we've received !bShouldShow; only hide when >= HideDebounceFrames (prevents flicker). */
	int32 NotShouldShowFrames = 0;
	/** Consecutive frames of in_motion; only reset StoppedTimeSeconds when >= InMotionDebounceSeconds (time-based). */
	int32 InMotionFrames = 0;
	float InMotionElapsedSeconds = 0.f;
	/** Seconds ball has been not visible while in_motion; when >= FadeDelayWhenBallLostSeconds, start fade (handles both-not-detected even when tracker keeps sending). */
	float InMotionBallLostElapsed = 0.f;

	/** Snapshot when fade started; used so fade isn't interrupted by new UDP data. */
	FVector SnapshotLaunchWorld = FVector::ZeroVector;
	float SnapshotSpeedDisplay = 0.f;
	float SnapshotPeakDisplay = 0.f;
	float SnapshotDistanceDisplay = 0.f;

	APuttMarkerWidget* SpawnWidget(FName Label);
	void HideAll();
	void SetAllOpacity(float Opacity);
	/** Snapshot marker positions from Stats; call when starting a fade. */
	void TakeFadeSnapshot(const FGolfPuttStats& Stats);
};

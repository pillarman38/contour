#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"
#include "UDPGolfReceiver.generated.h"

USTRUCT(BlueprintType)
struct FGolfTrackedObject
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	float X = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	float Y = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	float VX = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	float VY = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	float Confidence = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	bool bVisible = false;
};

USTRUCT(BlueprintType)
struct FGolfPuttStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Stats")
	int32 PuttNumber = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Stats")
	FString State;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Stats")
	float LaunchSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Stats")
	float CurrentSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Stats")
	float PeakSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Stats")
	float TotalDistance = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Stats")
	float BreakDistance = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Stats")
	float TimeInMotion = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Stats")
	FVector2D StartPos = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Stats")
	FVector2D PeakSpeedPos = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Stats")
	FVector2D FinalPos = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct FGolfHoleInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	float X = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	float Y = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	float Radius = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	bool bVisible = false;
};

USTRUCT(BlueprintType)
struct FGolfBallInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	FGolfTrackedObject Tracked;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	FString Username;

	/** Tracker identity; matches ball_placements[].stable_id and StatsApi claim-ball index. */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	int32 StableId = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	FGolfPuttStats Stats;

	/** Per-ball aim target (camera pixels); use with AimLineBallSortedIndex on BallToHoleLineActor. */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	int32 TargetHoleIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	float TargetHoleX = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	float TargetHoleY = 0.f;
};

/** Where a claimed player should return their ball (camera pixels). Drive placement marker actors from this. */
USTRUCT(BlueprintType)
struct FGolfBallPlacement
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Placement")
	FString Username;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Placement")
	int32 StableId = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Placement")
	float PixelX = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Placement")
	float PixelY = 0.f;

	/** True while the ball is not visible: show marker at PixelX/PixelY until they replace the ball. */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Placement")
	bool bWaitingPlacement = false;

	/** True when returning after a made putt (green-center line); false = last-known position (occlusion). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Placement")
	bool bAfterPuttReturn = false;
};

/** One row per claimed ball waiting to be replaced; use with a marker actor pool (show + set world, hide extras). */
USTRUCT(BlueprintType)
struct FGolfWaitingPlacementWorld
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Placement")
	int32 StableId = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Placement")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Placement")
	FString Username;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Placement")
	bool bAfterPuttReturn = false;
};

USTRUCT(BlueprintType)
struct FGolfUDPPayload
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	FGolfTrackedObject Ball;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	FGolfTrackedObject Putter;

	/** All putter detections this frame (same order as tracker; use for multi crosshair). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	TArray<FGolfTrackedObject> Putters;

	/** First hole (backward compat). Same as Holes[0] when Holes.Num() > 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	FGolfHoleInfo Hole;

	/** All detected holes this frame (updates when camera moves). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	TArray<FGolfHoleInfo> Holes;

	/** Multi-ball: balls with username and per-ball stats (empty = use Ball). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	TArray<FGolfBallInfo> Balls;

	/** Per-claimed-user return spot when the ball was picked up or lost (empty if none). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	TArray<FGolfBallPlacement> BallPlacements;

	/** Target hole index for ball-to-hole line (from contour app). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	int32 TargetHoleIndex = 0;

	/** Target hole position in pixels (for stable line when hole order changes). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	float TargetHoleX = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	float TargetHoleY = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	FGolfPuttStats Stats;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	bool bPuttMade = false;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	int64 TimestampMs = 0;

	/** Stable ball id while Contour user selects a hole (-1 = hide). INT32_MAX when UDP omits field (legacy: show crosshair). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	int32 HoleAimBallIndex = 2147483647;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPuttMade);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUDPDataReceived, FGolfUDPPayload, Payload);

class ABallToHoleLineActor;

/**
 * Receives UDP JSON datagrams from the golf-sim tracker and drives
 * Ball / Putter / Hole actors in the world.
 */
UCLASS(Blueprintable, meta = (DisplayName = "UDP Golf Receiver"))
class GOLFSIMUE_API AUDPGolfReceiver : public AActor
{
	GENERATED_BODY()

public:
	AUDPGolfReceiver();

	// ── Network settings ────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golf|Network")
	int32 ListenPort = 7001;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golf|Network")
	FString ListenIP = TEXT("0.0.0.0");

	// ── Actor references (set in editor) ────────────────────────────────
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Golf|Actors", meta = (AllowAbstract = "false"))
	AActor* BallActor = nullptr;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Golf|Actors", meta = (AllowAbstract = "false"))
	AActor* PutterActor = nullptr;

	/**
	 * Template actor for per-putter crosshairs (e.g. your crosshair_Blueprint placed in the level).
	 * When bAutoSpawnPutterCrosshairs is true, one instance per entry in PuttersData is spawned from this class.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Golf|Actors", meta = (AllowAbstract = "false"))
	AActor* PutterCrosshairTemplate = nullptr;

	/** When true, spawn/move PutterCrosshairActorsSpawned to match PuttersData (Contour-style multi putter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golf|Actors")
	bool bAutoSpawnPutterCrosshairs = true;

	/** Runtime-spawned crosshair actors (same count as PuttersData when auto-spawn is on). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Golf|Actors")
	TArray<AActor*> PutterCrosshairActorsSpawned;

	/** Single hole actor (backward compat). Moved to first hole when set. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Golf|Actors", meta = (AllowAbstract = "false"))
	AActor* HoleActor = nullptr;

	/** Optional array of hole actors; index 0 = first hole, 1 = second, etc. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Golf|Actors", meta = (AllowAbstract = "false"))
	TArray<AActor*> HoleActors;

	/** Optional array of ball actors for multi-ball mode; index 0 = first ball, etc. When empty, uses BallActor for single ball. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Golf|Actors", meta = (AllowAbstract = "false"))
	TArray<AActor*> BallActors;

	/**
	 * First "Ball to Hole Line" actor in the level (covers BallsData[0]). When bAutoSpawnAimLines is true,
	 * additional line actors are spawned from this actor's class for balls 1..N-1.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Golf|Actors", meta = (AllowAbstract = "false"))
	ABallToHoleLineActor* AimLineActor = nullptr;

	/** Spawn one aim line per tracked ball (same count as balls); uses AimLineActor as template. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golf|Actors")
	bool bAutoSpawnAimLines = false;

	/** Runtime-spawned aim lines for balls 1..N-1 (ball 0 uses AimLineActor). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Golf|Actors")
	TArray<ABallToHoleLineActor*> AimLineActorsSpawned;

	/** Template actor for ball placement markers (e.g. PlaceBallMarker_Blueprint). One instance is spawned per claimed ball. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Golf|Actors", meta = (AllowAbstract = "false"))
	AActor* PlacementMarkerActor = nullptr;

	/** Runtime-spawned placement marker instances (one per BallPlacementsData entry). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Golf|Actors")
	TArray<AActor*> PlacementMarkerActorsSpawned;

	// ── Pixel-to-World coordinate mapping ───────────────────────────────
	/** Size of the camera frame in pixels (width, height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golf|Mapping")
	FVector2D PixelBoundsMax = FVector2D(1920.f, 1080.f);

	/** World-space min corner that pixel (0,0) maps to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golf|Mapping")
	FVector WorldBoundsMin = FVector(0.f, 0.f, 0.f);

	/** World-space max corner that pixel (PixelMax) maps to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golf|Mapping")
	FVector WorldBoundsMax = FVector(300.f, 200.f, 0.f);

	/** If true, interpolate actor positions for smoother movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golf|Movement")
	bool bInterpolate = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golf|Movement",
		meta = (EditCondition = "bInterpolate", ClampMin = "1.0", ClampMax = "60.0"))
	float InterpolationSpeed = 15.f;

	// ── Live data (read from Blueprint / UI) ────────────────────────────
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	FGolfTrackedObject BallData;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	FGolfTrackedObject PutterData;

	/** All putters this frame (mirrors last payload); drives multi crosshair when bAutoSpawnPutterCrosshairs. */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	TArray<FGolfTrackedObject> PuttersData;

	/** First hole (same as Holes[0] when Holes.Num() > 0). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	FGolfHoleInfo HoleData;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	TArray<FGolfHoleInfo> HolesData;

	/** Multi-ball: per-ball tracking + username + stats (for Blueprint to display "Username - Putt #N" per ball). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	TArray<FGolfBallInfo> BallsData;

	/** Latest ball return hints from tracker (placement marker actors). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	TArray<FGolfBallPlacement> BallPlacementsData;

	/** Target hole index for ball-to-hole line (from contour app). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	int32 TargetHoleIndex = 0;

	/** Target hole position in pixels (for stable line when hole order changes). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	float TargetHoleX = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	float TargetHoleY = 0.f;

	/** Mirrors UDP payload; INT32_MAX = field omitted (legacy show crosshair). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	int32 HoleAimBallIndex = 2147483647;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	FGolfPuttStats PuttStats;

	/** First non-empty username in BallsData (position order). Use GetBallLabelForSortedIndex / GetPrimaryBallLabel for labels. */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	FString PrimaryUsername;

	/** When true, each tick sets the first UTextRenderComponent on Ball Actor / extra ball actors to GetBallLabelForSortedIndex. Disable if your Blueprint drives the same text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Golf|Data")
	bool bAutoApplyBallLabelText = true;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	bool bPuttMade = false;

	/** Last received UDP payload (for Blueprint Break node; use from Event Tick or from On UDP Data Received). */
	UFUNCTION(BlueprintPure, Category = "Golf|Data", meta = (DisplayName = "Get Last Payload"))
	FGolfUDPPayload GetLastPayload() const;

	/** Get last payload from any actor (returns default if not a UDP Golf Receiver). Right‑click graph → search "Get Last Payload From Actor", plug actor into Actor pin. Takes UObject* so Blueprint never triggers Cast<AActor> on CDO. */
	UFUNCTION(BlueprintPure, Category = "Golf|Data", meta = (DisplayName = "Get Last Payload From Actor"))
	static FGolfUDPPayload GetLastPayloadFromActor(UObject* ActorOrObject);

	/** Format putt stats as text for display. Use from Blueprint: pass Stats from Break Golf UDP Payload, then Set your PuttStatsText variable to the return value. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Golf|Data", meta = (DisplayName = "Putt Stats To Text"))
	static FText PuttStatsToText(const FGolfPuttStats& Stats);

	/** Format username + putt number for display above a ball (e.g. "Alice - Putt #3" or "Putt #3" if no username). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Golf|Data", meta = (DisplayName = "Ball Label To Text"))
	static FText BallLabelToText(const FString& Username, int32 PuttNumber);

	/** Get the primary ball label (uses PrimaryUsername + PuttStats). Prefer this over BallLabelToText when showing the main ball. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Golf|Data", meta = (DisplayName = "Get Primary Ball Label"))
	FText GetPrimaryBallLabel() const;

	/**
	 * Label for the ball at sorted index (matches BallsData order: 0 = Ball Actor, 1 = first extra ball, ...).
	 * If exactly one ball has a username, all indices use PrimaryUsername + PuttStats so "Putt #N" alone does not appear on the wrong ball.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Golf|Data", meta = (DisplayName = "Get Ball Label For Sorted Index"))
	FText GetBallLabelForSortedIndex(int32 SortedIndex) const;

	/** Format only peak speed (in/s) for display. Use this for a "Peak Speed" label so it shows a number (e.g. "17.1 in/s"), not time. Stats.PeakSpeed is a float in inches/sec — do not format it as time (HH:MM:SS) or it will show wrong. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Golf|Data", meta = (DisplayName = "Peak Speed To Text"))
	static FText PeakSpeedToText(const FGolfPuttStats& Stats);

	/** Seconds since last UDP packet was received. Large value if never received. Use to detect tracker disconnect or stale data (e.g. markers should fade when no packets for 2+ sec). */
	UFUNCTION(BlueprintPure, Category = "Golf|Data", meta = (DisplayName = "Seconds Since Last UDP"))
	float GetSecondsSinceLastUDP() const { return LastUDPReceiveTime > 0.0 ? static_cast<float>(FPlatformTime::Seconds() - LastUDPReceiveTime) : 9999.f; }

	// ── Events ──────────────────────────────────────────────────────────
	UPROPERTY(BlueprintAssignable, Category = "Golf|Events")
	FOnPuttMade OnPuttMade;

	UPROPERTY(BlueprintAssignable, Category = "Golf|Events")
	FOnUDPDataReceived OnUDPDataReceived;

protected:
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

public:
	/** Convert pixel coordinates to world-space position using the configured mapping bounds. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Golf|Mapping", meta = (DisplayName = "Pixel To World"))
	FVector PixelToWorld(float PixelX, float PixelY) const;

	/**
	 * If ball_placements has an entry for this stable_id with waiting=true, returns world position and true.
	 * Use from Place Ball Marker: set Stable Id to the player's claimed tracker id (same as claim-ball ball_index).
	 * Always drive the False branch (no placement / not waiting): hide the actor or Set Hidden In Game so markers clear when the ball is back.
	 */
	UFUNCTION(BlueprintCallable, Category = "Golf|Placement", meta = (DisplayName = "Get Placement World For Stable Id"))
	bool GetPlacementWorldForStableId(int32 StableId, FVector& OutWorld);

	/** Every ball_placements row with waiting=true, converted to world space. Drive multiple marker actors: for pool index i, if i < array length set location + show, else hide. */
	UFUNCTION(BlueprintCallable, Category = "Golf|Placement", meta = (DisplayName = "Get Waiting Placement Markers World"))
	void GetWaitingPlacementMarkersWorld(UPARAM(ref) TArray<FGolfWaitingPlacementWorld>& OutMarkers);

	/**
	 * Resolves the first UDP Golf Receiver in the current world. Use when a Blueprint variable
	 * was left unset (None) — e.g. wire Event BeginPlay → Set UDP Golf Receiver = this node with Self as world context.
	 */
	UFUNCTION(BlueprintCallable, Category = "Golf|UDP", meta = (WorldContext = "WorldContextObject", DisplayName = "Get UDP Golf Receiver In World"))
	static AUDPGolfReceiver* GetUDPReceiverInWorld(UObject* WorldContextObject);

private:
	bool ParsePayload(const FString& Json, FGolfUDPPayload& Out) const;
	void StartReceiver();
	void StopReceiver();

	class FSocket* Socket = nullptr;
	FRunnableThread* ReceiverThread = nullptr;

	class FUDPReceiverRunnable : public FRunnable
	{
	public:
		FUDPReceiverRunnable(AUDPGolfReceiver* InOwner) : Owner(InOwner) {}
		virtual bool Init() override { return true; }
		virtual uint32 Run() override;
		virtual void Stop() override { bStopping = true; }

		FThreadSafeBool bStopping = false;
	private:
		AUDPGolfReceiver* Owner = nullptr;
	};

	FUDPReceiverRunnable* ReceiverRunnable = nullptr;

	FCriticalSection DataLock;
	FGolfUDPPayload LatestPayload;
	FGolfUDPPayload CachedPayloadForBlueprint;
	bool bNewDataAvailable = false;
	bool bPreviousPuttMade = false;

	/** World time when we last received a valid UDP packet. Used for staleness check (markers fade when tracker stops). */
	double LastUDPReceiveTime = 0.0;

	/**
	 * Per visual ball slot: primary BallActor = 0, BallActors[i] = i+1. Tracker stable_id to follow.
	 * UDP balls[] are sorted by (x,y) each frame; slot index must not be confused with sorted index or actors swap when balls cross.
	 */
	UPROPERTY(Transient)
	TArray<int32> BallActorLatchedStableIds;
};

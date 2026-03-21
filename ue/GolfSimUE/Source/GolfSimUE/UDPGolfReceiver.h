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

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Tracking")
	FGolfPuttStats Stats;
};

USTRUCT(BlueprintType)
struct FGolfUDPPayload
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	FGolfTrackedObject Ball;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	FGolfTrackedObject Putter;

	/** First hole (backward compat). Same as Holes[0] when Holes.Num() > 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	FGolfHoleInfo Hole;

	/** All detected holes this frame (updates when camera moves). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	TArray<FGolfHoleInfo> Holes;

	/** Multi-ball: balls with username and per-ball stats (empty = use Ball). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Payload")
	TArray<FGolfBallInfo> Balls;

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
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPuttMade);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUDPDataReceived, FGolfUDPPayload, Payload);

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

	/** Single hole actor (backward compat). Moved to first hole when set. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Golf|Actors", meta = (AllowAbstract = "false"))
	AActor* HoleActor = nullptr;

	/** Optional array of hole actors; index 0 = first hole, 1 = second, etc. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Golf|Actors", meta = (AllowAbstract = "false"))
	TArray<AActor*> HoleActors;

	/** Optional array of ball actors for multi-ball mode; index 0 = first ball, etc. When empty, uses BallActor for single ball. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Golf|Actors", meta = (AllowAbstract = "false"))
	TArray<AActor*> BallActors;

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

	/** First hole (same as Holes[0] when Holes.Num() > 0). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	FGolfHoleInfo HoleData;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	TArray<FGolfHoleInfo> HolesData;

	/** Multi-ball: per-ball tracking + username + stats (for Blueprint to display "Username - Putt #N" per ball). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	TArray<FGolfBallInfo> BallsData;

	/** Target hole index for ball-to-hole line (from contour app). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	int32 TargetHoleIndex = 0;

	/** Target hole position in pixels (for stable line when hole order changes). */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	float TargetHoleX = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	float TargetHoleY = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	FGolfPuttStats PuttStats;

	/** Primary ball's username (from BallsData[0] when available). Use with BallLabelToText for consistent "Username - Putt #N" display. */
	UPROPERTY(BlueprintReadOnly, Category = "Golf|Data")
	FString PrimaryUsername;

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
};

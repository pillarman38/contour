#include "PuttMarkerActor.h"
#include "UDPGolfReceiver.h"
#include "Engine/World.h"
#include "Serialization/Archive.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UnrealType.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Components/TextRenderComponent.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

// Safe check before IsA/Cast; avoids "Cast of Default__Object to Actor failed"
static bool IsValidReceiverRef(const UObject* O)
{
	if (!O) return false;
	if (O->GetName().StartsWith(TEXT("Default__"))) return false;
	UClass* C = O->GetClass();
	if (!C || C == UObject::StaticClass()) return false;
	if (O->IsTemplate()) return false;
	return C->IsChildOf(AActor::StaticClass());
}

// ─────────────────────────────────────────────────────────────────────────────
// APuttMarkerWidget
// ─────────────────────────────────────────────────────────────────────────────
APuttMarkerWidget::APuttMarkerWidget()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	MarkerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MarkerMesh"));
	MarkerMesh->SetupAttachment(SceneRoot);
	MarkerMesh->SetCastShadow(false);
}

void APuttMarkerWidget::SetMarkerText(const FText& Text)
{
	if (UTextRenderComponent* TextComp = FindComponentByClass<UTextRenderComponent>())
	{
		TextComp->SetText(Text);
	}
}

void APuttMarkerWidget::SetMarkerVisible(bool bVisible)
{
	SetActorHiddenInGame(!bVisible);
}

void APuttMarkerWidget::SetMarkerScale(FVector Scale)
{
	if (MarkerMesh)
	{
		MarkerMesh->SetRelativeScale3D(Scale);
	}
}

void APuttMarkerWidget::BeginPlay()
{
	Super::BeginPlay();
	MeshDynamicMaterials.Reset();
	// Iterate ALL StaticMeshComponents (Blueprint may add meshes with different names)
	TArray<UStaticMeshComponent*> MeshComps;
	GetComponents<UStaticMeshComponent>(MeshComps);
	for (UStaticMeshComponent* MeshComp : MeshComps)
	{
		if (!MeshComp) continue;
		const int32 NumSlots = MeshComp->GetNumMaterials();
		for (int32 i = 0; i < NumSlots; ++i)
		{
			if (UMaterialInterface* Mat = MeshComp->GetMaterial(i))
			{
				UMaterialInstanceDynamic* MID = MeshComp->CreateAndSetMaterialInstanceDynamic(i);
				if (MID) { MeshDynamicMaterials.Add(MID); }
			}
		}
	}
}

void APuttMarkerWidget::SetMarkerOpacity(float Opacity)
{
	const float ScaleVal = FMath::Max(0.01f, Opacity);
	// Text alpha: always applies
	if (UTextRenderComponent* TextComp = FindComponentByClass<UTextRenderComponent>())
	{
		const uint8 A = static_cast<uint8>(FMath::Clamp(Opacity * 255.f, 0.f, 255.f));
		TextComp->SetTextRenderColor(FColor(DefaultTextColor.R, DefaultTextColor.G, DefaultTextColor.B, A));
	}
	// Material opacity param: works when material has "Opacity" and is Translucent
	for (UMaterialInstanceDynamic* MID : MeshDynamicMaterials)
	{
		if (MID) { MID->SetScalarParameterValue(FName("Opacity"), Opacity); }
	}
	// Scale mesh: cache Blueprint default on first use, then apply Opacity multiplier during fade
	TArray<UStaticMeshComponent*> MeshComps;
	GetComponents<UStaticMeshComponent>(MeshComps);
	if (CachedMeshBaseScales.Num() != MeshComps.Num())
	{
		CachedMeshBaseScales.Reset();
		for (UStaticMeshComponent* MeshComp : MeshComps)
		{
			if (MeshComp) CachedMeshBaseScales.Add(MeshComp->GetRelativeScale3D());
		}
	}
	for (int32 i = 0; i < MeshComps.Num() && i < CachedMeshBaseScales.Num(); ++i)
	{
		if (UStaticMeshComponent* MeshComp = MeshComps[i])
		{
			const FVector& Base = CachedMeshBaseScales[i];
			MeshComp->SetRelativeScale3D(FVector(Base.X * ScaleVal, Base.Y * ScaleVal, Base.Z * ScaleVal));
		}
	}
}

int32 APuttMarkerWidget::GetMeshBlendMode() const
{
	if (MeshDynamicMaterials.Num() == 0) return -1;
	if (UMaterialInstanceDynamic* MID = MeshDynamicMaterials[0])
		return static_cast<int32>(MID->GetBlendMode());
	return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// APuttMarkerActor
// ─────────────────────────────────────────────────────────────────────────────
APuttMarkerActor::APuttMarkerActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void APuttMarkerActor::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	if (Ar.IsLoading() && GolfReceiver)
	{
		// Check safe predicates first (avoid IsA on Default__Object which can trigger engine Cast fatal)
		if (GolfReceiver->GetClass() == UObject::StaticClass() ||
		    GolfReceiver->GetName().StartsWith(TEXT("Default__")) ||
		    GolfReceiver->IsTemplate() || !GolfReceiver->IsA<AUDPGolfReceiver>()) { GolfReceiver = nullptr; }
	}
}

void APuttMarkerActor::PostLoad()
{
	Super::PostLoad();
	if (GolfReceiver && (GolfReceiver->GetClass() == UObject::StaticClass() ||
	    GolfReceiver->GetName().StartsWith(TEXT("Default__")) ||
	    GolfReceiver->IsTemplate() || !GolfReceiver->IsA<AUDPGolfReceiver>())) { GolfReceiver = nullptr; }
}

void APuttMarkerActor::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
	if (!IsTemplate())
	{
		if (!GolfReceiver || GolfReceiver->GetName().StartsWith(TEXT("Default__")) ||
		    !GolfReceiver->IsA<AUDPGolfReceiver>() || GolfReceiver->IsTemplate()) { GolfReceiver = nullptr; }
	}
}

void APuttMarkerActor::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	if (!GolfReceiver || GolfReceiver->GetName().StartsWith(TEXT("Default__")) ||
	    !GolfReceiver->IsA<AUDPGolfReceiver>() || GolfReceiver->IsTemplate()) { GolfReceiver = nullptr; }
}

#if WITH_EDITOR
void APuttMarkerActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (!GolfReceiver || GolfReceiver->GetName().StartsWith(TEXT("Default__")) ||
	    !GolfReceiver->IsA<AUDPGolfReceiver>() || GolfReceiver->IsTemplate()) { GolfReceiver = nullptr; }
}
#endif

void APuttMarkerActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (!IsTemplate())
	{
		if (!GolfReceiver || GolfReceiver->GetClass() == UObject::StaticClass() ||
		    GolfReceiver->GetName().StartsWith(TEXT("Default__")) ||
		    !GolfReceiver->IsA<AUDPGolfReceiver>() || GolfReceiver->IsTemplate()) { GolfReceiver = nullptr; }
	}
}

APuttMarkerWidget* APuttMarkerActor::SpawnWidget(FName Label)
{
	if (!MarkerClass) return nullptr;

	UWorld* World = GetWorld();
	if (!World) return nullptr;

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APuttMarkerWidget* Widget = World->SpawnActor<APuttMarkerWidget>(MarkerClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Widget)
	{
		Widget->SetActorScale3D(GetActorScale3D());
		Widget->SetMarkerVisible(false);
	}
	return Widget;
}

void APuttMarkerActor::BeginPlay()
{
	Super::BeginPlay();
	if (!GolfReceiver || GolfReceiver->GetName().StartsWith(TEXT("Default__")) ||
	    GolfReceiver->GetClass() == UObject::StaticClass() || !GolfReceiver->IsA<AActor>() || GolfReceiver->IsTemplate()) { GolfReceiver = nullptr; }

	StatsWidget = SpawnWidget(TEXT("StatsMarker"));
}

void APuttMarkerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (StatsWidget) { StatsWidget->Destroy(); StatsWidget = nullptr; }
	Super::EndPlay(EndPlayReason);
}

void APuttMarkerActor::HideAll()
{
	if (StatsWidget) StatsWidget->SetMarkerVisible(false);
	StoppedTimeSeconds = 0.f;
	bIsFading = false;
	bIsFadingIn = false;
	FadeElapsedSeconds = 0.f;
	FadeInElapsedSeconds = 0.f;
}

void APuttMarkerActor::SetAllOpacity(float Opacity)
{
	if (StatsWidget) StatsWidget->SetMarkerOpacity(Opacity);
}

void APuttMarkerActor::TakeFadeSnapshot(const FGolfPuttStats& Stats)
{
	SnapshotSpeedDisplay = Stats.LaunchSpeed * SpeedScale;
	SnapshotPeakDisplay = Stats.PeakSpeed * SpeedScale;
	SnapshotDistanceDisplay = Stats.TotalDistance * DistanceScale;
	SnapshotLaunchWorld = GolfReceiver ? GolfReceiver->PixelToWorld(Stats.StartPos.X, Stats.StartPos.Y) : FVector::ZeroVector;
}

void APuttMarkerActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (IsTemplate()) return;

	if (!GolfReceiver || GolfReceiver->GetName().StartsWith(TEXT("Default__")) ||
	    GolfReceiver->GetClass() == UObject::StaticClass() || !GolfReceiver->IsA<AActor>() || GolfReceiver->IsTemplate())
	{
		GolfReceiver = nullptr;  // Clear bad ref (CDO/corrupt) to avoid engine "Cast of Default__Object to Actor failed"
		HideAll();
		return;
	}

	const FGolfPuttStats& Stats = GolfReceiver->PuttStats;

	const bool bInMotion = Stats.State.Equals(TEXT("in_motion"), ESearchCase::IgnoreCase);
	const bool bStopped  = Stats.State.Equals(TEXT("stopped"), ESearchCase::IgnoreCase);

	const bool bShouldShow = (bInMotion && bShowDuringMotion) || (bStopped && bShowWhenStopped && !bFadeCompleted);

	// Immediate reset when new putt starts (in_motion + ball visible): show markers right away
	const bool bBallVisible = (GolfReceiver && GolfReceiver->BallData.bVisible);
	if (bFadeCompleted && bInMotion && bBallVisible)
	{
		bFadeCompleted = false;
		StoppedTimeSeconds = 0.f;
		InMotionElapsedSeconds = 0.f;
		InMotionBallLostElapsed = 0.f;
		bIsFadingIn = true;
		FadeInElapsedSeconds = 0.f;
		SetAllOpacity(0.f);
	}

	// If we're fading, continue the fade even when state changes (next putt started); don't interrupt.
	if (bIsFading)
	{
		FadeElapsedSeconds += FMath::Min(DeltaTime, 0.5f);
		const float SafeFadeDur = FMath::Max(FadeDurationSeconds, 0.1f);  // Guard against 0 causing instant hide
		const float Opacity = FMath::Max(0.f, 1.f - FadeElapsedSeconds / SafeFadeDur);
		SetAllOpacity(Opacity);
		if (Opacity <= 0.f)
		{
			bFadeCompleted = true;
			bIsFading = false;
			HideAll();
			FadeElapsedSeconds = 0.f;
			return;
		}
		// Use snapshot for positions (below); skip normal state handling
	}
	else if (!bShouldShow)
	{
		if (!bInMotion) { InMotionFrames = 0; InMotionElapsedSeconds = 0.f; }  // Clear debounce only when not in_motion
		// Stopped but not showing: may still have visible markers from in_motion; must fade when ball is lost.
		// When bFadeCompleted, markers are already hidden; do not re-start fade (fixes repeated fade loop).
		if (bStopped && !bFadeCompleted)
		{
			const bool bBallLost = (GolfReceiver && (!GolfReceiver->BallData.bVisible || GolfReceiver->GetSecondsSinceLastUDP() > 2.f));
			if (bBallLost)
			{
				StoppedTimeSeconds += FMath::Min(DeltaTime, 0.5f);
				const float DelayThreshold = FadeDelayWhenBallLostSeconds;
				if (StoppedTimeSeconds >= DelayThreshold)
				{
					bIsFading = true;
					bIsFadingIn = false;
					FadeElapsedSeconds = 0.f;
					TakeFadeSnapshot(Stats);
				}
			}
			return;  // Don't debounce-hide; keep waiting for in_motion to reset (or fade if ball lost, above)
		}
		// Empty state = malformed/missing UDP. When UDP is stale (tracker stopped), allow fade so markers don't stay forever.
		if (Stats.State.IsEmpty())
		{
			NotShouldShowFrames = 0;
			if (!GolfReceiver || GolfReceiver->GetSecondsSinceLastUDP() <= 2.f)
				return;  // Transient packet loss: preserve display
			// UDP stale 2+ sec (tracker off): fall through to idle fade path below
		}
		// in_motion but !bShowDuringMotion: require time-based debounce so UDP flicker cannot reset fade timer
	if (bInMotion && !bIsFading)
	{
		InMotionFrames++;
			InMotionElapsedSeconds += FMath::Min(DeltaTime, 0.5f);
			if (InMotionElapsedSeconds >= InMotionDebounceSeconds)
			{
				bFadeCompleted = false;
				StoppedTimeSeconds = 0.f;
				InMotionElapsedSeconds = 0.f;
			}
		}
		// Markers should always fade: accumulate time toward fade when showing + idle, or start fade instead of instant hide
		const bool bIdleOrEmpty = !bInMotion && !bStopped;
		const bool bBallLostHere = (GolfReceiver && (!GolfReceiver->BallData.bVisible || GolfReceiver->GetSecondsSinceLastUDP() > 2.f));
		if (StoppedTimeSeconds > 0.f || bIsFading || (bIdleOrEmpty && bBallLostHere))
		{
			NotShouldShowFrames = 0;
			// When idle (or empty state) and ball lost: accumulate from 0 so markers fade when ball not detected.
			// Skip when bFadeCompleted: markers already hidden, avoid repeated fade loop.
			if (bIdleOrEmpty && !bIsFading && bBallLostHere && !bFadeCompleted)
			{
				StoppedTimeSeconds += FMath::Min(DeltaTime, 0.5f);
				const float EffectiveDelay = FadeDelayWhenBallLostSeconds;
				if (StoppedTimeSeconds >= EffectiveDelay)
				{
					// Start fade from idle/empty path - markers should always fade when ball lost
					bIsFading = true;
					bIsFadingIn = false;
					FadeElapsedSeconds = 0.f;
					TakeFadeSnapshot(Stats);
				}
			}
			else if (!bInMotion && !bStopped && !bIsFading && StoppedTimeSeconds > 0.f && !bBallLostHere)
			{
				// Original idle path when ball visible (use longer delay)
				StoppedTimeSeconds += FMath::Min(DeltaTime, 0.5f);
				const float EffectiveDelay = FadeDelaySeconds;
				if (StoppedTimeSeconds >= EffectiveDelay)
				{
					bIsFading = true;
					bIsFadingIn = false;
					FadeElapsedSeconds = 0.f;
					TakeFadeSnapshot(Stats);
				}
			}
			return;
		}
		NotShouldShowFrames++;
		if (NotShouldShowFrames >= HideDebounceFrames && !bFadeCompleted)
		{
			// Start fade instead of instant hide - markers should always fade, no matter what.
			// Skip when bFadeCompleted: markers already hidden, avoid repeated fade loop.
			bIsFading = true;
			bIsFadingIn = false;
			FadeElapsedSeconds = 0.f;
			TakeFadeSnapshot(Stats);
			NotShouldShowFrames = 0;
		}
		return;
	}
	else
	{
		NotShouldShowFrames = 0;
	}
	// When fading, skip in_motion reset so we don't overwrite opacity with 0 and interrupt the fade
	if (bInMotion && !bIsFading)
	{
		InMotionFrames++;
		InMotionElapsedSeconds += FMath::Min(DeltaTime, 0.5f);
		const bool bBallLostInMotion = (GolfReceiver && !GolfReceiver->BallData.bVisible);
		if (bBallLostInMotion)
			InMotionBallLostElapsed += FMath::Min(DeltaTime, 0.5f);
		else
			InMotionBallLostElapsed = 0.f;
		// Require time-based debounce so UDP flicker cannot reset fade timer (prevents "last putt never fades")
		// When ball is visible, use minimal debounce (0.05s) so markers show immediately for new putts
		const float ResetDebounce = (GolfReceiver && GolfReceiver->BallData.bVisible) ? 0.05f : InMotionDebounceSeconds;
		if (InMotionElapsedSeconds >= ResetDebounce)
		{
			// When UDP is stale 2+ sec (tracker stopped), or ball+hole both not detected (tracker may keep sending), markers should fade
			const bool bUDPStale = (GolfReceiver && GolfReceiver->GetSecondsSinceLastUDP() > 2.f);
			const bool bBallLostLongEnough = (bBallLostInMotion && InMotionBallLostElapsed >= FadeDelayWhenBallLostSeconds);
			if (bUDPStale || bBallLostLongEnough)
			{
				// Ball lost / both not detected / tracker stopped: start fade so pointers don't stay forever
				bIsFading = true;
				bIsFadingIn = false;
				FadeElapsedSeconds = 0.f;
				TakeFadeSnapshot(Stats);
				StoppedTimeSeconds = 0.f;
				InMotionElapsedSeconds = 0.f;
				InMotionBallLostElapsed = 0.f;
			}
			else
			{
				bFadeCompleted = false;
				StoppedTimeSeconds = 0.f;
				InMotionElapsedSeconds = 0.f;
				bIsFadingIn = true;
				FadeInElapsedSeconds = 0.f;
				SetAllOpacity(0.f);
			}
		}
	}
	else if (bStopped && !bIsFading)
	{
		NotShouldShowFrames = 0;
		InMotionFrames = 0;
		InMotionElapsedSeconds = 0.f;  // Clear in_motion debounce
		InMotionBallLostElapsed = 0.f;
		InMotionBallLostElapsed = 0.f;
		StoppedTimeSeconds += FMath::Min(DeltaTime, 0.5f);
		// When ball is not detected (e.g. in hole, off-camera) or UDP is stale (tracker stopped), use shorter delay
		const bool bBallLost = (GolfReceiver && (!GolfReceiver->BallData.bVisible || GolfReceiver->GetSecondsSinceLastUDP() > 2.f));
		const float DelayThreshold = bBallLost ? FadeDelayWhenBallLostSeconds : FadeDelaySeconds;
		if (StoppedTimeSeconds >= DelayThreshold)
		{
			// Start fade: snapshot positions so we don't jump when new UDP packets arrive
			if (!bIsFading)
			{
				bIsFading = true;
				TakeFadeSnapshot(Stats);
				bIsFadingIn = false;
				FadeElapsedSeconds = 0.f;
				SnapshotSpeedDisplay = Stats.LaunchSpeed * SpeedScale;
				SnapshotPeakDisplay = Stats.PeakSpeed * SpeedScale;
				SnapshotDistanceDisplay = Stats.TotalDistance * DistanceScale;
				SnapshotLaunchWorld = GolfReceiver->PixelToWorld(Stats.StartPos.X, Stats.StartPos.Y);
			}
			const float SafeFadeDur = FMath::Max(FadeDurationSeconds, 0.1f);  // Guard against 0 causing instant hide
			const float Opacity = FMath::Max(0.f, 1.f - FadeElapsedSeconds / SafeFadeDur);
			FadeElapsedSeconds += FMath::Min(DeltaTime, 0.5f);
			SetAllOpacity(Opacity);
			if (Opacity <= 0.f)
			{
				bFadeCompleted = true;
				bIsFading = false;
				HideAll();
				FadeElapsedSeconds = 0.f;
				return;
			}
		}
		else
		{
			// Opacity applied in "Reset to full size" block below
		}
	}
	else if (!bIsFading)
	{
		NotShouldShowFrames = 0;
		// Opacity applied in "Reset to full size" block below
	}
	// When bIsFading: don't overwrite opacity; we set it in the top fade block

	// Reset to full size before showing (markers may have been left at scale 0.01 after previous fade)
	// Apply fade-in or full opacity when showing
	if (!bIsFading)
	{
		if (bIsFadingIn)
		{
			FadeInElapsedSeconds += FMath::Min(DeltaTime, 0.5f);
			const float SafeFadeIn = FMath::Max(FadeInDurationSeconds, 0.01f);
			const float Opacity = FMath::Min(1.f, FadeInElapsedSeconds / SafeFadeIn);
			SetAllOpacity(Opacity);
			if (Opacity >= 1.f) { bIsFadingIn = false; }
		}
		else
		{
			SetAllOpacity(1.f);
		}
	}

	// Use snapshot during fade, else live Stats. Single marker at putt start with "L:launch P:peak D:distance".
	const bool bUseSnapshot = bIsFading;
	const float SpeedDisplay   = bUseSnapshot ? SnapshotSpeedDisplay : (Stats.LaunchSpeed * SpeedScale);
	const float PeakDisplay    = bUseSnapshot ? SnapshotPeakDisplay : (Stats.PeakSpeed * SpeedScale);
	const float DistanceDisplay = bUseSnapshot ? SnapshotDistanceDisplay : (Stats.TotalDistance * DistanceScale);
	const bool bHasLaunchPos = bUseSnapshot ? true : !Stats.StartPos.IsZero();
	const bool bShowStats = !bFadeCompleted && (bStopped || (bInMotion && bHasLaunchPos) || bIsFading);

	const FVector LaunchWorld = bUseSnapshot ? SnapshotLaunchWorld : GolfReceiver->PixelToWorld(Stats.StartPos.X, Stats.StartPos.Y);

	if (StatsWidget)
	{
		if (bShowStats)
		{
			StatsWidget->SetActorLocation(LaunchWorld);
			StatsWidget->SetMarkerText(FText::FromString(FString::Printf(
				TEXT("L:%.1f P:%.1f D:%.1f"),
				SpeedDisplay, PeakDisplay, DistanceDisplay)));
			StatsWidget->SetMarkerVisible(true);
		}
		else
		{
			StatsWidget->SetMarkerVisible(false);
		}
	}
}

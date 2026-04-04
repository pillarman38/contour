#include "BallToHoleLineActor.h"
#include "UDPGolfReceiver.h"
#include "Engine/World.h"
#include <cfloat>
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/PlatformTime.h"
#include "Serialization/Archive.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UnrealType.h"

ABallToHoleLineActor::ABallToHoleLineActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// Simple line mesh component (visual only). Attached to the actor root.
	LineMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LineMesh"));
	if (LineMeshComponent)
	{
		LineMeshComponent->SetupAttachment(RootComponent);
		LineMeshComponent->SetCastShadow(false);
		LineMeshComponent->SetVisibility(false, true);
	}

	DistanceTextComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DistanceText"));
	if (DistanceTextComponent)
	{
		DistanceTextComponent->SetupAttachment(RootComponent);
		DistanceTextComponent->SetVisibility(false);
		DistanceTextComponent->SetHorizontalAlignment(EHTA_Center);
		DistanceTextComponent->SetVerticalAlignment(EVRTA_TextCenter);
	}
	// SplineComponent is created lazily when the line is enabled.
}

static bool IsValidActorRef(const UObject* O)
{
	if (!O || !IsValid(O)) return false;
	if (O->GetName().StartsWith(TEXT("Default__"))) return false;
	UClass* C = O->GetClass();
	if (!C || C == UObject::StaticClass()) return false;
	if (O->IsTemplate()) return false;
	// Use IsChildOf instead of IsA to avoid Cast<AActor> on CDO (engine fatal)
	return C->IsChildOf(AActor::StaticClass());
}

void ABallToHoleLineActor::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	if (Ar.IsLoading())
	{
		if (!IsValidActorRef(GolfReceiver)) { GolfReceiver = nullptr; }
		if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
		if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
	}
}

void ABallToHoleLineActor::PostLoad()
{
	Super::PostLoad();
	if (!IsValidActorRef(GolfReceiver)) { GolfReceiver = nullptr; }
	if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
	if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
}

void ABallToHoleLineActor::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
	if (!IsTemplate())
	{
		if (!IsValidActorRef(GolfReceiver)) { GolfReceiver = nullptr; }
		if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
		if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
	}
}

void ABallToHoleLineActor::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	if (!IsValidActorRef(GolfReceiver)) { GolfReceiver = nullptr; }
	if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
	if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
}

#if WITH_EDITOR
void ABallToHoleLineActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (!IsValidActorRef(GolfReceiver)) { GolfReceiver = nullptr; }
	if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
	if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
}
#endif

void ABallToHoleLineActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (!IsTemplate())
	{
		if (!IsValidActorRef(GolfReceiver)) { GolfReceiver = nullptr; }
		if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
		if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
	}
}

void ABallToHoleLineActor::BeginPlay()
{
	Super::BeginPlay();
	// Clear any invalid actor refs (e.g. CDO) to avoid Cast<AActor> fatal
	if (!IsValidActorRef(GolfReceiver)) { GolfReceiver = nullptr; }
	if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
	if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
	if (bEnableLine && !IsTemplate())
	{
		// Create spline once at start (not in Tick) so we can re-attach LineMesh before first Tick
		if (!SplineComponent)
		{
			SplineComponent = NewObject<USplineComponent>(this, TEXT("Spline"));
			if (SplineComponent)
			{
				SplineComponent->RegisterComponent();
				SetRootComponent(SplineComponent);
				if (LineMeshComponent)
				{
					LineMeshComponent->AttachToComponent(SplineComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
				}
				if (DistanceTextComponent)
				{
					DistanceTextComponent->AttachToComponent(SplineComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
				}
			}
		}
		SetActorTickEnabled(true);
	}
}

void ABallToHoleLineActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (IsTemplate() || !bEnableLine) { return; }
	// Spline and LineMesh attach are done only in BeginPlay (never SetupAttachment after registration).
	if (!IsValid(SplineComponent)) { return; }

	// Handle fade-out: advance timer, then instant-hide when complete
	if (bIsFadingOut)
	{
		FadeOutElapsedSeconds += FMath::Min(DeltaTime, 0.5f);
		const float SafeDur = FMath::Max(FadeOutDurationSeconds, 0.01f);
		const float Opacity = FMath::Max(0.f, 1.f - FadeOutElapsedSeconds / SafeDur);
		SetLineOpacity(Opacity);
		if (Opacity <= 0.f)
		{
			bIsFadingOut = false;
			if (LineMeshComponent) { LineMeshComponent->SetVisibility(false, true); }
			if (SplineComponent) { SplineComponent->SetVisibility(false); }
			if (DistanceTextComponent) { DistanceTextComponent->SetVisibility(false); }
		}
		return;
	}

	// Resolve ball: from receiver if set and has ball, else our BallActor
	AActor* ResolvedBall = BallActor;
	bool bHasValidHole = IsValidActorRef(HoleActor);
	AUDPGolfReceiver* ValidReceiver = (IsValidActorRef(GolfReceiver) && GolfReceiver->GetClass()->IsChildOf(AUDPGolfReceiver::StaticClass())) ? GolfReceiver : nullptr;
	bool bPerBallAim = false;
	if (ValidReceiver)
	{
		if (IsValidActorRef(ValidReceiver->BallActor)) { ResolvedBall = ValidReceiver->BallActor; }
		bHasValidHole = (ValidReceiver->TargetHoleX != 0.f || ValidReceiver->TargetHoleY != 0.f)
			|| IsValidActorRef(ResolveTargetHole(ValidReceiver));
		if (ValidReceiver->BallsData.IsValidIndex(AimLineBallSortedIndex))
		{
			const FGolfBallInfo& Bi = ValidReceiver->BallsData[AimLineBallSortedIndex];
			if (Bi.Username.IsEmpty())
			{
				SetLineVisibility(false);
				return;
			}
			bPerBallAim = (Bi.TargetHoleX != 0.f || Bi.TargetHoleY != 0.f)
				|| (Bi.TargetHoleIndex >= 0 && ValidReceiver->HolesData.IsValidIndex(Bi.TargetHoleIndex));
			if (!bPerBallAim)
			{
				SetLineVisibility(false);
				return;
			}
			bHasValidHole = true;
		}
	}

	// Reject when ball or hole invalid (per-ball aim uses tracker pixels — BallActor optional)
	if (!bHasValidHole || (!bPerBallAim && !IsValidActorRef(ResolvedBall)))
	{
		bHasLastBallLocation = false;
		BothLostElapsedSeconds = 0.f;
		UpdateLine();
		return;
	}

	// Compute state first so we can gate BothLost (only when idle/stopped)
	bool bAllowUpdate = false;
	if (ValidReceiver)
	{
		const FString& State = ValidReceiver->PuttStats.State;
		const bool bIdle = State.Equals(TEXT("idle"), ESearchCase::IgnoreCase);
		const bool bStopped = State.Equals(TEXT("stopped"), ESearchCase::IgnoreCase);
		bAllowUpdate = (bIdle || bStopped || State.IsEmpty());
	}
	else
	{
		const FVector BallLoc = ResolvedBall->GetActorLocation();
		if (!bHasLastBallLocation || (BallLoc - LastBallLocation).Size() <= RollingThreshold)
		{
			bAllowUpdate = true;
		}
		LastBallLocation = ResolvedBall->GetActorLocation();
		bHasLastBallLocation = true;
	}

	// BothLost: only when idle/stopped (putt over). During in_motion, TracePersistSeconds controls visibility.
	if (bAllowUpdate && ValidReceiver && BothLostHideDelaySeconds > 0.f)
	{
		const bool bBallNotVisible = (ValidReceiver->BallsData.IsValidIndex(AimLineBallSortedIndex))
			? !ValidReceiver->BallsData[AimLineBallSortedIndex].Tracked.bVisible
			: !ValidReceiver->BallData.bVisible;
		int32 ThIdxForHole = ValidReceiver->TargetHoleIndex;
		if (ValidReceiver->BallsData.IsValidIndex(AimLineBallSortedIndex))
		{
			const int32 Ti = ValidReceiver->BallsData[AimLineBallSortedIndex].TargetHoleIndex;
			if (Ti >= 0)
			{
				ThIdxForHole = Ti;
			}
		}
		const int32 ThIdx = FMath::Clamp(ThIdxForHole, 0, FMath::Max(0, ValidReceiver->HolesData.Num() - 1));
		const bool bHoleNotVisible = (ValidReceiver->HolesData.Num() == 0)
			? !ValidReceiver->HoleData.bVisible
			: !ValidReceiver->HolesData[ThIdx].bVisible;
		const bool bUDPStale = ValidReceiver->GetSecondsSinceLastUDP() > 2.f;
		const bool bBothLost = bBallNotVisible && (bHoleNotVisible || bUDPStale);

		if (bBothLost)
		{
			BothLostElapsedSeconds += FMath::Min(DeltaTime, 0.5f);
			if (BothLostElapsedSeconds >= BothLostHideDelaySeconds)
			{
				SetLineVisibility(false);
				return;
			}
		}
		else
		{
			BothLostElapsedSeconds = 0.f;
		}
	}
	else if (!ValidReceiver)
	{
		BothLostElapsedSeconds = 0.f;
	}

	if (bAllowUpdate)
	{
		// Reset motion timer when we're idle/stopped so we measure consecutive in_motion time, not total accumulated
		bWasInMotion = false;
		IdleStoppedElapsed += DeltaTime;
		const int32 CurrentPuttNumber = ValidReceiver ? ValidReceiver->PuttStats.PuttNumber : 0;
		// Only reset when debounce reached AND putt number changed (new putt); avoids reset during same-putt flicker
		if (IdleStoppedElapsed >= IdleStoppedDebounceSeconds && CurrentPuttNumber != LastPuttNumberWhenReset)
		{
			InMotionPersistElapsed = 0.f;
			bWasInMotion = false;
			IdleStoppedElapsed = 0.f;
			LastPuttNumberWhenReset = CurrentPuttNumber;
		}
		SetLineVisibility(true);
		UpdateLine();
		if (ValidReceiver)
		{
			if (bPerBallAim && ValidReceiver->BallsData.IsValidIndex(AimLineBallSortedIndex)
				&& ValidReceiver->BallsData[AimLineBallSortedIndex].Tracked.bVisible)
			{
				const FGolfTrackedObject& Tr = ValidReceiver->BallsData[AimLineBallSortedIndex].Tracked;
				LastBallLocation = ValidReceiver->PixelToWorld(Tr.X, Tr.Y);
			}
			else if (IsValidActorRef(ResolvedBall))
			{
				LastBallLocation = ResolvedBall->GetActorLocation();
			}
			bHasLastBallLocation = true;
		}
		return;
	}
	IdleStoppedElapsed = 0.f;

	// Ball in motion: run persist timer and hide line after TracePersistSeconds
	const bool bJustEnteredMotion = !bWasInMotion;
	if (!bWasInMotion) { bWasInMotion = true; InMotionPersistElapsed = 0.f; }
	InMotionPersistElapsed += DeltaTime;
	if (TracePersistSeconds > 0.f && InMotionPersistElapsed >= TracePersistSeconds)
	{
		SetLineVisibility(false);
	}
}

AActor* ABallToHoleLineActor::ResolveTargetHole(AUDPGolfReceiver* ValidReceiver) const
{
	if (!ValidReceiver) return nullptr;
	const float TX = ValidReceiver->TargetHoleX;
	const float TY = ValidReceiver->TargetHoleY;
	if (TX != 0.f || TY != 0.f)
	{
		const FVector TargetWorld = ValidReceiver->PixelToWorld(TX, TY);
		AActor* Best = nullptr;
		float BestD2 = FLT_MAX;
		if (IsValidActorRef(ValidReceiver->HoleActor))
		{
			const float D2 = FVector::DistSquared(ValidReceiver->HoleActor->GetActorLocation(), TargetWorld);
			if (D2 < BestD2) { BestD2 = D2; Best = ValidReceiver->HoleActor; }
		}
		for (AActor* A : ValidReceiver->HoleActors)
		{
			if (IsValidActorRef(A))
			{
				const float D2 = FVector::DistSquared(A->GetActorLocation(), TargetWorld);
				if (D2 < BestD2) { BestD2 = D2; Best = A; }
			}
		}
		if (Best) return Best;
	}
	const int32 TargetIdx = FMath::Clamp(ValidReceiver->TargetHoleIndex, 0, ValidReceiver->HoleActors.Num());
	if (IsValidActorRef(ValidReceiver->HoleActor) && TargetIdx == 0) return ValidReceiver->HoleActor;
	if (TargetIdx > 0 && ValidReceiver->HoleActors.IsValidIndex(TargetIdx - 1) && IsValidActorRef(ValidReceiver->HoleActors[TargetIdx - 1])) return ValidReceiver->HoleActors[TargetIdx - 1];
	if (IsValidActorRef(ValidReceiver->HoleActor)) return ValidReceiver->HoleActor;
	if (ValidReceiver->HoleActors.Num() > 0 && IsValidActorRef(ValidReceiver->HoleActors[0])) return ValidReceiver->HoleActors[0];
	return nullptr;
}

void ABallToHoleLineActor::UpdateLine()
{
	if (IsTemplate() || !IsValid(SplineComponent) || !bEnableLine) { return; }

	AActor* ResolvedBall = BallActor;
	FVector HoleLoc = FVector::ZeroVector;
	bool bHasHoleLoc = false;
	bool bUsePerBallAim = false;
	FVector PerBallBallLoc = FVector::ZeroVector;

	AUDPGolfReceiver* ValidReceiver = (IsValidActorRef(GolfReceiver) && GolfReceiver->GetClass()->IsChildOf(AUDPGolfReceiver::StaticClass())) ? GolfReceiver : nullptr;

	if (ValidReceiver && ValidReceiver->BallsData.IsValidIndex(AimLineBallSortedIndex))
	{
		const FGolfBallInfo& Bi = ValidReceiver->BallsData[AimLineBallSortedIndex];
		if (Bi.Tracked.bVisible)
		{
			PerBallBallLoc = ValidReceiver->PixelToWorld(Bi.Tracked.X, Bi.Tracked.Y);
			if (Bi.TargetHoleX != 0.f || Bi.TargetHoleY != 0.f)
			{
				HoleLoc = ValidReceiver->PixelToWorld(Bi.TargetHoleX, Bi.TargetHoleY);
				bHasHoleLoc = true;
				bUsePerBallAim = true;
			}
			else if (Bi.TargetHoleIndex >= 0 && ValidReceiver->HolesData.IsValidIndex(Bi.TargetHoleIndex))
			{
				const FGolfHoleInfo& H = ValidReceiver->HolesData[Bi.TargetHoleIndex];
				HoleLoc = ValidReceiver->PixelToWorld(H.X, H.Y);
				bHasHoleLoc = true;
				bUsePerBallAim = true;
			}
		}
	}

	if (!bUsePerBallAim)
	{
		if (ValidReceiver)
		{
			if (IsValidActorRef(ValidReceiver->BallActor)) { ResolvedBall = ValidReceiver->BallActor; }
			const float TX = ValidReceiver->TargetHoleX;
			const float TY = ValidReceiver->TargetHoleY;
			if (TX != 0.f || TY != 0.f)
			{
				HoleLoc = ValidReceiver->PixelToWorld(TX, TY);
				bHasHoleLoc = true;
			}
		}
		if (!bHasHoleLoc)
		{
			AActor* ResolvedHole = ResolveTargetHole(ValidReceiver);
			if (!IsValidActorRef(ResolvedHole)) { ResolvedHole = HoleActor; }
			if (IsValidActorRef(ResolvedHole))
			{
				HoleLoc = ResolvedHole->GetActorLocation();
				bHasHoleLoc = true;
			}
		}
	}

	const bool bBallOk = bUsePerBallAim || IsValidActorRef(ResolvedBall);
	if (!bBallOk || !bHasHoleLoc)
	{
		SplineComponent->ClearSplinePoints();
		if (DistanceTextComponent) { DistanceTextComponent->SetVisibility(false); }
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	const FVector BallLoc = bUsePerBallAim ? PerBallBallLoc : ResolvedBall->GetActorLocation();

	// ── Straight line mesh between ball and hole ────────────────────────
	if (LineMeshComponent)
	{
		if (!LineMesh)
		{
			LineMeshComponent->SetVisibility(false, true);
		}
		else
		{
			LineMeshComponent->SetStaticMesh(LineMesh);

			const FVector Dir = HoleLoc - BallLoc;
			const float Length = Dir.Size();
			if (Length > KINDA_SMALL_NUMBER && LineMeshBaseLength > KINDA_SMALL_NUMBER)
			{
				const FVector Mid = (BallLoc + HoleLoc) * 0.5f;
				const FRotator Rot = FRotationMatrix::MakeFromX(Dir).Rotator();

				LineMeshComponent->SetWorldLocation(Mid);
				LineMeshComponent->SetWorldRotation(Rot);

				const float ScaleX = Length / LineMeshBaseLength;
				const FVector CurrentScale = LineMeshComponent->GetComponentScale();
				LineMeshComponent->SetWorldScale3D(FVector(ScaleX, CurrentScale.Y, CurrentScale.Z));

				LineMeshComponent->SetVisibility(true, true);

				// Display distance to hole in inches (Unreal uses cm: 1 unit = 1 cm)
				if (bShowDistanceInches && DistanceTextComponent)
				{
					const float DistanceInches = Length / 2.54f;
					DistanceTextComponent->SetText(FText::FromString(FString::Printf(TEXT("%.1f in"), DistanceInches)));
					DistanceTextComponent->SetWorldLocation(Mid + FVector(0.f, 0.f, DistanceTextHeightOffset));
					// Face up (horizontal) and adhere to line angle.
					// MakeFromYX(Y,X): Y=line (text flow), X=perp, Z=Y×X points UP so text faces sky.
					FVector LineDirXY(Dir.X, Dir.Y, 0.f);
					if (LineDirXY.Normalize())
					{
						const FVector PerpDir(-LineDirXY.Y, LineDirXY.X, 0.f);
						FRotator R = FRotationMatrix::MakeFromYX(LineDirXY, PerpDir).Rotator();
						DistanceTextComponent->SetWorldRotation(FRotator(R.Pitch + DistanceTextPitchOffset, R.Yaw, R.Roll + DistanceTextRollOffset));
					}
					DistanceTextComponent->SetVisibility(true);
				}
				else if (DistanceTextComponent)
				{
					DistanceTextComponent->SetVisibility(false);
				}
			}
			else
			{
				LineMeshComponent->SetVisibility(false, true);
				if (DistanceTextComponent) { DistanceTextComponent->SetVisibility(false); }
			}
		}
	}

	const int32 N = FMath::Clamp(NumSamplePoints, 2, 32);
	TArray<FVector> Points;
	Points.Reserve(N);

	const float TraceStartZ = BallLoc.Z + TraceHeightAbove;
	const FVector TraceEndOffset(0.f, 0.f, -2.f * TraceHalfLength);

	// Never pass this as IgnoreActor when template/CDO or no world (avoids "Cast of Default__Object to Actor failed")
	AActor* IgnoreActor = (GetWorld() && !IsTemplate()) ? this : nullptr;
	FCollisionQueryParams QueryParams(TEXT("BallToHoleLine"), false, IgnoreActor);

	for (int32 i = 0; i < N; ++i)
	{
		const float t = (N > 1) ? (float)i / (float)(N - 1) : 0.f;
		const FVector SampleXY(FMath::Lerp(BallLoc.X, HoleLoc.X, t), FMath::Lerp(BallLoc.Y, HoleLoc.Y, t), TraceStartZ);
		const FVector TraceEnd = SampleXY + TraceEndOffset;

		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, SampleXY, TraceEnd, TraceChannel, QueryParams);

		FVector Point = bHit ? Hit.ImpactPoint : FVector(SampleXY.X, SampleXY.Y, FMath::Lerp(BallLoc.Z, HoleLoc.Z, t));
		Points.Add(Point);
	}

	SplineComponent->ClearSplinePoints();
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		SplineComponent->AddSplinePoint(Points[i], ESplineCoordinateSpace::World, true);
	}
	SplineComponent->UpdateSpline();
}

void ABallToHoleLineActor::SetLineVisibility(bool bVisible)
{
	bIsFadingOut = false;
	if (bVisible)
	{
		SetLineOpacity(1.f);
		if (LineMeshComponent) { LineMeshComponent->SetVisibility(true, true); }
		if (SplineComponent) { SplineComponent->SetVisibility(true); }
		// DistanceTextComponent visibility is set in UpdateLine when line is drawn
	}
	else
	{
		if (FadeOutDurationSeconds > 0.f && LineMeshComponent && LineMeshComponent->GetNumMaterials() > 0)
		{
			StartFadeOut();
		}
		else
		{
			if (LineMeshComponent) { LineMeshComponent->SetVisibility(false, true); }
			if (SplineComponent) { SplineComponent->SetVisibility(false); }
			if (DistanceTextComponent) { DistanceTextComponent->SetVisibility(false); }
		}
	}
}

void ABallToHoleLineActor::SetLineOpacity(float Opacity)
{
	EnsureLineMeshDynamicMaterials();
	for (UMaterialInstanceDynamic* MID : LineMeshDynamicMaterials)
	{
		if (MID) { MID->SetScalarParameterValue(FName("Opacity"), Opacity); }
	}
}

void ABallToHoleLineActor::StartFadeOut()
{
	bIsFadingOut = true;
	FadeOutElapsedSeconds = 0.f;
}

void ABallToHoleLineActor::EnsureLineMeshDynamicMaterials()
{
	if (!LineMeshComponent || LineMeshDynamicMaterials.Num() > 0) { return; }
	const int32 NumSlots = LineMeshComponent->GetNumMaterials();
	for (int32 i = 0; i < NumSlots; ++i)
	{
		if (UMaterialInterface* Mat = LineMeshComponent->GetMaterial(i))
		{
			UMaterialInstanceDynamic* MID = LineMeshComponent->CreateAndSetMaterialInstanceDynamic(i);
			if (MID) { LineMeshDynamicMaterials.Add(MID); }
		}
	}
}

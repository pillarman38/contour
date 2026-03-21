#include "BallTrailSplineActor.h"
#include "UDPGolfReceiver.h"
#include "Components/SplineMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

static bool IsValidActorRef(const UObject* O)
{
	if (!O || !IsValid(O)) return false;
	if (O->GetName().StartsWith(TEXT("Default__"))) return false;
	UClass* C = O->GetClass();
	if (!C || C == UObject::StaticClass()) return false;
	if (O->IsTemplate()) return false;
	return C->IsChildOf(AActor::StaticClass());
}

ABallTrailSplineActor::ABallTrailSplineActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(RootScene);
}

void ABallTrailSplineActor::BeginPlay()
{
	Super::BeginPlay();
	if (!IsValidActorRef(GolfReceiver)) { GolfReceiver = nullptr; }
	if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
	if (!TrailMaterial && bEnableTrail)
	{
		TrailMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Trail.M_Trail"));
	}
	if (bEnableTrail && !IsTemplate())
	{
		SetActorTickEnabled(true);
	}
}

void ABallTrailSplineActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (IsTemplate() || !bEnableTrail) return;

	AUDPGolfReceiver* ValidReceiver = (IsValidActorRef(GolfReceiver) && GolfReceiver->GetClass()->IsChildOf(AUDPGolfReceiver::StaticClass())) ? GolfReceiver : nullptr;
	AActor* ResolvedBall = BallActor;
	if (ValidReceiver && IsValidActorRef(ValidReceiver->BallActor))
	{
		ResolvedBall = ValidReceiver->BallActor;
	}

	FVector BallPos = FVector::ZeroVector;
	bool bBallValid = false;
	if (ValidReceiver && ValidReceiver->BallData.bVisible)
	{
		BallPos = ValidReceiver->PixelToWorld(ValidReceiver->BallData.X, ValidReceiver->BallData.Y);
		bBallValid = true;
	}
	else if (IsValidActorRef(ResolvedBall))
	{
		BallPos = ResolvedBall->GetActorLocation();
		bBallValid = true;
	}

	const FString State = ValidReceiver ? ValidReceiver->PuttStats.State : FString();
	const bool bIdle = State.Equals(TEXT("idle"), ESearchCase::IgnoreCase);
	const bool bStopped = State.Equals(TEXT("stopped"), ESearchCase::IgnoreCase);
	const bool bInMotion = State.Equals(TEXT("in_motion"), ESearchCase::IgnoreCase);

	// Handle fade-out
	if (bIsFadingOut)
	{
		FadeOutElapsedSeconds += FMath::Min(DeltaTime, 0.5f);
		const float SafeDur = FMath::Max(FadeOutDurationSeconds, 0.01f);
		const float Opacity = FMath::Max(0.f, 1.f - FadeOutElapsedSeconds / SafeDur);
		SetTrailOpacity(Opacity);
		if (Opacity <= 0.f)
		{
			bIsFadingOut = false;
			ClearTrail();
		}
		return;
	}

	// Ball in motion: record positions (clear trail on new putt)
	if (bInMotion && bBallValid)
	{
		const int32 CurrentPutt = ValidReceiver ? ValidReceiver->PuttStats.PuttNumber : 0;
		if (CurrentPutt != LastPuttNumber)
		{
			ClearTrail();
			LastPuttNumber = CurrentPutt;
		}
		bWasInMotion = true;
		StoppedElapsedSeconds = 0.f;
		if (TrailPoints.Num() == 0 || (BallPos - TrailPoints.Last()).SizeSquared() > FMath::Square(SampleDistanceThreshold))
		{
			TrailPoints.Add(BallPos);
			if (TrailPoints.Num() > MaxTrailPoints)
			{
				TrailPoints.RemoveAt(0);
			}
		}
		RebuildSplineMeshes();
		return;
	}

	// Ball stopped or idle: run persist timer
	if (bStopped || bIdle || State.IsEmpty())
	{
		if (bWasInMotion && TrailPoints.Num() > 0)
		{
			StoppedElapsedSeconds += FMath::Min(DeltaTime, 0.5f);
			if (TrailPersistSeconds > 0.f && StoppedElapsedSeconds >= TrailPersistSeconds)
			{
				if (FadeOutDurationSeconds > 0.f && SplineMeshPool.Num() > 0)
				{
					bIsFadingOut = true;
					FadeOutElapsedSeconds = 0.f;
				}
				else
				{
					ClearTrail();
				}
			}
		}
		bWasInMotion = false;
	}
}

void ABallTrailSplineActor::RebuildSplineMeshes()
{
	if (!TrailMesh || !TrailMaterial) return;

	while (SplineMeshPool.Num() < FMath::Max(0, TrailPoints.Num() - 1))
	{
		USplineMeshComponent* Seg = NewObject<USplineMeshComponent>(this, FName(*FString::Printf(TEXT("TrailSeg%d"), SplineMeshPool.Num())));
		if (Seg)
		{
			Seg->SetupAttachment(RootComponent);
			Seg->SetStaticMesh(TrailMesh);
			Seg->SetMaterial(0, TrailMaterial);
			Seg->SetCastShadow(false);
			Seg->RegisterComponent();
			SplineMeshPool.Add(Seg);
		}
		else break;
	}

	const FVector Tangent = FVector(100.f, 0.f, 0.f);
	for (int32 i = 0; i < TrailPoints.Num() - 1; ++i)
	{
		if (i < SplineMeshPool.Num() && SplineMeshPool[i])
		{
			SplineMeshPool[i]->SetMaterial(0, TrailMaterial);  // Reset to base (opacity 1) after a prior fade
			SplineMeshPool[i]->SetVisibility(true);
			SplineMeshPool[i]->SetStartAndEnd(TrailPoints[i], Tangent, TrailPoints[i + 1], Tangent, true);
		}
	}
	for (int32 i = TrailPoints.Num() - 1; i < SplineMeshPool.Num(); ++i)
	{
		if (SplineMeshPool[i]) { SplineMeshPool[i]->SetVisibility(false); }
	}
}

void ABallTrailSplineActor::ClearTrail()
{
	TrailPoints.Empty();
	TrailDynamicMaterials.Empty();
	for (USplineMeshComponent* Seg : SplineMeshPool)
	{
		if (Seg)
		{
			Seg->SetVisibility(false);
			if (TrailMaterial) { Seg->SetMaterial(0, TrailMaterial); }
		}
	}
	StoppedElapsedSeconds = 0.f;
	bWasInMotion = false;
}

void ABallTrailSplineActor::SetTrailOpacity(float Opacity)
{
	EnsureTrailMaterials();
	for (UMaterialInstanceDynamic* MID : TrailDynamicMaterials)
	{
		if (MID) { MID->SetScalarParameterValue(FName("Opacity"), Opacity); }
	}
}

void ABallTrailSplineActor::EnsureTrailMaterials()
{
	// Only create MIDs for segments that don't have one yet (avoid resetting opacity during fade)
	while (TrailDynamicMaterials.Num() < SplineMeshPool.Num())
	{
		const int32 i = TrailDynamicMaterials.Num();
		if (i < SplineMeshPool.Num() && SplineMeshPool[i])
		{
			UMaterialInstanceDynamic* MID = SplineMeshPool[i]->CreateAndSetMaterialInstanceDynamic(0);
			if (MID) { TrailDynamicMaterials.Add(MID); }
			else break;
		}
		else break;
	}
}

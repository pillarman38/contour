#include "UDPGolfReceiver.h"
#include "BallToHoleLineActor.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"
#include "Serialization/Archive.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "IPAddress.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Set.h"

DEFINE_LOG_CATEGORY_STATIC(LogGolfUDP, Log, All);

/** Blueprint meshes are often Static; UDP-driven actors must be Movable or transforms do not apply correctly. */
static void EnsureUdpDrivenActorMovable(AActor* A)
{
	if (!A || !IsValid(A) || A->IsTemplate()) return;
	if (USceneComponent* Root = A->GetRootComponent())
	{
		if (Root->Mobility != EComponentMobility::Movable)
		{
			Root->SetMobility(EComponentMobility::Movable);
		}
	}
	A->ForEachComponent<UStaticMeshComponent>(false, [](UStaticMeshComponent* SM)
	{
		if (SM && SM->Mobility != EComponentMobility::Movable)
		{
			SM->SetMobility(EComponentMobility::Movable);
		}
	});
}

/** Sorted row in BallsData for this stable_id, or INDEX_NONE (no sorted-index fallback — that re-binds slots when balls cross). */
static int32 FindRowByStableIdOnly(const TArray<FGolfBallInfo>& Balls, int32 StableId)
{
	if (StableId < 0)
	{
		return INDEX_NONE;
	}
	for (int32 i = 0; i < Balls.Num(); ++i)
	{
		if (Balls[i].StableId == StableId)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

// ─────────────────────────────────────────────────────────────────────────────
AUDPGolfReceiver::AUDPGolfReceiver()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

// Reject any ref that would cause "Cast of Default__Object to Actor failed"
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

void AUDPGolfReceiver::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	if (Ar.IsLoading())
	{
		if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
		if (!IsValidActorRef(PutterActor)) { PutterActor = nullptr; }
		if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
		if (!IsValidActorRef(AimLineActor)) { AimLineActor = nullptr; }
		if (!IsValidActorRef(PlacementMarkerActor)) { PlacementMarkerActor = nullptr; }
		if (!IsValidActorRef(PutterCrosshairTemplate)) { PutterCrosshairTemplate = nullptr; }
		for (int32 i = HoleActors.Num() - 1; i >= 0; --i)
		{
			if (!IsValidActorRef(HoleActors[i])) { HoleActors.RemoveAt(i); }
		}
		for (int32 i = BallActors.Num() - 1; i >= 0; --i)
		{
			if (!IsValidActorRef(BallActors[i])) { BallActors.RemoveAt(i); }
		}
		for (int32 i = AimLineActorsSpawned.Num() - 1; i >= 0; --i)
		{
			if (!IsValidActorRef(AimLineActorsSpawned[i])) { AimLineActorsSpawned.RemoveAt(i); }
		}
		for (int32 i = PlacementMarkerActorsSpawned.Num() - 1; i >= 0; --i)
		{
			if (!IsValidActorRef(PlacementMarkerActorsSpawned[i])) { PlacementMarkerActorsSpawned.RemoveAt(i); }
		}
		for (int32 i = PutterCrosshairActorsSpawned.Num() - 1; i >= 0; --i)
		{
			if (!IsValidActorRef(PutterCrosshairActorsSpawned[i])) { PutterCrosshairActorsSpawned.RemoveAt(i); }
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
void AUDPGolfReceiver::PostLoad()
{
	Super::PostLoad();
	if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
	if (!IsValidActorRef(PutterActor)) { PutterActor = nullptr; }
	if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
	if (!IsValidActorRef(PlacementMarkerActor)) { PlacementMarkerActor = nullptr; }
	if (!IsValidActorRef(PutterCrosshairTemplate)) { PutterCrosshairTemplate = nullptr; }
	for (int32 i = HoleActors.Num() - 1; i >= 0; --i)
	{
		if (!IsValidActorRef(HoleActors[i])) { HoleActors.RemoveAt(i); }
	}
	for (int32 i = BallActors.Num() - 1; i >= 0; --i)
	{
		if (!IsValidActorRef(BallActors[i])) { BallActors.RemoveAt(i); }
	}
}

void AUDPGolfReceiver::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
	if (!IsTemplate())
	{
		if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
		if (!IsValidActorRef(PutterActor)) { PutterActor = nullptr; }
		if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
		if (!IsValidActorRef(PlacementMarkerActor)) { PlacementMarkerActor = nullptr; }
		if (!IsValidActorRef(PutterCrosshairTemplate)) { PutterCrosshairTemplate = nullptr; }
		for (int32 i = HoleActors.Num() - 1; i >= 0; --i)
		{
			if (!IsValidActorRef(HoleActors[i])) { HoleActors.RemoveAt(i); }
		}
	}
}

void AUDPGolfReceiver::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
	if (!IsValidActorRef(PutterActor)) { PutterActor = nullptr; }
	if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
	if (!IsValidActorRef(PlacementMarkerActor)) { PlacementMarkerActor = nullptr; }
	if (!IsValidActorRef(PutterCrosshairTemplate)) { PutterCrosshairTemplate = nullptr; }
	for (int32 i = HoleActors.Num() - 1; i >= 0; --i)
	{
		if (!IsValidActorRef(HoleActors[i])) { HoleActors.RemoveAt(i); }
	}
	for (int32 i = BallActors.Num() - 1; i >= 0; --i)
	{
		if (!IsValidActorRef(BallActors[i])) { BallActors.RemoveAt(i); }
	}
}

#if WITH_EDITOR
void AUDPGolfReceiver::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
	if (!IsValidActorRef(PutterActor)) { PutterActor = nullptr; }
	if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
	if (!IsValidActorRef(PlacementMarkerActor)) { PlacementMarkerActor = nullptr; }
	if (!IsValidActorRef(PutterCrosshairTemplate)) { PutterCrosshairTemplate = nullptr; }
	for (int32 i = HoleActors.Num() - 1; i >= 0; --i)
	{
		if (!IsValidActorRef(HoleActors[i])) { HoleActors.RemoveAt(i); }
	}
	for (int32 i = BallActors.Num() - 1; i >= 0; --i)
	{
		if (!IsValidActorRef(BallActors[i])) { BallActors.RemoveAt(i); }
	}
}
#endif

void AUDPGolfReceiver::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (!IsTemplate())
	{
		if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
		if (!IsValidActorRef(PutterActor)) { PutterActor = nullptr; }
		if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
		if (!IsValidActorRef(PlacementMarkerActor)) { PlacementMarkerActor = nullptr; }
		if (!IsValidActorRef(PutterCrosshairTemplate)) { PutterCrosshairTemplate = nullptr; }
		for (int32 i = HoleActors.Num() - 1; i >= 0; --i)
		{
			if (!IsValidActorRef(HoleActors[i])) { HoleActors.RemoveAt(i); }
		}
		for (int32 i = BallActors.Num() - 1; i >= 0; --i)
		{
			if (!IsValidActorRef(BallActors[i])) { BallActors.RemoveAt(i); }
		}
	}
}

void AUDPGolfReceiver::BeginPlay()
{
	Super::BeginPlay();
	BallActorLatchedStableIds.Reset();
	if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
	if (!IsValidActorRef(PutterActor)) { PutterActor = nullptr; }
	if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
	if (!IsValidActorRef(PlacementMarkerActor)) { PlacementMarkerActor = nullptr; }
	if (!IsValidActorRef(PutterCrosshairTemplate)) { PutterCrosshairTemplate = nullptr; }
	for (int32 i = HoleActors.Num() - 1; i >= 0; --i)
	{
		if (!IsValidActorRef(HoleActors[i])) { HoleActors.RemoveAt(i); }
	}
	for (int32 i = BallActors.Num() - 1; i >= 0; --i)
	{
		if (!IsValidActorRef(BallActors[i])) { BallActors.RemoveAt(i); }
	}
	StartReceiver();
}

void AUDPGolfReceiver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Destroy any dynamically spawned hole actors
	for (AActor* A : HoleActors)
	{
		if (IsValid(A) && !A->IsTemplate())
		{
			A->Destroy();
		}
	}
	HoleActors.Empty();
	// Destroy any dynamically spawned ball actors
	for (AActor* A : BallActors)
	{
		if (IsValid(A) && !A->IsTemplate())
		{
			A->Destroy();
		}
	}
	BallActors.Empty();
	BallActorLatchedStableIds.Reset();
	for (ABallToHoleLineActor* L : AimLineActorsSpawned)
	{
		if (IsValid(L) && !L->IsTemplate())
		{
			L->Destroy();
		}
	}
	AimLineActorsSpawned.Empty();
	for (AActor* A : PlacementMarkerActorsSpawned)
	{
		if (IsValid(A) && !A->IsTemplate())
		{
			A->Destroy();
		}
	}
	PlacementMarkerActorsSpawned.Empty();
	for (AActor* C : PutterCrosshairActorsSpawned)
	{
		if (IsValid(C) && !C->IsTemplate())
		{
			C->Destroy();
		}
	}
	PutterCrosshairActorsSpawned.Empty();
	StopReceiver();
	Super::EndPlay(EndPlayReason);
}

FGolfUDPPayload AUDPGolfReceiver::GetLastPayload() const
{
	return CachedPayloadForBlueprint;
}

FGolfUDPPayload AUDPGolfReceiver::GetLastPayloadFromActor(UObject* ActorOrObject)
{
	// Reject null, CDO, or non-Actor WITHOUT calling IsA on Default__Object (would trigger engine Cast fatal)
	if (!ActorOrObject) return FGolfUDPPayload();
	if (ActorOrObject->GetClass() == UObject::StaticClass()) return FGolfUDPPayload();
	if (ActorOrObject->GetName().StartsWith(TEXT("Default__"))) return FGolfUDPPayload();
	if (ActorOrObject->IsTemplate()) return FGolfUDPPayload();
	if (!ActorOrObject->GetClass()->IsChildOf(AActor::StaticClass())) return FGolfUDPPayload();
	AActor* Actor = static_cast<AActor*>(ActorOrObject);
	if (AUDPGolfReceiver* Receiver = Cast<AUDPGolfReceiver>(Actor))
	{
		return Receiver->GetLastPayload();
	}
	return FGolfUDPPayload();
}

/** Second line for ball labels (same values as former PuttMarkerActor L/P/D line). */
static FString BallStatsLineForLabel(const FGolfPuttStats& S)
{
	if (!S.State.Equals(TEXT("in_motion"), ESearchCase::IgnoreCase) &&
		!S.State.Equals(TEXT("stopped"), ESearchCase::IgnoreCase))
	{
		return FString();
	}
	return FString::Printf(TEXT("L:%.1f P:%.1f D:%.1f"), S.LaunchSpeed, S.PeakSpeed, S.TotalDistance);
}

FText AUDPGolfReceiver::PuttStatsToText(const FGolfPuttStats& Stats)
{
	const FString Line = FString::Printf(
		TEXT("Putt #%d  State: %s\nLaunch: %.1f  Peak: %.1f  Current: %.1f\nDistance: %.1f  Break: %.1f  Time: %.2fs"),
		Stats.PuttNumber,
		*Stats.State,
		Stats.LaunchSpeed,
		Stats.PeakSpeed,
		Stats.CurrentSpeed,
		Stats.TotalDistance,
		Stats.BreakDistance,
		Stats.TimeInMotion
	);
	return FText::FromString(Line);
}

FText AUDPGolfReceiver::BallLabelToText(const FString& Username, int32 PuttNumber)
{
	if (Username.IsEmpty())
	{
		return FText::FromString(TEXT("Claim this ball"));
	}
	return FText::FromString(FString::Printf(TEXT("%s - Putt #%d"), *Username, PuttNumber));
}

FText AUDPGolfReceiver::GetPrimaryBallLabel() const
{
	const FString StatsLine = BallStatsLineForLabel(PuttStats);
	if (PrimaryUsername.IsEmpty())
	{
		if (StatsLine.IsEmpty())
		{
			return FText::FromString(TEXT("Claim this ball"));
		}
		return FText::FromString(FString::Printf(TEXT("Claim this ball\n%s"), *StatsLine));
	}
	FString Top = FString::Printf(TEXT("%s - Putt #%d"), *PrimaryUsername, PuttStats.PuttNumber);
	if (!StatsLine.IsEmpty())
	{
		Top += FString::Printf(TEXT("\n%s"), *StatsLine);
	}
	return FText::FromString(Top);
}

FText AUDPGolfReceiver::GetBallLabelForSortedIndex(int32 SortedIndex) const
{
	if (!BallsData.IsValidIndex(SortedIndex))
	{
		return FText::GetEmpty();
	}
	const FGolfBallInfo& Ball = BallsData[SortedIndex];
	FString StatsLine = BallStatsLineForLabel(Ball.Stats);
	// Sender historically only filled stats on balls[0]; root "stats" still has session putt metrics.
	if (StatsLine.IsEmpty())
	{
		StatsLine = BallStatsLineForLabel(PuttStats);
	}
	if (Ball.Username.IsEmpty())
	{
		if (StatsLine.IsEmpty())
		{
			return FText::FromString(TEXT("Claim this ball"));
		}
		return FText::FromString(FString::Printf(TEXT("Claim this ball\n%s"), *StatsLine));
	}
	FString Top = FString::Printf(TEXT("%s - Putt #%d"), *Ball.Username, Ball.Stats.PuttNumber);
	if (!StatsLine.IsEmpty())
	{
		Top += FString::Printf(TEXT("\n%s"), *StatsLine);
	}
	return FText::FromString(Top);
}

FText AUDPGolfReceiver::PeakSpeedToText(const FGolfPuttStats& Stats)
{
	const FString S = FString::Printf(TEXT("%.1f in/s"), Stats.PeakSpeed);
	return FText::FromString(S);
}

// ─────────────────────────────────────────────────────────────────────────────
// Socket lifecycle
// ─────────────────────────────────────────────────────────────────────────────
void AUDPGolfReceiver::StartReceiver()
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogGolfUDP, Error, TEXT("Failed to get socket subsystem"));
		return;
	}

	Socket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("GolfUDPReceiver"), false);
	if (!Socket)
	{
		UE_LOG(LogGolfUDP, Error, TEXT("Failed to create UDP socket"));
		return;
	}

	Socket->SetReuseAddr(true);
	Socket->SetNonBlocking(false);
	Socket->SetRecvErr(true);

	TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid = false;
	Addr->SetIp(*ListenIP, bIsValid);
	if (!bIsValid)
	{
		UE_LOG(LogGolfUDP, Error, TEXT("Invalid listen IP: %s"), *ListenIP);
		SocketSubsystem->DestroySocket(Socket);
		Socket = nullptr;
		return;
	}
	Addr->SetPort(ListenPort);

	if (!Socket->Bind(*Addr))
	{
		UE_LOG(LogGolfUDP, Error, TEXT("Failed to bind UDP socket to %s:%d"), *ListenIP, ListenPort);
		SocketSubsystem->DestroySocket(Socket);
		Socket = nullptr;
		return;
	}

	UE_LOG(LogGolfUDP, Log, TEXT("UDP socket bound to %s:%d"), *ListenIP, ListenPort);

	ReceiverRunnable = new FUDPReceiverRunnable(this);
	ReceiverThread = FRunnableThread::Create(ReceiverRunnable, TEXT("GolfUDPReceiverThread"), 0, TPri_Normal);
}

void AUDPGolfReceiver::StopReceiver()
{
	if (ReceiverRunnable)
	{
		ReceiverRunnable->Stop();
	}

	if (Socket)
	{
		Socket->Close();
	}

	if (ReceiverThread)
	{
		ReceiverThread->WaitForCompletion();
		delete ReceiverThread;
		ReceiverThread = nullptr;
	}

	delete ReceiverRunnable;
	ReceiverRunnable = nullptr;

	if (Socket)
	{
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Background receive thread
// ─────────────────────────────────────────────────────────────────────────────
uint32 AUDPGolfReceiver::FUDPReceiverRunnable::Run()
{
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(8192);

	while (!bStopping)
	{
		if (!Owner || !Owner->Socket)
		{
			FPlatformProcess::Sleep(0.01f);
			continue;
		}

		int32 BytesRead = 0;
		TSharedRef<FInternetAddr> Sender = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

		if (Owner->Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(50)))
		{
			if (Owner->Socket->RecvFrom(Buffer.GetData(), Buffer.Num(), BytesRead, *Sender))
			{
				if (BytesRead > 0 && BytesRead < Buffer.Num())
				{
					// Null-terminate so conversion stops at exact boundary
					Buffer[BytesRead] = 0;

					FString JsonStr;
					FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Buffer.GetData()), BytesRead);
					// FString(InCount, InSrc) - count first, pointer second; copies exactly Length() TCHARs
					JsonStr = FString(Converter.Length(), Converter.Get());

					FGolfUDPPayload Payload;
					if (Owner->ParsePayload(JsonStr, Payload))
					{
						FScopeLock Lock(&Owner->DataLock);
						Owner->LatestPayload = Payload;
						Owner->bNewDataAvailable = true;
					}
				}
			}
		}
	}

	return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON parsing — matches the format from unreal_sender.cpp
// ─────────────────────────────────────────────────────────────────────────────
bool AUDPGolfReceiver::ParsePayload(const FString& Json, FGolfUDPPayload& Out) const
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);

	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogGolfUDP, Warning, TEXT("Failed to parse UDP JSON"));
		return false;
	}

	Out.TimestampMs = static_cast<int64>(Root->GetNumberField(TEXT("timestamp_ms")));
	Out.bPuttMade = Root->GetBoolField(TEXT("putt_made"));
	Out.HoleAimBallIndex = 2147483647; // INT32_MAX: field absent → legacy crosshair on
	if (Root->HasField(TEXT("hole_aim_ball_index")))
	{
		Out.HoleAimBallIndex = static_cast<int32>(Root->GetNumberField(TEXT("hole_aim_ball_index")));
	}

	auto ParseTracked = [](const TSharedPtr<FJsonObject>& Obj, FGolfTrackedObject& T)
	{
		if (!Obj.IsValid()) return;
		T.X = Obj->GetNumberField(TEXT("x"));
		T.Y = Obj->GetNumberField(TEXT("y"));
		T.VX = Obj->GetNumberField(TEXT("vx"));
		T.VY = Obj->GetNumberField(TEXT("vy"));
		T.Confidence = Obj->GetNumberField(TEXT("conf"));
		T.bVisible = Obj->GetBoolField(TEXT("visible"));
	};

	ParseTracked(Root->GetObjectField(TEXT("ball")), Out.Ball);
	ParseTracked(Root->GetObjectField(TEXT("putter")), Out.Putter);

	Out.Putters.Reset();
	static constexpr int32 kMaxPuttersUdp = 8;
	const TArray<TSharedPtr<FJsonValue>>* PuttersArray = nullptr;
	if (Root->TryGetArrayField(TEXT("putters"), PuttersArray) && PuttersArray)
	{
		for (const TSharedPtr<FJsonValue>& Elem : *PuttersArray)
		{
			const TSharedPtr<FJsonObject>* PO = nullptr;
			if (Elem.IsValid() && Elem->TryGetObject(PO) && PO && (*PO).IsValid())
			{
				FGolfTrackedObject T;
				ParseTracked(*PO, T);
				Out.Putters.Add(T);
				if (Out.Putters.Num() >= kMaxPuttersUdp) break;
			}
		}
	}
	if (Out.Putters.Num() == 0 && Out.Putter.bVisible)
	{
		Out.Putters.Add(Out.Putter);
	}

	Out.Holes.Reset();
	const TArray<TSharedPtr<FJsonValue>>* HolesArray = nullptr;
	if (Root->TryGetArrayField(TEXT("holes"), HolesArray) && HolesArray)
	{
		for (const TSharedPtr<FJsonValue>& Elem : *HolesArray)
		{
			const TSharedPtr<FJsonObject>* HO = nullptr;
			if (Elem.IsValid() && Elem->TryGetObject(HO) && HO && (*HO).IsValid())
			{
				const auto& H = *HO;
				FGolfHoleInfo Hi;
				Hi.X = H->GetNumberField(TEXT("x"));
				Hi.Y = H->GetNumberField(TEXT("y"));
				Hi.Radius = H->GetNumberField(TEXT("radius"));
				Hi.bVisible = H->GetBoolField(TEXT("visible"));
				Out.Holes.Add(Hi);
			}
		}
	}
	Out.TargetHoleIndex = 0;
	Out.TargetHoleX = 0.f;
	Out.TargetHoleY = 0.f;
	if (Root->HasField(TEXT("target_hole_index")))
	{
		Out.TargetHoleIndex = FMath::Max(0, static_cast<int32>(Root->GetNumberField(TEXT("target_hole_index"))));
	}
	if (Root->HasField(TEXT("target_hole_x"))) { Out.TargetHoleX = Root->GetNumberField(TEXT("target_hole_x")); }
	if (Root->HasField(TEXT("target_hole_y"))) { Out.TargetHoleY = Root->GetNumberField(TEXT("target_hole_y")); }
	if (Out.Holes.Num() > 0)
	{
		Out.Hole = Out.Holes[FMath::Clamp(Out.TargetHoleIndex, 0, Out.Holes.Num() - 1)];
	}
	else
	{
		const TSharedPtr<FJsonObject>* HoleObj = nullptr;
		if (Root->TryGetObjectField(TEXT("hole"), HoleObj) && HoleObj && (*HoleObj).IsValid())
		{
			const auto& H = *HoleObj;
			Out.Hole.X = H->GetNumberField(TEXT("x"));
			Out.Hole.Y = H->GetNumberField(TEXT("y"));
			Out.Hole.Radius = H->GetNumberField(TEXT("radius"));
			Out.Hole.bVisible = H->GetBoolField(TEXT("visible"));
			Out.Holes.Add(Out.Hole);
		}
	}

	const TSharedPtr<FJsonObject>* StatsObj = nullptr;
	if (Root->TryGetObjectField(TEXT("stats"), StatsObj) && StatsObj && (*StatsObj).IsValid())
	{
		const auto& S = *StatsObj;
		Out.Stats.PuttNumber = S->GetIntegerField(TEXT("putt_number"));
		Out.Stats.State = S->GetStringField(TEXT("state"));
		Out.Stats.LaunchSpeed = S->GetNumberField(TEXT("launch_speed"));
		Out.Stats.CurrentSpeed = S->GetNumberField(TEXT("current_speed"));
		Out.Stats.PeakSpeed = S->GetNumberField(TEXT("peak_speed"));
		Out.Stats.TotalDistance = S->GetNumberField(TEXT("total_distance"));
		Out.Stats.BreakDistance = S->GetNumberField(TEXT("break_distance"));
		Out.Stats.TimeInMotion = S->GetNumberField(TEXT("time_in_motion"));
		Out.Stats.StartPos = FVector2D(
			S->GetNumberField(TEXT("start_x")),
			S->GetNumberField(TEXT("start_y")));
		Out.Stats.PeakSpeedPos = FVector2D(
			S->GetNumberField(TEXT("peak_speed_x")),
			S->GetNumberField(TEXT("peak_speed_y")));
		Out.Stats.FinalPos = FVector2D(
			S->GetNumberField(TEXT("final_x")),
			S->GetNumberField(TEXT("final_y")));
	}

	Out.Balls.Reset();
	const TArray<TSharedPtr<FJsonValue>>* BallsArray = nullptr;
	if (Root->TryGetArrayField(TEXT("balls"), BallsArray) && BallsArray)
	{
		for (const TSharedPtr<FJsonValue>& Elem : *BallsArray)
		{
			const TSharedPtr<FJsonObject>* BO = nullptr;
			if (Elem.IsValid() && Elem->TryGetObject(BO) && BO && (*BO).IsValid())
			{
				const auto& B = *BO;
				FGolfBallInfo Bi;
				ParseTracked(B, Bi.Tracked);
				Bi.Username = B->GetStringField(TEXT("username"));
				if (B->HasField(TEXT("stable_id")))
				{
					Bi.StableId = static_cast<int32>(B->GetNumberField(TEXT("stable_id")));
				}
				if (B->HasField(TEXT("target_hole_index")))
				{
					Bi.TargetHoleIndex = static_cast<int32>(B->GetNumberField(TEXT("target_hole_index")));
				}
				if (B->HasField(TEXT("target_hole_x"))) { Bi.TargetHoleX = B->GetNumberField(TEXT("target_hole_x")); }
				if (B->HasField(TEXT("target_hole_y"))) { Bi.TargetHoleY = B->GetNumberField(TEXT("target_hole_y")); }
				const TSharedPtr<FJsonObject>* SObj = nullptr;
				if (B->TryGetObjectField(TEXT("stats"), SObj) && SObj && (*SObj).IsValid())
				{
					const auto& S = *SObj;
					Bi.Stats.PuttNumber = S->GetIntegerField(TEXT("putt_number"));
					Bi.Stats.State = S->GetStringField(TEXT("state"));
					Bi.Stats.LaunchSpeed = S->GetNumberField(TEXT("launch_speed"));
					Bi.Stats.CurrentSpeed = S->GetNumberField(TEXT("current_speed"));
					Bi.Stats.PeakSpeed = S->GetNumberField(TEXT("peak_speed"));
					Bi.Stats.TotalDistance = S->GetNumberField(TEXT("total_distance"));
					Bi.Stats.BreakDistance = S->GetNumberField(TEXT("break_distance"));
					Bi.Stats.TimeInMotion = S->GetNumberField(TEXT("time_in_motion"));
					Bi.Stats.StartPos = FVector2D(
						S->GetNumberField(TEXT("start_x")),
						S->GetNumberField(TEXT("start_y")));
					Bi.Stats.PeakSpeedPos = FVector2D(
						S->GetNumberField(TEXT("peak_speed_x")),
						S->GetNumberField(TEXT("peak_speed_y")));
					Bi.Stats.FinalPos = FVector2D(
						S->GetNumberField(TEXT("final_x")),
						S->GetNumberField(TEXT("final_y")));
				}
				Out.Balls.Add(Bi);
			}
		}
	}

	Out.BallPlacements.Reset();
	const TArray<TSharedPtr<FJsonValue>>* PlacementsArray = nullptr;
	if (Root->TryGetArrayField(TEXT("ball_placements"), PlacementsArray) && PlacementsArray)
	{
		for (const TSharedPtr<FJsonValue>& Elem : *PlacementsArray)
		{
			const TSharedPtr<FJsonObject>* PO = nullptr;
			if (Elem.IsValid() && Elem->TryGetObject(PO) && PO && (*PO).IsValid())
			{
				const auto& P = *PO;
				FGolfBallPlacement Pi;
				Pi.Username = P->GetStringField(TEXT("username"));
				Pi.StableId = static_cast<int32>(P->GetNumberField(TEXT("stable_id")));
				Pi.PixelX = P->GetNumberField(TEXT("pixel_x"));
				Pi.PixelY = P->GetNumberField(TEXT("pixel_y"));
				Pi.bWaitingPlacement = P->GetBoolField(TEXT("waiting"));
				bool bAfterPutt = false;
				P->TryGetBoolField(TEXT("after_putt"), bAfterPutt);
				Pi.bAfterPuttReturn = bAfterPutt;
				Out.BallPlacements.Add(Pi);
			}
		}
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel → World coordinate mapping
// ─────────────────────────────────────────────────────────────────────────────
FVector AUDPGolfReceiver::PixelToWorld(float PixelX, float PixelY) const
{
	const float NormX = (PixelBoundsMax.X > 0.f) ? FMath::Clamp(PixelX / PixelBoundsMax.X, 0.f, 1.f) : 0.f;
	const float NormY = (PixelBoundsMax.Y > 0.f) ? FMath::Clamp(PixelY / PixelBoundsMax.Y, 0.f, 1.f) : 0.f;

	return FVector(
		FMath::Lerp(WorldBoundsMin.X, WorldBoundsMax.X, NormX),
		FMath::Lerp(WorldBoundsMin.Y, WorldBoundsMax.Y, NormY),
		FMath::Lerp(WorldBoundsMin.Z, WorldBoundsMax.Z, 0.5f)
	);
}

bool AUDPGolfReceiver::GetPlacementWorldForStableId(int32 StableId, FVector& OutWorld)
{
	OutWorld = FVector::ZeroVector;
	if (StableId < 0)
	{
		return false;
	}
	for (const FGolfBallPlacement& P : BallPlacementsData)
	{
		if (P.StableId != StableId)
		{
			continue;
		}
		if (!P.bWaitingPlacement)
		{
			return false;
		}
		OutWorld = PixelToWorld(P.PixelX, P.PixelY);
		return true;
	}
	return false;
}

void AUDPGolfReceiver::GetWaitingPlacementMarkersWorld(TArray<FGolfWaitingPlacementWorld>& OutMarkers)
{
	OutMarkers.Reset();
	for (const FGolfBallPlacement& P : BallPlacementsData)
	{
		if (!P.bWaitingPlacement || P.StableId < 0)
		{
			continue;
		}
		FGolfWaitingPlacementWorld Row;
		Row.StableId = P.StableId;
		Row.WorldLocation = PixelToWorld(P.PixelX, P.PixelY);
		Row.Username = P.Username;
		Row.bAfterPuttReturn = P.bAfterPuttReturn;
		OutMarkers.Add(Row);
	}
}

AUDPGolfReceiver* AUDPGolfReceiver::GetUDPReceiverInWorld(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}
	return Cast<AUDPGolfReceiver>(
		UGameplayStatics::GetActorOfClass(WorldContextObject, AUDPGolfReceiver::StaticClass()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick — apply latest data to actors
// ─────────────────────────────────────────────────────────────────────────────
void AUDPGolfReceiver::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	FGolfUDPPayload Payload;
	bool bHasNew = false;

	{
		FScopeLock Lock(&DataLock);
		if (bNewDataAvailable)
		{
			Payload = LatestPayload;
			bHasNew = true;
			bNewDataAvailable = false;
		}
	}

	if (bHasNew)
	{
		LastUDPReceiveTime = FPlatformTime::Seconds();
		BallData = Payload.Ball;
		PutterData = Payload.Putter;
		PuttersData = Payload.Putters;
		HoleData = Payload.Hole;
		HolesData = Payload.Holes;
		BallsData = Payload.Balls;
		BallPlacementsData = Payload.BallPlacements;
		TargetHoleIndex = FMath::Clamp(Payload.TargetHoleIndex, 0, FMath::Max(0, Payload.Holes.Num() - 1));
		TargetHoleX = Payload.TargetHoleX;
		TargetHoleY = Payload.TargetHoleY;
		HoleAimBallIndex = Payload.HoleAimBallIndex;
		PuttStats = Payload.Stats;
		// Multi-ball: tracks are position-sorted; claimed stable_id may be Balls[1]+ while legacy used only [0].
		PrimaryUsername = FString();
		for (const FGolfBallInfo& Bi : Payload.Balls)
		{
			if (!Bi.Username.IsEmpty())
			{
				PrimaryUsername = Bi.Username;
				break;
			}
		}
		bPuttMade = Payload.bPuttMade;
		CachedPayloadForBlueprint = Payload;

		OnUDPDataReceived.Broadcast(Payload);

		if (bPuttMade && !bPreviousPuttMade)
		{
			OnPuttMade.Broadcast();
		}
		bPreviousPuttMade = bPuttMade;

		// Dynamically spawn/destroy hole actors to match detected hole count.
		// HoleActor = first hole; HoleActors[i] = hole i+1. Spawn copies of HoleActor when a second+ hole is detected.
		const int32 NeedCount = HolesData.Num();
		const int32 HaveCount = (IsValidActorRef(HoleActor) ? 1 : 0) + HoleActors.Num();
		const bool bHoleActorValid = IsValidActorRef(HoleActor);
		if (bHoleActorValid && NeedCount > 1)
		{
			UWorld* World = GetWorld();
			if (World && !IsTemplate())
			{
				// Spawn more when we need them
				while (HoleActors.Num() < NeedCount - 1)
				{
					const int32 HoleIdx = HoleActors.Num() + 1;
					if (!HolesData.IsValidIndex(HoleIdx)) break;
					FActorSpawnParameters Params;
					Params.Owner = this;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					FVector SpawnLoc = PixelToWorld(HolesData[HoleIdx].X, HolesData[HoleIdx].Y);
					SpawnLoc.Z = 0.f;
					AActor* Spawned = World->SpawnActor<AActor>(HoleActor->GetClass(), SpawnLoc, FRotator::ZeroRotator, Params);
					if (Spawned)
					{
						HoleActors.Add(Spawned);
					}
					else break;
				}
				// Destroy excess when fewer holes detected
				while (HoleActors.Num() > FMath::Max(0, NeedCount - 1))
				{
					AActor* ToDestroy = HoleActors.Pop();
					if (IsValid(ToDestroy))
					{
						ToDestroy->Destroy();
					}
				}
			}
		}
		else if (NeedCount <= 1 && HoleActors.Num() > 0)
		{
			for (AActor* A : HoleActors)
			{
				if (IsValid(A)) A->Destroy();
			}
			HoleActors.Empty();
		}

		// Spawn/destroy placement marker actors to match BallPlacementsData count
		if (IsValidActorRef(PlacementMarkerActor))
		{
			UWorld* World = GetWorld();
			if (World && !IsTemplate())
			{
				const int32 NeedMarkers = BallPlacementsData.Num();
				while (PlacementMarkerActorsSpawned.Num() < NeedMarkers)
				{
					FActorSpawnParameters Params;
					Params.Owner = this;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					AActor* Spawned = World->SpawnActor<AActor>(PlacementMarkerActor->GetClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
					if (Spawned)
					{
						Spawned->SetActorHiddenInGame(true);
						PlacementMarkerActorsSpawned.Add(Spawned);
					}
					else break;
				}
				while (PlacementMarkerActorsSpawned.Num() > FMath::Max(0, NeedMarkers))
				{
					AActor* ToDestroy = PlacementMarkerActorsSpawned.Pop();
					if (IsValid(ToDestroy)) ToDestroy->Destroy();
				}
			}
		}
	}

	// Position and show/hide placement markers each frame
	for (int32 i = 0; i < PlacementMarkerActorsSpawned.Num(); ++i)
	{
		if (!BallPlacementsData.IsValidIndex(i)) continue;
		AActor* Marker = PlacementMarkerActorsSpawned[i];
		if (!IsValid(Marker)) continue;
		const FGolfBallPlacement& P = BallPlacementsData[i];
		if (P.bWaitingPlacement && P.StableId >= 0)
		{
			EnsureUdpDrivenActorMovable(Marker);
			const FVector TargetLoc = PixelToWorld(P.PixelX, P.PixelY);
			if (bInterpolate)
			{
				Marker->SetActorLocation(
					FMath::VInterpTo(Marker->GetActorLocation(), TargetLoc, DeltaTime, InterpolationSpeed));
			}
			else
			{
				Marker->SetActorLocation(TargetLoc);
			}
			Marker->SetActorHiddenInGame(false);
		}
		else
		{
			Marker->SetActorHiddenInGame(true);
		}
	}
	auto MoveActor = [this, DeltaTime](AActor* Target, const FGolfTrackedObject& Data)
	{
		if (!Target || !Data.bVisible || !IsValidActorRef(Target) || Target->IsTemplate()) return;
		EnsureUdpDrivenActorMovable(Target);

		const FVector TargetLocation = PixelToWorld(Data.X, Data.Y);

		if (bInterpolate)
		{
			const FVector Current = Target->GetActorLocation();
			Target->SetActorLocation(
				FMath::VInterpTo(Current, TargetLocation, DeltaTime, InterpolationSpeed));
		}
		else
		{
			Target->SetActorLocation(TargetLocation);
		}
	};

	// Ball movement: use BallsData when available (multi-ball), else legacy BallData
	if (BallsData.Num() > 0)
	{
		// Spawn / trim extra ball actors (primary BallActor is always the first slot).
		// During putts the tracker may keep extra balls[] rows (stale tracks) while only some are visible —
		// sizing the pool by row count alone leaves ghost meshes. Prefer visible count when it is lower.
		const int32 BallsDataNum = BallsData.Num();
		int32 VisibleBalls = 0;
		for (const FGolfBallInfo& Bi : BallsData)
		{
			if (Bi.Tracked.bVisible)
			{
				++VisibleBalls;
			}
		}
		int32 NeedBalls = BallsDataNum;
		if (VisibleBalls > 0 && VisibleBalls < BallsDataNum)
		{
			NeedBalls = VisibleBalls;
		}
		const int32 MaxExtraBallActors = FMath::Max(0, NeedBalls - 1);
		if (IsValidActorRef(BallActor))
		{
			UWorld* World = GetWorld();
			if (World && !IsTemplate())
			{
				if (NeedBalls > 1)
				{
					while (BallActors.Num() < MaxExtraBallActors)
					{
						const int32 BallIdx = BallActors.Num() + 1;
						if (!BallsData.IsValidIndex(BallIdx)) break;
						FActorSpawnParameters Params;
						Params.Owner = this;
						Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
						FVector SpawnLoc = PixelToWorld(BallsData[BallIdx].Tracked.X, BallsData[BallIdx].Tracked.Y);
						SpawnLoc.Z = BallActor->GetActorLocation().Z;
						AActor* Spawned = World->SpawnActor<AActor>(BallActor->GetClass(), SpawnLoc, FRotator::ZeroRotator, Params);
						if (Spawned) BallActors.Add(Spawned);
						else break;
					}
				}
				// When NeedBalls is 1, MaxExtraBallActors is 0 — must still destroy spawns from prior multi-ball frames.
				while (BallActors.Num() > MaxExtraBallActors)
				{
					AActor* ToDestroy = BallActors.Pop();
					if (IsValid(ToDestroy)) ToDestroy->Destroy();
				}
			}
		}

		// Latch each visual slot to a tracker stable_id (balls[] are sorted by x,y; order swaps when balls cross).
		const int32 ExpectedSlots = (IsValidActorRef(BallActor) ? 1 : 0) + BallActors.Num();
		TArray<int32> SlotToBallsDataRow;
		if (ExpectedSlots > 0)
		{
			const int32 OldNum = BallActorLatchedStableIds.Num();
			if (OldNum != ExpectedSlots)
			{
				BallActorLatchedStableIds.SetNum(ExpectedSlots);
				for (int32 j = OldNum; j < ExpectedSlots; ++j)
				{
					BallActorLatchedStableIds[j] = BallsData.IsValidIndex(j) ? BallsData[j].StableId : -1;
				}
			}
		}

		// Map slot -> BallsData row: prefer stable_id; never fall back to slot index (causes cross-swaps). Use nearest world match for the rest.
		if (ExpectedSlots > 0 && IsValidActorRef(BallActor))
		{
			SlotToBallsDataRow.Init(INDEX_NONE, ExpectedSlots);
			TSet<int32> ClaimedRows;
			// Bind by stable_id even when the track is not visible (brief occlusion / motion blur during putts).
			// Requiring bVisible here forced nearest-visible fallback and stole another ball's row → wrong username on mesh.
			for (int32 Slot = 0; Slot < ExpectedSlots; ++Slot)
			{
				if (!BallActorLatchedStableIds.IsValidIndex(Slot)) continue;
				const int32 Sid = BallActorLatchedStableIds[Slot];
				if (Sid < 0) continue;
				const int32 Di = FindRowByStableIdOnly(BallsData, Sid);
				if (Di != INDEX_NONE && !ClaimedRows.Contains(Di))
				{
					SlotToBallsDataRow[Slot] = Di;
					ClaimedRows.Add(Di);
				}
			}
			for (int32 Slot = 0; Slot < ExpectedSlots; ++Slot)
			{
				if (SlotToBallsDataRow[Slot] != INDEX_NONE) continue;
				AActor* Act = (Slot == 0) ? BallActor : BallActors[Slot - 1];
				if (!IsValidActorRef(Act)) continue;
				const FVector W = Act->GetActorLocation();
				float BestD2 = 1.e20f;
				int32 Best = INDEX_NONE;
				for (int32 i = 0; i < BallsData.Num(); ++i)
				{
					if (ClaimedRows.Contains(i) || !BallsData[i].Tracked.bVisible) continue;
					const FVector P = PixelToWorld(BallsData[i].Tracked.X, BallsData[i].Tracked.Y);
					const float D2 = FVector::DistSquared(W, P);
					if (D2 < BestD2)
					{
						BestD2 = D2;
						Best = i;
					}
				}
				if (Best != INDEX_NONE)
				{
					SlotToBallsDataRow[Slot] = Best;
					ClaimedRows.Add(Best);
					if (BallActorLatchedStableIds.IsValidIndex(Slot) && BallActorLatchedStableIds[Slot] < 0 && BallsData[Best].StableId >= 0)
					{
						BallActorLatchedStableIds[Slot] = BallsData[Best].StableId;
					}
				}
			}
		}

		if (IsValidActorRef(BallActor) && SlotToBallsDataRow.IsValidIndex(0) && SlotToBallsDataRow[0] != INDEX_NONE)
		{
			const int32 Di = SlotToBallsDataRow[0];
			if (BallsData[Di].Tracked.bVisible)
			{
				MoveActor(BallActor, BallsData[Di].Tracked);
			}
		}
		for (int32 i = 0; i < BallActors.Num(); ++i)
		{
			const int32 Slot = i + 1;
			if (!SlotToBallsDataRow.IsValidIndex(Slot) || SlotToBallsDataRow[Slot] == INDEX_NONE) continue;
			const int32 Di = SlotToBallsDataRow[Slot];
			if (!BallsData[Di].Tracked.bVisible) continue;
			MoveActor(BallActors[i], BallsData[Di].Tracked);
		}
		if (bAutoApplyBallLabelText)
		{
			auto ApplyBallLabel = [this, &SlotToBallsDataRow](AActor* A, int32 SlotIndex)
			{
				if (!IsValidActorRef(A) || A->IsTemplate()) return;
				int32 SortedIdx = SlotIndex;
				if (SlotToBallsDataRow.IsValidIndex(SlotIndex) && SlotToBallsDataRow[SlotIndex] != INDEX_NONE)
				{
					SortedIdx = SlotToBallsDataRow[SlotIndex];
				}
				const FText Label = GetBallLabelForSortedIndex(SortedIdx);
				TArray<UTextRenderComponent*> Texts;
				A->GetComponents<UTextRenderComponent>(Texts);
				if (Texts.Num() > 0 && Texts[0])
				{
					Texts[0]->SetText(Label);
				}
			};
			ApplyBallLabel(BallActor, 0);
			for (int32 i = 0; i < BallActors.Num(); ++i)
			{
				ApplyBallLabel(BallActors[i], i + 1);
			}
		}

		if (!bAutoSpawnAimLines && AimLineActorsSpawned.Num() > 0)
		{
			for (ABallToHoleLineActor* L : AimLineActorsSpawned)
			{
				if (IsValid(L) && !L->IsTemplate())
				{
					L->Destroy();
				}
			}
			AimLineActorsSpawned.Empty();
		}
		else if (bAutoSpawnAimLines && IsValidActorRef(AimLineActor) && !AimLineActor->IsTemplate())
		{
			AimLineActor->GolfReceiver = this;
			UWorld* WorldAim = GetWorld();
			if (WorldAim && !IsTemplate())
			{
				const int32 NeedLines = BallsData.Num();
				while (AimLineActorsSpawned.Num() < FMath::Max(0, NeedLines - 1))
				{
					const int32 LineIdx = AimLineActorsSpawned.Num() + 1;
					if (!BallsData.IsValidIndex(LineIdx)) break;
					FActorSpawnParameters AimParams;
					AimParams.Owner = this;
					AimParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					// Defer BeginPlay until after we set bEnableLine / receiver / sorted index (otherwise spline never builds).
					AimParams.bDeferConstruction = true;
					const FVector SpawnLoc = AimLineActor->GetActorLocation();
					const FRotator SpawnRot = AimLineActor->GetActorRotation();
					ABallToHoleLineActor* SpawnedLine = WorldAim->SpawnActor<ABallToHoleLineActor>(
						AimLineActor->GetClass(), SpawnLoc, SpawnRot, AimParams);
					if (SpawnedLine)
					{
						SpawnedLine->GolfReceiver = this;
						SpawnedLine->AimLineBallSortedIndex = LineIdx;
						SpawnedLine->bEnableLine = AimLineActor->bEnableLine;
						SpawnedLine->bShowDistanceInches = AimLineActor->bShowDistanceInches;
						const FTransform SpawnXform(SpawnRot, SpawnLoc);
						SpawnedLine->FinishSpawning(SpawnXform);
						AimLineActorsSpawned.Add(SpawnedLine);
					}
					else
					{
						break;
					}
				}
				while (AimLineActorsSpawned.Num() > FMath::Max(0, NeedLines - 1))
				{
					ABallToHoleLineActor* ToDestroy = AimLineActorsSpawned.Pop();
					if (IsValid(ToDestroy) && !ToDestroy->IsTemplate())
					{
						ToDestroy->Destroy();
					}
				}
			}
			if (SlotToBallsDataRow.IsValidIndex(0) && SlotToBallsDataRow[0] != INDEX_NONE)
			{
				AimLineActor->AimLineBallSortedIndex = SlotToBallsDataRow[0];
			}
			else
			{
				AimLineActor->AimLineBallSortedIndex = 0;
			}
			for (int32 Li = 0; Li < AimLineActorsSpawned.Num(); ++Li)
			{
				if (!AimLineActorsSpawned[Li]) continue;
				const int32 Slot = Li + 1;
				if (SlotToBallsDataRow.IsValidIndex(Slot) && SlotToBallsDataRow[Slot] != INDEX_NONE)
				{
					AimLineActorsSpawned[Li]->AimLineBallSortedIndex = SlotToBallsDataRow[Slot];
				}
				else
				{
					AimLineActorsSpawned[Li]->AimLineBallSortedIndex = Slot;
				}
			}
		}
	}
	else
	{
		if (AimLineActorsSpawned.Num() > 0)
		{
			for (ABallToHoleLineActor* L : AimLineActorsSpawned)
			{
				if (IsValid(L) && !L->IsTemplate())
				{
					L->Destroy();
				}
			}
			AimLineActorsSpawned.Empty();
		}
		MoveActor(BallActor, BallData);
		if (bAutoApplyBallLabelText && IsValidActorRef(BallActor) && !BallActor->IsTemplate())
		{
			TArray<UTextRenderComponent*> Texts;
			BallActor->GetComponents<UTextRenderComponent>(Texts);
			if (Texts.Num() > 0 && Texts[0])
			{
				Texts[0]->SetText(GetPrimaryBallLabel());
			}
		}
	}
	MoveActor(PutterActor, PutterData);

	auto MoveCrosshair = [this, DeltaTime](AActor* Target, const FGolfTrackedObject& Data)
	{
		if (!Target || !IsValidActorRef(Target) || Target->IsTemplate()) return;
		EnsureUdpDrivenActorMovable(Target);
		if (!Data.bVisible)
		{
			Target->SetActorHiddenInGame(true);
			return;
		}
		Target->SetActorHiddenInGame(false);
		const FVector TargetLocation = PixelToWorld(Data.X, Data.Y);
		if (bInterpolate)
		{
			const FVector Current = Target->GetActorLocation();
			Target->SetActorLocation(
				FMath::VInterpTo(Current, TargetLocation, DeltaTime, InterpolationSpeed));
		}
		else
		{
			Target->SetActorLocation(TargetLocation);
		}
	};

	// Field omitted from JSON (Contour never connected): show crosshair. Explicit -1: hide.
	const bool bHoleAimLegacy = (HoleAimBallIndex == 2147483647);
	if (!bHoleAimLegacy && HoleAimBallIndex < 0)
	{
		for (AActor* C : PutterCrosshairActorsSpawned)
		{
			if (IsValid(C) && !C->IsTemplate())
			{
				C->Destroy();
			}
		}
		PutterCrosshairActorsSpawned.Empty();
		if (IsValidActorRef(PutterCrosshairTemplate) && !PutterCrosshairTemplate->IsTemplate())
		{
			PutterCrosshairTemplate->SetActorHiddenInGame(true);
		}
	}
	else
	{
		// Multi-putter crosshairs (spawn from template; same pixel mapping as PutterActor)
		if (!bAutoSpawnPutterCrosshairs || !IsValidActorRef(PutterCrosshairTemplate) || PutterCrosshairTemplate->IsTemplate())
		{
			for (AActor* C : PutterCrosshairActorsSpawned)
			{
				if (IsValid(C) && !C->IsTemplate())
				{
					C->Destroy();
				}
			}
			PutterCrosshairActorsSpawned.Empty();
			// Placed crosshair as template with auto-spawn off: drive the level instance (no duplicate spawns).
			if (!bAutoSpawnPutterCrosshairs && IsValidActorRef(PutterCrosshairTemplate) && !PutterCrosshairTemplate->IsTemplate())
			{
				const FGolfTrackedObject& PcData = (PuttersData.Num() > 0) ? PuttersData[0] : PutterData;
				MoveCrosshair(PutterCrosshairTemplate, PcData);
			}
		}
		else
		{
			UWorld* WorldP = GetWorld();
			const int32 NeedPutters = FMath::Clamp(PuttersData.Num(), 0, 8);
			if (WorldP && !IsTemplate())
			{
				while (PutterCrosshairActorsSpawned.Num() < NeedPutters)
				{
					const int32 Idx = PutterCrosshairActorsSpawned.Num();
					if (!PuttersData.IsValidIndex(Idx)) break;
					FActorSpawnParameters PcParams;
					PcParams.Owner = this;
					PcParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					FVector SpawnLoc = PixelToWorld(PuttersData[Idx].X, PuttersData[Idx].Y);
					SpawnLoc.Z = PutterCrosshairTemplate->GetActorLocation().Z;
					AActor* SpawnedP = WorldP->SpawnActor<AActor>(
						PutterCrosshairTemplate->GetClass(), SpawnLoc, PutterCrosshairTemplate->GetActorRotation(), PcParams);
					if (SpawnedP)
					{
						// SpawnActor uses the class CDO — match the placed template's scale (editor instance ≠ CDO).
						SpawnedP->SetActorScale3D(PutterCrosshairTemplate->GetActorScale3D());
						PutterCrosshairActorsSpawned.Add(SpawnedP);
					}
					else
					{
						break;
					}
				}
				while (PutterCrosshairActorsSpawned.Num() > NeedPutters)
				{
					AActor* ToDestroy = PutterCrosshairActorsSpawned.Pop();
					if (IsValid(ToDestroy) && !ToDestroy->IsTemplate())
					{
						ToDestroy->Destroy();
					}
				}
			}
			for (int32 i = 0; i < PutterCrosshairActorsSpawned.Num(); ++i)
			{
				if (!PuttersData.IsValidIndex(i)) continue;
				MoveCrosshair(PutterCrosshairActorsSpawned[i], PuttersData[i]);
			}
		}
	}

	// Move single HoleActor (backward compat) from first hole
	if (IsValidActorRef(HoleActor) && HolesData.Num() > 0 && HolesData[0].bVisible)
	{
		EnsureUdpDrivenActorMovable(HoleActor);
		FVector TargetLocation = PixelToWorld(HolesData[0].X, HolesData[0].Y);
		TargetLocation.Z = 0.f;
		if (bInterpolate)
		{
			const FVector Current = HoleActor->GetActorLocation();
			HoleActor->SetActorLocation(
				FMath::VInterpTo(Current, TargetLocation, DeltaTime, InterpolationSpeed));
		}
		else
		{
			HoleActor->SetActorLocation(TargetLocation);
		}
	}

	// Move multiple hole actors from HolesData (HoleActors[i] = hole i+1; hole 0 uses HoleActor above)
	for (int32 i = 0; i < HoleActors.Num(); ++i)
	{
		const int32 DataIdx = i + 1;
		if (!HolesData.IsValidIndex(DataIdx)) continue;
		AActor* Target = HoleActors[i];
		const FGolfHoleInfo& HoleInfo = HolesData[DataIdx];
		if (!Target || !HoleInfo.bVisible || !IsValidActorRef(Target) || Target->IsTemplate()) continue;

		EnsureUdpDrivenActorMovable(Target);
		FVector TargetLocation = PixelToWorld(HoleInfo.X, HoleInfo.Y);
		TargetLocation.Z = 0.f;
		if (bInterpolate)
		{
			const FVector Current = Target->GetActorLocation();
			Target->SetActorLocation(
				FMath::VInterpTo(Current, TargetLocation, DeltaTime, InterpolationSpeed));
		}
		else
		{
			Target->SetActorLocation(TargetLocation);
		}
	}

}

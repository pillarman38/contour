#include "UDPGolfReceiver.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Serialization/Archive.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "IPAddress.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

DEFINE_LOG_CATEGORY_STATIC(LogGolfUDP, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
AUDPGolfReceiver::AUDPGolfReceiver()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
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

// ─────────────────────────────────────────────────────────────────────────────
void AUDPGolfReceiver::PostLoad()
{
	Super::PostLoad();
	// Always clear invalid actor refs (including CDO) to avoid engine "Cast of Default__Object to Actor failed"
	if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
	if (!IsValidActorRef(PutterActor)) { PutterActor = nullptr; }
	if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
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
	// Clear bad refs before saving so they are never persisted
	if (!IsTemplate())
	{
		if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
		if (!IsValidActorRef(PutterActor)) { PutterActor = nullptr; }
		if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
		for (int32 i = HoleActors.Num() - 1; i >= 0; --i)
		{
			if (!IsValidActorRef(HoleActors[i])) { HoleActors.RemoveAt(i); }
		}
	}
}

void AUDPGolfReceiver::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	// Validate actor refs on the duplicated copy (PIE, copy/paste) to avoid "Cast of Default__Object to Actor failed"
	if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
	if (!IsValidActorRef(PutterActor)) { PutterActor = nullptr; }
	if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
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
	if (!IsValidActorRef(BallActor)) { BallActor = nullptr; }
	if (!IsValidActorRef(PutterActor)) { PutterActor = nullptr; }
	if (!IsValidActorRef(HoleActor)) { HoleActor = nullptr; }
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
		return FText::FromString(FString::Printf(TEXT("Putt #%d"), PuttNumber));
	}
	return FText::FromString(FString::Printf(TEXT("%s - Putt #%d"), *Username, PuttNumber));
}

FText AUDPGolfReceiver::GetPrimaryBallLabel() const
{
	return BallLabelToText(PrimaryUsername, PuttStats.PuttNumber);
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
	Buffer.SetNumUninitialized(2048);

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
		HoleData = Payload.Hole;
		HolesData = Payload.Holes;
		BallsData = Payload.Balls;
		TargetHoleIndex = FMath::Clamp(Payload.TargetHoleIndex, 0, FMath::Max(0, Payload.Holes.Num() - 1));
		TargetHoleX = Payload.TargetHoleX;
		TargetHoleY = Payload.TargetHoleY;
		PuttStats = Payload.Stats;
		PrimaryUsername = (Payload.Balls.Num() > 0) ? Payload.Balls[0].Username : FString();
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
	}

	auto MoveActor = [this, DeltaTime](AActor* Target, const FGolfTrackedObject& Data)
	{
		if (!Target || !Data.bVisible || !IsValidActorRef(Target) || Target->IsTemplate()) return;

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
		MoveActor(BallActor, BallsData[0].Tracked);
		// Spawn additional ball actors when BallsData.Num() > 1
		const int32 NeedBalls = BallsData.Num();
		const int32 HaveBalls = (IsValidActorRef(BallActor) ? 1 : 0) + BallActors.Num();
		if (IsValidActorRef(BallActor) && NeedBalls > 1)
		{
			UWorld* World = GetWorld();
			if (World && !IsTemplate())
			{
				while (BallActors.Num() < NeedBalls - 1)
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
				while (BallActors.Num() > FMath::Max(0, NeedBalls - 1))
				{
					AActor* ToDestroy = BallActors.Pop();
					if (IsValid(ToDestroy)) ToDestroy->Destroy();
				}
			}
		}
		// Move additional ball actors
		for (int32 i = 0; i < BallActors.Num(); ++i)
		{
			const int32 DataIdx = i + 1;
			if (!BallsData.IsValidIndex(DataIdx)) continue;
			MoveActor(BallActors[i], BallsData[DataIdx].Tracked);
		}
	}
	else
	{
		MoveActor(BallActor, BallData);
	}
	MoveActor(PutterActor, PutterData);

	// Move single HoleActor (backward compat) from first hole
	if (IsValidActorRef(HoleActor) && HolesData.Num() > 0 && HolesData[0].bVisible)
	{
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

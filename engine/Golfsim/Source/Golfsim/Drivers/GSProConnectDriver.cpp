#include "Drivers/GSProConnectDriver.h"

#include "Events/EventTypes.h"

#include "Common/TcpSocketBuilder.h"     // FTcpSocketBuilder, FIPv4Address (Networking)
#include "Sockets.h"                      // FSocket
#include "SocketSubsystem.h"              // ISocketSubsystem, FInternetAddr, PLATFORM_SOCKETSUBSYSTEM
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"
#include "Containers/Queue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ConfigCacheIni.h"          // GConfig

#include <atomic>

namespace
{
	constexpr double GMphToMps = 0.44704;   // miles/hour -> meters/second (GSPro BallData.Speed is mph)

	// Our display club name -> GSPro's club code (DR / W3 / I7 / H4 / PW / GW / SW / LW / PT). Used only
	// for the optional {Code:201} player push; an unmapped name returns empty (no Club field sent).
	FString DisplayClubToGSPro(const FString& Display)
	{
		const FString D = Display.TrimStartAndEnd();
		if (D.IsEmpty())                                       return FString();
		if (D.Equals(TEXT("Driver"), ESearchCase::IgnoreCase)) return TEXT("DR");
		if (D.Equals(TEXT("Putter"), ESearchCase::IgnoreCase)) return TEXT("PT");
		if (D.Equals(TEXT("Pitching Wedge"), ESearchCase::IgnoreCase) || D.Equals(TEXT("PW"), ESearchCase::IgnoreCase)) return TEXT("PW");
		if (D.Equals(TEXT("Gap Wedge"),      ESearchCase::IgnoreCase) || D.Equals(TEXT("GW"), ESearchCase::IgnoreCase)) return TEXT("GW");
		if (D.Equals(TEXT("Sand Wedge"),     ESearchCase::IgnoreCase) || D.Equals(TEXT("SW"), ESearchCase::IgnoreCase)) return TEXT("SW");
		if (D.Equals(TEXT("Lob Wedge"),      ESearchCase::IgnoreCase) || D.Equals(TEXT("LW"), ESearchCase::IgnoreCase)) return TEXT("LW");

		// "<n>-Wood" / "<n>-Iron" / "<n>-Hybrid" -> W<n> / I<n> / H<n>.
		TArray<FString> Parts;
		D.ParseIntoArray(Parts, TEXT("-"));
		if (Parts.Num() == 2)
		{
			const FString& Num = Parts[0];
			const FString Type = Parts[1].ToLower();
			if (Type == TEXT("wood"))   return TEXT("W") + Num;
			if (Type == TEXT("iron"))   return TEXT("I") + Num;
			if (Type == TEXT("hybrid")) return TEXT("H") + Num;
		}
		return FString();   // unknown -> no Club in the push (first cut; extend as the bag grows)
	}

	// A connection-status change marshaled from the socket thread to the game thread.
	struct FGSProConnectStatus
	{
		bool bConnected = false;
		FString Detail;
	};
}

// ---------------------------------------------------------------------------------------------------
// Listener thread. Owns all blocking socket I/O. Parsed shots + status changes cross to the game
// thread over SPSC queues (drained by the driver's FTSTicker); {Code:200} acks + the {Code:201}
// player push are written here under ClientCS.
// ---------------------------------------------------------------------------------------------------
class FGSProConnectListener : public FRunnable
{
public:
	explicit FGSProConnectListener(int32 InPort) : Port(InPort) {}

	virtual ~FGSProConnectListener() override
	{
		bStop = true;
		if (Thread)
		{
			Thread->WaitForCompletion();   // Run() has returned: it closed both sockets
			delete Thread;
			Thread = nullptr;
		}
		// Safety net in case Run() never started.
		CloseClient(false);
		if (ListenSocket && SocketSub)
		{
			ListenSocket->Close();
			SocketSub->DestroySocket(ListenSocket);
			ListenSocket = nullptr;
		}
	}

	// Bind + listen, then spawn the thread. Returns false if the socket subsystem or bind fails.
	bool Start()
	{
		SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!SocketSub)
		{
			return false;
		}
		ListenSocket = FTcpSocketBuilder(TEXT("GSProConnectListen"))
			.AsReusable()
			.BoundToAddress(FIPv4Address(127, 0, 0, 1))
			.BoundToPort(Port)
			.Listening(8)
			.Build();
		if (!ListenSocket)
		{
			return false;   // port in use (another GSPro/connector listener?) or bind denied
		}
		ListenSocket->SetNonBlocking(true);
		Thread = FRunnableThread::Create(this, TEXT("GSProConnectListener"), 0, TPri_BelowNormal);
		return Thread != nullptr;
	}

	virtual void Stop() override { bStop = true; }

	virtual uint32 Run() override
	{
		while (!bStop)
		{
			// 1. Accept a pending connection (last-wins: a new connector replaces the old client).
			bool bPending = false;
			if (ListenSocket->HasPendingConnection(bPending) && bPending)
			{
				AcceptNewClient();
			}

			// 2. Service the current client, if any.
			FSocket* C = nullptr;
			{
				FScopeLock Lock(&ClientCS);
				C = ClientSocket;
			}
			if (!C)
			{
				// Idle: block briefly for an incoming connection so we don't spin the core.
				ListenSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(100));
				continue;
			}

			if (C->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(50)))
			{
				uint8 Buf[4096];
				int32 Read = 0;
				bool bRecvOk = false;
				{
					FScopeLock Lock(&ClientCS);
					if (ClientSocket == C)
					{
						bRecvOk = ClientSocket->Recv(Buf, sizeof(Buf), Read);
					}
				}
				if (!bRecvOk || Read <= 0)
				{
					CloseClient(/*bEnqueueStatus=*/true);   // graceful close or error
					continue;
				}
				const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Buf), Read);
				RecvBuffer.AppendChars(Conv.Get(), Conv.Length());
				ProcessBuffer();
			}
		}

		// Teardown on the socket thread (single owner of socket destruction).
		CloseClient(false);
		if (ListenSocket)
		{
			ListenSocket->Close();
			SocketSub->DestroySocket(ListenSocket);
			ListenSocket = nullptr;
		}
		return 0;
	}

	// --- Game-thread accessors (drained by the driver's ticker) -----------------------------------
	bool PopShot(FShotTakenEvent& Out)    { return ShotQueue.Dequeue(Out); }
	bool PopStatus(FGSProConnectStatus& Out) { return StatusQueue.Dequeue(Out); }

	// Called from the game thread (SetSelectedClub): update the player + push a fresh {Code:201}.
	void SetClub(const FString& Code)
	{
		{
			FScopeLock Lock(&ClientCS);
			ClubCode = Code;
		}
		SendPlayer();
	}

private:
	void AcceptNewClient()
	{
		const TSharedRef<FInternetAddr> Remote = SocketSub->CreateInternetAddr();
		FSocket* New = ListenSocket->Accept(*Remote, TEXT("GSProConnectClient"));
		if (!New)
		{
			return;
		}
		New->SetNonBlocking(true);
		{
			FScopeLock Lock(&ClientCS);
			if (ClientSocket)
			{
				ClientSocket->Close();
				SocketSub->DestroySocket(ClientSocket);
			}
			ClientSocket = New;
			RecvBuffer.Empty();
		}
		StatusQueue.Enqueue({ true, FString::Printf(TEXT("GSPro Connect: client connected (%s)"), *Remote->ToString(true)) });
		SendPlayer();   // GSPro pushes player info on connect so the connector knows handedness/club
	}

	void CloseClient(bool bEnqueueStatus)
	{
		{
			FScopeLock Lock(&ClientCS);
			if (ClientSocket)
			{
				ClientSocket->Close();
				SocketSub->DestroySocket(ClientSocket);
				ClientSocket = nullptr;
			}
			RecvBuffer.Empty();
		}
		if (bEnqueueStatus)
		{
			StatusQueue.Enqueue({ false, TEXT("GSPro Connect: client disconnected (listening)") });
		}
	}

	// Split RecvBuffer on '\n'; ack + parse each complete line. Remainder stays buffered.
	void ProcessBuffer()
	{
		int32 NewlineIdx = INDEX_NONE;
		while (RecvBuffer.FindChar(TCHAR('\n'), NewlineIdx))
		{
			FString Line = RecvBuffer.Left(NewlineIdx);
			RecvBuffer.RightChopInline(NewlineIdx + 1, EAllowShrinking::No);
			Line.TrimStartAndEndInline();   // strips a trailing '\r' + whitespace
			if (!Line.IsEmpty())
			{
				HandleLine(Line);
			}
		}
	}

	void HandleLine(const FString& Line)
	{
		// Ack every message the connector sends -- it treats {Code:200} as "the server got it".
		SendRaw(TEXT("{\"Code\":200}\n"));

		FShotTakenEvent Shot;
		bool bSpinEstimated = false;
		if (UGSProConnectDriver::ParseShot(Line, Shot, bSpinEstimated))
		{
			UE_LOG(LogTemp, Display,
				TEXT("golfsim GSProConnect: shot %.1f mph launch %.1f spin %.0f%s -> shot.taken"),
				Shot.BallSpeedMps / GMphToMps, Shot.LaunchAngleDeg, Shot.BackspinRpm,
				bSpinEstimated ? TEXT(" (est)") : TEXT(""));
			ShotQueue.Enqueue(Shot);
		}
		// Heartbeats / non-shot messages parse to false: acked above, nothing published.
	}

	void SendPlayer()
	{
		FString Line;
		{
			FScopeLock Lock(&ClientCS);
			Line = ClubCode.IsEmpty()
				? FString::Printf(TEXT("{\"Code\":201,\"Message\":\"GolfForge Player\",\"Player\":{\"Handed\":\"%s\"}}\n"), *Handed)
				: FString::Printf(TEXT("{\"Code\":201,\"Message\":\"GolfForge Player\",\"Player\":{\"Handed\":\"%s\",\"Club\":\"%s\"}}\n"), *Handed, *ClubCode);
		}
		SendRaw(Line);
	}

	void SendRaw(const FString& Line)
	{
		FScopeLock Lock(&ClientCS);
		if (!ClientSocket)
		{
			return;
		}
		const FTCHARToUTF8 Conv(*Line);
		int32 Sent = 0;
		ClientSocket->Send(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length(), Sent);
	}

	int32 Port = 921;
	ISocketSubsystem* SocketSub = nullptr;
	FSocket* ListenSocket = nullptr;
	FRunnableThread* Thread = nullptr;

	FCriticalSection ClientCS;          // guards ClientSocket + Handed/ClubCode (accessed by both threads)
	FSocket* ClientSocket = nullptr;
	FString Handed = TEXT("RH");        // default handedness; SetClub updates the club code
	FString ClubCode;

	FString RecvBuffer;                 // socket-thread only
	std::atomic<bool> bStop{ false };

	TQueue<FShotTakenEvent, EQueueMode::Spsc> ShotQueue;        // socket thread -> game thread
	TQueue<FGSProConnectStatus, EQueueMode::Spsc> StatusQueue;  // socket thread -> game thread
};

// ---------------------------------------------------------------------------------------------------
// UGSProConnectDriver
// ---------------------------------------------------------------------------------------------------

// ~FGSProConnectListener stops + joins the thread and closes the sockets. Defined here where the
// listener type is complete (the member is a raw owned ptr; see the header note).
UGSProConnectDriver::~UGSProConnectDriver()
{
	if (DrainHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DrainHandle);
		DrainHandle.Reset();
	}
	if (Listener)
	{
		Listener->Stop();
		delete Listener;
		Listener = nullptr;
	}
}

void UGSProConnectDriver::Connect()
{
	if (Listener)
	{
		return;   // already listening
	}

	int32 Port = 921;
	GConfig->GetInt(TEXT("LaunchMonitor.GSProConnect"), TEXT("Port"), Port, GGameIni);

	Listener = new FGSProConnectListener(Port);
	if (!Listener->Start())
	{
		delete Listener;
		Listener = nullptr;
		UE_LOG(LogTemp, Warning, TEXT("golfsim GSProConnect: failed to bind 127.0.0.1:%d (in use?)"), Port);
		SetStatus(false, FString::Printf(TEXT("GSPro Connect: bind failed on %d"), Port));
		return;
	}

	// Drain the cross-thread queues on the game thread for as long as we're listening.
	DrainHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UGSProConnectDriver::DrainQueues), 0.0f);

	UE_LOG(LogTemp, Display, TEXT("golfsim GSProConnect: listening on 127.0.0.1:%d"), Port);
	SetStatus(false, FString::Printf(TEXT("GSPro Connect: listening on %d"), Port));   // Off until a client connects
}

void UGSProConnectDriver::Disconnect()
{
	if (DrainHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DrainHandle);
		DrainHandle.Reset();
	}
	if (Listener)
	{
		Listener->Stop();
		delete Listener;   // ~FGSProConnectListener joins the thread + closes sockets
		Listener = nullptr;
	}
	SetStatus(false, TEXT("GSPro Connect: stopped"));
}

void UGSProConnectDriver::InjectTestMessage(const FString& Payload)
{
	// Console/test path: run a GSPro-shaped payload straight through parse->publish with no socket.
	FShotTakenEvent Shot;
	bool bSpinEstimated = false;
	if (ParseShot(Payload, Shot, bSpinEstimated))
	{
		UE_LOG(LogTemp, Display,
			TEXT("golfsim GSProConnect: injected shot %.1f mph%s -> shot.taken"),
			Shot.BallSpeedMps / GMphToMps, bSpinEstimated ? TEXT(" (est)") : TEXT(""));
		PublishShot(Shot);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("golfsim GSProConnect: injected payload did not parse as a shot"));
	}
}

void UGSProConnectDriver::SetSelectedClub(const FString& ClubDisplayName)
{
	if (!Listener)
	{
		return;   // not listening; nothing to push to
	}
	const FString Code = DisplayClubToGSPro(ClubDisplayName);
	if (Code.IsEmpty())
	{
		return;
	}
	Listener->SetClub(Code);   // updates player state + pushes {Code:201} if a client is connected
}

bool UGSProConnectDriver::DrainQueues(float /*DeltaTime*/)
{
	if (!Listener)
	{
		return true;
	}
	FShotTakenEvent Shot;
	while (Listener->PopShot(Shot))
	{
		PublishShot(Shot);   // synchronous EventBus dispatch on the game thread
	}
	FGSProConnectStatus St;
	while (Listener->PopStatus(St))
	{
		SetStatus(St.bConnected, St.Detail);
	}
	return true;   // keep ticking while listening (removed in Disconnect)
}

// ---------------------------------------------------------------------------------------------------
// Parsing (pure + defensive -- unit-tested headless; no socket/world)
// ---------------------------------------------------------------------------------------------------

bool UGSProConnectDriver::ParseShot(const FString& Json, FShotTakenEvent& Out, bool& bOutSpinEstimated)
{
	bOutSpinEstimated = false;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	return ParseShotObject(Root, Out, bOutSpinEstimated);
}

bool UGSProConnectDriver::ParseShotObject(const TSharedPtr<FJsonObject>& Root, FShotTakenEvent& Out,
	bool& bOutSpinEstimated)
{
	bOutSpinEstimated = false;
	if (!Root.IsValid())
	{
		return false;
	}

	// Heartbeats / "no ball data this message" are valid GSPro traffic but not shots -- ack only.
	const TSharedPtr<FJsonObject>* Opts = nullptr;
	if (Root->TryGetObjectField(TEXT("ShotDataOptions"), Opts) && Opts && Opts->IsValid())
	{
		bool bHeartbeat = false;
		(*Opts)->TryGetBoolField(TEXT("IsHeartBeat"), bHeartbeat);
		bool bContainsBall = true;   // absent -> assume a shot (lenient)
		(*Opts)->TryGetBoolField(TEXT("ContainsBallData"), bContainsBall);
		if (bHeartbeat || !bContainsBall)
		{
			return false;
		}
	}

	const TSharedPtr<FJsonObject>* BallPtr = nullptr;
	if (!Root->TryGetObjectField(TEXT("BallData"), BallPtr) || !BallPtr || !BallPtr->IsValid())
	{
		return false;
	}
	const FJsonObject* Ball = BallPtr->Get();

	// Ball speed (mph, always -- GSPro's Units field governs distance, not speed) is required.
	double SpeedMph = 0.0;
	if (!Ball->TryGetNumberField(TEXT("Speed"), SpeedMph) || SpeedMph <= 0.0)
	{
		return false;
	}

	Out = FShotTakenEvent();
	Out.Source = TEXT("gsproconnect");
	Out.Lie = TEXT("tee");
	Out.BallSpeedMps = SpeedMph * GMphToMps;

	double VLA = 0.0;
	Ball->TryGetNumberField(TEXT("VLA"), VLA);   // vertical launch angle
	Out.LaunchAngleDeg = VLA;

	double HLA = 0.0;
	Ball->TryGetNumberField(TEXT("HLA"), HLA);   // horizontal launch angle, + = right
	Out.AzimuthDeg = HLA;

	// Spin: prefer TotalSpin+SpinAxis (decompose, like OpenFlight), then measured BackSpin[/SideSpin],
	// else estimate from launch angle. SpinAxis/SideSpin sign: + = fade/right (our SidespinRpm convention).
	double TotalSpin = 0.0;
	const bool bHasTotal = Ball->TryGetNumberField(TEXT("TotalSpin"), TotalSpin) && TotalSpin > 0.0;
	double BackSpin = 0.0;
	const bool bHasBack = Ball->TryGetNumberField(TEXT("BackSpin"), BackSpin);
	double SideSpin = 0.0;
	const bool bHasSide = Ball->TryGetNumberField(TEXT("SideSpin"), SideSpin);

	if (bHasTotal)
	{
		double Axis = 0.0;
		Ball->TryGetNumberField(TEXT("SpinAxis"), Axis);
		const double AxisRad = FMath::DegreesToRadians(Axis);
		Out.BackspinRpm = TotalSpin * FMath::Cos(AxisRad);
		Out.SidespinRpm = TotalSpin * FMath::Sin(AxisRad);
	}
	else if (bHasBack)
	{
		Out.BackspinRpm = BackSpin;
		Out.SidespinRpm = bHasSide ? SideSpin : 0.0;
	}
	else
	{
		// No spin reported: launch-angle heuristic so the flight is plausible (matches OpenFlight).
		Out.BackspinRpm = FMath::Clamp(VLA * 350.0, 1500.0, 9000.0);
		Out.SidespinRpm = 0.0;
		bOutSpinEstimated = true;
	}
	Out.bSpinEstimated = bOutSpinEstimated;

	return true;
}

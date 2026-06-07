#include "Drivers/GSProConnectDriver.h"

#include "Events/EventTypes.h"

#include "Common/TcpSocketBuilder.h"     // FTcpSocketBuilder, FIPv4Address (Networking)
#include "Sockets.h"                      // FSocket
#include "SocketSubsystem.h"              // ISocketSubsystem, FInternetAddr, PLATFORM_SOCKETSUBSYSTEM
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformTime.h"            // FPlatformTime::Seconds (re-arm scheduling)
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
	FGSProConnectListener(int32 InPort, const FGSProConnectProfile& InProfile)
		: Port(InPort), Profile(InProfile) {}

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

			// 2. Fire a scheduled re-arm once its settle has elapsed (armed on club-data / end-of-shot).
			if (RearmDueTime > 0.0 && FPlatformTime::Seconds() >= RearmDueTime)
			{
				RearmDueTime = 0.0;
				UE_LOG(LogTemp, Display, TEXT("golfsim GSProConnect: sending re-arm (Code:201) -- ready for next shot"));
				SendPlayer();
			}

			// 3. Service the current client, if any.
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
		if (Profile.bSendPlayerOnConnect)
		{
			SendPlayer();   // GSPro pushes player info on connect (arms squaregolf; sets initial club elsewhere)
		}
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

	// Pull every complete JSON object out of RecvBuffer (delimited OR concatenated framing), handle each,
	// and keep any partial trailing object buffered for the next recv.
	void ProcessBuffer()
	{
		TArray<FString> Objects;
		const int32 Consumed = UGSProConnectDriver::ExtractJsonObjects(RecvBuffer, Objects);
		if (Consumed > 0)
		{
			RecvBuffer.RightChopInline(Consumed, EAllowShrinking::No);
		}
		for (const FString& Obj : Objects)
		{
			HandleMessage(Obj);
		}
	}

	void HandleMessage(const FString& Msg)
	{
		// Raw connector frame, for protocol/field-mapping troubleshooting (enable `log LogTemp Verbose`).
		UE_LOG(LogTemp, Verbose, TEXT("golfsim GSProConnect: raw <- %s"), *Msg);

		// Ack every message. The connector only checks non-empty == received (body not inspected).
		SendRaw(TEXT("{\"Code\":200,\"Message\":\"Shot received\"}\n"));

		// Ball-data = the shot itself: parse + publish (tagged with this connector's id for provenance).
		FShotTakenEvent Shot;
		bool bSpinEstimated = false;
		if (UGSProConnectDriver::ParseShot(Msg, Shot, bSpinEstimated))
		{
			Shot.Source = Profile.Id;
			UE_LOG(LogTemp, Display,
				TEXT("golfsim GSProConnect[%s]: shot %.1f mph launch %.1f spin %.0f%s -> shot.taken"),
				*Profile.Id, Shot.BallSpeedMps / GMphToMps, Shot.LaunchAngleDeg, Shot.BackspinRpm,
				bSpinEstimated ? TEXT(" (est)") : TEXT(""));
			ShotQueue.Enqueue(Shot);
		}

		// ARM-MODEL connectors only (squaregolf): club-data marks end-of-shot; the connector fires one
		// shot per arm then resets (~2-3s), so we re-arm with a {Code:201} after a settle. NON-arm
		// connectors (springbok/generic) send shots autonomously and read ANY 201 as a club change, so
		// we must NOT re-arm them. Gated on the profile.
		if (Profile.bArmModel && MessageHasClubData(Msg))
		{
			RearmDueTime = FPlatformTime::Seconds() + Profile.RearmSettleSeconds;
			UE_LOG(LogTemp, Display,
				TEXT("golfsim GSProConnect[%s]: club-data (end of shot) -> re-arm in %.1fs"),
				*Profile.Id, Profile.RearmSettleSeconds);
		}
	}

	// True if the message's ShotDataOptions.ContainsClubData is set (the connector's end-of-shot marker).
	static bool MessageHasClubData(const FString& Json)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Opts = nullptr;
		if (!Root->TryGetObjectField(TEXT("ShotDataOptions"), Opts) || !Opts || !Opts->IsValid())
		{
			return false;
		}
		bool bClub = false;
		(*Opts)->TryGetBoolField(TEXT("ContainsClubData"), bClub);
		return bClub;
	}

	void SendPlayer()
	{
		FString Line;
		{
			FScopeLock Lock(&ClientCS);
			// Always include Player.Club -- springbok throws KeyError on a 201 without it; default to the
			// profile's club until one is selected. Message MUST be the verbatim "GSPro Player Information":
			// connectors switch on that exact text (squaregolf treats it as the arm signal; a custom string
			// falls through as "Unknown GSPro message type"). Confirmed vs squaregolf + springbok contracts.
			const FString Club = ClubCode.IsEmpty() ? Profile.DefaultClub : ClubCode;
			Line = FString::Printf(
				TEXT("{\"Code\":201,\"Message\":\"GSPro Player Information\",\"Player\":{\"Handed\":\"%s\",\"Club\":\"%s\"}}\n"),
				*Handed, *Club);
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

	FGSProConnectProfile Profile;       // per-connector behavior (arm model, default club, ...)
	FString RecvBuffer;                 // socket-thread only
	double RearmDueTime = 0.0;          // socket-thread only; FPlatformTime::Seconds() due time, 0 = none
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

	Listener = new FGSProConnectListener(Port, Profile);
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

int32 UGSProConnectDriver::ExtractJsonObjects(const FString& Buffer, TArray<FString>& OutObjects)
{
	// Walk the buffer tracking brace depth, ignoring braces inside JSON strings (and escaped quotes).
	// Each time depth returns to 0 on a '}' we've got one complete top-level object. Works whether the
	// connector delimits objects (newlines/whitespace, which sit at depth 0 and are skipped) or
	// concatenates them back-to-back. Returns chars consumed up to the end of the last complete object.
	int32 Depth = 0;
	int32 Start = INDEX_NONE;
	int32 Consumed = 0;
	bool bInString = false;
	bool bEscaped = false;
	const int32 Len = Buffer.Len();
	for (int32 i = 0; i < Len; ++i)
	{
		const TCHAR Ch = Buffer[i];
		if (bInString)
		{
			if (bEscaped)            { bEscaped = false; }
			else if (Ch == TCHAR('\\')) { bEscaped = true; }
			else if (Ch == TCHAR('"'))  { bInString = false; }
			continue;
		}
		if (Ch == TCHAR('"'))
		{
			bInString = true;
		}
		else if (Ch == TCHAR('{'))
		{
			if (Depth == 0) { Start = i; }
			++Depth;
		}
		else if (Ch == TCHAR('}'))
		{
			if (Depth > 0)
			{
				--Depth;
				if (Depth == 0 && Start != INDEX_NONE)
				{
					OutObjects.Add(Buffer.Mid(Start, i - Start + 1));
					Consumed = i + 1;
					Start = INDEX_NONE;
				}
			}
		}
	}
	return Consumed;
}

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

	// Spin: prefer the connector's MEASURED BackSpin/SideSpin (already sign-flipped to our +=fade/right
	// convention -- consume as-is). Only decompose TotalSpin+SpinAxis when explicit components are
	// absent (e.g. a connector that sends only total+axis); else estimate from launch angle. Note: when
	// a device reports all of them they need not be self-consistent (TotalSpin != hypot(Back,Side)), so
	// the measured components are authoritative, not the decomposition.
	double BackSpin = 0.0;
	// springbok sends "Backspin" (lowercase s); squaregolf/GSPro use "BackSpin". Accept either.
	const bool bHasBack = Ball->TryGetNumberField(TEXT("BackSpin"), BackSpin)
		|| Ball->TryGetNumberField(TEXT("Backspin"), BackSpin);
	double SideSpin = 0.0;
	const bool bHasSide = Ball->TryGetNumberField(TEXT("SideSpin"), SideSpin);
	double TotalSpin = 0.0;
	const bool bHasTotal = Ball->TryGetNumberField(TEXT("TotalSpin"), TotalSpin) && TotalSpin > 0.0;

	if (bHasBack && bHasSide)
	{
		Out.BackspinRpm = BackSpin;
		Out.SidespinRpm = SideSpin;   // + = fade/right
	}
	else if (bHasTotal)
	{
		double Axis = 0.0;
		Ball->TryGetNumberField(TEXT("SpinAxis"), Axis);   // + = fade/right tilt
		const double AxisRad = FMath::DegreesToRadians(Axis);
		Out.BackspinRpm = TotalSpin * FMath::Cos(AxisRad);
		Out.SidespinRpm = TotalSpin * FMath::Sin(AxisRad);
	}
	else if (bHasBack)
	{
		Out.BackspinRpm = BackSpin;
		Out.SidespinRpm = 0.0;
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

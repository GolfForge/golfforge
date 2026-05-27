#include "Drivers/OpenFlightDriver.h"

#include "Events/EventTypes.h"

#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Modules/ModuleManager.h"   // LoadModuleChecked (ensure WebSockets is loaded before use)
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ConfigCacheIni.h"     // GConfig

namespace
{
	constexpr double GMphToMps = 0.44704;   // miles/hour -> meters/second
}

FString UOpenFlightDriver::ResolveUrl() const
{
	// Build from host + port read separately. A "//" stored directly in an INI value gets truncated
	// by UE's config parser, so the scheme/slashes live in code only. The path + query is the
	// Engine.IO v4 WebSocket-transport handshake endpoint (OpenFlight is Flask-SocketIO).
	FString Host = TEXT("localhost");
	int32 Port = 8080;
	GConfig->GetString(TEXT("OpenFlight"), TEXT("Host"), Host, GGameIni);
	GConfig->GetInt(TEXT("OpenFlight"), TEXT("Port"), Port, GGameIni);
	return FString::Printf(TEXT("ws://%s:%d/socket.io/?EIO=4&transport=websocket"), *Host, Port);
}

void UOpenFlightDriver::Connect()
{
	bIntentionalDisconnect = false;
	if (Socket.IsValid() && Socket->IsConnected())
	{
		return;   // transport already up
	}
	Url = ResolveUrl();

	// Ensure the WebSockets module is loaded (linked via Build.cs, but a packaged build may not
	// auto-load it before first use).
	FWebSocketsModule& WSModule = FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	Socket = WSModule.CreateWebSocket(Url, FString());
	Socket->OnConnected().AddUObject(this, &UOpenFlightDriver::HandleConnected);
	Socket->OnConnectionError().AddUObject(this, &UOpenFlightDriver::HandleConnectionError);
	Socket->OnClosed().AddUObject(this, &UOpenFlightDriver::HandleClosed);
	Socket->OnMessage().AddUObject(this, &UOpenFlightDriver::HandleMessage);

	UE_LOG(LogTemp, Display, TEXT("golfsim OpenFlight: connecting to %s ..."), *Url);
	SetStatus(false, TEXT("OpenFlight: connecting..."));
	Socket->Connect();
}

void UOpenFlightDriver::Disconnect()
{
	bIntentionalDisconnect = true;
	CancelReconnect();
	if (Socket.IsValid())
	{
		if (Socket->IsConnected())
		{
			Socket->Close();
		}
		Socket.Reset();   // destroys the socket + its AddUObject bindings
	}
	SetStatus(false, TEXT("OpenFlight: disconnected"));
}

void UOpenFlightDriver::InjectTestMessage(const FString& Payload)
{
	// Console/test path: treat the arg as a shot JSON payload (bypasses the Socket.IO framing) and
	// run it straight through the parse->publish path -- proves the chain with no server.
	FShotTakenEvent Shot;
	bool bSpinEstimated = false;
	if (ParseShot(Payload, Shot, bSpinEstimated))
	{
		UE_LOG(LogTemp, Display,
			TEXT("golfsim OpenFlight: injected shot %.1f mph%s -> shot.taken"),
			Shot.BallSpeedMps / GMphToMps, bSpinEstimated ? TEXT(" (est)") : TEXT(""));
		PublishShot(Shot);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("golfsim OpenFlight: injected payload did not parse as a shot"));
	}
}

void UOpenFlightDriver::RequestSimulatedShot()
{
	// OpenFlight (mock mode) emits a fake shot when it receives a Socket.IO "simulate_shot" event.
	if (Socket.IsValid() && Socket->IsConnected())
	{
		Socket->Send(TEXT("42[\"simulate_shot\"]"));
		UE_LOG(LogTemp, Display, TEXT("golfsim OpenFlight: requested simulate_shot"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("golfsim OpenFlight: not connected; cannot request a shot"));
	}
}

void UOpenFlightDriver::HandleConnected()
{
	// Raw WebSocket is up; the Socket.IO handshake follows (server sends Engine.IO open '0', we reply
	// '40' to connect the default namespace, server acks '40'). Status goes green on that ack.
	UE_LOG(LogTemp, Display, TEXT("golfsim OpenFlight: websocket open; Socket.IO handshake..."));
}

void UOpenFlightDriver::HandleConnectionError(const FString& Error)
{
	UE_LOG(LogTemp, Warning, TEXT("golfsim OpenFlight: connection error: %s"), *Error);
	SetStatus(false, FString::Printf(TEXT("OpenFlight: error (%s)"), *Error));
	if (!bIntentionalDisconnect)
	{
		ScheduleReconnect();
	}
}

void UOpenFlightDriver::HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogTemp, Warning, TEXT("golfsim OpenFlight: closed (code=%d clean=%d reason=%s)"),
		StatusCode, bWasClean ? 1 : 0, *Reason);
	SetStatus(false, TEXT("OpenFlight: disconnected"));
	if (!bIntentionalDisconnect)
	{
		ScheduleReconnect();
	}
}

void UOpenFlightDriver::HandleMessage(const FString& Message)
{
	// Engine.IO/Socket.IO frames arrive as text. Log raw at Verbose for protocol/schema discovery.
	UE_LOG(LogTemp, Verbose, TEXT("golfsim OpenFlight: raw frame: %s"), *Message);
	if (Message.IsEmpty())
	{
		return;
	}

	const TCHAR Eio = Message[0];
	if (Eio == TCHAR('0'))   // Engine.IO OPEN -> connect the Socket.IO default namespace
	{
		if (Socket.IsValid())
		{
			Socket->Send(TEXT("40"));
		}
		return;
	}
	if (Eio == TCHAR('2'))   // Engine.IO PING -> PONG (keep the connection alive)
	{
		if (Socket.IsValid())
		{
			Socket->Send(TEXT("3"));
		}
		return;
	}
	if (Eio != TCHAR('4'))   // 1=close, 3=pong (unused) -- nothing to do
	{
		return;
	}

	// Engine.IO MESSAGE: the next char is the Socket.IO packet type.
	if (Message.Len() < 2)
	{
		return;
	}
	const TCHAR Sio = Message[1];
	if (Sio == TCHAR('0'))   // CONNECT ack ("40" / "40{...}") -> namespace connected
	{
		ReconnectAttempt = 0;
		UE_LOG(LogTemp, Display, TEXT("golfsim OpenFlight: connected (%s)"), *Url);
		SetStatus(true, TEXT("OpenFlight: connected"));
		return;
	}
	if (Sio == TCHAR('1'))   // DISCONNECT
	{
		SetStatus(false, TEXT("OpenFlight: disconnected"));
		return;
	}
	if (Sio != TCHAR('2'))   // only EVENT (42) carries shot data (4=ack, 4x=connect_error, etc.)
	{
		return;
	}

	// EVENT: payload after "42" (and any ack id) is a JSON array ["event", arg]. Parse from the '['.
	int32 BracketIdx = INDEX_NONE;
	if (!Message.FindChar(TCHAR('['), BracketIdx))
	{
		return;
	}
	const FString ArrayJson = Message.Mid(BracketIdx);
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArrayJson);
	TArray<TSharedPtr<FJsonValue>> Arr;
	if (!FJsonSerializer::Deserialize(Reader, Arr) || Arr.Num() < 2 || !Arr[0].IsValid() || !Arr[1].IsValid())
	{
		return;
	}
	if (Arr[0]->AsString() != TEXT("shot"))
	{
		return;   // session_state / trigger_status / club_changed / ... -- not a shot
	}
	const TSharedPtr<FJsonObject> Payload = Arr[1]->AsObject();   // { "shot": {...}, "stats": {...} }
	if (!Payload.IsValid())
	{
		return;
	}

	FShotTakenEvent Shot;
	bool bSpinEstimated = false;
	if (ParseShotObject(Payload, Shot, bSpinEstimated))
	{
		UE_LOG(LogTemp, Display,
			TEXT("golfsim OpenFlight: shot %.1f mph launch %.1f spin %.0f%s -> shot.taken"),
			Shot.BallSpeedMps / GMphToMps, Shot.LaunchAngleDeg, Shot.BackspinRpm,
			bSpinEstimated ? TEXT(" (est)") : TEXT(""));
		PublishShot(Shot);
	}
}

void UOpenFlightDriver::ScheduleReconnect()
{
	CancelReconnect();
	// Exponential backoff 1,2,4,8,16,30(cap) seconds.
	const float Delay = FMath::Min(30.f, FMath::Pow(2.f, static_cast<float>(ReconnectAttempt)));
	ReconnectAttempt = FMath::Min(ReconnectAttempt + 1, 5);
	UE_LOG(LogTemp, Display, TEXT("golfsim OpenFlight: reconnecting in %.0fs"), Delay);

	TWeakObjectPtr<UOpenFlightDriver> WeakThis(this);
	ReconnectHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakThis](float) -> bool
		{
			if (UOpenFlightDriver* Self = WeakThis.Get())
			{
				if (!Self->bIntentionalDisconnect)
				{
					Self->Connect();
				}
			}
			return false;   // one-shot
		}), Delay);
}

void UOpenFlightDriver::CancelReconnect()
{
	if (ReconnectHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ReconnectHandle);
		ReconnectHandle.Reset();
	}
}

bool UOpenFlightDriver::ParseShot(const FString& Json, FShotTakenEvent& Out, bool& bOutSpinEstimated)
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

bool UOpenFlightDriver::ParseShotObject(const TSharedPtr<FJsonObject>& Payload, FShotTakenEvent& Out,
	bool& bOutSpinEstimated)
{
	bOutSpinEstimated = false;
	if (!Payload.IsValid())
	{
		return false;
	}

	// OpenFlight's "shot" event payload is { "shot": <fields>, "stats": <...> }. Accept that wrapper
	// (or data/payload), or a bare shot object.
	const FJsonObject* Shot = Payload.Get();
	const TSharedPtr<FJsonObject>* Wrapped = nullptr;
	if (Payload->TryGetObjectField(TEXT("shot"), Wrapped)
		|| Payload->TryGetObjectField(TEXT("data"), Wrapped)
		|| Payload->TryGetObjectField(TEXT("payload"), Wrapped))
	{
		if (Wrapped && Wrapped->IsValid())
		{
			Shot = Wrapped->Get();
		}
	}

	// Ball speed is the one field we require to treat this as a shot.
	double BallMph = 0.0;
	if (!Shot->TryGetNumberField(TEXT("ball_speed_mph"), BallMph) || BallMph <= 0.0)
	{
		return false;
	}

	Out = FShotTakenEvent();
	Out.Source = TEXT("openflight");
	Out.Lie = TEXT("tee");
	Out.BallSpeedMps = BallMph * GMphToMps;

	double LaunchDeg = 0.0;
	Shot->TryGetNumberField(TEXT("launch_angle_vertical"), LaunchDeg);     // up = positive
	Out.LaunchAngleDeg = LaunchDeg;

	double AzimuthDeg = 0.0;
	Shot->TryGetNumberField(TEXT("launch_angle_horizontal"), AzimuthDeg);  // aim L/R of target line
	Out.AzimuthDeg = AzimuthDeg;

	FString Club;
	if (Shot->TryGetStringField(TEXT("club"), Club) && !Club.IsEmpty() && Club != TEXT("unknown"))
	{
		Out.Club = Club;
	}

	double Smash = 0.0;
	if (Shot->TryGetNumberField(TEXT("smash_factor"), Smash))
	{
		Out.SmashFactor = Smash;
	}

	double SpinRpm = 0.0;
	if (Shot->TryGetNumberField(TEXT("spin_rpm"), SpinRpm) && SpinRpm > 0.0)
	{
		// Decompose total spin about its tilt axis into back/side. spin_axis_deg: 0 = pure backspin,
		// + = fade (right), - = draw (left) -- matching our SidespinRpm sign convention.
		double AxisDeg = 0.0;
		Shot->TryGetNumberField(TEXT("spin_axis_deg"), AxisDeg);
		const double AxisRad = FMath::DegreesToRadians(AxisDeg);
		Out.BackspinRpm = SpinRpm * FMath::Cos(AxisRad);
		Out.SidespinRpm = SpinRpm * FMath::Sin(AxisRad);
	}
	else
	{
		// Spin missing (~40-50% of OpenFlight shots): launch-angle heuristic so the flight is
		// plausible. Tunable first cut; flagged estimated so the UI can mark it.
		Out.BackspinRpm = FMath::Clamp(LaunchDeg * 350.0, 1500.0, 9000.0);
		Out.SidespinRpm = 0.0;
		bOutSpinEstimated = true;
	}
	Out.bSpinEstimated = bOutSpinEstimated;

	return true;
}

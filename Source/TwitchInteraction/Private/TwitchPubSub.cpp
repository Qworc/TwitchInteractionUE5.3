// Copyright (c) 2021 Zeplin455.

#include "TwitchPubSub.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"

// Sets default values for this component's properties
UTwitchPubSub::UTwitchPubSub()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}

// Called when the game starts
void UTwitchPubSub::BeginPlay()
{
	Super::BeginPlay();

	// ...
	FModuleManager::Get().LoadModuleChecked("WebSockets");
}

void UTwitchPubSub::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Disconnect();
}

// Called every frame
void UTwitchPubSub::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

void UTwitchPubSub::SetInfo(const FString _oauth, const FString _authType, const FString _channelId)
{
	authToken = _oauth;
	authType = _authType;
	channelId = _channelId;
	Init = true;
}

bool UTwitchPubSub::Connect(FString &result)
{
	const FString ServerURL = TEXT("wss://pubsub-edge.twitch.tv:443/"); // Your server URL. You can use ws, wss or wss+insecure.
	const FString ServerProtocol = TEXT("wss");

	Socket = FWebSocketsModule::Get().CreateWebSocket(ServerURL, ServerProtocol);

	Socket->OnConnected().AddLambda([this]() -> void
																	{
		// This code will run once connected.
			UE_LOG(LogTemp, Warning, TEXT("Connected to twitch pubsub"));
			SendMessage("{\"type\":\"PING\"}");
			RequestEventSubs();

			GetWorld()->GetTimerManager().SetTimer(UpdateTimer, this, &UTwitchPubSub::UpdatePing, 10.f, true); });

	Socket->OnConnectionError().AddLambda([](const FString &Error) -> void
																				{
																					// This code will run if the connection failed. Check Error to see what happened.
																				});

	Socket->OnClosed().AddLambda([](int32 StatusCode, const FString &Reason, bool bWasClean) -> void
															 {
																 // This code will run when the connection to the server has been terminated.
																 // Because of an error or a call to Socket->Close().
															 });

	Socket->OnMessage().AddLambda([this](const FString &Message) -> void
																{ ProcessMessage(Message); });

	Socket->OnRawMessage().AddLambda([](const void *Data, SIZE_T Size, SIZE_T BytesRemaining) -> void
																	 {
																		 // This code will run when we receive a raw (binary) message from the server.
																	 });

	Socket->OnMessageSent().AddLambda([](const FString &MessageString) -> void
																		{
																			// This code is called after we sent a message to the server.
																		});

	Socket->Connect();

	// if (Socket->IsConnected())
	//{
	//	RequestEventSubs();
	//	return true;
	// }
	// else
	//{
	//	return false;
	// }
	return true;
}

bool UTwitchPubSub::Disconnect()
{
	if (Socket != nullptr && Socket->IsConnected())
	{
		Socket->Close();
	}

	return true;
}

bool UTwitchPubSub::SendMessage(FString _message)
{
	if (Socket != nullptr && Socket->IsConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("SEND - %s"), *_message);
		Socket->Send(_message);
		return true;
	}
	else
	{
		return false;
	}
}

void UTwitchPubSub::UpdatePing()
{
	if (Socket == nullptr && Socket->IsConnected())
	{
		return;
	}

	pingTicker += 10;

	// ping every 1 minute
	if (pingTicker >= 60)
	{
		pingTicker = 0;
		LastUpdate = FDateTime::Now();
		SendMessage("{\"type\":\"PING\"}");
	}
}

void UTwitchPubSub::RequestEventSubs()
{
	FTwitchEventSubRequest requestInfo;
	requestInfo.type = "LISTEN";
	requestInfo.nonce = FString::FromInt(requestCounter++);
	requestInfo.data.auth_token = authToken;

	// requestInfo.data.topics = listenTopics;
	requestInfo.data.topics.Add(FString("channel-bits-events-v2.") + FString(channelId));
	requestInfo.data.topics.Add(FString("channel-bits-badge-unlocks.") + FString(channelId));
	requestInfo.data.topics.Add(FString("channel-points-channel-v1.") + FString(channelId));
	requestInfo.data.topics.Add(FString("channel-subscribe-events-v1.") + FString(channelId));

	FString result;
	FJsonObjectConverter::UStructToJsonObjectString(requestInfo, result, 0, 0, 0, nullptr, false);

	SendMessage(result);
}

void UTwitchPubSub::ProcessMessage(const FString _jsonStr)
{
	FTwitchMessage targetMessage;

	UE_LOG(LogTemp, Warning, TEXT("RECV : %s"), *_jsonStr);

	if (!FJsonObjectConverter::JsonObjectStringToUStruct(_jsonStr, &targetMessage, 0, 0))
	{
		// ERROR
		UE_LOG(LogTemp, Error, TEXT("Deserialize Error : %s"), *_jsonStr);
	}

	if (targetMessage.type == "PONG")
	{
		// Pong received
	}

	if (targetMessage.type == "RESPONSE")
	{
	}

	if (targetMessage.type == "MESSAGE")
	{
		if (targetMessage.data.topic.StartsWith("channel-bits-events-v2"))
		{
			FTwitchEventBitsRoot twitchEventBitsMessage;
			FJsonObjectConverter::JsonObjectStringToUStruct(_jsonStr, &twitchEventBitsMessage, 0, 0);
			OnBitsEventReceived.Broadcast(twitchEventBitsMessage.data.message.data);
		}

		if (targetMessage.data.topic.StartsWith("channel-bits-badge-unlocks"))
		{
			FString fixBadgeJson = _jsonStr.Replace(TEXT("\\\""), TEXT("\""));
			FTwitchEventBitsBadgeRoot twitchEventBitsBadgeMessage;
			FJsonObjectConverter::JsonObjectStringToUStruct(_jsonStr, &twitchEventBitsBadgeMessage, 0, 0);
			OnBitsBadgeEventReceived.Broadcast(twitchEventBitsBadgeMessage.data.message);
		}

		if (targetMessage.data.topic.StartsWith("channel-subscribe-events-v1"))
		{
			FTwitchEventSubscribe twitchSubscribeMessage;
			FJsonObjectConverter::JsonObjectStringToUStruct(_jsonStr, &twitchSubscribeMessage, 0, 0);
			OnSubscribeEventReceived.Broadcast(twitchSubscribeMessage.data.message);
		}

		if (targetMessage.data.topic.StartsWith("channel-points-channel-v1"))
		{
			FTwitchEventRedeemRoot twitchRedeemMessage;
			if (!FJsonObjectConverter::JsonObjectStringToUStruct(targetMessage.data.message, &twitchRedeemMessage, 0, 0))
			{
				// ERROR
				UE_LOG(LogTemp, Error, TEXT("Deserialize Error : %s"), *targetMessage.data.message);
			}
			UE_LOG(LogTemp, Warning, TEXT("message : %s"), *targetMessage.data.message);
			UE_LOG(LogTemp, Warning, TEXT("topic : %s"), *twitchRedeemMessage.type);
			UE_LOG(LogTemp, Warning, TEXT("Redemption.id : %s"), *twitchRedeemMessage.data.redemption.id);
			UE_LOG(LogTemp, Warning, TEXT("Redemption.user.id : %s"), *twitchRedeemMessage.data.redemption.user.id);
			UE_LOG(LogTemp, Warning, TEXT("Redemption.user.display_name : %s"), *twitchRedeemMessage.data.redemption.user.display_name);
			UE_LOG(LogTemp, Warning, TEXT("Redemption.user.login : %s"), *twitchRedeemMessage.data.redemption.user.login);
			UE_LOG(LogTemp, Warning, TEXT("Redemption.channel_id : %s"), *twitchRedeemMessage.data.redemption.channel_id);
			UE_LOG(LogTemp, Warning, TEXT("Redemption.redeemed_at : %s"), *twitchRedeemMessage.data.redemption.redeemed_at);
			UE_LOG(LogTemp, Warning, TEXT("Redemption.status : %s"), *twitchRedeemMessage.data.redemption.status);
			OnRedeemEventReceived.Broadcast(twitchRedeemMessage.data);
		}
	}
}

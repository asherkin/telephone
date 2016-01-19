#include "extension.h"

#include <libwebsockets.h>

#include <CDetour/detours.h>

#include <string.h>

Telephone g_Telephone;
SMEXT_LINK(&g_Telephone);

IGameConfig *gameConfig;

CDetour *detourBroadcastVoiceData;

lws_context *websocket;

#ifndef NDEBUG
#define DEBUG_LOG rootconsole->ConsolePrint
#else
#define DEBUG_LOG(...)
#endif

class IClient;

DETOUR_DECL_MEMBER4(BroadcastVoiceData, void, IClient *, client, int, bytes, char *, data, long long, xuid)
{
	// Get it to the other in-game players first.
	DETOUR_MEMBER_CALL(BroadcastVoiceData)(client, bytes, data, xuid);

	DEBUG_LOG(">>> SV_BroadcastVoiceData(%p, %d, %p, %lld)", client, bytes, data, xuid);

#if 0
	static int packet = 0;
	char filename[64];
	sprintf(filename, "voice_%02d.dat", packet++);
	FILE *file = fopen(filename, "wb");
	fwrite(data, bytes, 1, file);
	fclose(file);
#endif
}

int callback_http(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
{
	switch (reason) {
		case LWS_CALLBACK_HTTP: {
			lws_return_http_status(wsi, 403, "Websocket connections only kthx.");
			return 1;
		}
	}

	return 0;
}

int callback_voice(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
{
	switch (reason) {
		case LWS_CALLBACK_ESTABLISHED: {
			DEBUG_LOG(">>> Client connected to voice websocket.");
			break;
		}

		case LWS_CALLBACK_CLOSED: {
			DEBUG_LOG(">>> Client disconnected from voice websocket.");
			break;
		}

		case LWS_CALLBACK_RECEIVE: {
			DEBUG_LOG(">>> Received message on voice websocket: %s", (char *)in);
			break;
		}
	}

	return 0;
}

lws_protocols protocols[] = {
	{ "http-only", callback_http, 0, 0 },
	{ "voice-chat", callback_voice, 0, 512 },
	{ nullptr, nullptr, 0, 0 },
};

void OnGameFrame(bool simulating)
{
	lws_service(websocket, 0);
}

bool Telephone::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	if (!gameconfs->LoadGameConfigFile("telephone.games", &gameConfig, error, maxlength)) {
		return false;
	}

	CDetourManager::Init(smutils->GetScriptingEngine(), gameConfig);

	detourBroadcastVoiceData = DETOUR_CREATE_MEMBER(BroadcastVoiceData, "BroadcastVoiceData");
	if (!detourBroadcastVoiceData) {
		gameconfs->CloseGameConfigFile(gameConfig);

		strncpy(error, "Error setting up BroadcastVoiceData detour", maxlength);
		return false;
	}

	lws_context_creation_info websocketParams = {};
	websocketParams.port = 9000;
	websocketParams.protocols = protocols;
	websocketParams.gid = -1;
	websocketParams.uid = -1;

	websocket = lws_create_context(&websocketParams);
	if (!websocket) {
		gameconfs->CloseGameConfigFile(gameConfig);

		strncpy(error, "Failed to start websocket server", maxlength);
		return false;
	}

	detourBroadcastVoiceData->EnableDetour();

	smutils->AddGameFrameHook(OnGameFrame);

	return true;
}

void Telephone::SDK_OnUnload()
{
	smutils->RemoveGameFrameHook(OnGameFrame);

	lws_context_destroy(websocket);

	detourBroadcastVoiceData->DisableDetour();

	gameconfs->CloseGameConfigFile(gameConfig);
}

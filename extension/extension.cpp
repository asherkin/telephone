#include "extension.h"

#ifdef strcasecmp
#undef strcasecmp
#endif
#include <libwebsockets.h>

#include <amtl/am-hashset.h>
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

struct WebsocketHashPolicy
{
	static uint32_t hash(const lws *const &value) {
		return (uint32_t)value;
	}

	static bool matches(const lws *const &value, const lws *const &key) {
		return (hash(value) == hash(key));
	}
};

typedef ke::HashSet<lws *, WebsocketHashPolicy> WebsocketSet;
WebsocketSet websockets;

class IClient;

DETOUR_DECL_MEMBER4(BroadcastVoiceData, void, IClient *, client, int, bytes, char *, data, long long, xuid)
{
	DEBUG_LOG(">>> SV_BroadcastVoiceData(%p, %d, %p, %lld)", client, bytes, data, xuid);

	// Get it to the other in-game players first.
	DETOUR_MEMBER_CALL(BroadcastVoiceData)(client, bytes, data, xuid);

#if 0
	static int packet = 0;
	char filename[64];
	sprintf(filename, "voice_%02d.dat", packet++);
	FILE *file = fopen(filename, "wb");
	fwrite(data, bytes, 1, file);
	fclose(file);
#endif

	unsigned char *socketBuffer = (unsigned char *)alloca(LWS_SEND_BUFFER_PRE_PADDING + bytes + LWS_SEND_BUFFER_POST_PADDING);
	memcpy(&socketBuffer[LWS_SEND_BUFFER_PRE_PADDING], data, bytes);

	for (WebsocketSet::iterator i = websockets.iter(); !i.empty(); i.next()) {
		lws_write(*i, &socketBuffer[LWS_SEND_BUFFER_PRE_PADDING], bytes, LWS_WRITE_BINARY);
	}
}

int callback_http(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
{
	switch (reason) {
		case LWS_CALLBACK_HTTP: {
			lws_return_http_status(wsi, HTTP_STATUS_FORBIDDEN, "Websocket connections only kthx.");
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
			WebsocketSet::Insert insert = websockets.findForAdd(wsi);
			if (!insert.found()) websockets.add(insert, wsi);
			break;
		}

		case LWS_CALLBACK_CLOSED: {
			DEBUG_LOG(">>> Client disconnected from voice websocket.");
			WebsocketSet::Result result = websockets.find(wsi);
			if (result.found()) websockets.remove(result);
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
	{ "voice-chat", callback_voice, 0, 2048 },
	{ nullptr, nullptr, 0, 0 },
};

void OnGameFrame(bool simulating)
{
	lws_service(websocket, 0);
}

bool Telephone::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	websockets.init();

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

#ifdef NDEBUG
	lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);
#endif

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

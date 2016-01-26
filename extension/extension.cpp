#include "extension.h"

#ifdef strcasecmp
#undef strcasecmp
#endif
#include <libwebsockets.h>

#include <amtl/am-hashset.h>
#include <CDetour/detours.h>

#include <string.h>

#include "IConplex.h"

Telephone g_Telephone;
SMEXT_LINK(&g_Telephone);

IConplex *conplex;

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

IConplex::ProtocolDetectionState ConplexHTTPDetector(const char *id, const unsigned char *buffer, unsigned int bufferLength)
{
	bool hasSpace = false;
	bool hasSlash = false;
	for (unsigned int i = 0; i < bufferLength; ++i) {
		if (hasSpace) {
			hasSlash = (buffer[i] == '/');
			return hasSlash ? IConplex::Match : IConplex::NoMatch;
		}

		hasSpace = (i >= 3) && (buffer[i] == ' ');
		if (hasSpace) {
			continue;
		}

		if (buffer[i] < 'A' || buffer[i] > 'Z') {
			return IConplex::NoMatch;
		}
	}

	return IConplex::NeedMoreData;
}

bool ConplexHTTPHandler(const char *id, int socket, const sockaddr *address, unsigned int addressLength)
{
	lws_adopt_socket(websocket, socket);
	return true; // LWS will close the socket on failure.
}

IConplex::ProtocolDetectionState ConplexHTTPSDetector(const char *id, const unsigned char *buffer, unsigned int bufferLength)
{
	if (bufferLength <= 0) return IConplex::NeedMoreData;
	if (buffer[0] != 0x16) return IConplex::NoMatch;
	if (bufferLength <= 1) return IConplex::NeedMoreData;
	if (buffer[1] != 0x03) return IConplex::NoMatch;
	if (bufferLength <= 5) return IConplex::NeedMoreData;
	if (buffer[5] != 0x01) return IConplex::NoMatch;
	if (bufferLength <= 6) return IConplex::NeedMoreData;
	if (buffer[6] != 0x00) return IConplex::NoMatch;
	if (bufferLength <= 8) return IConplex::NeedMoreData;
	if (((buffer[3] * 256) + buffer[4]) != ((buffer[7] * 256) + buffer[8] + 4)) return IConplex::NoMatch;
	return IConplex::Match;
}

bool ConplexHTTPSHandler(const char *id, int socket, const sockaddr *address, unsigned int addressLength)
{
	// We don't actually handle HTTPS connections yet.
	// Implementation will be the same as ConplexHTTPHandler apart from handing off to a dedicated HTTPS daemon.
	return false;
}

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

	sharesys->AddDependency(myself, "conplex.ext", true, true);

	SM_GET_IFACE(CONPLEX, conplex);

	if (!conplex->RegisterProtocolHandler("HTTP", ConplexHTTPDetector, ConplexHTTPHandler)) {
		gameconfs->CloseGameConfigFile(gameConfig);

		strncpy(error, "Failed to register handler for HTTP protocol", maxlength);
		return false;
	}

	if (!conplex->RegisterProtocolHandler("HTTPS", ConplexHTTPSDetector, ConplexHTTPSHandler)) {
		gameconfs->CloseGameConfigFile(gameConfig);

		conplex->DropProtocolHandler("HTTP");
		strncpy(error, "Failed to register handler for HTTPS protocol", maxlength);
		return false;
	}

	lws_context_creation_info websocketParams = {};
	websocketParams.port = CONTEXT_PORT_NO_LISTEN;
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

	if (conplex) {
		conplex->DropProtocolHandler("HTTP");
		conplex->DropProtocolHandler("HTTPS");
	}

	lws_context_destroy(websocket);

	detourBroadcastVoiceData->DisableDetour();

	gameconfs->CloseGameConfigFile(gameConfig);
}

bool Telephone::QueryInterfaceDrop(SMInterface *interface)
{
	if (conplex && interface == conplex) {
		return false;
	}

	return true;
}

void Telephone::NotifyInterfaceDrop(SMInterface *interface)
{
	if (conplex && interface == conplex) {
		conplex->DropProtocolHandler("HTTP");
		conplex->DropProtocolHandler("HTTPS");
		conplex = NULL;
	}
}

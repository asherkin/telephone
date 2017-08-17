#include "extension.h"

#ifdef strcasecmp
#undef strcasecmp
#endif
#include <libwebsockets.h>

#include <amtl/am-hashset.h>
#include <amtl/am-inlinelist.h>
#include <amtl/am-refcounting.h>
#include <CDetour/detours.h>

#include <string.h>

// CS:GO uses std::string to store the voice data.
#include <string>

enum TelephoneEvent: unsigned char
{
	TelephoneEvent_VoiceData,
};

enum VoiceDataType: unsigned char
{
	VoiceDataType_Steam,
	VoiceDataType_Speex,
	VoiceDataType_Celt,
};

Telephone g_Telephone;
SMEXT_LINK(&g_Telephone);

IGameConfig *g_gameConfig = nullptr;
int g_steamIdOffset = 0;

CDetour *g_detourBroadcastVoiceData = nullptr;

lws_context *g_websocket = nullptr;

char g_configNetworkInterface[64] = "127.0.0.1";
short g_configNetworkPort = 9000;
VoiceDataType g_configVoiceDataType = VoiceDataType_Steam;

#ifndef NDEBUG
#define DEBUG_LOG rootconsole->ConsolePrint
#else
#define DEBUG_LOG(...)
#endif

struct VoiceBuffer: ke::Refcounted<VoiceBuffer>
{
	VoiceBuffer(const void *data, size_t bytes, VoiceDataType voiceDataType, uint64_t steamId) {
		this->length = sizeof(TelephoneEvent) + sizeof(voiceDataType) + bytes;

		// The Steam voice data already includes the client's steamid as part of the
		// data stream. For the native engine codecs we need to pack it in ourselves.
		if (voiceDataType != VoiceDataType_Steam) {
			this->length += sizeof(steamId);
		}

		this->data = new unsigned char[LWS_SEND_BUFFER_PRE_PADDING + this->length + LWS_SEND_BUFFER_POST_PADDING];
		size_t cursor = LWS_SEND_BUFFER_PRE_PADDING;

		this->data[cursor] = TelephoneEvent_VoiceData;
		cursor += sizeof(TelephoneEvent);

		this->data[cursor] = voiceDataType;
		cursor += sizeof(voiceDataType);

		if (voiceDataType != VoiceDataType_Steam) {
			memcpy(&this->data[cursor], &steamId, sizeof(steamId));
			cursor += sizeof(steamId);
		}

		memcpy(&this->data[cursor], data, bytes);
	}

	~VoiceBuffer() {
		delete[] data;
	}

	unsigned char *data;
	size_t length;
};

struct VoiceBufferNode: ke::Ref<VoiceBuffer>, ke::InlineListNode<VoiceBufferNode>
{
	VoiceBufferNode(VoiceBuffer *buffer): ke::Ref<VoiceBuffer>(buffer), ke::InlineListNode<VoiceBufferNode>() {
		// Do nothing.
	}
};

struct Websocket
{
	// For real use, set everything later with init() due to moveable issues.
	Websocket(): wsi(nullptr), queue(nullptr) {
	}

	// Used for hashing only.
	Websocket(lws *wsi): wsi(wsi), queue(nullptr) {
	}

	~Websocket() {
		delete queue;
	}

	// TODO: Once we update to a version of SM with modern AMTL,
	// this needs to be converted to a move constructor.
	// (and the copy ctor deletes below can go)
	Websocket(ke::Moveable<Websocket> other) {
		wsi = other->wsi;
		queue = other->queue;

		other->wsi = nullptr;
		other->queue = nullptr;
	}

	Websocket(Websocket const &other) = delete;
	Websocket &operator =(Websocket const &other) = delete;

	void init(lws *wsi) {
		this->wsi = wsi;
		queue = new ke::InlineList<VoiceBufferNode>();
	}

	lws *wsi;
	ke::InlineList<VoiceBufferNode> *queue;
};

struct WebsocketHashPolicy
{
	static uint32_t hash(const Websocket &value) {
		return (uint32_t)value.wsi;
	}

	static bool matches(const Websocket &value, const Websocket &key) {
		return (hash(value) == hash(key));
	}
};

typedef ke::HashSet<Websocket, WebsocketHashPolicy> WebsocketSet;
WebsocketSet g_websockets;

void BroadcastVoiceData_Callback(uint64_t steamId, int bytes, const char *data)
{
	DEBUG_LOG(">>> Got %d bytes of voice data from %lld", bytes, steamId);

	if (!data || bytes <= 0) {
		return;
	}

#if 0
	// This is useful for dumping voice data for debugging.
	static int packet = 0;
	sprintf(filename, "voice_%p_%02d.bin", client, packet++);
	file = fopen(filename, "wb");
	fwrite(data, bytes, 1, file);
	fclose(file);
#endif

#if 1
	// We only create the voice buffer if there is at least one client listening.
	VoiceBuffer *buffer = nullptr;
	for (WebsocketSet::iterator i = g_websockets.iter(); !i.empty(); i.next()) {
		if (!buffer) {
			// Ideally, we'd read sv_use_steam_voice and sv_voicecodec here,
			// rather than relying on the user configuring us correctly.
			buffer = new VoiceBuffer(data, bytes, g_configVoiceDataType, steamId);
		}

		// Queue the voice buffer on each socket, the buffer itself is refcounted
		// and freed automatically when it's been sent to every client.
		Websocket &socket = *i;
		socket.queue->append(new VoiceBufferNode(buffer));
		lws_callback_on_writable(socket.wsi);
	}
#endif
}

class IClient;
class CCLCMsg_VoiceData;

DETOUR_DECL_STATIC4(BroadcastVoiceData, void, IClient *, client, int, bytes, char *, data, long long, xuid)
{
	DEBUG_LOG(">>> SV_BroadcastVoiceData(%p, %d, %p, %lld)", client, bytes, data, xuid);

	DETOUR_STATIC_CALL(BroadcastVoiceData)(client, bytes, data, xuid);

#if 0
	// This is useful for getting the correct m_SteamID offset.
	char filename[64];
	sprintf(filename, "voice_%p_client.bin", client);
	FILE *file = fopen(filename, "wb");
	fwrite(client, 1024, 1, file);
	fclose(file);
#endif

	// xuid isn't populated pre-CS:GO, so get the SteamID from the client instead.
	uint64_t steamId = *(uint64_t *)((uintptr_t)client + g_steamIdOffset);

	BroadcastVoiceData_Callback(steamId, bytes, data);
}

#ifdef _WIN32
// This function has been LTCG'd to __fastcall.
DETOUR_DECL_STATIC0(BroadcastVoiceData_CSGO, void)
{
	IClient *client;
	CCLCMsg_VoiceData *message;
	__asm
	{
		mov client, ecx
		mov message, edx
	}

	// Call the original func before logging to try and avoid the registers being overwritten.
	DETOUR_STATIC_CALL(BroadcastVoiceData_CSGO)();
#else
DETOUR_DECL_STATIC2(BroadcastVoiceData_CSGO, void, IClient *, client, CCLCMsg_VoiceData *, message)
{
	DETOUR_STATIC_CALL(BroadcastVoiceData_CSGO)(client, message);
#endif

	DEBUG_LOG(">>> SV_BroadcastVoiceData(%p, %p)", client, message);

	// TODO: Gamedata this.

	// If this breaks on Linux only, check the libstdc++ ABI in use, see comment in AMBuilder.
	std::string *voiceData = *(std::string **)((intptr_t)message + 8);

	// The xuid in the message is helpfully set to the steamid on CS:GO, which makes finding these offsets quite easy.
	uint64_t steamId = *(uint64_t *)((uintptr_t)message + 12);

	BroadcastVoiceData_Callback(steamId, voiceData->size(), voiceData->data());
}

int callback_http(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
{
	switch (reason) {
		case LWS_CALLBACK_HTTP: {
			lws_return_http_status(wsi, HTTP_STATUS_FORBIDDEN, "Websocket connections only kthx.");

			if (lws_http_transaction_completed(wsi))
				return -1;

			return 1;
		}
	}

	return 0;
}

int callback_telephone(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
{
	switch (reason) {
		case LWS_CALLBACK_ESTABLISHED: {
			DEBUG_LOG(">>> Client connected to voice websocket.");

			WebsocketSet::Insert insert = g_websockets.findForAdd(wsi);
			if (!insert.found()) {
				if (!g_websockets.add(insert)) {
					smutils->LogError(myself, "Failed to add voice websocket to active set.");
					break;
				}

				insert->init(wsi);
			}

			break;
		}

		case LWS_CALLBACK_CLOSED: {
			DEBUG_LOG(">>> Client disconnected from voice websocket.");

			WebsocketSet::Result result = g_websockets.find(wsi);
			if (result.found()) {
				g_websockets.remove(result);
			}

			break;
		}

		case LWS_CALLBACK_RECEIVE: {
			DEBUG_LOG(">>> Received message on voice websocket: %s", (char *)in);

			break;
		}

		case LWS_CALLBACK_SERVER_WRITEABLE: {
			DEBUG_LOG(">>> Voice websocket ready for writing.");

			WebsocketSet::Result result = g_websockets.find(wsi);
			if (result.found() && !result->queue->empty()) {
				do {
					DEBUG_LOG(">>> Send voice data to websocket.");
					VoiceBufferNode *bufferNode = *result->queue->begin();
					VoiceBuffer *buffer = *bufferNode;
					lws_write(result->wsi, &buffer->data[LWS_SEND_BUFFER_PRE_PADDING], buffer->length, LWS_WRITE_BINARY);
					result->queue->remove(bufferNode);
					delete bufferNode;
				} while (lws_partial_buffered(result->wsi) != 1 && !result->queue->empty());

				if (!result->queue->empty()) {
					DEBUG_LOG(">>> Websocket buffered, waiting for another writable callback.");
					lws_callback_on_writable(result->wsi);
				}
			}

			break;
		}
	}

	return 0;
}

lws_protocols g_protocols[] = {
	{ "http-only", callback_http, 0, 0 },
	{ "telephone", callback_telephone, 0, 0 },
	{ nullptr, nullptr, 0, 0 },
};

void OnGameFrame(bool simulating)
{
	lws_service(g_websocket, 0);
}

bool Telephone::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	g_websockets.init();

	if (!this->ReloadConfigFile(error, maxlength)) {
		return false;
	}

	if (!gameconfs->LoadGameConfigFile("telephone.games", &g_gameConfig, error, maxlength)) {
		return false;
	}

	CDetourManager::Init(smutils->GetScriptingEngine(), g_gameConfig);

	const char *usesCSGOBroadcastVoiceData = g_gameConfig->GetKeyValue("UsesCSGOBroadcastVoiceData");
	if (usesCSGOBroadcastVoiceData && usesCSGOBroadcastVoiceData[0] == '1') {
		g_detourBroadcastVoiceData = DETOUR_CREATE_STATIC(BroadcastVoiceData_CSGO, "BroadcastVoiceData");
	} else {
		g_detourBroadcastVoiceData = DETOUR_CREATE_STATIC(BroadcastVoiceData, "BroadcastVoiceData");
	}

	if (!g_detourBroadcastVoiceData) {
		gameconfs->CloseGameConfigFile(g_gameConfig);

		strncpy(error, "Error setting up BroadcastVoiceData detour", maxlength);
		return false;
	}

	if (!g_gameConfig->GetOffset("IClient::m_SteamID", &g_steamIdOffset)) {
		smutils->LogMessage(myself, "WARNING: IClient::m_SteamID offset missing from gamedata.");

		// Jump past potential vtable pointers to real data.
		// This only exists to have something hopefully unique as a fallback, and isn't really a steamid.
		g_steamIdOffset = 12;
	}

#ifdef NDEBUG
	lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);
#endif

	lws_context_creation_info websocketParams = {};

	websocketParams.iface = g_configNetworkInterface[0] ? g_configNetworkInterface : nullptr;
	websocketParams.port = g_configNetworkPort;
	websocketParams.protocols = g_protocols;
	websocketParams.gid = -1;
	websocketParams.uid = -1;

	g_websocket = lws_create_context(&websocketParams);
	if (!g_websocket) {
		gameconfs->CloseGameConfigFile(g_gameConfig);

		strncpy(error, "Failed to start websocket server", maxlength);
		return false;
	}

	g_detourBroadcastVoiceData->EnableDetour();

	smutils->AddGameFrameHook(OnGameFrame);

	return true;
}

void Telephone::SDK_OnUnload()
{
	smutils->RemoveGameFrameHook(OnGameFrame);

	lws_context_destroy(g_websocket);

	g_detourBroadcastVoiceData->DisableDetour();

	gameconfs->CloseGameConfigFile(g_gameConfig);
}

void Telephone::OnCoreMapStart(edict_t *pEdictList, int edictCount, int clientMax)
{
	char error[1024];
	if (!this->ReloadConfigFile(error, sizeof(error))) {
		smutils->LogError(myself, "%s", error);
	}
}

bool Telephone::ReadINI_KeyValue(const char *key, const char *value, bool invalid_tokens, bool equal_token, bool quotes, unsigned int *curtok)
{
	if (strcasecmp(key, "interface") == 0) {
		if (value) {
			strncpy(g_configNetworkInterface, value, sizeof(g_configNetworkInterface));
		} else {
			g_configNetworkInterface[0] = '\0';
		}
	} else if (strcasecmp(key, "port") == 0) {
		if (value) {
			g_configNetworkPort = atoi(value);
		} else {
			smutils->LogError(myself, "Invalid port value in Telephone config file");
		}
	} else if (strcasecmp(key, "voice-codec") == 0) {
		if (value && strcasecmp(value, "steam") == 0) {
			g_configVoiceDataType = VoiceDataType_Steam;
		} else if (value && strcasecmp(value, "speex") == 0) {
			g_configVoiceDataType = VoiceDataType_Speex;
		} else if (value && strcasecmp(value, "celt") == 0) {
			g_configVoiceDataType = VoiceDataType_Celt;
		} else {
			smutils->LogError(myself, "Unknown voice-codec value in Telephone config file: %s", value ? value : "(null)");
		}
	} else {
		smutils->LogError(myself, "Unknown key in Telephone config file: %s", key ? key : "(null)");
	}

	return true;
}

bool Telephone::ReloadConfigFile(char *error, size_t maxlength)
{
	char configFilePath[1024];
	smutils->BuildPath(Path_SM, configFilePath, sizeof(configFilePath), "configs/telephone.ini");

	unsigned int line = 0, col = 0;
	if (!textparsers->ParseFile_INI(configFilePath, this, &line, &col)) {
		if (line == 0) {
			strncpy(error, "Telephone config file could not be read", maxlength);
		} else {
			snprintf(error, maxlength, "Failed to parse Telephone config file, error on line %d column %d", line, col);
		}

		return false;
	}

	return true;
}

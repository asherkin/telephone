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

// CS:GO/Protobuf uses std::string to store the voice data.
#include <string>

#ifndef NDEBUG
#define DEBUG_LOG rootconsole->ConsolePrint
#else
#define DEBUG_LOG(...)
#endif

constexpr unsigned char kTelephoneProtocolVersion = 1;

enum TelephoneEvent: unsigned char
{
	TelephoneEvent_VoiceData = 0,
};

enum VoiceDataType: unsigned char
{
	VoiceDataType_Steam = 0,
	VoiceDataType_Miles = 1,
	VoiceDataType_Speex = 2,
	VoiceDataType_Celt  = 3,
};

Telephone g_Telephone;
SMEXT_LINK(&g_Telephone);

IGameConfig *g_gameConfig = nullptr;
int g_steamIdOffset = 0;

CDetour *g_detourBroadcastVoiceData = nullptr;

lws_context *g_lwsContext = nullptr;
lws_vhost *g_lwsDefaultVhost = nullptr;

char g_configNetworkInterface[64] = "127.0.0.1";
short g_configNetworkPort = 9000;
VoiceDataType g_configVoiceDataType = VoiceDataType_Steam;

struct VoiceBuffer: ke::Refcounted<VoiceBuffer>
{
	VoiceBuffer(const void *data, size_t bytes, VoiceDataType voiceDataType, uint64_t steamId) {
		this->length = 1 + 1 + 1 + bytes;

		// The Steam voice data already includes the client's steamid as part of the
		// data stream. For the native engine codecs we need to pack it in ourselves.
		if (voiceDataType != VoiceDataType_Steam) {
			this->length += sizeof(steamId);
		}

		this->data = new unsigned char[LWS_PRE + this->length];
		size_t cursor = LWS_PRE;

		this->data[cursor] = kTelephoneProtocolVersion;
		cursor += 1;

		this->data[cursor] = TelephoneEvent_VoiceData;
		cursor += 1;

		this->data[cursor] = voiceDataType;
		cursor += 1;

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

void BroadcastVoiceData_Callback(uint64_t steamId, int bytes, const char *data, bool forceSteamVoice)
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
			VoiceDataType voiceDataType = forceSteamVoice ? VoiceDataType_Steam : g_configVoiceDataType;
			buffer = new VoiceBuffer(data, bytes, voiceDataType, steamId);
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

	// xuid isn't populated pre-CS:GO/Protobuf, so get the SteamID from the client instead.
	uint64_t steamId = *(uint64_t *)((uintptr_t)client + g_steamIdOffset);

	BroadcastVoiceData_Callback(steamId, bytes, data, false);
}

#ifdef _WIN32
// This function has been LTCG'd to __fastcall.
DETOUR_DECL_STATIC0(BroadcastVoiceData_Protobuf, void)
{
	IClient *client;
	CCLCMsg_VoiceData *message;
	__asm
	{
		mov client, ecx
		mov message, edx
	}

	// Call the original func before logging to try and avoid the registers being overwritten.
	DETOUR_STATIC_CALL(BroadcastVoiceData_Protobuf)();
#else
DETOUR_DECL_STATIC2(BroadcastVoiceData_Protobuf, void, IClient *, client, CCLCMsg_VoiceData *, message)
{
	DETOUR_STATIC_CALL(BroadcastVoiceData_Protobuf)(client, message);
#endif

	DEBUG_LOG(">>> SV_BroadcastVoiceData(%p, %p)", client, message);

	// TODO: Gamedata this.

	// If this breaks on Linux only, check the libstdc++ ABI in use, see comment in AMBuilder.
	std::string *voiceData = *(std::string **)((uintptr_t)message + 8);

	// The xuid in the message is helpfully set to the steamid on CS:GO/Protobuf, which makes finding these offsets quite easy.
	uint64_t steamId = *(uint64_t *)((uintptr_t)message + 12);

	// CS:GO/Protobuf allows individual clients to choose between Steam or Engine voice encoding, so this can differ per-packet.
	// Note: this is different from our enum, here 0 = steam, 1 = engine (sv_voicecodec).
	int voiceFormat = *(int *)((uintptr_t)message + 20);

	bool forceSteamVoice = (voiceFormat == 0);
	BroadcastVoiceData_Callback(steamId, voiceData->size(), voiceData->data(), forceSteamVoice);
}

int OnWebsocketCallback(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
{
	switch (reason) {
		case LWS_CALLBACK_HTTP: {
			lws_return_http_status(wsi, HTTP_STATUS_FORBIDDEN, "Websocket connections only kthx.");

			if (lws_http_transaction_completed(wsi))
				return -1;

			return 1;
		}

		case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
			DEBUG_LOG(">>> Client failed to connect: %s", in ? (const char *)in : "unknown error");
			break;
		}

		case LWS_CALLBACK_CLIENT_ESTABLISHED:
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

		case LWS_CALLBACK_CLIENT_RECEIVE:
		case LWS_CALLBACK_RECEIVE: {
			DEBUG_LOG(">>> Received message on voice websocket: %s", (char *)in);

			break;
		}

		case LWS_CALLBACK_CLIENT_WRITEABLE:
		case LWS_CALLBACK_SERVER_WRITEABLE: {
			DEBUG_LOG(">>> Voice websocket ready for writing.");

			WebsocketSet::Result result = g_websockets.find(wsi);
			if (result.found() && !result->queue->empty()) {
				do {
					DEBUG_LOG(">>> Send voice data to websocket.");
					VoiceBufferNode *bufferNode = *result->queue->begin();
					VoiceBuffer *buffer = *bufferNode;
					lws_write(result->wsi, &buffer->data[LWS_PRE], buffer->length, LWS_WRITE_BINARY);
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

		// Callbacks we might want to handle.
		case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH: //2
		case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: //24
			break;

		// Callback that are noise.
		case LWS_CALLBACK_PROTOCOL_INIT: //27
		case LWS_CALLBACK_PROTOCOL_DESTROY: //28
		case LWS_CALLBACK_WSI_CREATE: //29
		case LWS_CALLBACK_WSI_DESTROY: //30
		case LWS_CALLBACK_GET_THREAD_ID: //31
		case LWS_CALLBACK_ADD_POLL_FD: //32
		case LWS_CALLBACK_DEL_POLL_FD: //33
		case LWS_CALLBACK_CHANGE_MODE_POLL_FD: //34
		case LWS_CALLBACK_LOCK_POLL: //35
		case LWS_CALLBACK_UNLOCK_POLL: //36
			break;

		default:
			DEBUG_LOG(">>> Unhandled LWS callback: %d", reason);
	}

	return 0;
}

lws_protocols g_protocols[] = {
	{ "telephone", OnWebsocketCallback, 0, 0 },
	{ nullptr,     nullptr,             0, 0 },
};

void OnGameFrame(bool simulating)
{
	lws_service(g_lwsContext, 0);
}

void CreateWebsocketServer()
{
	lws_context_creation_info serverParams = {};
	serverParams.vhost_name = "test-server";
	serverParams.iface = g_configNetworkInterface[0] ? g_configNetworkInterface : nullptr;
	serverParams.port = g_configNetworkPort;
	serverParams.protocols = g_protocols;

	lws_create_vhost(g_lwsContext, &serverParams);
}

void CreateWebsocketClient()
{
	lws_client_connect_info clientParams = {};
	clientParams.context = g_lwsContext;
	clientParams.vhost = g_lwsDefaultVhost;
	clientParams.address = "127.0.0.1"; //g_configNetworkInterface;
	clientParams.port = 9001; //g_configNetworkPort;
	clientParams.path = "/";
	clientParams.host = "127.0.0.1";
	clientParams.origin = "csgo://192.168.0.7:27015";

	lws_client_connect_via_info(&clientParams);
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

	const char *usesProtobufBroadcastVoiceData = g_gameConfig->GetKeyValue("UsesProtobufBroadcastVoiceData");
	if (usesProtobufBroadcastVoiceData && usesProtobufBroadcastVoiceData[0] == '1') {
		g_detourBroadcastVoiceData = DETOUR_CREATE_STATIC(BroadcastVoiceData_Protobuf, "BroadcastVoiceData");
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

	lws_context_creation_info contextParams = {};
	contextParams.options = LWS_SERVER_OPTION_SKIP_SERVER_CANONICAL_NAME | LWS_SERVER_OPTION_EXPLICIT_VHOSTS | LWS_SERVER_OPTION_JUST_USE_RAW_ORIGIN;
	contextParams.gid = -1;
	contextParams.uid = -1;

	g_lwsContext = lws_create_context(&contextParams);
	if (!g_lwsContext) {
		gameconfs->CloseGameConfigFile(g_gameConfig);

		strncpy(error, "Failed to create websocket context", maxlength);
		return false;
	}

	lws_context_creation_info vhostParams = {};
	vhostParams.vhost_name = "telephone";
	vhostParams.port = CONTEXT_PORT_NO_LISTEN;
	vhostParams.protocols = g_protocols;

	g_lwsDefaultVhost = lws_create_vhost(g_lwsContext, &vhostParams);
	if (!g_lwsDefaultVhost) {
		gameconfs->CloseGameConfigFile(g_gameConfig);

		strncpy(error, "Failed to create websocket vhost", maxlength);
		return false;
	}

	//CreateWebsocketServer();
	CreateWebsocketClient();

	g_detourBroadcastVoiceData->EnableDetour();

	smutils->AddGameFrameHook(OnGameFrame);

	return true;
}

void Telephone::SDK_OnUnload()
{
	smutils->RemoveGameFrameHook(OnGameFrame);

	lws_context_destroy(g_lwsContext);

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

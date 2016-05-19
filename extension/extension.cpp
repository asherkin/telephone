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

enum netadrtype_t
{
	NA_NULL = 0,
	NA_LOOPBACK,
	NA_BROADCAST,
	NA_IP,
};

struct netadr_t
{
	netadrtype_t type;
	unsigned char ip[4];
	unsigned short port;
};

struct VoiceBuffer: ke::Refcounted<VoiceBuffer>
{
	VoiceBuffer(void *data, size_t bytes): length(bytes) {
		this->data = new unsigned char[LWS_SEND_BUFFER_PRE_PADDING + bytes + LWS_SEND_BUFFER_POST_PADDING];
		memcpy(&this->data[LWS_SEND_BUFFER_PRE_PADDING], data, bytes);
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
WebsocketSet websockets;

class IClient;

DETOUR_DECL_STATIC4(BroadcastVoiceData, void, IClient *, client, int, bytes, char *, data, long long, xuid)
{
	DEBUG_LOG(">>> SV_BroadcastVoiceData(%p, %d, %p, %lld)", client, bytes, data, xuid);

#if 1
	// Get it to the other in-game players first.
	DETOUR_STATIC_CALL(BroadcastVoiceData)(client, bytes, data, xuid);
#endif

#if 0
	static int packet = 0;
	char filename[64];
	sprintf(filename, "voice_%02d.dat", packet++);
	FILE *file = fopen(filename, "wb");
	fwrite(data, bytes, 1, file);
	fclose(file);
#endif

#if 1
	VoiceBuffer *buffer = nullptr;
	for (WebsocketSet::iterator i = websockets.iter(); !i.empty(); i.next()) {
		if (!buffer) {
			buffer = new VoiceBuffer(data, bytes);
		}

		Websocket &socket = *i;
		socket.queue->append(new VoiceBufferNode(buffer));
		lws_callback_on_writable(socket.wsi);
	}
#endif
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

int callback_telephone(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
{
	switch (reason) {
		case LWS_CALLBACK_ESTABLISHED: {
			DEBUG_LOG(">>> Client connected to voice websocket.");

			WebsocketSet::Insert insert = websockets.findForAdd(wsi);
			if (!insert.found()) {
				if (!websockets.add(insert)) {
					smutils->LogError(myself, "Failed to add voice websocket to active set.");
					break;
				}

				insert->init(wsi);
			}

			break;
		}

		case LWS_CALLBACK_CLOSED: {
			DEBUG_LOG(">>> Client disconnected from voice websocket.");

			WebsocketSet::Result result = websockets.find(wsi);
			if (result.found()) {
				websockets.remove(result);
			}

			break;
		}

		case LWS_CALLBACK_RECEIVE: {
			DEBUG_LOG(">>> Received message on voice websocket: %s", (char *)in);

			break;
		}

		case LWS_CALLBACK_SERVER_WRITEABLE: {
			DEBUG_LOG(">>> Voice websocket ready for writing.");

			WebsocketSet::Result result = websockets.find(wsi);
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

lws_protocols protocols[] = {
	{ "http-only", callback_http, 0, 0 },
	{ "telephone", callback_telephone, 0, 0 },
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

	detourBroadcastVoiceData = DETOUR_CREATE_STATIC(BroadcastVoiceData, "BroadcastVoiceData");
	if (!detourBroadcastVoiceData) {
		gameconfs->CloseGameConfigFile(gameConfig);

		strncpy(error, "Error setting up BroadcastVoiceData detour", maxlength);
		return false;
	}

	netadr_t *net_local_adr = nullptr;
	gameConfig->GetAddress("net_local_adr", (void **)&net_local_adr);

#ifdef NDEBUG
	lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);
#endif

	lws_context_creation_info websocketParams = {};

	if (net_local_adr) {
		char ifaceIp[16];
		smutils->Format(ifaceIp, sizeof(ifaceIp), "%u.%u.%u.%u", net_local_adr->ip[0], net_local_adr->ip[1], net_local_adr->ip[2], net_local_adr->ip[3]);
		DEBUG_LOG(">>> Got host ip: %s", ifaceIp);
		websocketParams.iface = ifaceIp;
	} else {
		smutils->LogError(myself, "WARNING: Failed to find net_local_adr, will bind to all interfaces.");
	}

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

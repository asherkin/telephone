#include <cstdio>

#ifdef strcasecmp
#undef strcasecmp
#endif
#include <libwebsockets.h>

#ifndef NDEBUG
#define DEBUG_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_LOG(...) do {} while(0)
#endif

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

constexpr int kServiceTimeout = 250;

bool g_wantsExit = false;

int callback_telephone(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
{
	switch (reason) {
		case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
			DEBUG_LOG(">>> Connection to voice websocket failed: %s\n", (const char *)in);
			g_wantsExit = true;
			break;
		}
		case LWS_CALLBACK_CLIENT_ESTABLISHED: {
			DEBUG_LOG(">>> Connection established to voice websocket.\n");
			break;
		}
		case LWS_CALLBACK_CLOSED: {
			DEBUG_LOG(">>> Connection to voice websocket closed.\n");
			g_wantsExit = true;
			break;
		}
		case LWS_CALLBACK_CLIENT_RECEIVE: {
			DEBUG_LOG(">>> Data received on voice websocket. (%u bytes)\n", len);
			break;
		}
	}

	return 0;
}

lws_protocols protocols[] = {
	{ "telephone", callback_telephone, 0, 0 },
	{ nullptr, nullptr, 0, 0 },
};

int main(int argc, char *argv[]) {
#ifdef NDEBUG
	lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);
#endif

	lws_context_creation_info websocketParams = {};

	websocketParams.iface = nullptr;
	websocketParams.port = CONTEXT_PORT_NO_LISTEN;
	websocketParams.protocols = protocols;
	websocketParams.gid = -1;
	websocketParams.uid = -1;

	lws_context *wsContext = lws_create_context(&websocketParams);
	if (!wsContext) {
		fprintf(stderr, "Failed to start websocket server\n");
		return 1;
	}

	lws *ws = lws_client_connect(wsContext, "192.168.0.5", 9000, 0, "/", "192.168.0.5:9000", nullptr, "telephone", -1);

	while(!g_wantsExit) {
		lws_service(wsContext, kServiceTimeout);
	}

	lws_context_destroy(wsContext);

	return 0;
}

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include <amtl/am-utility.h>
#include <amtl/am-hashmap.h>

#ifdef strcasecmp
#undef strcasecmp
#endif
#include <libwebsockets.h>

#include <celt.h>

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

constexpr unsigned kCeltSampleRate = 22050;
constexpr unsigned kCeltRawFrameSize = 512;
constexpr unsigned kCeltPacketSize = 64;

bool g_wantsExit = false;

class CeltDecoder {
public:
	CeltDecoder() {
		mode_ = nullptr;
		decoder_ = nullptr;
	}

	~CeltDecoder() {
		if (decoder_) {
			celt_decoder_destroy(decoder_);
		}

		if (mode_) {
			celt_mode_destroy(mode_);
		}
	}

	bool Init() {
		int error = 0;
		mode_ = celt_mode_create(kCeltSampleRate, kCeltRawFrameSize, &error);
		if (error != CELT_OK) {
			fprintf(stderr, "celt_mode_create error: %s (%d)\n", celt_strerror(error), error);
			return false;
		}

		error = 0;
		decoder_ = celt_decoder_create_custom(mode_, 1, &error);
		if (error != CELT_OK) {
			fprintf(stderr, "celt_decoder_create_custom error: %s (%d)\n", celt_strerror(error), error);
			return false;
		}

		celt_decoder_ctl(decoder_, CELT_RESET_STATE_REQUEST, nullptr);

		return true;
	}

	bool Decode(const uint8_t *in, size_t inLen, int16_t *out, size_t &outLen) {
		if (inLen != kCeltPacketSize) {
			fprintf(stderr, "Bad input length passed to CeltDecoder::Decode\n");
			return false;
		}

		if (outLen < kCeltRawFrameSize) {
			fprintf(stderr, "Bad output length passed to CeltDecoder::Decode\n");
			return false;
		}

		int decodeRet = celt_decode(decoder_, in, inLen, out, outLen);
		if (decodeRet < CELT_OK) {
			fprintf(stderr, "celt_decode error: %s (%d)\n", celt_strerror(decodeRet), decodeRet);
			return false;
		}

		outLen = decodeRet;
		return true;
	}
	
	CELTMode *mode_;
	CELTDecoder *decoder_;
};

struct SteamIdPolicy
{
	static inline uint32_t hash(uint64_t s) {
		return ke::HashInt64(*reinterpret_cast<int64_t *>(&s));
	}
	static inline bool matches(uint64_t s1, uint64_t s2) {
		return s1 == s2;
	}
};

ke::HashMap<uint64_t, ke::AutoPtr<CeltDecoder>, SteamIdPolicy> g_decoders;

CeltDecoder *GetDecoder(uint64_t steamId) {
	auto result = g_decoders.findForAdd(steamId);
	if (result.found()) {
		fprintf(stderr, "Returning existing decoder for %llu\n", steamId);
		return (*result).value;
	}

	ke::AutoPtr<CeltDecoder> decoder(new CeltDecoder());
	if (!decoder->Init()) {
		fprintf(stderr, "Failed to create decoder for %llu\n", steamId);
		delete decoder;
		return nullptr;
	}

	CeltDecoder *retDecoder = decoder;
	g_decoders.add(result, steamId, ke::Move(decoder));
	fprintf(stderr, "Returning newly created decoder for %llu\n", steamId);
	return retDecoder;
}

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

			if (len < 1) {
				fprintf(stderr, "Not enough data (event)\n");
				break;
			}

			uint8_t *data = (uint8_t *)in;

			if (data[0] != TelephoneEvent_VoiceData) {
				DEBUG_LOG("Unknown event type %u\n", data[0]);
				break;
			}

			if (len < 10) {
				fprintf(stderr, "Not enough data (voice)\n");
				break;
			}

			if (data[1] != VoiceDataType_Celt) {
				fprintf(stderr, "Unsupported voice data type %u\n", data[1]);
				break;
			}

			uint64_t steamId;
			memcpy(&steamId, &data[2], sizeof(steamId));
			DEBUG_LOG("Got voice data from %llu\n", steamId);
			
			uint8_t *voiceData = &data[10];
			size_t voiceDataLen = len - 10;

			if ((voiceDataLen % kCeltPacketSize) != 0) {
				fprintf(stderr, "CELT payload not a multiple of packet size\n");
				break;
			}

			CeltDecoder *decoder = GetDecoder(steamId);
			if (!decoder) {
				fprintf(stderr, "Failed to get decoder for %llu\n", steamId);
				break;
			}

			for (size_t i = 0; i < voiceDataLen; i += kCeltPacketSize) {
				int16_t buffer[kCeltRawFrameSize];
				size_t bufferLen = kCeltRawFrameSize;
				if (!decoder->Decode(&voiceData[i], kCeltPacketSize, buffer, bufferLen)) {
					continue;
				}

				fwrite(buffer, sizeof(int16_t), bufferLen, stdout);
				DEBUG_LOG("Wrote %u samples of decompressed voice data\n", bufferLen);
			}

			break;
		}
	}

	return 0;
}

lws_protocols protocols[] = {
	{ "telephone", callback_telephone, 0, 0 },
	{ nullptr, nullptr, 0, 0 },
};

// native-client\telephone-client\telephone-client.exe | D:\sox\sox.exe -t raw -r 22050 -e signed -b 16 -c 1 - -t waveaudio

int main(int argc, char *argv[]) {
#ifdef _WIN32
	_setmode(fileno(stdout), _O_BINARY);
#endif

	g_decoders.init();

#ifdef NDEBUG
	lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);
#endif

	lws_context_creation_info websocketParams = {};

	websocketParams.options = LWS_SERVER_OPTION_SKIP_SERVER_CANONICAL_NAME | LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_SERVER_OPTION_JUST_USE_RAW_ORIGIN;
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

	lws_client_connect_info websocketConnectInfo = {};
	websocketConnectInfo.context = wsContext;
	websocketConnectInfo.address = "192.168.0.5";
	websocketConnectInfo.port = 9000;
	websocketConnectInfo.path = "/";
	websocketConnectInfo.host = "192.168.0.5:9000";
	websocketConnectInfo.protocol = "telephone";

	lws *ws = lws_client_connect_via_info(&websocketConnectInfo);

	while(!g_wantsExit) {
		lws_service(wsContext, kServiceTimeout);
	}

	lws_context_destroy(wsContext);

	return 0;
}

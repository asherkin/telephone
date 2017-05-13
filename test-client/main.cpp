#include <iostream>
#include <memory.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "../decoders/celt/src/celt.h"

// ./telephone-test-client 2>/dev/null | play -t raw -r 22050 -e signed -b 16 -c 1 -
// ./telephone-test-client 2>/dev/null | ffplay -nodisp -autoexit -f s16le -ar 22050 -ac 1 -

int main(int argc, char *argv[]) {
	constexpr int sampleRate = 22050;
	constexpr int rawFrameSize = 512;
	constexpr int packetSize = 64;

#ifdef _WIN32
	setmode(fileno(stdout), O_BINARY);
#endif

	int error = 0;
	auto mode = celt_mode_create(sampleRate, rawFrameSize, &error);
	if (error != CELT_OK) {
		fprintf(stderr, "celt_mode_create error: %s (%d)\n", celt_strerror(error), error);
		return 1;
	}

	error = 0;
	auto decoderState = celt_decoder_create_custom(mode, 1, &error);
	if (error != CELT_OK) {
		fprintf(stderr, "celt_decoder_create_custom error: %s (%d)\n", celt_strerror(error), error);
		return 1;
	}

	celt_decoder_ctl(decoderState, CELT_RESET_STATE_REQUEST, NULL);

	int i = 0;
	//const char *format = "D:/Code/telephone/test-client/data/voice_0B7A3E14_%02d.bin";
	const char *format = "D:/Code/telephone/test-client/tf2_data/voice_%02d.bin";
	while(true) {
		char filename[512];
		snprintf(filename, sizeof(filename), format, i);
		FILE *file = fopen(filename, "rb");
		if (!file) {
			break;
		}

		fprintf(stderr, "Reading voice packet %d...\n", i);

		while (true) {
			char data[packetSize];
			size_t length = fread(data, packetSize, 1, file);
			if (length != 1) {
				if (feof(file)) fprintf(stderr, "Reached end of file.\n");
				if (ferror(file)) fprintf(stderr, "Error reading file.\n");
				break;
			}

			// Valve code does something like this before decoding, but it seems to produce identical output...
			// unsigneddata[j] = (data[j] < 0) ? (data[j] + 256) : data[j];

			celt_int16 decompressed[rawFrameSize];
			auto numSamples = celt_decode(decoderState, (unsigned char *)data, packetSize, decompressed, rawFrameSize);
			if (numSamples < CELT_OK) {
				fprintf(stderr, "celt_decode error: %s (%d)\n", celt_strerror(numSamples), numSamples);
				memset(decompressed, 0, sizeof(decompressed));
				fwrite(decompressed, sizeof(celt_int16), rawFrameSize, stdout);
				continue;
			};

			fprintf(stderr, "Decoded %d samples.\n", numSamples);

			fwrite(decompressed, sizeof(celt_int16), numSamples, stdout);
		}

		fclose(file);
		i++;
	}

	celt_decoder_destroy(decoderState);

	celt_mode_destroy(mode);
}

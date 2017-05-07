#include <iostream>

#include "../decoders/celt/src/celt.h"

int main(int argc, char *argv[]) {
	constexpr int sampleRate = 22050;
	constexpr int rawFrameSize = 512;
	constexpr int packetSize = 64;

	int error = 0;
	auto mode = celt_mode_create(sampleRate, rawFrameSize, &error);
	if (error != CELT_OK) {
		printf("celt_mode_create error: %s (%d)\n", celt_strerror(error), error);
		return 1;
	}

	error = 0;
	auto decoderState = celt_decoder_create_custom(mode, 1, &error);
	if (error != CELT_OK) {
		printf("celt_decoder_create_custom error: %s (%d)\n", celt_strerror(error), error);
		return 1;
	}

	celt_decoder_ctl(decoderState, CELT_RESET_STATE_REQUEST, NULL);

	FILE *outputFile = fopen("output.wav", "wb");
	if (!outputFile) {
		printf("Failed to open output file.\n");
		return 1;
	}

	unsigned char wavHeader[] = {
		0x52, 0x49, 0x46, 0x46, 0xFF, 0xFF, 0xFF, 0xFF, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20,
		0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x22, 0x56, 0x00, 0x00, 0x44, 0xAC, 0x00, 0x00,
		0x02, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0xFF, 0xFF, 0xFF, 0xFF,
	};

	fwrite(wavHeader, 1, sizeof(wavHeader), outputFile);

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

		printf("Reading voice packet %d...\n", i);

		while (true) {
			char data[packetSize];
			size_t length = fread(data, packetSize, 1, file);
			if (length != 1) {
				if (feof(file)) printf("Reached end of file.\n");
				if (ferror(file)) printf("Error reading file.\n");
				break;
			}

			// Valve code does something like this before decoding, but it seems to produce identical output...
			// unsigneddata[j] = (data[j] < 0) ? (data[j] + 256) : data[j];

			celt_int16 decompressed[rawFrameSize];
			auto numSamples = celt_decode(decoderState, (unsigned char *)data, packetSize, decompressed, rawFrameSize);
			if (numSamples < CELT_OK) {
				printf("celt_decod error: %s (%d)\n", celt_strerror(numSamples), numSamples);
				memset(decompressed, 0, sizeof(decompressed));
				fwrite(decompressed, sizeof(celt_int16), rawFrameSize, outputFile);
				continue;
			};

			printf("Decoded %d samples.\n", numSamples);

			fwrite(decompressed, sizeof(celt_int16), numSamples, outputFile);
		}

		fclose(file);
		i++;
	}

	size_t outputSize = ftell(outputFile);
	fseek(outputFile, sizeof(uint32_t), SEEK_SET);
	uint32_t outputFileSizeMinusRIFFHeader = outputSize - (sizeof(uint32_t) * 2);
	fwrite(&outputFileSizeMinusRIFFHeader, sizeof(uint32_t), 1, outputFile);
	fseek(outputFile, sizeof(wavHeader) - sizeof(uint32_t), SEEK_SET);
	uint32_t outputFileSizeMinusHeader = outputSize - sizeof(wavHeader);
	fwrite(&outputFileSizeMinusHeader, sizeof(uint32_t), 1, outputFile);

	fclose(outputFile);

	celt_decoder_destroy(decoderState);

	celt_mode_destroy(mode);
}

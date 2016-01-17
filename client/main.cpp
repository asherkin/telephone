#include <SKP_Silk_SDK_API.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

uint32_t crc32_16bytes(const void* data, size_t length, uint32_t previousCrc32 = 0);

int main(int argc, char *argv[]) {
  int decoderSize;
  SKP_Silk_SDK_Get_Decoder_Size(&decoderSize);

  void *decoderState = malloc(decoderSize);
  SKP_Silk_SDK_InitDecoder(decoderState);

  int packet = 0;
  char filename[64];

  FILE *outputFile = fopen("voice.dat", "wb");

  while (true) {
  	sprintf(filename, "voice_%02d.dat", packet++);
  	FILE *file = fopen(filename, "rb");
    if (!file) {
      break;
    }

    printf("Loaded packet %d...\n", packet);

    fseek(file, -((long)sizeof(uint32_t)), SEEK_END);

    size_t bodySize = ftell(file);
    printf("Body size: %ld\n", bodySize);

    fseek(file, 0, SEEK_SET);

    void *bodyData = malloc(bodySize);
    fread(bodyData, bodySize, 1, file);

    uint32_t crc;
    fread(&crc, sizeof(crc), 1, file);

    bool crcGood = (crc32_16bytes(bodyData, bodySize) == crc);
    printf("CRC32: %u (%s)\n", crc, crcGood ? "GOOD" : "BAD");

    fclose(file);

    if (!crcGood) {
      free(bodyData);
      continue;
    }

    uint64_t steamid = *(uint64_t *)bodyData;
    printf("SteamID: %llu\n", steamid);

    long packetSize = bodySize - sizeof(steamid);
    void *packetData = (char *)bodyData + sizeof(steamid);

    uint16_t sampleRate;

    while (packetSize > 0) {
      uint8_t payloadType = *(uint8_t *)packetData;
      packetSize -= sizeof(payloadType);
      packetData = (char *)packetData + sizeof(payloadType);

      switch (payloadType) {
        case 0: { // Silence
          printf("Silence payload type not implemented!\n");

          uint16_t numSamples = *(uint16_t *)packetData;
          packetSize -= sizeof(numSamples);
          packetData = (char *)packetData + sizeof(numSamples);
          break;
        }

        case 1: // Unknown codec.
        case 2: // Speex.
        case 3: // Uncompressed.
        case 4: { // Silk.
          uint16_t voiceDataSizeTemp = *(uint16_t *)packetData;
          packetSize -= sizeof(voiceDataSizeTemp);
          packetData = (char *)packetData + sizeof(voiceDataSizeTemp);

          void *voiceData = packetData;
          packetSize -= voiceDataSizeTemp;
          packetData = (char *)packetData + voiceDataSizeTemp;

          printf("Got %u bytes of type %u voice data.\n", voiceDataSizeTemp, payloadType);

          if (payloadType != 4) {
            printf("Only the SILK codec is supported.\n");
            break;
          }

          int16_t uncompressedSamples[8192];
          int16_t numUncompressedSamples = 0;
          int16_t maxUncompressedSamples = sizeof(uncompressedSamples) / sizeof(int16_t);

          long voiceDataSize = voiceDataSizeTemp;
          while (voiceDataSize > 0) {
            int16_t voiceChunkSize = *(int16_t *)voiceData;
            voiceDataSize -= sizeof(voiceChunkSize);
            voiceData = (char *)voiceData + sizeof(voiceChunkSize);

            if (voiceChunkSize == -1) {
              printf("Resetting SILK decoder state.\n");
              SKP_Silk_SDK_InitDecoder(decoderState);
              continue;
            } else if (voiceChunkSize == 0) {
              printf("Got empty SILK payload.\n");
              continue;
            }

            void *voiceChunk = voiceData;
            voiceDataSize -= voiceChunkSize;
            voiceData = (char *)voiceData + voiceChunkSize;

            printf("Got %d bytes of SILK payload.\n", voiceChunkSize);

            SKP_SILK_SDK_DecControlStruct decoderControl;
            decoderControl.API_sampleRate = sampleRate;

            do {
              int16_t samples = maxUncompressedSamples - numUncompressedSamples;
              int ret = SKP_Silk_SDK_Decode(decoderState, &decoderControl, 0, (uint8_t *)voiceChunk, voiceChunkSize, &uncompressedSamples[numUncompressedSamples], &samples);
              if (ret != SKP_SILK_NO_ERROR) {
                printf("Failed to decode voice data! (%d)\n", ret);
                break;
              }
              numUncompressedSamples += samples;
            } while (decoderControl.moreInternalDecoderFrames != 0);
          }
          if (voiceDataSize != 0) {
            printf("Extra bytes at end of voice data!\n");
            packetSize = -1; // Bail.
          }

          printf("Decoded %d samples of voice data.\n", numUncompressedSamples);
          fwrite(uncompressedSamples, sizeof(int16_t), numUncompressedSamples, outputFile);
          break;
        }

        case 10: {
          printf("Payload type 10 not implemented!\n");

          uint8_t unk1 = *(uint8_t *)packetData;
          packetSize -= sizeof(unk1);
          packetData = (char *)packetData + sizeof(unk1);

          uint8_t unk2 = *(uint8_t *)packetData;
          packetSize -= sizeof(unk2);
          packetData = (char *)packetData + sizeof(unk2);
          break;
        }

        case 11: {
          sampleRate = *(uint16_t *)packetData;
          packetSize -= sizeof(sampleRate);
          packetData = (char *)packetData + sizeof(sampleRate);

          printf("Got sample rate: %u\n", sampleRate);
          break;
        }

        default: {
          printf("Unknown payload type: %u\n", payloadType);
          packetSize = -1; // Bail.
          break;
        }
      }
    }
    if (packetSize != 0) {
      printf("Extra bytes at end of packet!\n");
    }

    free(bodyData);
  }

  printf("%d total packets.\n", packet - 1);

  fclose(outputFile);
  free(decoderState);
  return 0;
}

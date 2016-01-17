#include <SKP_Silk_SDK_API.h>

#include <SDL.h>
#include <SDL_audio.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

uint32_t crc32_16bytes(const void* data, size_t length, uint32_t previousCrc32 = 0);

int main(int argc, char *argv[]) {
  int decoderSize;
  SKP_Silk_SDK_Get_Decoder_Size(&decoderSize);

  void *decoderState = malloc(decoderSize);
  if (SKP_Silk_SDK_InitDecoder(decoderState) != 0) {
    printf("Failed to init SILK decoder.\n");
    return 1;
  }

  if (SDL_Init(SDL_INIT_AUDIO) != 0) {
    free(decoderState);
    printf("Failed to init SDL: %s\n", SDL_GetError());
    return 1;
  }

  SDL_AudioSpec audioSpec;
  SDL_memset(&audioSpec, 0, sizeof(audioSpec));
  audioSpec.freq = 16000;
  audioSpec.format = AUDIO_S16LSB;
  audioSpec.channels = 1;
  audioSpec.samples = 8192;

  SDL_AudioDeviceID audioDevice = SDL_OpenAudioDevice(NULL, 0, &audioSpec, NULL, 0);
  if (audioDevice == 0) {
    free(decoderState);
    printf("Failed to open SDL audio device: %s\n", SDL_GetError());
    return 1;
  }

  SDL_PauseAudioDevice(audioDevice, 0);

  int packet = 0;
  char filename[64];

  FILE *outputFile = fopen("voice.wav", "wb");

  unsigned char wavHeader[] = {
    0x52, 0x49, 0x46, 0x46, 0xFF, 0xFF, 0xFF, 0xFF, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20,
    0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80, 0x3E, 0x00, 0x00, 0x00, 0x7D, 0x00, 0x00,
    0x02, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0xFF, 0xFF, 0xFF, 0xFF,
  };

  fwrite(wavHeader, 1, sizeof(wavHeader), outputFile);

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

          if (SDL_QueueAudio(audioDevice, uncompressedSamples, numUncompressedSamples * sizeof(int16_t)) == 0) {
            printf("Queued audio for playback.\n");
          } else {
            printf("Failed to queue audio for playback: %s\n", SDL_GetError());
          }

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

  size_t outputSize = ftell(outputFile);
  fseek(outputFile, sizeof(uint32_t), SEEK_SET);
  uint32_t outputFileSizeMinusRIFFHeader = outputSize - (sizeof(uint32_t) * 2);
  fwrite(&outputFileSizeMinusRIFFHeader, sizeof(uint32_t), 1, outputFile);
  fseek(outputFile, sizeof(wavHeader) - sizeof(uint32_t), SEEK_SET);
  uint32_t outputFileSizeMinusHeader = outputSize - sizeof(wavHeader);
  fwrite(&outputFileSizeMinusHeader, sizeof(uint32_t), 1, outputFile);

  fclose(outputFile);

  free(decoderState);

  while (SDL_GetQueuedAudioSize(audioDevice) > 0) {
    SDL_Delay(40); // 2x the SILK audio segment size.
  }

  SDL_CloseAudioDevice(audioDevice);

  return 0;
}

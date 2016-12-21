var Module = {};
importScripts('decoders.js');

// void *malloc(size_t size);
var malloc = Module.cwrap('malloc', 'number', ['number']);

// void free(void *ptr);
var free = Module.cwrap('free', null, ['number']);

// uint32_t crc32_16bytes(const void *data, size_t length, uint32_t previousCrc32 = 0);
var crc32_16bytes = Module.cwrap('crc32_16bytes', 'number', ['number', 'number', 'number']);

// SKP_int SKP_Silk_SDK_Get_Decoder_Size(SKP_int32 *decSizeBytes);
var SKP_Silk_SDK_Get_Decoder_Size = Module.cwrap('SKP_Silk_SDK_Get_Decoder_Size', 'number', ['number']);

// SKP_int SKP_Silk_SDK_InitDecoder(void *decState);
var SKP_Silk_SDK_InitDecoder = Module.cwrap('SKP_Silk_SDK_InitDecoder', 'number', ['number']);

// SKP_int SKP_Silk_SDK_Decode(void *decState, SKP_SILK_SDK_DecControlStruct *decControl, SKP_int lostFlag, const SKP_uint8 *inData, const SKP_int nBytesIn, SKP_int16 *samplesOut, SKP_int16*nSamplesOut);
var SKP_Silk_SDK_Decode = Module.cwrap('SKP_Silk_SDK_Decode', 'number', ['number', 'number', 'number', 'number', 'number', 'number', 'number']);

// void speex_bits_init(SpeexBits *bits);
var speex_bits_init = Module.cwrap('speex_bits_init', null, ['number']);

// void speex_bits_read_from(SpeexBits *bits, const char *bytes, int len);
var speex_bits_read_from = Module.cwrap('speex_bits_read_from', null, ['number', 'number', 'number']);

// void speex_bits_destroy(SpeexBits *bits);
var speex_bits_destroy = Module.cwrap('speex_bits_destroy', null, ['number']);

// void *speex_decoder_init(const SpeexMode *mode);
var speex_decoder_init = Module.cwrap('speex_decoder_init', 'number', ['number']);

// void speex_decoder_destroy(void *state);
var speex_decoder_destroy = Module.cwrap('speex_decoder_destroy', null, ['number']);

// int speex_decode(void *state, SpeexBits *bits, float *out);
var speex_decode = Module.cwrap('speex_decode', 'number', ['number', 'number', 'number']);

// int speex_decoder_ctl(void *state, int request, void *ptr);
var speex_decoder_ctl = Module.cwrap('speex_decoder_ctl', 'number', ['number', 'number', 'number']);

// const SpeexMode *speex_lib_get_mode(int mode);
var speex_lib_get_mode = Module.cwrap('speex_lib_get_mode', 'number', ['number']);

// CELTMode *celt_mode_create(celt_int32 Fs, int frame_size, int *error);
var celt_mode_create = Module.cwrap('celt_mode_create', 'number', ['number', 'number', 'number']);

// void celt_mode_destroy(CELTMode *mode);
var celt_mode_destroy = Module.cwrap('celt_mode_destroy', null, ['number']);

// CELTDecoder *celt_decoder_create_custom(const CELTMode *mode, int channels, int *error);
var celt_decoder_create_custom = Module.cwrap('celt_decoder_create_custom', 'number', ['number', 'number', 'number']);

// void celt_decoder_destroy(CELTDecoder *st);
var celt_decoder_destroy = Module.cwrap('celt_decoder_destroy', null, ['number']);

// int celt_decode_float(CELTDecoder *st, const unsigned char *data, int len, float *pcm, int frame_size);
var celt_decode_float = Module.cwrap('celt_decode_float', 'number', ['number', 'number', 'number', 'number', 'number']);

// int celt_decoder_ctl(CELTDecoder *st, int request, ...);
var celt_decoder_ctl = Module.cwrap('celt_decoder_ctl', 'number', ['number', 'number']);

var ptrDecoderSize = malloc(4);
SKP_Silk_SDK_Get_Decoder_Size(ptrDecoderSize);
var decoderSize = Module.getValue(ptrDecoderSize, 'i32')
//console.log('Decoder state size: ' + decoderSize);
free(ptrDecoderSize);

var ptrDecoderControl = malloc(4 * 5);
Module.setValue(ptrDecoderControl, 16000, 'i32'); // API_sampleRate
Module.setValue(ptrDecoderControl + (4 * 1), 0, 'i32'); // frameSize
Module.setValue(ptrDecoderControl + (4 * 2), 0, 'i32'); // framesPerPacket
Module.setValue(ptrDecoderControl + (4 * 3), 0, 'i32'); // moreInternalDecoderFrames
Module.setValue(ptrDecoderControl + (4 * 4), 0, 'i32'); // inBandFECOffset

var maxSamples = 8192;
var ptrSamplesOut = malloc(maxSamples << 1);

var ptrSampleCount = malloc(2);

var decoders = [];

function renderSteamId(steamAccountFlags, steamAccountId) {
  if (steamAccountFlags === 0x01100001) {
    return '[U:1:' + steamAccountId + ']';
  }
  return steamAccountFlags + '-' + steamAccountId;
}

function getOrCreateDecoderForSteamId(steamAccountFlags, steamAccountId) {
  var bucket = decoders[steamAccountFlags];
  if (typeof bucket === 'undefined') {
    bucket = decoders[steamAccountFlags] = [];
  }

  var decoder = bucket[steamAccountId];
  if (typeof decoder === 'undefined') {
    decoder = bucket[steamAccountId] = malloc(decoderSize);
    SKP_Silk_SDK_InitDecoder(decoder);
    console.log('Initializing new decoder for ' + renderSteamId(steamAccountFlags, steamAccountId));
  }

  return {
    steamAccountFlags: steamAccountFlags,
    steamAccountId: steamAccountId,
    ptrDecoderState: decoder,
  };
}

function decodeSilkData(decoder, ptrDataIn, dataLength) {
  var cursor = 0;

  var sampleCount = 0;
  Module.setValue(ptrSampleCount, maxSamples, 'i16');

  while (cursor < dataLength) {
    var length = (Module.getValue(ptrDataIn + cursor, 'i8') & 0x00FF) | ((Module.getValue(ptrDataIn + cursor + 1, 'i8') << 8) & 0xFF00);
    cursor += 2;
    //console.log('Chunk length: ' + length);

    if (length === 0) {
      continue;
    }

    if (length === -1) {
      SKP_Silk_SDK_InitDecoder(decoder.ptrDecoderState);
      continue;
    }

    var moreInternalDecoderFrames = 0;
    do {
      Module.setValue(ptrSampleCount, maxSamples - sampleCount, 'i16');

      var ret = SKP_Silk_SDK_Decode(decoder.ptrDecoderState, ptrDecoderControl, 0, ptrDataIn + cursor, length, ptrSamplesOut + (sampleCount << 1), ptrSampleCount);
      if (ret !== 0) {
        postMessage({
          type: 'decode-error',
          error: ret,
        });
      }

      var decodedSamples = Module.getValue(ptrSampleCount, 'i16');
      sampleCount += decodedSamples;

      moreInternalDecoderFrames = Module.getValue(ptrDecoderControl + (4 * 3), 'i32');

      //console.log('Decoded ' + decodedSamples + ' samples.');
    } while (moreInternalDecoderFrames > 0 && sampleCount <= maxSamples);

    cursor += length;
  }

  if (sampleCount > 0) {
    var sampleRate = Module.getValue(ptrDecoderControl, 'i32');
    //var samplesOut = Module.HEAP16.subarray(ptrSamplesOut >> 1, (ptrSamplesOut >> 1) + sampleCount);
    //var samplesOut = Module.HEAPU8.buffer.slice(ptrSamplesOut, ptrSamplesOut + (sampleCount << 1));

    var samplesOut = new Float32Array(sampleCount * 2);
    for (var i = 0; i < sampleCount; ++i) {
      var sample = Module.HEAP16[(ptrSamplesOut >> 1) + i] / 0xFFFF;
      samplesOut[i * 2] = sample;
      samplesOut[(i * 2) + 1] = sample;
    }

    postMessage({
      type: 'audio-data',
      id: decoder.ptrDecoderState,
      avatar: (decoder.steamAccountFlags === 0x01100001) ? decoder.steamAccountId : null,
      sampleRate: sampleRate * 2,
      samples: samplesOut,
    }, [
      samplesOut.buffer,
    ]);
  }
}

function processVoicePacket(ptrDataIn, dataLength) {
  var cursor = 0;

  var steamAccountId =
    ((Module.getValue(ptrDataIn + cursor + 0, 'i8') << (8 * 0)) & 0x000000FF) |
    ((Module.getValue(ptrDataIn + cursor + 1, 'i8') << (8 * 1)) & 0x0000FF00) |
    ((Module.getValue(ptrDataIn + cursor + 2, 'i8') << (8 * 2)) & 0x00FF0000) |
    ((Module.getValue(ptrDataIn + cursor + 3, 'i8') << (8 * 3)) & 0xFF000000);
  cursor += 4;

  var steamAccountFlags =
    ((Module.getValue(ptrDataIn + cursor + 0, 'i8') << (8 * 0)) & 0x000000FF) |
    ((Module.getValue(ptrDataIn + cursor + 1, 'i8') << (8 * 1)) & 0x0000FF00) |
    ((Module.getValue(ptrDataIn + cursor + 2, 'i8') << (8 * 2)) & 0x00FF0000) |
    ((Module.getValue(ptrDataIn + cursor + 3, 'i8') << (8 * 3)) & 0xFF000000);
  cursor += 4;

  var decoder = getOrCreateDecoderForSteamId(steamAccountFlags, steamAccountId);

  dataLength -= 4; // The CRC32 at the end.

  while (cursor < dataLength) {
    var payloadType = Module.getValue(ptrDataIn + cursor, 'i8');
    cursor += 1;

    switch (payloadType) {
      case 0: {
        var numSamples = (Module.getValue(ptrDataIn + cursor, 'i8') & 0x00FF) | ((Module.getValue(ptrDataIn + cursor + 1, 'i8') << 8) & 0xFF00);
        cursor += 2;
        break;
      }

      case 1:
      case 2:
      case 3:
      case 4: {
        var length = (Module.getValue(ptrDataIn + cursor, 'i8') & 0x00FF) | ((Module.getValue(ptrDataIn + cursor + 1, 'i8') << 8) & 0xFF00);
        cursor += 2;

        if (payloadType === 4) {
          decodeSilkData(decoder, ptrDataIn + cursor, length);
        } else {
          postMessage({
            type: 'unsupported-codec',
            codec: payloadType,
          });
        }

        cursor += length;
        break;
      }

      case 10: {
        cursor += 2;
        break;
      }

      case 11: {
        var sampleRate = (Module.getValue(ptrDataIn + cursor, 'i8') & 0x00FF) | ((Module.getValue(ptrDataIn + cursor + 1, 'i8') << 8) & 0xFF00);
        cursor += 2;
        Module.setValue(ptrDecoderControl, sampleRate, 'i32');
        break;
      }

      default: {
        postMessage({
          type: 'unknown-payload',
          payload: payloadType,
        });
        break;
      }
    }
  }
}

var TelephoneEvent = {
  VoiceData: 0,
};

var VoiceDataType = {
  Steam: 0,
  Speex: 1,
  Celt:  2,
};

function processPacket(e) {
  var dataLength = e.data.byteLength;
  var ptrDataIn = malloc(dataLength);
  Module.HEAPU8.set(new Uint8Array(e.data), ptrDataIn);

  var cursor = 0;

  var telephoneEvent = Module.getValue(ptrDataIn + cursor, 'i8');
  cursor += 1;

  var voiceDataType = Module.getValue(ptrDataIn + cursor, 'i8');
  cursor += 1;

  switch (telephoneEvent) {
    case TelephoneEvent.VoiceData: {
      processVoicePacket(ptrDataIn + cursor, dataLength - cursor);
      break;
    }
    default: {
      postMessage({
        type: 'unknown-packet',
        packet: packetType,
      });
      break;
    }
  }

  free(ptrDataIn);
};

var websocket = null;

function onWebsocketOpen(e) {
  postMessage({
    type: 'socket-open',
  });
}

function onWebsocketError(e) {
  postMessage({
    type: 'socket-error',
  });
}

function onWebsocketClose(e) {
  websocket = null;

  postMessage({
    type: 'socket-close',
    code: e.code,
    reason: e.reason,
  });
}

function disconnect() {
  if (websocket) {
    websocket.close();
  }

  for (var bucket in decoders) {
    for (var decoder in bucket) {
      free(decoders[bucket][decoder]);
    }
  }

  decoders = [];
}

function connectToServer(address) {
  disconnect();

  websocket = new WebSocket(address, 'telephone');
  websocket.binaryType = 'arraybuffer';
  websocket.onopen = onWebsocketOpen;
  websocket.onerror = onWebsocketError;
  websocket.onclose = onWebsocketClose;
  websocket.onmessage = processPacket;
}

onmessage = function(e) {
  switch(e.data.type) {
    case 'connect':
      connectToServer(e.data.address);
      break;
    case 'disconnect':
      disconnect();
      break;
    default:
      console.log(e.data);
      break;
  }
};

//free(ptrSamplesOut);
//free(ptrSampleCount);
//free(ptrDecoderControl);

var Module = {};
importScripts('decoders.js');

/*
 * Native functions.
 */

// void *malloc(size_t size);
var malloc = Module.cwrap('malloc', 'number', ['number']);

// void free(void *ptr);
var free = Module.cwrap('free', null, ['number']);

// uint32_t crc32_16bytes(const void *data, size_t length, uint32_t previousCrc32 = 0);
var crc32_16bytes = Module.cwrap('crc32_16bytes', 'number', ['number', 'number', 'number']);

// SRC_STATE *src_new(int converter_type, int channels, int *error);
var src_new = Module.cwrap('src_new', 'number', ['number', 'number', 'number']);;

// SRC_STATE *src_delete(SRC_STATE *state);
var src_delete = Module.cwrap('src_delete', 'number', ['number']);;

// int src_process(SRC_STATE *state, SRC_DATA *data);
var src_process = Module.cwrap('src_process', 'number', ['number', 'number']);;

// void src_short_to_float_array(const short *in, float *out, int len);
var src_short_to_float_array = Module.cwrap('src_short_to_float_array', null, ['number', 'number', 'number']);;

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

/*
 * Native constants.
 */

/*
 * Constants and helper functions.
 */

var TelephoneEvent = {
  VoiceData: 0,
};

var VoiceDataType = {
  Steam: 0,
  Speex: 1,
  Celt:  2,
};

var VoiceDataTypeNames = [
  "Steam (SILK)",
  "Speex",
  "CELT",
];

function readInt8(ptr) {
  return (Module.HEAPU8[ptr] & 0xFF);
}

function readInt16(ptr) {
  return ((Module.HEAPU8[ptr    ]     ) & 0x00FF) |
         ((Module.HEAPU8[ptr + 1] << 8) & 0xFF00);
}

function readInt32(ptr) {
  return ((Module.HEAPU8[ptr    ]      ) & 0x000000FF) |
         ((Module.HEAPU8[ptr + 1] <<  8) & 0x0000FF00) |
         ((Module.HEAPU8[ptr + 2] << 16) & 0x00FF0000) |
         ((Module.HEAPU8[ptr + 3] << 24) & 0xFF000000);
}

function renderSteamId(steamAccountFlags, steamAccountId) {
  if (steamAccountFlags === 0x01100001) {
    return '[U:1:' + steamAccountId + ']';
  }
  return steamAccountFlags + '-' + steamAccountId;
}

/*
 * Decoder classes.
 */

var SteamDecoder = function() {
  throw new Error('Steam (SILK) decoder not implemented.');

  this.sampleRate = 16000;
};

SteamDecoder.prototype.release = function() {

};

SteamDecoder.prototype.process = function(ptrDataIn, dataLength) {
  // Drop the CRC32 from the end, we're not going to check it.
  dataLength -= 4;

  var cursor = 0;
  while (cursor < dataLength) {
    var payloadType = readInt8(ptrDataIn + cursor);
    cursor += 1;

    switch (payloadType) {
      // Silence.
      case 0: {
        var numSamples = readInt16(ptrDataIn + cursor);
        cursor += 2;

        // TODO: Send an empty audio buffer to play.

        break;
      }

      // Voice data in various codecs.
      case 1:   // Unknown (Miles?)
      case 2:   // Speex
      case 3:   // Uncompressed PCM
      case 4: { // SILK
        var length = readInt16(ptrDataIn + cursor);
        cursor += 2;

        if (payloadType === 4) {
          //decodeSilkData(decoder, ptrDataIn + cursor, length);
        } else {
          postMessage({
            type: 'unsupported-codec',
            codec: payloadType,
          });
        }

        cursor += length;
        break;
      }

      // Unknown payload, maybe an older version of 11?
      /*case 10: {
        cursor += 2;
        break;
      }*/

      // Set sample rate.
      case 11: {
        var sampleRate = readInt16(ptrDataIn + cursor);
        cursor += 2;

        // TODO: Just store this in the decoder state?
        this.sampleRate = sampleRate;

        break;
      }

      default: {
        postMessage({
          type: 'unknown-payload',
          payload: payloadType,
        });
        return; // We need to bail out, else we'll be misaligned.
      }
    }
  }
};

var SpeexDecoder = function() {
  throw new Error('Speex decoder not implemented.');
};

SpeexDecoder.prototype.release = function() {

};

SpeexDecoder.prototype.process = function(ptrDataIn, dataLength) {

};

var CeltDecoder = function() {
  throw new Error('CELT decoder not implemented.');
};

CeltDecoder.prototype.release = function() {

};

CeltDecoder.prototype.process = function(ptrDataIn, dataLength) {

};

/*
 * Global state.
 */

var websocket = null;
var decoders = [];
var outputSampleRate = 48000;

/*
 * The meat.
 */

function getOrCreateDecoder(voiceDataType, steamAccountFlags, steamAccountId) {
  var outerBucket = decoders[voiceDataType];
  if (typeof outerBucket === 'undefined') {
    outerBucket = decoders[voiceDataType] = [];
  }

  var innerBucket = outerBucket[steamAccountFlags];
  if (typeof innerBucket === 'undefined') {
    innerBucket = outerBucket[steamAccountFlags] = [];
  }

  var decoder = innerBucket[steamAccountId];
  if (typeof decoder === 'undefined') {
    console.log('Initializing new ' + VoiceDataTypeNames[voiceDataType] + ' decoder for ' + renderSteamId(steamAccountFlags, steamAccountId));

    switch (voiceDataType) {
      case VoiceDataType.Steam: {
        decoder = innerBucket[steamAccountId] = new SteamDecoder();
        break;
      }
      case VoiceDataType.Speex: {
        decoder = innerBucket[steamAccountId] = new SpeexDecoder();
        break;
      }
      case VoiceDataType.Celt: {
        decoder = innerBucket[steamAccountId] = new CeltDecoder();
        break;
      }
    }
  }

  return decoder;
}

function processVoicePacket(ptrDataIn, dataLength) {
  var cursor = 0;

  var voiceDataType = readInt8(ptrDataIn + cursor);
  cursor += 1;

  var steamAccountId = readInt32(ptrDataIn + cursor);
  cursor += 4;

  var steamAccountFlags = readInt32(ptrDataIn + cursor);
  cursor += 4;

  var decoder = getOrCreateDecoder(voiceDataType, steamAccountFlags, steamAccountId);
  if (!decoder) {
    postMessage({
      type: 'unknown-decoder',
      decoder: voiceDataType,
    });
  }

  decoder.process(ptrDataIn + cursor, dataLength - cursor);
}

function processPacket(e) {
  var dataLength = e.data.byteLength;
  var ptrDataIn = malloc(dataLength);
  Module.HEAPU8.set(new Uint8Array(e.data), ptrDataIn);

  var cursor = 0;

  var telephoneEvent = readInt8(ptrDataIn + cursor);
  cursor += 1;

  switch (telephoneEvent) {
    case TelephoneEvent.VoiceData: {
      processVoicePacket(ptrDataIn + cursor, dataLength - cursor);
      break;
    }
    default: {
      postMessage({
        type: 'unknown-event',
        event: telephoneEvent,
      });
      break;
    }
  }

  free(ptrDataIn);
};

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

  for (var outerBucket in decoders) {
    for (var innerBucket in decoders[outerBucket]) {
      for (var decoder in decoders[outerBucket][innerBucket]) {
        decoders[outerBucket][innerBucket][decoder].release();
      }
    }
  }

  decoders = [];
}

function connectToServer(address, sampleRate) {
  disconnect();

  outputSampleRate = sampleRate;

  websocket = new WebSocket(address, 'telephone');
  websocket.binaryType = 'arraybuffer';
  websocket.onopen = onWebsocketOpen;
  websocket.onerror = onWebsocketError;
  websocket.onclose = onWebsocketClose;
  websocket.onmessage = processPacket;
}

self.onmessage = function(e) {
  switch(e.data.type) {
    case 'connect':
      connectToServer(e.data.address, e.data.outputSampleRate);
      break;
    case 'disconnect':
      disconnect();
      break;
    default:
      console.log(e.data);
      break;
  }
};

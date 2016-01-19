var Module = {};
importScripts('silk.js');

var SKP_Silk_SDK_get_version = Module.cwrap('SKP_Silk_SDK_get_version', 'string', []);
var SKP_Silk_SDK_Get_Decoder_Size = Module.cwrap('SKP_Silk_SDK_Get_Decoder_Size', 'number', ['number']);
var SKP_Silk_SDK_InitDecoder = Module.cwrap('SKP_Silk_SDK_InitDecoder', 'number', ['number']);
var SKP_Silk_SDK_Decode = Module.cwrap('SKP_Silk_SDK_Decode', 'number', ['number', 'number', 'number', 'number', 'number', 'number', 'number']);

console.log('Silk SDK Version: ' + SKP_Silk_SDK_get_version());

var ptrDecoderSize = Module._malloc(4);
SKP_Silk_SDK_Get_Decoder_Size(ptrDecoderSize);
var decoderSize = Module.getValue(ptrDecoderSize, 'i32')
console.log('Decoder state size: ' + decoderSize);
Module._free(ptrDecoderSize);

var ptrDecoderControl = Module._malloc(4 * 5);
Module.setValue(ptrDecoderControl + (4 * 0), 16000, 'i32'); // API_sampleRate
Module.setValue(ptrDecoderControl + (4 * 1), 0, 'i32'); // frameSize
Module.setValue(ptrDecoderControl + (4 * 2), 0, 'i32'); // framesPerPacket
Module.setValue(ptrDecoderControl + (4 * 3), 0, 'i32'); // moreInternalDecoderFrames
Module.setValue(ptrDecoderControl + (4 * 4), 0, 'i32'); // inBandFECOffset

var maxSamples = 8192;
var ptrSamplesOut = Module._malloc(maxSamples << 1);

var ptrSampleCount = Module._malloc(2);

var decoders = [];

function getOrCreateDecoderForSteamId(flags, accountid) {
  var bucket = decoders[flags];
  if (typeof bucket === 'undefined') {
    bucket = decoders[flags] = [];
  }

  var decoder = bucket[accountid];
  if (typeof decoder === 'undefined') {
    decoder = bucket[accountid] = Module._malloc(decoderSize);
    console.log('Initializing decoder: ' + SKP_Silk_SDK_InitDecoder(decoder));
  }

  return decoder;
}

function processPacket(e) {
  var dataLength = e.data.byteLength;
  var ptrDataIn = Module._malloc(dataLength);
  Module.HEAPU8.set(new Uint8Array(e.data), ptrDataIn);

  var cursor = 0;

  // This is safe since it is at the start of the allocation and thus always aligned.
  var steamAccountFlags = Module.getValue(ptrDataIn + cursor + 4, 'i32') | 0;
  var steamAccountId = Module.getValue(ptrDataIn + cursor, 'i32') | 0;

  cursor += 8;

  var ptrDecoderState = getOrCreateDecoderForSteamId(steamAccountFlags, steamAccountId);

  cursor += 6; // Skip the rest of the header.
  dataLength -= 4; // The CRC32 at the end.

  var sampleCount = 0;
  Module.setValue(ptrSampleCount, maxSamples, 'i16');

  while (cursor < dataLength) {
    var length = ((Module.getValue(ptrDataIn + cursor + 1, 'i8') << 8) & 0xFFFFFF00) | (Module.getValue(ptrDataIn + cursor, 'i8') & 0xFF);
    console.log('Chunk length: ' + length);

    cursor += 2;

    if (length === 0) {
      continue;
    }

    if (length === -1) {
      console.log('Reinitializing decoder: ' + SKP_Silk_SDK_InitDecoder(ptrDecoderState));
      continue;
    }

    var moreInternalDecoderFrames = 0;
    do {
      Module.setValue(ptrSampleCount, maxSamples - sampleCount, 'i16');
      console.log('Decoding samples: ' + SKP_Silk_SDK_Decode(ptrDecoderState, ptrDecoderControl, 0, ptrDataIn + cursor, dataLength - cursor, ptrSamplesOut + (sampleCount << 1), ptrSampleCount));

      sampleCount += Module.getValue(ptrSampleCount, 'i16');
      console.log('Decoded samples: ' + sampleCount);

      moreInternalDecoderFrames = Module.getValue(ptrDecoderControl + (4 * 3), 'i32');
    } while (moreInternalDecoderFrames > 0 && sampleCount <= maxSamples);

    cursor += length;
  }

  if (sampleCount > 0) {
    var samplesOut = Module.HEAP16.subarray(ptrSamplesOut >> 1, (ptrSamplesOut >> 1) + sampleCount);
    postMessage({
      'type': 'audio-data',
      'samples': samplesOut,
    });
  }

  Module._free(ptrDataIn);
};

var websocket;

function onWebsocketOpen(e) {
  postMessage({
    'type': 'socket-open',
    'event': e.toString(),
  });
}

function onWebsocketError(e) {
  postMessage({
    'type': 'socket-error',
    'event': e.toString(),
  });
}

function onWebsocketClose(e) {
  postMessage({
    'type': 'socket-close',
    'event': e.toString(),
  });
}

onmessage = function(e) {
  if (websocket) websocket.close(1001, 'Changing Server');

  websocket = new WebSocket(e.data, 'voice-chat');
  websocket.binaryType = 'arraybuffer';
  websocket.onopen = onWebsocketOpen;
  websocket.onerror = onWebsocketError;
  websocket.onclose = onWebsocketClose;
  websocket.onmessage = processPacket;
}

//Module._free(ptrSamplesOut);
//Module._free(ptrSampleCount);
//Module._free(ptrDecoderControl);
//Module._free(ptrDecoderState);

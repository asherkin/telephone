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
//console.log('Decoder state size: ' + decoderSize);
Module._free(ptrDecoderSize);

var ptrDecoderControl = Module._malloc(4 * 5);
Module.setValue(ptrDecoderControl, 16000, 'i32'); // API_sampleRate
Module.setValue(ptrDecoderControl + (4 * 1), 0, 'i32'); // frameSize
Module.setValue(ptrDecoderControl + (4 * 2), 0, 'i32'); // framesPerPacket
Module.setValue(ptrDecoderControl + (4 * 3), 0, 'i32'); // moreInternalDecoderFrames
Module.setValue(ptrDecoderControl + (4 * 4), 0, 'i32'); // inBandFECOffset

var maxSamples = 8192;
var ptrSamplesOut = Module._malloc(maxSamples << 1);

var ptrSampleCount = Module._malloc(2);

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
    decoder = bucket[steamAccountId] = Module._malloc(decoderSize);
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
    var length = ((Module.getValue(ptrDataIn + cursor + 1, 'i8') << 8) & 0xFFFFFF00) | (Module.getValue(ptrDataIn + cursor, 'i8') & 0xFF);
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
      if (ret != 0) {
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

function processPacket(e) {
  var dataLength = e.data.byteLength;
  var ptrDataIn = Module._malloc(dataLength);
  Module.HEAPU8.set(new Uint8Array(e.data), ptrDataIn);

  var cursor = 0;

  // This is safe since it is at the start of the allocation and thus always aligned.
  var steamAccountFlags = Module.getValue(ptrDataIn + cursor + 4, 'i32');
  var steamAccountId = Module.getValue(ptrDataIn + cursor, 'i32');
  cursor += 8;

  var decoder = getOrCreateDecoderForSteamId(steamAccountFlags, steamAccountId);

  dataLength -= 4; // The CRC32 at the end.

  while (cursor < dataLength) {
    var payloadType = Module.getValue(ptrDataIn + cursor, 'i8');
    cursor += 1;

    switch (payloadType) {
      case 0: {
        var numSamples = ((Module.getValue(ptrDataIn + cursor + 1, 'i8') << 8) & 0xFFFFFF00) | (Module.getValue(ptrDataIn + cursor, 'i8') & 0xFF);
        cursor += 2;
        break;
      }

      case 1:
      case 2:
      case 3:
      case 4: {
        var length = ((Module.getValue(ptrDataIn + cursor + 1, 'i8') << 8) & 0xFFFFFF00) | (Module.getValue(ptrDataIn + cursor, 'i8') & 0xFF);
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
        var sampleRate = ((Module.getValue(ptrDataIn + cursor + 1, 'i8') << 8) & 0xFFFFFF00) | (Module.getValue(ptrDataIn + cursor, 'i8') & 0xFF);
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

  Module._free(ptrDataIn);
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
      Module._free(decoders[bucket][decoder]);
    }
  }

  decoders = [];
}

function connectToServer(address) {
  disconnect();

  websocket = new WebSocket(address, 'voice-chat');
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

//Module._free(ptrSamplesOut);
//Module._free(ptrSampleCount);
//Module._free(ptrDecoderControl);

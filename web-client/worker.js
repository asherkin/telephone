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

var ptrDecoderState = Module._malloc(decoderSize);
console.log('Initializing decoder: ' + SKP_Silk_SDK_InitDecoder(ptrDecoderState));

var ptrDecoderControl = Module._malloc(4 * 5);
Module.setValue(ptrDecoderControl + (4 * 0), 16000, 'i32'); // API_sampleRate
Module.setValue(ptrDecoderControl + (4 * 1), 0, 'i32'); // frameSize
Module.setValue(ptrDecoderControl + (4 * 2), 0, 'i32'); // framesPerPacket
Module.setValue(ptrDecoderControl + (4 * 3), 0, 'i32'); // moreInternalDecoderFrames
Module.setValue(ptrDecoderControl + (4 * 4), 0, 'i32'); // inBandFECOffset

onmessage = function(e) {
  var ptrDataIn = Module._malloc(e.data.length);
  Module.writeArrayToMemory(e.data, ptrDataIn);

  var maxSamples = 8192;
  var ptrSamplesOut = Module._malloc(maxSamples << 1);

  var ptrSampleCount = Module._malloc(2);
  Module.setValue(ptrSampleCount, maxSamples, 'i16');

  var sampleCount = 0;

  var cursor = 0;
  while (cursor < e.data.length) {
    var length = ((Module.getValue(ptrDataIn + cursor + 1, 'i8') << 8) & 0xFFFFFF00) | (Module.getValue(ptrDataIn + cursor, 'i8') & 0xFF);
    console.log('Chunk length: ' + length);

    cursor += 2;

    if (length === -1) {
      console.log('Reinitializing decoder: ' + SKP_Silk_SDK_InitDecoder(ptrDecoderState));
      continue;
    }

    var moreInternalDecoderFrames = 0;
    do {
      Module.setValue(ptrSampleCount, maxSamples - sampleCount, 'i16');
      console.log('Decoding samples: ' + SKP_Silk_SDK_Decode(ptrDecoderState, ptrDecoderControl, 0, ptrDataIn + cursor, e.data.length - cursor, ptrSamplesOut + (sampleCount << 1), ptrSampleCount));

      sampleCount += Module.getValue(ptrSampleCount, 'i16');
      console.log('Decoded samples: ' + sampleCount);

      moreInternalDecoderFrames = Module.getValue(ptrDecoderControl + (4 * 3), 'i32');
    } while (moreInternalDecoderFrames > 0);

    cursor += length;
  }

  var samplesOut = Module.HEAP16.subarray(ptrSamplesOut >> 1, (ptrSamplesOut >> 1) + sampleCount);
  //console.log(samplesOut);

  postMessage(samplesOut);

  Module._free(ptrSamplesOut);
  Module._free(ptrSampleCount);
  Module._free(ptrDataIn);
};

//Module._free(ptrDecoderControl);
//Module._free(ptrDecoderState);

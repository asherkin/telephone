var Silk = require('./silk');

var SKP_Silk_SDK_get_version = Silk.cwrap('SKP_Silk_SDK_get_version', 'string', []);
var SKP_Silk_SDK_Get_Decoder_Size = Silk.cwrap('SKP_Silk_SDK_Get_Decoder_Size', 'number', ['number']);
var SKP_Silk_SDK_InitDecoder = Silk.cwrap('SKP_Silk_SDK_InitDecoder', 'number', ['number']);
var SKP_Silk_SDK_Decode = Silk.cwrap('SKP_Silk_SDK_Decode', 'number', ['number', 'number', 'number', 'number', 'number', 'number', 'number']);

console.log(SKP_Silk_SDK_get_version());

var ptrDecoderSize = Silk._malloc(4);
console.log(SKP_Silk_SDK_Get_Decoder_Size(ptrDecoderSize));
var decoderSize = Silk.getValue(ptrDecoderSize, 'i32')
console.log(decoderSize);
Silk._free(ptrDecoderSize);

var ptrDecoderState = Silk._malloc(decoderSize);
console.log(SKP_Silk_SDK_InitDecoder(ptrDecoderState));

var ptrDecoderControl = Silk._malloc(4 * 5);
Silk.setValue(ptrDecoderControl + (4 * 0), 16000, 'i32');
Silk.setValue(ptrDecoderControl + (4 * 1), 0, 'i32');
Silk.setValue(ptrDecoderControl + (4 * 2), 0, 'i32');
Silk.setValue(ptrDecoderControl + (4 * 3), 0, 'i32');
Silk.setValue(ptrDecoderControl + (4 * 4), 0, 'i32');

var testData = [0xA5, 0xFF, 0x56, 0xB2, 0x30, 0xCE, 0x63, 0x2F, 0x37, 0x26, 0x8D, 0xAD, 0x1D, 0xD1, 0x01, 0x67, 0x3E, 0xDA, 0xF1, 0x29, 0x15, 0x4B, 0x14, 0xCF, 0x81, 0xB4, 0xBE, 0x04, 0xA9, 0x20, 0xE1, 0xA8, 0x4A, 0xBA, 0xB1, 0xB8, 0xA3, 0xD3, 0x7C, 0x09, 0x95, 0xDC, 0x4D, 0xF9, 0x79, 0x58, 0x0B, 0x4A, 0xAC, 0x02, 0xFB, 0x64, 0xE1];

var ptrDataIn = Silk._malloc(testData.length);
Silk.writeArrayToMemory(testData, ptrDataIn);

var ptrSamplesOut = Silk._malloc(8192 * 2);

var ptrSampleCount = Silk._malloc(2);
Silk.setValue(ptrSampleCount, 8192, 'i16');

console.log(SKP_Silk_SDK_Decode(ptrDecoderState, ptrDecoderControl, 0, ptrDataIn, testData.length, ptrSamplesOut, ptrSampleCount));

var sampleCount = Silk.getValue(ptrSampleCount, 'i16');
console.log(sampleCount);

var samplesOut = Silk.HEAP16.subarray(ptrSamplesOut >> 1, (ptrSamplesOut >> 1) + sampleCount);
console.log(samplesOut);

Silk._free(ptrSamplesOut);

Silk._free(ptrSampleCount);

Silk._free(ptrDataIn);

Silk._free(ptrDecoderControl);

Silk._free(ptrDecoderState);

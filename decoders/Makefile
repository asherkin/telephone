MAKEFLAGS += --no-builtin-rules
.SUFFIXES:

emcc = emcc -O3 --llvm-lto 1 --closure 1 --memory-init-file 0 -s AGGRESSIVE_VARIABLE_ELIMINATION=1 -s WARN_UNALIGNED=1 -s NO_FILESYSTEM=1 -s NO_BROWSER=1 -s EXPORTED_FUNCTIONS="['_malloc','_free','_crc32_16bytes','_src_new','_src_delete','_src_process','_src_short_to_float_array','_SKP_Silk_SDK_Get_Decoder_Size','_SKP_Silk_SDK_InitDecoder','_SKP_Silk_SDK_Decode','_celt_mode_create','_celt_decoder_create_custom','_celt_decode_float','_celt_decoder_ctl','_celt_decoder_destroy','_celt_mode_destroy','_speex_decoder_ctl','_speex_bits_read_from','_speex_decode','_speex_bits_init','_speex_decoder_init','_speex_lib_get_mode','_speex_decoder_destroy','_speex_bits_destroy']"

decoders.js: crc32.bc libsamplerate.bc silk.bc celt.bc speex.bc
	$(emcc) $^ -o $@

.PHONY: clean
clean:
	rm -rf decoders.js crc32.bc libsamplerate.bc silk.bc celt.bc speex.bc

crc32.bc: crc32.cpp
	$(emcc) $^ -o $@

libsamplerate.bc: libsamplerate/include/*.h libsamplerate/src/*.h libsamplerate/src/*.c
	$(emcc) -Ilibsamplerate/include libsamplerate/src/*.c -o $@

silk.bc: silk/include/*.h silk/src/*.h silk/src/*.c
	$(emcc) -Isilk/include silk/src/*.c -o $@

celt.bc: celt/include/*.h celt/src/*.h celt/src/*.c
	$(emcc) -Wno-parentheses -Wno-tautological-pointer-compare -DHAVE_CONFIG_H -Icelt/include celt/src/bands.c celt/src/celt.c celt/src/cwrs.c celt/src/entcode.c celt/src/entdec.c celt/src/entenc.c celt/src/header.c celt/src/kiss_fft.c celt/src/laplace.c celt/src/mathops.c celt/src/mdct.c celt/src/modes.c celt/src/pitch.c celt/src/plc.c celt/src/quant_bands.c celt/src/rate.c celt/src/vq.c -o $@

speex.bc: speex/include/*.h speex/src/*.h speex/src/*.c
	$(emcc) -DHAVE_CONFIG_H -Ispeex/include speex/src/cb_search.c speex/src/exc_10_32_table.c speex/src/exc_8_128_table.c speex/src/filters.c speex/src/gain_table.c speex/src/hexc_table.c speex/src/high_lsp_tables.c speex/src/lsp.c speex/src/ltp.c speex/src/speex.c speex/src/stereo.c speex/src/vbr.c speex/src/vq.c speex/src/bits.c speex/src/exc_10_16_table.c speex/src/exc_20_32_table.c speex/src/exc_5_256_table.c speex/src/exc_5_64_table.c speex/src/gain_table_lbr.c speex/src/hexc_10_32_table.c speex/src/lpc.c speex/src/lsp_tables_nb.c speex/src/modes.c speex/src/modes_wb.c speex/src/nb_celp.c speex/src/quant_lsp.c speex/src/sb_celp.c speex/src/speex_callbacks.c speex/src/speex_header.c speex/src/window.c -o $@

cmake_minimum_required(VERSION 2.8.0)
project(telephone-test-client)

file(GLOB libcelt_SRC
	../decoders/celt/src/bands.c
	../decoders/celt/src/celt.c
	../decoders/celt/src/cwrs.c
	../decoders/celt/src/entcode.c
	../decoders/celt/src/entdec.c
	../decoders/celt/src/entenc.c
	../decoders/celt/src/header.c
	../decoders/celt/src/kiss_fft.c
	../decoders/celt/src/laplace.c
	../decoders/celt/src/mathops.c
	../decoders/celt/src/mdct.c
	../decoders/celt/src/modes.c
	../decoders/celt/src/pitch.c
	../decoders/celt/src/plc.c
	../decoders/celt/src/quant_bands.c
	../decoders/celt/src/rate.c
	../decoders/celt/src/vq.c
)
add_library(libcelt STATIC ${libcelt_SRC})
target_include_directories(libcelt PRIVATE ../decoders/celt/include/)
set_target_properties(libcelt PROPERTIES LINKER_LANGUAGE C)
set_target_properties(libcelt PROPERTIES COMPILE_DEFINITIONS "HAVE_CONFIG_H")

add_executable(${PROJECT_NAME} main.cpp)
add_dependencies(${PROJECT_NAME} libcelt)
target_link_libraries(${PROJECT_NAME} libcelt)

cmake_minimum_required(VERSION 3.20)

project(ese_snesapu LANGUAGES CXX RC)

if(NOT WIN32)
	message(FATAL_ERROR "ese_snesapu supports Windows only")
endif()

add_library(ese_snesapu SHARED
	dllmain.cpp
	ese_snesapu.cpp
	ese_snesapu.rc
	script700.cpp
	snesapu.def
	stdafx.cpp
	gme/blargg_common.cpp
	gme/blargg_errors.cpp
	gme/Blip_Buffer.cpp
	gme/Classic_Emu.cpp
	gme/Data_Reader.cpp
	gme/Dual_Resampler.cpp
	gme/Effects_Buffer.cpp
	gme/Fir_Resampler.cpp
	gme/gme.cpp
	gme/Gme_File.cpp
	gme/Gme_Loader.cpp
	gme/Multi_Buffer.cpp
	gme/Music_Emu.cpp
	gme/Resampler.cpp
	gme/Snes_Spc.cpp
	gme/Spc_Cpu.cpp
	gme/Spc_Dsp.cpp
	gme/Spc_Emu.cpp
	gme/Spc_Filter.cpp
	gme/Track_Filter.cpp
)

target_compile_features(ese_snesapu PRIVATE cxx_std_11)
target_compile_definitions(ese_snesapu PRIVATE
	WIN32
	_WINDOWS
	_USRDLL
	ESE_SNESAPU_EXPORTS
)

if(MSVC)
	target_compile_options(ese_snesapu PRIVATE /W3)
	target_link_options(ese_snesapu PRIVATE /SUBSYSTEM:WINDOWS)
endif()

set_target_properties(ese_snesapu PROPERTIES
	OUTPUT_NAME snesapu
	PREFIX ""
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

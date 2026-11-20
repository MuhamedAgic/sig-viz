cmake_minimum_required(VERSION 3.25) # 3.25: FetchContent SYSTEM
project(sig-viz LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ON for a build to hand out: no console window and no raylib log spam.
option(PRODUCTION_BUILD "Build for shipping" OFF)

# SIMD for everything, the libraries included. Needs a CPU from ~2013 or later.
if(MSVC)
	add_compile_options(/arch:AVX2)
else()
	add_compile_options(-mavx2 -mfma)
endif()


# ---- third party -----------------------------------------------------------------------------------

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build raylib as a static library" FORCE)

add_subdirectory(thirdparty/raylib-5.0)    # window, OpenGL context, input, audio device
add_subdirectory(thirdparty/imgui-docking) # the UI
add_subdirectory(thirdparty/rlImgui)       # renders ImGui through raylib
add_subdirectory(thirdparty/implot)        # plots: spectrum, spectrogram axes, constellation, eye diagram
add_subdirectory(thirdparty/pocketfft)     # fast Fourier transforms

# AFF3CT: forward error correction (polar codes), interleavers, noisy channels. Too big to keep in the
# repository (and it needs its git history to configure), so it is downloaded on the first configure.
# Only the parts we use are switched on; the first build still takes several minutes.
include(FetchContent)
set(AFF3CT_COMPILE_EXE        OFF CACHE BOOL   "" FORCE)
set(AFF3CT_COMPILE_STATIC_LIB ON  CACHE BOOL   "" FORCE)
set(AFF3CT_COMPILE_SHARED_LIB OFF CACHE BOOL   "" FORCE)
set(AFF3CT_PREC               "32" CACHE STRING "" FORCE) # float / int32 only: a third less to compile
set(SPU_STACKTRACE            OFF CACHE BOOL   "" FORCE)  # would pull in cpptrace + libdwarf
set(SPU_BENCHMARK             OFF CACHE BOOL   "" FORCE)
set(SPU_TESTS                 OFF CACHE BOOL   "" FORCE)
set(SPU_COLORS                OFF CACHE BOOL   "" FORCE)
FetchContent_Declare(aff3ct
	GIT_REPOSITORY https://github.com/aff3ct/aff3ct.git
	GIT_TAG        v4.7.2 # e1b52965ae0c5d9a4386c2ba5c75a84e80709524
	GIT_SHALLOW    TRUE
	GIT_SUBMODULES lib/MIPP lib/date lib/cli lib/streampu # not conf/ and refs/: 100+ MB of simulation data
	SYSTEM)                                               # its headers are not ours to warn about
FetchContent_MakeAvailable(aff3ct)


# ---- the app ---------------------------------------------------------------------------------------

# Every .cpp/.hpp under src/ is picked up automatically (headers too, so IDEs show them).
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.hpp)

add_executable(sig-viz ${SOURCES})
target_include_directories(sig-viz PRIVATE src)
# miniaudio's declarations (raylib compiles miniaudio in; src/app/microphone.cpp records with it)
target_include_directories(sig-viz SYSTEM PRIVATE thirdparty/raylib-5.0/src/external)
target_link_libraries(sig-viz PRIVATE raylib_static imgui rlimgui implot pocketfft aff3ct-static-lib)
target_compile_definitions(sig-viz PRIVATE PRODUCTION_BUILD=$<BOOL:${PRODUCTION_BUILD}>)

# Warnings for our own code only; the libraries are not ours to fix.
if(MSVC)
	target_compile_options(sig-viz PRIVATE /W4 /utf-8)
else()
	target_compile_options(sig-viz PRIVATE -Wall -Wextra)
endif()

# AFF3CT's decoders are templates, compiled into coding.cpp: optimise it even in debug builds,
# or decoding (and the error-rate curve) runs ~10x slower.
if(NOT MSVC)
	set_source_files_properties(src/core/coding.cpp PROPERTIES COMPILE_OPTIONS "-O2")
endif()

if(WIN32)
	target_link_libraries(sig-viz PRIVATE ws2_32) # sockets, for rtl_tcp
endif()

if(PRODUCTION_BUILD AND WIN32)
	if(MSVC)
		target_link_options(sig-viz PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
	else()
		target_link_options(sig-viz PRIVATE -mwindows)
	endif()
endif()


# ---- tests -----------------------------------------------------------------------------------------

# Numeric checks of src/core (no UI). Build and run: cmake --build <dir> && ctest --test-dir <dir>
enable_testing()
add_executable(core-tests tests/core_tests.cpp src/core/coding.cpp)
target_include_directories(core-tests PRIVATE src)
target_link_libraries(core-tests PRIVATE pocketfft aff3ct-static-lib)
if(MSVC)
	target_compile_options(core-tests PRIVATE /W4 /utf-8)
endif()
add_test(NAME core COMMAND core-tests)

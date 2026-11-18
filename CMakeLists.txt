cmake_minimum_required(VERSION 3.21)
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


# ---- the app ---------------------------------------------------------------------------------------

# Every .cpp/.hpp under src/ is picked up automatically (headers too, so IDEs show them).
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.hpp)

add_executable(sig-viz ${SOURCES})
target_include_directories(sig-viz PRIVATE src)
target_link_libraries(sig-viz PRIVATE raylib_static imgui rlimgui)
target_compile_definitions(sig-viz PRIVATE PRODUCTION_BUILD=$<BOOL:${PRODUCTION_BUILD}>)

# Warnings for our own code only; the libraries are not ours to fix.
if(MSVC)
	target_compile_options(sig-viz PRIVATE /W4 /utf-8)
else()
	target_compile_options(sig-viz PRIVATE -Wall -Wextra)
endif()

if(PRODUCTION_BUILD AND WIN32)
	if(MSVC)
		target_link_options(sig-viz PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
	else()
		target_link_options(sig-viz PRIVATE -mwindows)
	endif()
endif()

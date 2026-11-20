#pragma once

// An RGBA image in GPU memory (a raylib texture) that ImGui and ImPlot can draw. Frees itself.

#include <cstdint>
#include <span>

#include <imgui.h>
#include <raylib.h>

class GpuImage {
public:
	GpuImage() = default;
	~GpuImage() {
		release();
	}

	GpuImage(const GpuImage&) = delete;
	GpuImage& operator=(const GpuImage&) = delete;

	// pixels: width * height colours made with IM_COL32, row by row starting at the top.
	void upload(std::span<const ImU32> pixels, int width, int height) {
		release();
		const Image image{
			.data = const_cast<ImU32*>(pixels.data()), // raylib only reads it
			.width = width,
			.height = height,
			.mipmaps = 1,
			.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, // IM_COL32's byte order
		};
		texture_ = LoadTextureFromImage(image);
		SetTextureFilter(texture_, TEXTURE_FILTER_BILINEAR);
	}

	[[nodiscard]] bool valid() const {
		return texture_.id != 0;
	}

	// rlImGui identifies textures by their OpenGL id.
	[[nodiscard]] ImTextureID id() const {
		return reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(texture_.id));
	}

private:
	void release() {
		if (valid()) {
			UnloadTexture(texture_);
		}
		texture_ = {};
	}

	Texture2D texture_{};
};

// The microphone, through miniaudio: the audio library raylib is built on. raylib compiles miniaudio
// in, so only its declarations are needed here, with the same settings raylib uses (they change the
// layout of miniaudio's structs, so they must match raylib's raudio.c).
//
// Kept out of every file that includes raylib.h: miniaudio pulls in Windows headers, whose names
// (CloseWindow, DrawText, ...) clash with raylib's.

#define MA_NO_JACK
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_COINIT_VALUE 2

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <miniaudio.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "live_input.hpp"

namespace {

	class Microphone final : public LiveInput {
	public:
		static constexpr unsigned rate = 48'000;

		bool open(std::string& error) {
			ma_device_config config = ma_device_config_init(ma_device_type_capture);
			config.capture.format = ma_format_f32;
			config.capture.channels = 1;
			config.sampleRate = rate;
			config.dataCallback = [](ma_device* device, void*, const void* input, ma_uint32 frames) {
				static_cast<Microphone*>(device->pUserData)->push(static_cast<const float*>(input), nullptr, frames);
			};
			config.pUserData = this;

			if (ma_device_init(nullptr, &config, &device_) != MA_SUCCESS) {
				error = "No microphone found (or it is in use).";
				return false;
			}
			initialised_ = true;
			if (ma_device_start(&device_) != MA_SUCCESS) {
				error = "The microphone would not start.";
				return false;
			}
			return true;
		}

		~Microphone() override {
			if (initialised_) {
				ma_device_uninit(&device_); // stops the callbacks before `this` is gone
			}
		}

		[[nodiscard]] bool iq() const override {
			return false;
		}
		[[nodiscard]] float sample_rate() const override {
			return static_cast<float>(rate);
		}

	private:
		ma_device device_{};
		bool      initialised_ = false;
	};

} // namespace

std::unique_ptr<LiveInput> open_microphone(std::string& error) {
	auto microphone = std::make_unique<Microphone>();
	if (!microphone->open(error)) {
		return nullptr;
	}
	return microphone;
}

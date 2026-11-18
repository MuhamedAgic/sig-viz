#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "audio_output.hpp"
#include "file_browser.hpp"
#include "core/peak_index.hpp"
#include "core/signal.hpp"
#include "core/signal_audio.hpp"
#include "core/signal_io.hpp"

// The main window: pick, load or save a signal, look at it and listen to it.
class SignalViewer {
public:
	SignalViewer();

	// Call once per frame, between rlImGuiBegin() and rlImGuiEnd().
	void draw();

private:
	struct Playback {
		float position = 0.0f; // seconds
		float speed = 1.0f;
		bool  playing = false;
		bool  loop = true;
		bool  scrubbing = false;

		void advance(float dt, float duration);
		void toggle(float duration);
	};

	// What the scope shows.
	struct View {
		float seconds = 0.02f;
		float gain = 1.0f;
	};

	struct Sound {
		bool               enabled = true;
		float              volume = 0.25f;
		sig::audio::Listen listen = sig::audio::Listen::Signal;
	};

	struct Status {
		std::string text;
		bool        is_error = false;
	};

	// Raw files have no header, so they need a format and sample rate before they can be loaded.
	struct ImportDialog {
		bool                  open_requested = false;
		std::filesystem::path path;
		sig::io::Format       format = sig::io::Format::CF32;
		float                 sample_rate = 1'000'000.0f;
	};

	enum class Pending { None, Load, Save }; // what the file browser is open for

	[[nodiscard]] const sig::Signal& current() const { return signals_[selected_]; }
	void select(std::size_t index);

	// ---- toolbar and files (signal_viewer.cpp) ----
	void draw_toolbar();
	void draw_file_dialogs();
	void draw_import_dialog();
	void begin_load(const std::filesystem::path& path);
	void load_file(const std::filesystem::path& path, const sig::io::LoadOptions& options);
	void save_to(std::filesystem::path path);
	[[nodiscard]] std::string suggested_file_name() const;

	// ---- playback and plots ----
	void draw_transport(float duration);
	void draw_sound_controls(const sig::Signal& signal);
	bool draw_overview(const sig::Signal& signal); // these two return true while being dragged
	bool draw_scope(const sig::Signal& signal);

	std::vector<sig::Signal>    signals_;
	std::size_t                 example_count_; // signals_[0, example_count_) are built in, the rest came from files
	std::size_t                 selected_ = 0;
	std::vector<sig::PeakIndex> peaks_;         // one per channel of the selected signal; rebuilt by select()
	float                       peak_ = 0.0f;   // largest |sample| of the selected signal

	Playback    playback_;
	View        view_;
	Sound       sound_;
	AudioOutput audio_;

	FileBrowser  browser_;
	Pending      pending_ = Pending::None;
	ImportDialog import_;
	Status       status_;
};

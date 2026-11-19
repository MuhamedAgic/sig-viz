#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "audio_output.hpp"
#include "file_browser.hpp"
#include "ui.hpp"
#include "waveform.hpp"
#include "workspace.hpp"
#include "core/signal_audio.hpp"
#include "core/signal_io.hpp"

// The main window: pick, load or save a signal, look at it, listen to it and mark parts of it.
//
// Mouse, on the overview and the scope:
//   drag               scrub (moves the playhead)
//   shift + drag       select a time range (analysed by the other windows, looped by playback)
//   right-click        place cursor A, then B (shows the time between them)
//   wheel (scope)      zoom
class SignalViewer {
public:
	static constexpr const char* title = "Signal Viewer";
	bool open = true;

	void draw(Workspace& ws);

private:
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
		double                center_hz = 0.0;
		double                start_seconds = 0.0;
		double                max_seconds = 0.0; // 0 = all
		std::uintmax_t        file_bytes = 0;
		bool                  sigmf = false;     // format and rate come from the .sigmf-meta
	};

	enum class Pending { None, Load, Save, SaveSelection }; // what the file browser is open for
	enum class Drag { Scrub, Select };                       // what a left-drag on a plot does

	void on_signal_changed(const Workspace& ws);

	// ---- toolbar and files ----
	void draw_toolbar(Workspace& ws);
	void draw_file_dialogs(Workspace& ws);
	void draw_import_dialog(Workspace& ws);
public:
	// Loads a file (asks for details first if it needs them). Also used for files dropped on the window.
	void begin_load(Workspace& ws, const std::filesystem::path& path);
	void open_load_dialog();

private:
	void load_file(Workspace& ws, const std::filesystem::path& path, const sig::io::LoadOptions& options);
	void save_to(const Workspace& ws, std::filesystem::path path, bool selection_only);

	// ---- playback, marks and plots ----
	void draw_transport(Workspace& ws);
	void draw_sound_controls(const Workspace& ws);
	void draw_marks(Workspace& ws);
	bool draw_overview(Workspace& ws); // these return true while scrubbing
	bool draw_scope(Workspace& ws);
	bool handle_mouse(Workspace& ws, const ui::Area& area, const waveform::Canvas& canvas, bool grab);
	void draw_marks_on(ImDrawList& dl, const waveform::Canvas& canvas, const Workspace& ws) const;

	TrackPtr    shown_; // the signal shown last frame, to notice when another window switched it
	TrackPtr    heard_; // what was audible last frame (the signal or ws.audition)
	View        view_;
	Sound       sound_;
	AudioOutput audio_;

	FileBrowser  browser_;
	Pending      pending_ = Pending::None;
	ImportDialog import_;
	Status       status_;

	Drag   drag_ = Drag::Scrub;
	double select_anchor_ = 0.0; // where a shift-drag started (seconds)
	int    next_cursor_ = 0;     // which cursor the next right-click places
};

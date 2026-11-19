#pragma once

#include <complex>
#include <optional>
#include <string>
#include <vector>

#include "workspace.hpp"
#include "core/detect.hpp"
#include "core/digital.hpp"

// Looks at the viewer's signal as digital data, around the playhead (or in the selection):
//   Constellation: I against Q at every symbol instant. Clean QPSK = 4 tight dots.
//   Eye diagram:   all symbols laid on top of each other. A wide-open eye = easy to decode.
//   Bits:          what the symbols decode to, and packets found after a sync word.
// "What is this?" guesses the scheme and symbol rate; "Follow carrier and timing" decodes like a real
// receiver, for recordings that are a bit off 0 Hz or off the symbol clock.
class DigitalWindow {
public:
	static constexpr const char* title = "Digital";
	bool open = true;

	void draw(Workspace& ws);

private:
	void draw_settings(Workspace& ws);
	void draw_identification(Workspace& ws);
	void draw_constellation(const Workspace& ws, const TimeRange& range, const std::vector<std::complex<float>>& symbols);
	void draw_eye(const sig::Signal& signal, const TimeRange& range);
	void draw_bits(const std::vector<std::complex<float>>& symbols);

	// For FSK the decoder looks at the frequency over time; computed once per signal.
	const sig::Signal& decision_signal(const Workspace& ws);

	sig::digital::Scheme   scheme_ = sig::digital::Scheme::BPSK;
	sig::digital::LineCode line_code_ = sig::digital::LineCode::NRZ;
	sig::digital::Timing   timing_;
	int                    symbols_ = 100;  // how many symbols around the playhead (without a selection)
	bool                   all_samples_ = true;
	int                    eye_channel_ = 0; // 0 = I, 1 = Q
	bool                   follow_ = false;  // carrier and timing recovery
	sig::digital::Recovery recovery_;

	std::optional<sig::analysis::Identification> identified_;

	std::string sync_ = "";
	int         sync_errors_ = 0;
	int         packet_bytes_ = 8;

	TrackPtr                   decision_track_;
	sig::digital::Scheme       decision_scheme_{};
	double                     decision_rate_ = 0.0;
	std::optional<sig::Signal> decision_;
};

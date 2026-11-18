#pragma once

#include <complex>
#include <vector>

#include "workspace.hpp"
#include "core/digital.hpp"

// Looks at the viewer's signal as digital data, around the playhead (or in the selection):
//   Constellation: I against Q at every symbol instant. Clean QPSK = 4 tight dots.
//   Eye diagram:   all symbols laid on top of each other. A wide-open eye = easy to decode.
//   Bits:          what the symbols decode to.
class DigitalWindow {
public:
	static constexpr const char* title = "Digital";
	bool open = true;

	void draw(const Workspace& ws);

private:
	void draw_constellation(const Workspace& ws, const TimeRange& range, const std::vector<std::complex<float>>& symbols);
	void draw_eye(const sig::Signal& signal, const TimeRange& range);
	void draw_bits(const std::vector<std::complex<float>>& symbols) const;

	sig::digital::Scheme scheme_ = sig::digital::Scheme::BPSK;
	sig::digital::Timing timing_;
	int                  symbols_ = 100;  // how many symbols around the playhead (without a selection)
	bool                 all_samples_ = true;
	int                  eye_channel_ = 0; // 0 = I, 1 = Q
};

#pragma once

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "workspace.hpp"
#include "core/transforms.hpp"

// Applies an operation to the viewer's signal. The result is added to the library as a new signal
// and shown, so operations can be chained, and the result compared with the original.
class TransformWindow {
public:
	static constexpr const char* title = "Transform";
	bool open = true;

	void draw(Workspace& ws);

private:
	enum class Op {
		Gain, Normalize, RemoveDc, AddNoise,
		Filter, ShiftFrequency, Resample,
		ToIq, ToReal, AmDemodulate, FmDemodulate,
		Trim, Combine,
	};

	static constexpr std::array all_ops{
		Op::Gain, Op::Normalize, Op::RemoveDc, Op::AddNoise,
		Op::Filter, Op::ShiftFrequency, Op::Resample,
		Op::ToIq, Op::ToReal, Op::AmDemodulate, Op::FmDemodulate,
		Op::Trim, Op::Combine,
	};

	friend constexpr std::string_view name(Op op) {
		switch (op) {
		case Op::Gain:           return "Gain";
		case Op::Normalize:      return "Normalise";
		case Op::RemoveDc:       return "Remove DC";
		case Op::AddNoise:       return "Add noise";
		case Op::Filter:         return "Filter";
		case Op::ShiftFrequency: return "Frequency shift";
		case Op::Resample:       return "Resample";
		case Op::ToIq:           return "Real -> I/Q";
		case Op::ToReal:         return "I/Q -> real";
		case Op::AmDemodulate:   return "AM demodulate";
		case Op::FmDemodulate:   return "FM demodulate";
		case Op::Trim:           return "Trim to selection";
		case Op::Combine:        return "Combine with B";
		}
		std::unreachable();
	}

	[[nodiscard]] static const char* explanation(Op op);

	void draw_parameters(const Workspace& ws);
	void draw_filter_response(const sig::Signal& signal) const;
	[[nodiscard]] sig::transform::Result apply(const Workspace& ws) const;
	[[nodiscard]] std::string label() const; // short description of what was done, for the new signal's name

	Op                           op_ = Op::Filter;
	float                        gain_db_ = -6.0f;
	float                        snr_db_ = 20.0f;
	int                          seed_ = 1;
	sig::transform::FilterSpec   filter_;
	float                        shift_hz_ = -1000.0f;
	float                        new_rate_ = 8000.0f;
	sig::transform::Combine      combine_ = sig::transform::Combine::Add;
	TrackPtr                     other_; // B, for Combine
	std::string                  error_;
};

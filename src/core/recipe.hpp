#pragma once

// A recipe is a list of transform steps, like a cooking recipe: "low-pass at 1 kHz, then normalise".
// Every signal made by the Transform window remembers the recipe that made it, so it can be changed,
// re-run on another signal, or saved to a text file and loaded again later.
//
// The text form is one step per line, e.g.
//   filter low-pass 300 3000 101
//   gain -6
//   normalize

#include <array>
#include <charconv>
#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "signal.hpp"
#include "transforms.hpp"
#include "units.hpp"

namespace sig::recipe {

	enum class Op {
		Gain, Normalize, RemoveDc, AddNoise,
		Filter, ShiftFrequency, Resample,
		ToIq, ToReal, AmDemodulate, FmDemodulate,
		Trim, Combine,
	};

	inline constexpr std::array all_ops{
		Op::Gain, Op::Normalize, Op::RemoveDc, Op::AddNoise,
		Op::Filter, Op::ShiftFrequency, Op::Resample,
		Op::ToIq, Op::ToReal, Op::AmDemodulate, Op::FmDemodulate,
		Op::Trim, Op::Combine,
	};

	[[nodiscard]] constexpr std::string_view name(Op op) {
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
		case Op::Trim:           return "Trim";
		case Op::Combine:        return "Combine with B";
		}
		std::unreachable();
	}

	// The word used in recipe files.
	[[nodiscard]] constexpr std::string_view keyword(Op op) {
		switch (op) {
		case Op::Gain:           return "gain";
		case Op::Normalize:      return "normalize";
		case Op::RemoveDc:       return "remove-dc";
		case Op::AddNoise:       return "add-noise";
		case Op::Filter:         return "filter";
		case Op::ShiftFrequency: return "shift";
		case Op::Resample:       return "resample";
		case Op::ToIq:           return "to-iq";
		case Op::ToReal:         return "to-real";
		case Op::AmDemodulate:   return "am-demodulate";
		case Op::FmDemodulate:   return "fm-demodulate";
		case Op::Trim:           return "trim";
		case Op::Combine:        return "combine";
		}
		std::unreachable();
	}

	[[nodiscard]] constexpr std::string_view keyword(transform::FilterType type) {
		switch (type) {
		case transform::FilterType::LowPass:  return "low-pass";
		case transform::FilterType::HighPass: return "high-pass";
		case transform::FilterType::BandPass: return "band-pass";
		case transform::FilterType::BandStop: return "band-stop";
		}
		std::unreachable();
	}

	[[nodiscard]] constexpr std::string_view keyword(transform::Combine combine) {
		switch (combine) {
		case transform::Combine::Add:      return "add";
		case transform::Combine::Subtract: return "subtract";
		case transform::Combine::Multiply: return "multiply";
		}
		std::unreachable();
	}

	// One operation and its settings. Only the settings of `op` matter; the rest keep their values so
	// switching back and forth in the UI doesn't lose them.
	struct Step {
		Op                      op = Op::Filter;
		float                   gain_db = -6.0f;
		float                   snr_db = 20.0f;
		unsigned                seed = 1;
		transform::FilterSpec   filter;
		float                   shift_hz = -1000.0f;
		float                   new_rate = 8000.0f;
		double                  trim_begin = 0.0; // seconds
		double                  trim_end = 1.0;
		transform::Combine      combine = transform::Combine::Add;
		std::shared_ptr<const Signal> other; // Combine: signal B
		std::string             other_name;  // ... and its name (what a recipe file stores)

		bool operator==(const Step&) const = default;
	};

	// Short description, used in signal names: "low-pass 1 kHz".
	[[nodiscard]] inline std::string describe(const Step& step) {
		using enum transform::FilterType;
		switch (step.op) {
		case Op::Gain:           return std::format("gain {:+g} dB", step.gain_db);
		case Op::Normalize:      return "normalised";
		case Op::RemoveDc:       return "DC removed";
		case Op::AddNoise:       return std::format("noise, SNR {:g} dB", step.snr_db);
		case Op::Filter:
			switch (step.filter.type) {
			case LowPass:  return "low-pass " + format_hz(step.filter.high_hz);
			case HighPass: return "high-pass " + format_hz(step.filter.low_hz);
			case BandPass: return std::format("band-pass {} - {}", format_hz(step.filter.low_hz), format_hz(step.filter.high_hz));
			case BandStop: return std::format("band-stop {} - {}", format_hz(step.filter.low_hz), format_hz(step.filter.high_hz));
			}
			break;
		case Op::ShiftFrequency: return std::format("shifted {}{}", step.shift_hz >= 0.0f ? "+" : "", format_hz(step.shift_hz));
		case Op::Resample:       return "resampled to " + format_hz(step.new_rate);
		case Op::ToIq:           return "I/Q";
		case Op::ToReal:         return "real";
		case Op::AmDemodulate:   return "AM demodulated";
		case Op::FmDemodulate:   return "FM demodulated";
		case Op::Trim:           return std::format("trimmed {} - {}", format_seconds(step.trim_begin), format_seconds(step.trim_end));
		case Op::Combine:        return std::format("{} {}", transform::name(step.combine).substr(2, 1), step.other_name); // "+ Sine 440 Hz"
		}
		return "";
	}

	[[nodiscard]] inline transform::Result apply(const Signal& signal, const Step& step) {
		switch (step.op) {
		case Op::Gain:           return transform::gain(signal, step.gain_db);
		case Op::Normalize:      return transform::normalize(signal);
		case Op::RemoveDc:       return transform::remove_dc(signal);
		case Op::AddNoise:       return transform::add_noise(signal, step.snr_db, step.seed);
		case Op::Filter:         return transform::filter(signal, step.filter);
		case Op::ShiftFrequency: return transform::shift_frequency(signal, step.shift_hz);
		case Op::Resample:       return transform::resample(signal, step.new_rate);
		case Op::ToIq:           return transform::to_iq(signal);
		case Op::ToReal:         return transform::to_real(signal);
		case Op::AmDemodulate:   return transform::am_demodulate(signal);
		case Op::FmDemodulate:   return transform::fm_demodulate(signal);
		case Op::Trim:           return transform::trim(signal, step.trim_begin, step.trim_end);
		case Op::Combine:
			if (!step.other) return std::unexpected(std::format("Signal B ('{}') is not loaded", step.other_name));
			return transform::combine(signal, *step.other, step.combine);
		}
		std::unreachable();
	}

	// All steps one after the other. The result is named "<input> | step 1 | step 2".
	[[nodiscard]] inline transform::Result run(const Signal& signal, std::span<const Step> steps) {
		Signal current = signal;
		for (const Step& step : steps) {
			auto next = apply(current, step);
			if (!next) return std::unexpected(std::format("{}: {}", describe(step), next.error()));
			current = std::move(*next);
			current.name = std::format("{} | {}", current.name, describe(step));
		}
		current.description = std::format("Made from {} by {} step(s).", signal.name, steps.size());
		return current;
	}

	// ---- text form ------------------------------------------------------------------------------------

	[[nodiscard]] inline std::string to_text(const Step& step) {
		std::string line{ keyword(step.op) };
		switch (step.op) {
		case Op::Gain:           line += std::format(" {:g}", step.gain_db); break;
		case Op::AddNoise:       line += std::format(" {:g} {}", step.snr_db, step.seed); break;
		case Op::Filter:         line += std::format(" {} {:g} {:g} {}", keyword(step.filter.type), step.filter.low_hz, step.filter.high_hz, step.filter.taps); break;
		case Op::ShiftFrequency: line += std::format(" {:g}", step.shift_hz); break;
		case Op::Resample:       line += std::format(" {:g}", step.new_rate); break;
		case Op::Trim:           line += std::format(" {:g} {:g}", step.trim_begin, step.trim_end); break;
		case Op::Combine:        line += std::format(" {} {}", keyword(step.combine), step.other_name); break;
		default:                 break;
		}
		return line;
	}

	[[nodiscard]] inline std::string to_text(std::span<const Step> steps) {
		std::string text = "# sig-viz recipe: one step per line\n";
		for (const Step& step : steps) text += to_text(step) + "\n";
		return text;
	}

	namespace detail {

		template <typename T>
		[[nodiscard]] bool read_number(std::istringstream& in, T& value) {
			std::string word;
			if (!(in >> word)) return false;
			const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
			return error == std::errc{} && end == word.data() + word.size();
		}

		template <typename E, std::size_t N>
		[[nodiscard]] bool read_keyword(std::istringstream& in, const std::array<E, N>& options, E& value) {
			std::string word;
			if (!(in >> word)) return false;
			for (const E option : options) {
				if (keyword(option) == word) {
					value = option;
					return true;
				}
			}
			return false;
		}

	} // namespace detail

	// A recipe file's steps. Combine steps only get B's name; the caller looks the signal up.
	[[nodiscard]] inline std::expected<std::vector<Step>, std::string> from_text(std::string_view text) {
		std::vector<Step> steps;
		std::istringstream lines{ std::string(text) };
		std::string line;
		for (int number = 1; std::getline(lines, line); ++number) {
			if (line.empty() || line.front() == '#' || line.find_first_not_of(" \t\r") == std::string::npos) continue;
			std::istringstream in{ line };
			Step step;
			bool ok = detail::read_keyword(in, all_ops, step.op);
			switch (step.op) {
			case Op::Gain:           ok = ok && detail::read_number(in, step.gain_db); break;
			case Op::AddNoise:       ok = ok && detail::read_number(in, step.snr_db) && detail::read_number(in, step.seed); break;
			case Op::Filter:
				ok = ok && detail::read_keyword(in, transform::all_filter_types, step.filter.type) && detail::read_number(in, step.filter.low_hz)
					&& detail::read_number(in, step.filter.high_hz) && detail::read_number(in, step.filter.taps);
				break;
			case Op::ShiftFrequency: ok = ok && detail::read_number(in, step.shift_hz); break;
			case Op::Resample:       ok = ok && detail::read_number(in, step.new_rate); break;
			case Op::Trim:           ok = ok && detail::read_number(in, step.trim_begin) && detail::read_number(in, step.trim_end); break;
			case Op::Combine:
				ok = ok && detail::read_keyword(in, transform::all_combines, step.combine);
				std::getline(in >> std::ws, step.other_name);
				ok = ok && !step.other_name.empty();
				break;
			default: break;
			}
			if (!ok) return std::unexpected(std::format("Line {} is not a step I know: '{}'", number, line));
			steps.push_back(std::move(step));
		}
		if (steps.empty()) return std::unexpected("The recipe has no steps");
		return steps;
	}

} // namespace sig::recipe

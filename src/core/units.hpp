#pragma once

// Numbers for humans: "1.5 kHz", "20 ms".

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace sig {

	[[nodiscard]] inline std::string format_hz(double hz) {
		const double size = std::abs(hz);
		if (size >= 1e6) return std::format("{:.4g} MHz", hz / 1e6);
		if (size >= 1e3) return std::format("{:.4g} kHz", hz / 1e3);
		return std::format("{:.4g} Hz", hz);
	}

	[[nodiscard]] inline std::string format_seconds(double seconds) {
		const double size = std::abs(seconds);
		if (size > 0.0 && size < 1e-3) return std::format("{:.4g} us", seconds * 1e6);
		if (size > 0.0 && size < 1.0)  return std::format("{:.4g} ms", seconds * 1e3);
		return std::format("{:.4g} s", seconds);
	}

	[[nodiscard]] inline double to_db(double power_ratio) { return 10.0 * std::log10(std::max(power_ratio, 1e-30)); }

} // namespace sig

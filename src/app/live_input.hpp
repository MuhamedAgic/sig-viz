#pragma once

// Samples arriving in real time: from the microphone, or from a software-defined radio (an RTL-SDR
// stick) through the rtl_tcp server program.
//
// A source collects samples on its own thread; the UI takes whatever has arrived once per frame. The
// two sides only meet at a small, locked buffer.

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class LiveInput {
public:
	virtual ~LiveInput() = default;

	[[nodiscard]] virtual bool  iq() const = 0; // radio: I/Q; microphone: real
	[[nodiscard]] virtual float sample_rate() const = 0;
	virtual void                tune(double /*hz*/) {
	} // radio: change the frequency while running

	// Moves the samples that arrived since the last call to the end of `i` (and `q`, for I/Q).
	void take(std::vector<float>& i, std::vector<float>& q) {
		const std::lock_guard lock(mutex_);
		i.insert(i.end(), i_.begin(), i_.end());
		q.insert(q.end(), q_.begin(), q_.end());
		i_.clear();
		q_.clear();
	}

	// Set when the source stopped by itself (unplugged, connection lost, ...).
	[[nodiscard]] std::string error() const {
		const std::lock_guard lock(mutex_);
		return error_;
	}

protected:
	// For the source's thread.
	void push(const float* i, const float* q, std::size_t count) {
		const std::lock_guard lock(mutex_);
		if (i_.size() > 50'000'000) {
			return; // the UI stopped taking: drop rather than eat all memory
		}
		i_.insert(i_.end(), i, i + count);
		if (q) {
			q_.insert(q_.end(), q, q + count);
		}
	}

	void fail(std::string message) {
		const std::lock_guard lock(mutex_);
		error_ = std::move(message);
	}

private:
	mutable std::mutex mutex_;
	std::vector<float> i_, q_;
	std::string        error_;
};

// The default recording device, mono, 48 kHz. Null (and `error` set) if there is none.
[[nodiscard]] std::unique_ptr<LiveInput> open_microphone(std::string& error);

struct RtlTcpSettings {
	std::string host = "127.0.0.1";
	int         port = 1234;
	double      frequency_hz = 100'000'000.0;
	unsigned    sample_rate = 1'024'000;
	float       gain_db = 0.0f; // 0 = automatic
};

// Connects to a running rtl_tcp server. Null (and `error` set) if that fails.
[[nodiscard]] std::unique_ptr<LiveInput> open_rtl_tcp(const RtlTcpSettings& settings, std::string& error);

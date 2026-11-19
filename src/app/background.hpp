#pragma once

// Runs slow work (FFTs of a whole signal, long filters, ...) on a worker thread, so the UI stays smooth.
//
//   Background<Grid> job;
//   job.start([=](std::stop_token stop) -> std::optional<Grid> { ... });  // replaces a running job
//   if (auto result = job.take()) use(std::move(*result));                // poll once per frame
//   if (job.busy()) show "computing..."
//
// The work gets copies (or shared_ptrs) of what it needs, never references into UI state. Starting a
// new job asks the old one to stop (it should check `stop` now and then and return std::nullopt); its
// result is thrown away. Returning std::nullopt also means "nothing to show".

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

template <typename T>
class Background {
public:
	using Work = std::function<std::optional<T>(std::stop_token)>;

	Background() = default;
	~Background() { cancel(); } // the threads join when the jobs are destroyed

	Background(const Background&) = delete;
	Background& operator=(const Background&) = delete;

	void start(Work work) {
		cancel();
		auto state = std::make_shared<State>();
		current_ = Job{
			.state = state,
			.thread = std::jthread([state, work = std::move(work)](std::stop_token stop) {
				state->result = work(stop);
				state->done = true; // publishes `result` to the UI thread
			}),
		};
	}

	// Stops waiting for the running job (if any).
	void cancel() {
		if (!current_) return;
		current_->thread.request_stop();
		retired_.push_back(std::move(*current_));
		current_.reset();
	}

	[[nodiscard]] bool busy() const { return current_ && !current_->state->done; }

	// The finished job's result, once. std::nullopt while it is still running (or when it gave none).
	[[nodiscard]] std::optional<T> take() {
		std::erase_if(retired_, [](const Job& job) { return job.state->done.load(); });
		if (!current_ || !current_->state->done) return std::nullopt;
		std::optional<T> result = std::move(current_->state->result);
		current_.reset();
		return result;
	}

private:
	struct State {
		std::optional<T>  result;
		std::atomic<bool> done = false;
	};

	struct Job {
		std::shared_ptr<State> state;
		std::jthread           thread;
	};

	std::optional<Job> current_;
	std::vector<Job>   retired_; // asked to stop; joined once they have
};

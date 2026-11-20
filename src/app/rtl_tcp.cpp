// A client for rtl_tcp, the network server that comes with the RTL-SDR drivers: run `rtl_tcp` on the
// computer with the radio stick, and this connects to it. No radio library needed: it is a plain TCP
// stream of 8-bit I/Q samples (the cu8 format), plus 5-byte commands to tune it.
//
// Kept out of every file that includes raylib.h: the Windows socket headers clash with raylib's names.

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
constexpr Socket no_socket = INVALID_SOCKET;
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
constexpr Socket no_socket = -1;
#define closesocket close
#endif

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <thread>
#include <vector>

#include "live_input.hpp"

namespace {

	class RtlTcp final : public LiveInput {
	public:
		explicit RtlTcp(const RtlTcpSettings& settings) : settings_{ settings } {
		}

		bool open(std::string& error) {
#if defined(_WIN32)
			WSADATA data;
			if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
				error = "Windows sockets would not start.";
				return false;
			}
			wsa_started_ = true;
#endif
			addrinfo hints{};
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			addrinfo* found = nullptr;
			if (getaddrinfo(settings_.host.c_str(), std::to_string(settings_.port).c_str(), &hints, &found) != 0 ||
			    !found) {
				error = std::format("Can't find '{}'.", settings_.host);
				return false;
			}
			for (addrinfo* a = found; a && socket_ == no_socket; a = a->ai_next) {
				socket_ = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
				if (socket_ != no_socket && connect(socket_, a->ai_addr, static_cast<int>(a->ai_addrlen)) != 0) {
					closesocket(socket_);
					socket_ = no_socket;
				}
			}
			freeaddrinfo(found);
			if (socket_ == no_socket) {
				error =
					std::format("Can't connect to {}:{}. Is rtl_tcp running there?", settings_.host, settings_.port);
				return false;
			}

			send_command(0x02, settings_.sample_rate);
			tune(settings_.frequency_hz);
			if (settings_.gain_db > 0.0) {
				send_command(0x03, 1); // manual gain
				send_command(0x04, static_cast<std::uint32_t>(std::lround(settings_.gain_db * 10.0)));
			}
			else {
				send_command(0x03, 0); // automatic gain
			}

			reader_ = std::jthread([this](std::stop_token stop) { read(stop); });
			return true;
		}

		~RtlTcp() override {
			reader_.request_stop();
			if (socket_ != no_socket) {
				shutdown(socket_, 2); // wakes the reader up from recv()
				closesocket(socket_);
			}
			if (reader_.joinable()) {
				reader_.join();
			}
#if defined(_WIN32)
			if (wsa_started_) {
				WSACleanup();
			}
#endif
		}

		[[nodiscard]] bool iq() const override {
			return true;
		}
		[[nodiscard]] float sample_rate() const override {
			return static_cast<float>(settings_.sample_rate);
		}

		void tune(double hz) override {
			send_command(0x01, static_cast<std::uint32_t>(std::llround(hz)));
		}

	private:
		// A command: one byte saying what, four bytes (big-endian) saying how much.
		void send_command(std::uint8_t command, std::uint32_t value) {
			const std::array<char, 5> bytes{ static_cast<char>(command),
				                             static_cast<char>(value >> 24),
				                             static_cast<char>(value >> 16),
				                             static_cast<char>(value >> 8),
				                             static_cast<char>(value) };
			::send(socket_, bytes.data(), static_cast<int>(bytes.size()), 0);
		}

		void read(const std::stop_token& stop) {
			std::vector<unsigned char> bytes(1 << 16);
			std::vector<float>         i, q;
			std::size_t                header = 12; // "RTL0", tuner type, gain count: not needed
			bool                       odd = false; // a byte of I arrived without its Q
			float                      pending_i = 0.0f;
			while (!stop.stop_requested()) {
				const int got = recv(socket_, reinterpret_cast<char*>(bytes.data()), static_cast<int>(bytes.size()), 0);
				if (got <= 0) {
					if (!stop.stop_requested()) {
						fail("The connection to rtl_tcp was closed.");
					}
					return;
				}
				std::size_t at = 0;
				const auto  count = static_cast<std::size_t>(got);
				while (header > 0 && at < count) {
					--header;
					++at;
				}
				i.clear();
				q.clear();
				for (; at < count; ++at) {
					const float value = (static_cast<float>(bytes[at]) - 127.5f) / 127.5f;
					if (!odd) {
						pending_i = value;
					}
					else {
						i.push_back(pending_i);
						q.push_back(value);
					}
					odd = !odd;
				}
				push(i.data(), q.data(), i.size());
			}
		}

		RtlTcpSettings settings_;
		Socket         socket_ = no_socket;
		bool           wsa_started_ = false;
		std::jthread   reader_;
	};

} // namespace

std::unique_ptr<LiveInput> open_rtl_tcp(const RtlTcpSettings& settings, std::string& error) {
	auto radio = std::make_unique<RtlTcp>(settings);
	if (!radio->open(error)) {
		return nullptr;
	}
	return radio;
}

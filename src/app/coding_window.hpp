#pragma once

#include <optional>
#include <string>
#include <vector>

#include "background.hpp"
#include "workspace.hpp"
#include "core/coding.hpp"
#include "core/radio_link.hpp"

// Error correction, end to end: a message is encoded with a polar code (AFF3CT), interleaved, damaged on
// purpose (or sent as a real signal through the other windows), deinterleaved and decoded. Shows how many
// bits arrived wrong, which ones, and whether the code repaired them; plus the classic error-rate curve.
class CodingWindow {
public:
	static constexpr const char* title = "Error correction";
	bool open = true;

	void draw(Workspace& ws);

private:
	enum class MessageSource { Text, Random };
	enum class Channel { Bits, Signal }; // damage the bits directly, or receive the viewer's signal

	// Everything the result depends on; a change starts a new run.
	struct Inputs {
		MessageSource                     source = MessageSource::Text;
		std::string                       text;
		int                               random_bits = 2000;
		unsigned                          seed = 1;
		sig::coding::CodeSpec             code;
		sig::coding::InterleaverSpec      interleaver;
		std::vector<sig::coding::Damage>  damage;
		Channel                           channel = Channel::Bits;
		TrackPtr                          signal;
		sig::link::Settings               link;

		bool operator==(const Inputs&) const = default;
	};

	struct Outcome {
		Inputs                                  inputs;
		std::optional<sig::coding::Transmission> tx;
		std::optional<sig::coding::Reception>    rx;
		std::optional<sig::coding::Reception>    uncoded; // the same damage without the code, to compare
		bool                                    inverted = false;
		std::string                             error;
	};

	struct Curve {
		std::string                          label;
		std::vector<sig::coding::CurvePoint> points;
	};

	[[nodiscard]] Inputs inputs(const Workspace& ws) const;
	[[nodiscard]] static Outcome run(Inputs in);

	void draw_message();
	void draw_code();
	void draw_channel(Workspace& ws);
	void draw_damage();
	void draw_summary(const Outcome& out);
	void draw_error_map(const Outcome& out);
	void draw_soft_bits(const Outcome& out);
	void draw_text(const Outcome& out);
	void draw_curve();

	// Settings
	MessageSource                    source_ = MessageSource::Text;
	std::string                      text_ = "Hello from sig-viz! Polar codes repair the bits a noisy channel breaks, "
		"and the interleaver spreads bursts of errors so that no codeword gets too many.";
	int                              random_bits_ = 2000;
	unsigned                         seed_ = 1;
	sig::coding::CodeSpec            code_;
	sig::coding::InterleaverSpec     interleaver_;
	std::vector<sig::coding::Damage> damage_{ sig::coding::Damage{ .pattern = sig::coding::Pattern::Burst, .effect = sig::coding::Effect::FlipHard } };
	Channel                          channel_ = Channel::Bits;
	sig::link::Settings              link_;
	std::string                      make_error_;

	// Result
	Background<Outcome>    job_;
	std::optional<Inputs>  requested_;
	std::optional<Outcome> outcome_;

	// Error-rate curve
	Background<std::vector<sig::coding::CurvePoint>> curve_job_;
	std::string        curve_label_;
	std::vector<Curve> curves_;
	float              curve_from_ = 0.0f, curve_to_ = 5.0f, curve_step_ = 0.5f;
	int                curve_frames_ = 2000;
};

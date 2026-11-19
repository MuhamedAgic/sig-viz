#pragma once

// Short explanations shown when hovering a "(?)" or a measurement name. Kept together so the
// wording stays consistent; the longer stories with examples are in the Guide window.
// (Plain ASCII only: the default font has no other characters.)

namespace tips {

	// ---- spectrum ----
	inline constexpr const char* fft_size =
		"How many samples go into one FFT.\n"
		"Bigger: tells close frequencies apart better (finer bars), but needs a longer piece of signal, "
		"so it reacts slower to changes. Like a camera: a long exposure shows more detail but blurs motion.";
	inline constexpr const char* window =
		"The FFT assumes the piece of signal repeats forever. The jump where its end meets its start "
		"smears energy everywhere ('leakage'). A window fades the piece in and out to hide that jump.\n\n"
		"Hann: sharp peaks.  Blackman: good all-rounder.  Flat top: exact levels.\n"
		"The leakage also limits how clean a tone can measure: SNR up to about 55 / 70 / 90 dB.";
	inline constexpr const char* dbfs =
		"Levels are in dBFS: decibels relative to full scale. 0 dBFS is the loudest a sample can be.\n"
		"-6 dB = half the amplitude, -20 dB = a tenth, -60 dB = a thousandth.\n\n"
		"Plot: drag to pan, wheel to zoom, double-click to fit, right-click for options.";
	inline constexpr const char* rms =
		"Root mean square: the 'average strength' of the signal, the level you hear as loudness.";
	inline constexpr const char* peak = "The single largest sample. Must stay below 0 dBFS, or the signal clips.";
	inline constexpr const char* dc =
		"The average value. Should be about 0: a signal that is shifted up or down wastes headroom.";
	inline constexpr const char* crest =
		"How much higher the peaks are than the average (peak / RMS).\n"
		"Sine: 3 dB.  Square: 0 dB.  Speech or a drum: 15+ dB (short, sharp peaks).";
	inline constexpr const char* strongest =
		"The loudest frequency, measured more precisely than the bar spacing by looking at its neighbours.";
	inline constexpr const char* snr =
		"Signal-to-noise ratio: how much stronger the main tone is than everything else.\n"
		"Like one voice at a party: 40 dB = a quiet room, 0 dB = the voice is as loud as the crowd.";
	inline constexpr const char* thd =
		"Total harmonic distortion: how strong the overtones (2x, 3x, ... the frequency) are compared to "
		"the tone itself. A pure sine has none; a square wave has a lot.";

	// ---- spectrogram ----
	inline constexpr const char* spectrogram_size =
		"Small: sharp in time (clicks stay short) but blurry in frequency.\n"
		"Large: sharp in frequency but blurry in time. You cannot have both: that is the uncertainty principle.";
	inline constexpr const char* db_range = "Levels below the first number are darkest, above the second brightest. "
		"Lower the first to see quiet details, raise it to hide the noise.";

	// ---- digital ----
	inline constexpr const char* scheme =
		"How bits become symbols.\n"
		"OOK: carrier on = 1, off = 0 (like Morse code with a flashlight).\n"
		"BPSK: the wave is flipped upside down for a 1. 1 bit per symbol.\n"
		"QPSK: four phases, 2 bits per symbol: I carries one bit, Q the other.";
	inline constexpr const char* symbol_rate =
		"Symbols per second (baud). Must match the transmitter, like knowing the tempo before you can count beats.";
	inline constexpr const char* timing =
		"Where inside each symbol to look, as a fraction of a symbol. The middle (0.5) is usually best: "
		"far away from the transitions. Watch the constellation get tighter as you tune it.";
	inline constexpr const char* constellation =
		"Every received symbol as a dot, like darts on a dartboard. The crosses are the bullseyes.\n"
		"Tight clusters = clean signal. Noise spreads them; when clusters touch, bits are read wrong.";
	inline constexpr const char* eye =
		"All symbols drawn on top of each other, like a long-exposure photo. The open gap in the middle "
		"(the 'eye') is where the receiver decides. Noise and filtering close the eye.";

	// ---- transforms / generator ----
	inline constexpr const char* taps =
		"How many neighbouring samples each output sample is mixed from. "
		"More taps = a sharper edge between 'kept' and 'removed' frequencies, but slower to compute.";
	inline constexpr const char* noise_snr =
		"How much stronger the signal is than the added noise. 20 dB = signal power 100x the noise power.";
	inline constexpr const char* iq =
		"Real: one number per sample. I/Q: two (I and Q), a point going round a circle. "
		"With I/Q you can tell which way it turns, so +1 kHz and -1 kHz are different. See the Guide.";
	inline constexpr const char* modulation =
		"Putting a message on a carrier wave.\n"
		"AM: the message changes how strong the carrier is (like a dimmer).\n"
		"FM: it changes how fast it vibrates (like a siren).\n"
		"Sweep: the frequency glides from one value to another (a chirp).";
	inline constexpr const char* carrier = "The radio frequency the data rides on. 0 Hz = baseband: the data itself, "
		"which is what the Digital window decodes.";

	// ---- viewer / compare ----
	inline constexpr const char* listen =
		"An I/Q signal is not one sound, so pick what to hear:\n"
		"I or Q: one of the two channels.\n"
		"AM: how strong the signal is (its envelope).\n"
		"FM: how fast its phase turns (its frequency).";
	inline constexpr const char* correlation =
		"How alike the shapes of A and B are: 1 = identical (even at another volume), 0 = unrelated, -1 = mirror images.";
	inline constexpr const char* auto_align =
		"Slides B along A, like sliding a transparency over a drawing, and keeps the position where they "
		"match best (cross-correlation). Finds delays.";
	inline constexpr const char* difference = "What is left after subtracting B from A. Silence means they are identical.";

} // namespace tips

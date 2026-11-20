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

	inline constexpr const char* ab_listen =
		"Switch between hearing the original (A) and the result (B) while it plays. Our ears forget quickly, "
		"so switching back and forth is the best way to hear what a filter really does.";

	// ---- digital receiver ----
	inline constexpr const char* line_coding =
		"How data bits become the bits that are sent.\n"
		"NRZ: as they are.\n"
		"Manchester: every bit becomes a pair (1 -> 10, 0 -> 01), so there is an edge in every bit and the "
		"receiver can always keep time. Common in remote controls and car keys.";
	inline constexpr const char* follow =
		"Decode like a real receiver: follow the carrier when it is a bit off 0 Hz (the constellation stops "
		"spinning) and adjust to the symbol clock (no Timing slider needed). Use it for real recordings.";
	inline constexpr const char* identify =
		"Guesses the modulation, symbol rate and frequency of the part around the playhead (or the selection), "
		"and explains how it decided. Works best on a part that only holds the signal: select a burst first.";
	inline constexpr const char* sync_word =
		"A fixed bit pattern a transmitter sends before every packet, so the receiver knows where the data "
		"starts. Type it here to find the packets; what follows is shown as bytes (hex) and text.";
	inline constexpr const char* bursts =
		"Radio recordings are mostly silence with short transmissions. This lists every stretch that is "
		"clearly louder than the background (the noise floor). Click one to select it.";

	// ---- live ----
	inline constexpr const char* rtl_tcp =
		"rtl_tcp is a small server that comes with the RTL-SDR drivers. Plug in the radio stick, run "
		"`rtl_tcp` (it listens on port 1234), then press Start here. Type a frequency and press Enter to tune, "
		"e.g. 100 MHz for FM radio, 433.92 MHz for remote controls and weather stations.";

	// ---- audio ----
	inline constexpr const char* pitch =
		"The note being played, like a guitar tuner. Found by sliding the sound along itself: at a shift of exactly "
		"one period it lines up with itself. 100 cents = one semitone (the step between two piano keys); within "
		"5 cents sounds in tune.";
	inline constexpr const char* lufs =
		"Loudness as we hear it (LUFS: loudness units relative to full scale). Unlike RMS it weighs frequencies "
		"like our ears do. Momentary = last 0.4 s, short-term = last 3 s, integrated = the whole signal without "
		"its silences. Streaming services play music at about -14 LUFS, TV at -23.";
	inline constexpr const char* clipping =
		"A recording can't go beyond full scale (1.0). When the sound was louder, the tops of the waves are cut "
		"off flat: clipping. It sounds harsh and can't be undone; record at a lower level next time.";

	// ---- spectrum analyser tools ----
	inline constexpr const char* max_hold =
		"Remembers the highest level each frequency has reached while you play or scrub. "
		"Catches short bursts you would otherwise miss.";
	inline constexpr const char* freeze =
		"Keeps a grey copy of the spectrum as it is now. Change something (a filter, another part of the "
		"signal) and compare the live trace with it: before and after on one plot.";

	// ---- error correction ----
	inline constexpr const char* polar_code =
		"Forward error correction: the sender adds clever redundancy, so the receiver can repair wrong bits "
		"without asking for them again. Like spelling a name on the phone: 'M as in Mike' survives a bad line.\n\n"
		"Polar codes (used in 5G) split N positions into good and bad ones: the good ones carry data, the bad "
		"ones are 'frozen' to 0, which the decoder knows. Off: the bits are sent as they are, to compare.\n"
		"Encoders and decoders come from the AFF3CT library.";
	inline constexpr const char* polar_nk =
		"N: bits per codeword, a power of two. K: how many of them carry information (CRC included).\n"
		"The rest (N - K) is redundancy. Rate K/N: 0.5 = half the bits are data. Lower rate = repairs more, "
		"but sends more. Longer codewords repair better at the same rate.";
	inline constexpr const char* polar_decoder =
		"SC decides bit after bit and never looks back: one early mistake ruins the rest.\n"
		"SCL keeps the L most likely versions (a list) and picks the best at the end: like keeping several "
		"guesses at a crossword answer until the crossing words decide.\n"
		"CA-SCL picks the version whose CRC checks out: the strongest.";
	inline constexpr const char* crc =
		"A checksum on every codeword's data (cyclic redundancy check). The receiver recomputes it: if it does not "
		"match, decoding failed. CA-SCL also uses it to choose between its guesses.";
	inline constexpr const char* polar_design =
		"Which positions are good or bad depends on how noisy the channel is expected to be. This is the "
		"signal-to-noise ratio the code is tuned for (Eb/N0, see the error-rate curve).";
	inline constexpr const char* interleaver =
		"Shuffles the bits before sending, and puts them back in order after receiving. Errors often come in "
		"bursts (a click, a fade); shuffled back, a burst becomes a few scattered errors in many codewords, "
		"which each can repair, instead of many errors in one codeword, which cannot.\n"
		"Like not putting all eggs in one basket.";
	inline constexpr const char* coding_channel =
		"Damage the bits directly: the soft bits are perfect (+1 / -1) and only your damage steps change them.\n\n"
		"Receive the viewer's signal: press 'Make a signal from the sent bits' first. The viewer's signal is then "
		"demodulated (radio settings) into soft bits, so noise, filters and other changes from the other windows "
		"show up here. The damage steps come on top.";
	inline constexpr const char* damage =
		"Soft bits: + means 0, - means 1, and the size says how sure the receiver is.\n"
		"Flip (soft): the sign changes, the confidence stays: a confident mistake.\n"
		"Flip (hard): becomes a fully confident mistake.\n"
		"Erase: becomes 0, 'no idea'. A decoder repairs about twice as many erasures as errors.\n"
		"Add noise: like a noisy radio channel; small values may cross 0 and become wrong.";
	inline constexpr const char* error_map =
		"Top: every bit as it went over the air. Bottom: the same bits after deinterleaving, one codeword per row. "
		"A burst on top (a red run) is spread out below. The strip on the left of each row turns red when that "
		"codeword could not be repaired.";
	inline constexpr const char* soft_bits =
		"Every received bit as a dot: its soft value. Dots far from 0 are sure, dots near 0 are doubtful. "
		"Red dots ended up on the wrong side of 0: those are the bit errors before decoding. Soft decoders use "
		"the doubt: a doubtful bit is the first suspect.";
	inline constexpr const char* eb_n0 =
		"Eb/N0: signal-to-noise ratio per message bit, in dB. A code spends energy on redundant bits; Eb/N0 counts "
		"only the bits that carry the message, so codes of different rates compare fairly. Every 3 dB doubles the energy.\n"
		"At very low Eb/N0 a code can be worse than none; then it falls steeply (the 'waterfall').";

} // namespace tips

#ifndef SIGNALSMITH_STRETCH_H
#define SIGNALSMITH_STRETCH_H

#include "signalsmith-linear/stft.h" // https://github.com/Signalsmith-Audio/linear

#include <vector>
#include <array>
#include <algorithm>
#include <functional>
#include <random>
#include <limits>
#include <type_traits>

// LYNX (round 190kr): the per-band state is kept as structure-of-arrays and the spectral loops run four bands at a
// time with SSE2 where available. Every value is computed by the same IEEE operations in the same order as the
// original per-band code (no reassociation, no FMA, no approximations), so the output is bit-identical.
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#	include <emmintrin.h>
#	define SIGNALSMITH_STRETCH_SSE2 1
#else
#	define SIGNALSMITH_STRETCH_SSE2 0
#endif
// LYNX (round 190kz): the same loops eight bands at a time with AVX2 where the CPU has it (picked at run time:
// src/core/CpuFeatures.h, LYNX_CPU=sse2 forces the four-at-a-time loops). The same IEEE operations per band in the
// same order (no FMA), so all paths give the same bits; the remaining bands go through the SSE2 / scalar loops.
#if SIGNALSMITH_STRETCH_SSE2 && (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__)) && defined(__has_include)
#	if __has_include("core/CpuFeatures.h")
#		include "core/CpuFeatures.h"
#		include <immintrin.h>
#		define SIGNALSMITH_STRETCH_AVX LYNX_CPU_DISPATCH
#	endif
#endif
#ifndef SIGNALSMITH_STRETCH_AVX
#	define SIGNALSMITH_STRETCH_AVX 0
#endif
#if SIGNALSMITH_STRETCH_AVX
#	define SIGNALSMITH_AVX2 __attribute__((target("avx2")))
#	define SIGNALSMITH_AVX2_INLINE __attribute__((target("avx2"), always_inline)) inline
#endif

namespace signalsmith { namespace stretch {

namespace _impl {
	template<bool conjugateSecond=false, typename V>
	static std::complex<V> mul(const std::complex<V> &a, const std::complex<V> &b) {
		return conjugateSecond ? std::complex<V>{
			b.real()*a.real() + b.imag()*a.imag(),
				b.real()*a.imag() - b.imag()*a.real()
		} : std::complex<V>{
			a.real()*b.real() - a.imag()*b.imag(),
			a.real()*b.imag() + a.imag()*b.real()
		};
	}
	template<typename V>
	static V norm(const std::complex<V> &a) {
		V r = a.real(), i = a.imag();
		return r*r + i*i;
	}
}

template<typename Sample=float, class RandomEngine=void>
struct SignalsmithStretch {
	static constexpr size_t version[3] = {1, 3, 2};

	SignalsmithStretch() : randomEngine(std::random_device{}()) {}
	SignalsmithStretch(long seed) : randomEngine(seed) {}
		
	// The difference between the internal position (centre of a block) and the input samples you're supplying
	int inputLatency() const {
		return int(stft.analysisLatency());
	}
	int outputLatency() const {
		return int(stft.synthesisLatency() + _splitComputation*stft.defaultInterval());
	}
	
	void reset() {
		stft.reset(0.1);
		stashedInput = stft.input;
		stashedOutput = stft.output;
		
		prevInputOffset = -1;
		clearBands(true);
		silenceCounter = 0;
		didSeek = false;
		blockProcess = {};
		freqEstimateWeighted = freqEstimateWeight = 0;
	}

	// Configures using a default preset
	void presetDefault(int nChannels, Sample sampleRate, bool splitComputation=false) {
		configure(nChannels, static_cast<int>(sampleRate*0.12), static_cast<int>(sampleRate*0.03), splitComputation);
	}
	void presetCheaper(int nChannels, Sample sampleRate, bool splitComputation=true) {
		configure(nChannels, static_cast<int>(sampleRate*0.1), static_cast<int>(sampleRate*0.04), splitComputation);
	}

	// Manual setup
	void configure(int nChannels, int blockSamples, int intervalSamples, bool splitComputation=false) {
		_splitComputation = splitComputation;
		channels = nChannels;
		stft.configure(channels, channels, blockSamples, intervalSamples + 1);
		stft.setInterval(intervalSamples, stft.kaiser);
		stft.reset(Sample(0.1));
		stashedInput = stft.input;
		stashedOutput = stft.output;

		bands = int(stft.bands());
		allocateBands();
		
		peaks.reserve(bands/2);
		energy.resize(bands);
		smoothedEnergy.resize(bands);
		outputMap.resize(bands);
		predEnergy.resize(channels*bands);
		predInR.resize(channels*bands);
		predInI.resize(channels*bands);

		blockProcess = {};
		formantMetric.resize(bands + 2);

		tmpProcessBuffer.resize(blockSamples + intervalSamples);
		tmpPreRollBuffer.resize(outputLatency()*channels);
	}
	// For querying the existing config
	int blockSamples() const {
		return int(stft.blockSamples());
	}
	int intervalSamples() const {
		return int(stft.defaultInterval());
	}
	bool splitComputation() const {
		return _splitComputation;
	}

	/// Frequency multiplier, and optional tonality limit (as multiple of sample-rate)
	void setTransposeFactor(Sample multiplier, Sample tonalityLimit=0) {
		freqMultiplier = multiplier;
		if (tonalityLimit > 0) {
			freqTonalityLimit = tonalityLimit/std::sqrt(multiplier); // compromise between input and output limits
		} else {
			freqTonalityLimit = 1;
		}
		customFreqMap = nullptr;
	}
	void setTransposeSemitones(Sample semitones, Sample tonalityLimit=0) {
		setTransposeFactor(std::pow(2, semitones/12), tonalityLimit);
	}
	// Sets a custom frequency map - should be monotonically increasing
	void setFreqMap(std::function<Sample(Sample)> inputToOutput) {
		customFreqMap = inputToOutput;
	}

	void setFormantFactor(Sample multiplier, bool compensatePitch=false) {
		formantMultiplier = multiplier;
		invFormantMultiplier = 1/multiplier;
		formantCompensation = compensatePitch;
	}
	void setFormantSemitones(Sample semitones, bool compensatePitch=false) {
		setFormantFactor(std::pow(2, semitones/12), compensatePitch);
	}
	// Rough guesstimate of the fundamental frequency, used for formant analysis. 0 means attempting to detect the pitch
	void setFormantBase(Sample baseFreq=0) {
		formantBaseFreq = baseFreq;
	}
	
	// Provide previous input ("pre-roll") to smoothly change the input location without interrupting the output.  This doesn't do any calculation, just copies intput to a buffer.
	// You should ideally feed it `seekLength()` frames of input, unless it's directly after a `.reset()` (in which case `.outputSeek()` might be a better choice)
	template<class Inputs>
	void seek(Inputs &&inputs, int inputSamples, double playbackRate) {
		tmpProcessBuffer.resize(0);
		tmpProcessBuffer.resize(stft.blockSamples() + stft.defaultInterval());

		int startIndex = std::max<int>(0, inputSamples - int(tmpProcessBuffer.size())); // start position in input
		int padStart = int(tmpProcessBuffer.size() + startIndex) - inputSamples; // start position in tmpProcessBuffer

		Sample totalEnergy = 0;
		for (int c = 0; c < channels; ++c) {
			auto &&inputChannel = inputs[c];
			for (int i = startIndex; i < inputSamples; ++i) {
				Sample s = inputChannel[i];
				totalEnergy += s*s;
				tmpProcessBuffer[i - startIndex + padStart] = s;
			}
			
			stft.writeInput(c, tmpProcessBuffer.size(), tmpProcessBuffer.data());
		}
		stft.moveInput(tmpProcessBuffer.size());
		if (totalEnergy >= noiseFloor) {
			silenceCounter = 0;
			silenceFirst = true;
		}
		didSeek = true;
		seekTimeFactor = (playbackRate*stft.defaultInterval() > 1) ? 1/playbackRate : stft.defaultInterval();
	}
	int seekLength() const {
		return int(stft.blockSamples() + stft.defaultInterval());
	}
	
	// Moves the input position *and* pre-calculates some output, so that the next samples returned from `.process()` are aligned to the beginning of the sample.
	// The time-stretch rate is inferred from `inputLength`, so use `.outputSeekLength()` to get a correct value for that.
	template<class Inputs>
	void outputSeek(Inputs &&inputs, int inputLength) {
		// TODO: add fade-out parameter to avoid clicks, instead of doing a full reset
		reset();
		// Assume we've been handed enough surplus input to produce `outputLatency()` samples of pre-roll
		int surplusInput = std::max<int>(inputLength - inputLatency(), 0);
		Sample playbackRate = surplusInput/Sample(outputLatency());

		// Move the input position to the start of the sound
		int seekSamples = inputLength - surplusInput;
		seek(inputs, seekSamples, playbackRate);
		
		tmpPreRollBuffer.resize(outputLatency()*channels);
		struct BufferOutput {
			Sample *samples;
			int length;
			
			Sample * operator[](int c) {
				return samples + c*length;
			}
		} preRollOutput{tmpPreRollBuffer.data(), outputLatency()};
		
		// Use the surplus input to produce pre-roll output
		OffsetIO<Inputs> offsetInput{inputs, seekSamples};
		process(offsetInput, surplusInput, preRollOutput, preRollOutput.length);
		
		// put the thing down, flip it and reverse it
		for (auto &v : tmpPreRollBuffer) v = -v;
		for (int c = 0; c < channels; ++c) {
			std::reverse(preRollOutput[c], preRollOutput[c] + preRollOutput.length);
			stft.addOutput(c, preRollOutput.length, preRollOutput[c]);
		}
	}
	int outputSeekLength(Sample playbackRate) const {
		return inputLatency() + playbackRate*outputLatency();
	}

	template<class Inputs, class Outputs>
	void process(Inputs &&inputs, int inputSamples, Outputs &&outputs, int outputSamples) {
#ifdef SIGNALSMITH_STRETCH_PROFILE_PROCESS_START
		SIGNALSMITH_STRETCH_PROFILE_PROCESS_START(inputSamples, outputSamples);
#endif
		int prevCopiedInput = 0;
		auto copyInput = [&](int toIndex){

			int length = std::min<int>(int(stft.blockSamples() + stft.defaultInterval()), toIndex - prevCopiedInput);
			tmpProcessBuffer.resize(length);
			int offset = toIndex - length;
			for (int c = 0; c < channels; ++c) {
				auto &&inputBuffer = inputs[c];
				for (int i = 0; i < length; ++i) {
					tmpProcessBuffer[i] = inputBuffer[i + offset];
				}
				stft.writeInput(c, length, tmpProcessBuffer.data());
			}
			stft.moveInput(length);
			prevCopiedInput = toIndex;
		};

		// LYNX (round 190kr): only `totalEnergy < noiseFloor` is used. Every term is >= 0 and float addition rounds
		// monotonically, so one term that is not below the floor (or a NaN) decides it without the sum; otherwise the
		// sum is taken as before.
		bool loud = false;
		for (int c = 0; c < channels && !loud; ++c) {
			auto &&inputChannel = inputs[c];
			for (int i = 0; i < inputSamples; ++i) {
				Sample s = inputChannel[i];
				if (!(s*s < noiseFloor)) {
					loud = true;
					break;
				}
			}
		}
		Sample totalEnergy = 0;
		if (!loud) {
			for (int c = 0; c < channels; ++c) {
				auto &&inputChannel = inputs[c];
				for (int i = 0; i < inputSamples; ++i) {
					Sample s = inputChannel[i];
					totalEnergy += s*s;
				}
			}
		}

		if (!loud && totalEnergy < noiseFloor) {
			if (silenceCounter >= 2*stft.blockSamples()) {
				if (silenceFirst) { // first block of silence processing
					silenceFirst = false;
					//stft.reset();
					blockProcess = {};
					clearBands(true);
				}
			
				if (inputSamples > 0) {
					// copy from the input, wrapping around if needed
					for (int outputIndex = 0; outputIndex < outputSamples; ++outputIndex) {
						int inputIndex = outputIndex%inputSamples;
						for (int c = 0; c < channels; ++c) {
							outputs[c][outputIndex] = inputs[c][inputIndex];
						}
					}
				} else {
					for (int c = 0; c < channels; ++c) {
						auto &&outputChannel = outputs[c];
						for (int outputIndex = 0; outputIndex < outputSamples; ++outputIndex) {
							outputChannel[outputIndex] = 0;
						}
					}
				}

				// Store input in history buffer
				copyInput(inputSamples);
				return;
			} else {
				silenceCounter += inputSamples;
			}
		} else {
			silenceCounter = 0;
			silenceFirst = true;
		}
		
		for (int outputIndex = 0; outputIndex < outputSamples;) {
			bool newBlock = blockProcess.samplesSinceLast >= stft.defaultInterval();
			if (newBlock) {
				blockProcess.step = 0;
				blockProcess.steps = 0; // how many processing steps this block will have
				blockProcess.samplesSinceLast = 0;
				
				// Time to process a spectrum!  Where should it come from in the input?
				int inputOffset = static_cast<int>(std::round(outputIndex*Sample(inputSamples)/outputSamples));
				int inputInterval = inputOffset - prevInputOffset;
				prevInputOffset = inputOffset;
				
				copyInput(inputOffset);
				// LYNX (round 190kr): without split computation the analysis runs now, before any more input is written,
				// so it reads stft.input itself (no copy of the whole input history, and no swaps)
				if (_splitComputation) stashedInput = stft.input; // save the input state, since that's what we'll analyse later
				if (_splitComputation) {
					stashedOutput = stft.output; // save the current output, and read from it
					stft.moveOutput(stft.defaultInterval()); // the actual input jumps forward in time by one interval, ready for the synthesis
				}

				blockProcess.newSpectrum = didSeek || (inputInterval > 0);
				blockProcess.mappedFrequencies = customFreqMap || freqMultiplier != 1;
				if (blockProcess.newSpectrum) {
					// make sure the previous input is the correct distance in the past (give or take 1 sample)
					blockProcess.reanalysePrev = didSeek || std::abs(inputInterval - int(stft.defaultInterval())) > 1;
					if (blockProcess.reanalysePrev) blockProcess.steps += stft.analyseSteps() + 1;

					// analyse a new input
					blockProcess.steps += stft.analyseSteps() + 1;
				}
				
				blockProcess.processFormants = formantMultiplier != 1 || (formantCompensation && blockProcess.mappedFrequencies);

				blockProcess.timeFactor = didSeek ? seekTimeFactor : stft.defaultInterval()/static_cast<Sample>(std::max(1, inputInterval));
				didSeek = false;

				updateProcessSpectrumSteps();
				blockProcess.steps += processSpectrumSteps;

				blockProcess.steps += stft.synthesiseSteps() + 1;
			}
			
			size_t processToStep = newBlock ? blockProcess.steps : 0;
			if (_splitComputation) {
				Sample processRatio = Sample(blockProcess.samplesSinceLast + 1)/stft.defaultInterval();
				processToStep = std::min(blockProcess.steps, static_cast<size_t>((blockProcess.steps + 0.999f)*processRatio));
			}
			
			while (blockProcess.step < processToStep) {
				size_t step = blockProcess.step++;
#ifdef SIGNALSMITH_STRETCH_PROFILE_PROCESS_STEP
				SIGNALSMITH_STRETCH_PROFILE_PROCESS_STEP(step, blockProcess.steps);
#endif
				if (blockProcess.newSpectrum) {
					if (blockProcess.reanalysePrev) {
						// analyse past input
						if (step < stft.analyseSteps()) {
							if (_splitComputation) stashedInput.swap(stft.input);
							if (sameAnalysis(step)) {
								stft.copyAnalysis(0, 1, stft.defaultInterval());
							} else {
								stft.analyseStep(step, stft.defaultInterval());
							}
							if (_splitComputation) stashedInput.swap(stft.input);
							continue;
						}
						step -= stft.analyseSteps();
						if (step < 1) {
							// Copy previous analysis to our band objects
							for (int c = 0; c < channels; ++c) {
								splitSpectrum(stft.spectrum(c), bandPrev(c), bandPrevI(c));
							}
							continue;
						}
						step -= 1;
					}

					// Analyse latest (stashed) input
					if (step < stft.analyseSteps()) {
						if (_splitComputation) stashedInput.swap(stft.input);
						if (sameAnalysis(step)) {
							stft.copyAnalysis(0, 1);
						} else {
							stft.analyseStep(step);
						}
						if (_splitComputation) stashedInput.swap(stft.input);
						continue;
					}
					step -= stft.analyseSteps();
					if (step < 1) {
						// Copy analysed spectrum into our band objects
						for (int c = 0; c < channels; ++c) {
							splitSpectrum(stft.spectrum(c), bandIn(c), bandInI(c));
						}
						continue;
					}
					step -= 1;
				}

				if (step < processSpectrumSteps) {
					processSpectrum(step);
					continue;
				}
				step -= processSpectrumSteps;

				if (step < 1) {
					// Copy band objects into spectrum
					for (int c = 0; c < channels; ++c) {
						const Sample *oR = bandOut(c), *oI = bandOutI(c);
						auto *spectrumBands = stft.spectrum(c);
						for (int b = 0; b < bands; ++b) {
							spectrumBands[b] = Complex{oR[b], oI[b]};
						}
					}
					continue;
				}
				step -= 1;
				
				if (step < stft.synthesiseSteps()) {
					stft.synthesiseStep(step);
					continue;
				}
			}
#ifdef SIGNALSMITH_STRETCH_PROFILE_PROCESS_ENDSTEP
			SIGNALSMITH_STRETCH_PROFILE_PROCESS_ENDSTEP();
#endif

			if (_splitComputation) {
				++blockProcess.samplesSinceLast;
				stashedOutput.swap(stft.output);
				for (int c = 0; c < channels; ++c) {
					auto &&outputChannel = outputs[c];
					Sample v = 0;
					stft.readOutput(c, 1, &v);
					outputChannel[outputIndex] = v;
				}
				stft.moveOutput(1);
				stashedOutput.swap(stft.output);
				++outputIndex;
				continue;
			}
			// LYNX (round 190kr): without split computation nothing is synthesised until the next block is due, so the
			// samples up to it are read in one run (the same divisions, then the same clearing of the output buffer)
			int run = int(std::min<size_t>(size_t(outputSamples - outputIndex), stft.defaultInterval() - blockProcess.samplesSinceLast));
			blockProcess.samplesSinceLast += run;
			for (int done = 0; done < run;) {
				Sample chunkBuffer[128];
				int length = std::min(run - done, 128);
				for (int c = 0; c < channels; ++c) {
					auto &&outputChannel = outputs[c];
					stft.readOutput(c, size_t(done), size_t(length), chunkBuffer);
					for (int i = 0; i < length; ++i) {
						outputChannel[outputIndex + done + i] = chunkBuffer[i];
					}
				}
				done += length;
			}
			stft.clearOutput(size_t(run));
			outputIndex += run;
		}
		
		copyInput(inputSamples);
		prevInputOffset -= inputSamples;
#ifdef SIGNALSMITH_STRETCH_PROFILE_PROCESS_END
		SIGNALSMITH_STRETCH_PROFILE_PROCESS_END();
#endif
	}

	// Read the remaining output, providing no further input.  If `outputSamples` is more than one interval, it will compute additional blocks assuming a zero-valued input
	template<class Outputs>
	void flush(Outputs &&outputs, int outputSamples, Sample playbackRate=0) {
		struct Zeros {
			struct Channel {
				Sample operator[](int) {
					return 0;
				}
			};
			Channel operator[](int) {
				return {};
			}
		} zeros;
		// If we're asked for more than an interval of extra output, then zero-pad the input
		int outputBlock = std::max<int>(0, outputSamples - stft.defaultInterval());
		if (outputBlock > 0) process(zeros, outputBlock*playbackRate, outputs, outputBlock);

		int tailSamples = outputSamples - outputBlock; // at most one interval
		tmpProcessBuffer.resize(tailSamples);
		stft.finishOutput(1);
		for (int c = 0; c < channels; ++c) {
			stft.readOutput(c, tailSamples, tmpProcessBuffer.data());
			auto &&outputChannel = outputs[c];
			for (int i = 0; i < tailSamples; ++i) {
				outputChannel[outputBlock + i] = tmpProcessBuffer[i];
			}
			stft.readOutput(c, tailSamples, tailSamples, tmpProcessBuffer.data());
			for (int i = 0; i < tailSamples; ++i) {
				outputChannel[outputBlock + tailSamples - 1 - i] -= tmpProcessBuffer[i];
			}
		}
		stft.reset(0.1f);
		// Reset the phase-vocoder stuff, so the next block gets a fresh start
		clearBands(false);
	}

	// Process a complete audio buffer all in one go
	template<class Inputs, class Outputs>
	bool exact(Inputs &&inputs, int inputSamples, Outputs &&outputs, int outputSamples) {
		Sample playbackRate = inputSamples/Sample(outputSamples);
		auto seekLength = outputSeekLength(playbackRate);
		if (inputSamples < seekLength) {
			// to short for this - zero the output just to be polite
			for (int c = 0; c < channels; ++c) {
				auto &&channel = outputs[c];
				for (int i = 0; i < outputSamples; ++i) {
					channel[i] = 0;
				}
			}
			return false;
		}

		outputSeek(inputs, seekLength);

		int outputIndex = outputSamples - seekLength/playbackRate;
		OffsetIO<Inputs> offsetInput{inputs, seekLength};
		process(offsetInput, inputSamples - seekLength, outputs, outputIndex);
		
		OffsetIO<Outputs> offsetOutput{outputs, outputIndex};
		flush(offsetOutput, outputSamples - outputIndex, playbackRate);
		return true;
	}

private:
	bool _splitComputation = false;
	struct {
		size_t samplesSinceLast = std::numeric_limits<size_t>::max();
		size_t steps = 0;
		size_t step = 0;
		
		bool newSpectrum = false;
		bool reanalysePrev = false;
		bool mappedFrequencies = false;
		bool processFormants = false;
		Sample timeFactor;
	} blockProcess;

	using Complex = std::complex<Sample>;
	static constexpr Sample noiseFloor{Sample(1e-15)};
	static constexpr Sample maxCleanStretch{2}; // time-stretch ratio before we start randomising phases
	size_t silenceCounter = 0;
	bool silenceFirst = true;

	Sample freqMultiplier = 1, freqTonalityLimit = 0.5;
	std::function<Sample(Sample)> customFreqMap = nullptr;
	
	bool formantCompensation = false; // compensate for pitch/freq change
	Sample formantMultiplier = 1, invFormantMultiplier = 1;

	using STFT = signalsmith::linear::DynamicSTFT<Sample, false, true>;
	STFT stft;
	typename STFT::Input stashedInput;
	typename STFT::Output stashedOutput;
	
	std::vector<Sample> tmpProcessBuffer, tmpPreRollBuffer;

	int channels = 0, bands = 0;
	int prevInputOffset = -1;
	bool didSeek = false;
	Sample seekTimeFactor = 1;

	Sample bandToFreq(Sample b) const {
		return stft.binToFreq(b);
	}
	Sample freqToBand(Sample f) const {
		return stft.freqToBin(f);
	}
	
	// LYNX (round 190kr): the band state as structure-of-arrays (it was an array of {input, prevInput, output,
	// inputEnergy} structs). Each channel's bands are contiguous, with `bandPad` bands of zeros either side that are
	// never written: a read just outside [0, bands) gives 0, as the original getBand() did.
	static constexpr int bandPad = 4;
	int bandStride = 0;
	std::vector<Sample> bandInR, bandInIm, bandPrevR, bandPrevIm, bandOutR, bandOutIm, bandEnergyV;
	Sample * bandIn(int c) { return bandInR.data() + c*bandStride + bandPad; }
	Sample * bandInI(int c) { return bandInIm.data() + c*bandStride + bandPad; }
	Sample * bandPrev(int c) { return bandPrevR.data() + c*bandStride + bandPad; }
	Sample * bandPrevI(int c) { return bandPrevIm.data() + c*bandStride + bandPad; }
	Sample * bandOut(int c) { return bandOutR.data() + c*bandStride + bandPad; }
	Sample * bandOutI(int c) { return bandOutIm.data() + c*bandStride + bandPad; }
	Sample * bandEnergy(int c) { return bandEnergyV.data() + c*bandStride + bandPad; }

	// scratch of the main prediction (per channel, and the maximum-energy channel's selection per band)
	std::vector<Sample> twistR, twistI, longTwistR, longTwistI; // per channel, bands (+ padding)
	std::vector<Sample> selTwistR, selTwistI, selLongTwistR, selLongTwistI, selUpR, selUpI, selLongUpR, selLongUpI;
	std::vector<Sample> drawDown, drawUp;
	// LYNX (round 190lv, memory): the scratch above holds ONE chunk of the main prediction (its bands [startB, endB) and the
	// twists up to endB + longVerticalStep), not all bands: each chunk's twists, selections and random draws are written
	// and read inside that chunk's step (processSpectrum), so nothing of another chunk is ever read from them. 1/8 of
	// the size (~0.15 MB less per voice pitch shifter: ~100 of them in FL's Thief). The `...p` pointers stand at band 0
	// for the chunk being worked on (setScratchChunk); the arithmetic is unchanged.
	int chunkStride = 0;
	Sample *twistRp = nullptr, *twistIp = nullptr, *longTwistRp = nullptr, *longTwistIp = nullptr;
	Sample *selTwistRp = nullptr, *selTwistIp = nullptr, *selLongTwistRp = nullptr, *selLongTwistIp = nullptr;
	Sample *selUpRp = nullptr, *selUpIp = nullptr, *selLongUpRp = nullptr, *selLongUpIp = nullptr;
	Sample *drawDownp = nullptr, *drawUpp = nullptr;
	void setScratchChunk(int startB, int endB, int longVerticalStep) {
		// the twists of the chunk reach endB + max(longVerticalStep, 1) (prepareMainPrediction's twistEnd)
		const int need = endB - startB + std::max(longVerticalStep, 1) + 8;
		if (need > chunkStride) sizeScratch(need); // (never with configure()'s own sizes: the steps cut the bands evenly)
		auto at = [&](std::vector<Sample> &v, size_t c, int stride) { return v.data() + c*size_t(stride) - startB; };
		twistRp = at(twistR, 0, chunkStride);
		twistIp = at(twistI, 0, chunkStride);
		longTwistRp = at(longTwistR, 0, chunkStride);
		longTwistIp = at(longTwistI, 0, chunkStride);
		selTwistRp = at(selTwistR, 0, 0);
		selTwistIp = at(selTwistI, 0, 0);
		selLongTwistRp = at(selLongTwistR, 0, 0);
		selLongTwistIp = at(selLongTwistI, 0, 0);
		selUpRp = at(selUpR, 0, 0);
		selUpIp = at(selUpI, 0, 0);
		selLongUpRp = at(selLongUpR, 0, 0);
		selLongUpIp = at(selLongUpI, 0, 0);
		drawDownp = at(drawDown, 0, 0);
		drawUpp = at(drawUp, 0, 0);
	}
	void sizeScratch(int window) {
		chunkStride = window;
		for (auto *v : {&twistR, &twistI, &longTwistR, &longTwistI}) {
			v->assign(size_t(window)*channels, Sample(0));
		}
		for (auto *v : {&selTwistR, &selTwistI, &selLongTwistR, &selLongTwistI, &selUpR, &selUpI, &selLongUpR, &selLongUpI, &drawDown, &drawUp}) {
			v->assign(size_t(window), Sample(0));
		}
	}
	std::vector<int> maxChannelOf;
	std::vector<Sample> rotTableR, rotTableI; // the new-spectrum rotation of each band (the original's running product)
	bool identityMap = false; // the output map is {b, 1} (no frequency mapping)
	// two channels, no frequency mapping: a new spectrum's rotation and the preliminary outputs wait until the main
	// prediction knows which channels are the maximum-energy one somewhere; the other channel's are never read (all its
	// outputs are phase-locked to the maximum's, and its previous inputs are replaced by the new ones)
	std::vector<char> rotationPending;
	bool preliminaryPending = false;
	std::vector<Sample> savedPredEnergy; // the predictions' energies of the block before (for the pending outputs)

	void allocateBands() {
		bandStride = bands + 2*bandPad;
		size_t total = size_t(bandStride)*channels;
		for (auto *v : {&bandInR, &bandInIm, &bandPrevR, &bandPrevIm, &bandOutR, &bandOutIm, &bandEnergyV}) {
			v->assign(total, Sample(0));
		}
		// (round 190lv) the main prediction's scratch: its largest chunk of bands, and the twists' reach past it
		{
			int largest = 0;
			for (size_t chunk = 0; chunk < splitMainPrediction; ++chunk) {
				largest = std::max(largest, int(bands*(chunk + 1)/splitMainPrediction) - int(bands*chunk/splitMainPrediction));
			}
			int longVerticalStep = static_cast<int>(std::round(Sample(stft.fftSamples())/stft.defaultInterval()));
			sizeScratch(largest + std::max(longVerticalStep, 1) + 8);
		}
		maxChannelOf.assign(size_t(bands), 0);
		rotationPending.assign(size_t(channels), 0);
		preliminaryPending = false;
		savedPredEnergy.assign(size_t(bands)*channels, Sample(0));
		// the rotation applied to every band of a new spectrum: exactly the running product the original computed
		rotTableR.assign(size_t(bands), Sample(0));
		rotTableI.assign(size_t(bands), Sample(0));
		Complex rot = std::polar(Sample(1), bandToFreq(0)*stft.defaultInterval()*Sample(2*M_PI));
		Sample freqStep = bandToFreq(1) - bandToFreq(0);
		Complex rotStep = std::polar(Sample(1), freqStep*stft.defaultInterval()*Sample(2*M_PI));
		for (int b = 0; b < bands; ++b) {
			rotTableR[b] = rot.real();
			rotTableI[b] = rot.imag();
			rot = _impl::mul(rot, rotStep);
		}
	}
	// all = input, prevInput, output and inputEnergy (a reset / silence); otherwise prevInput and output (a flush)
	void clearBands(bool all) {
		auto zero = [](std::vector<Sample> &v) { std::fill(v.begin(), v.end(), Sample(0)); };
		std::fill(rotationPending.begin(), rotationPending.end(), char(0));
		preliminaryPending = false;
		zero(bandPrevR);
		zero(bandPrevIm);
		zero(bandOutR);
		zero(bandOutIm);
		if (all) {
			zero(bandInR);
			zero(bandInIm);
			zero(bandEnergyV);
		}
	}
	void splitSpectrum(const Complex *spectrum, Sample *re, Sample *im) {
		for (int b = 0; b < bands; ++b) {
			re[b] = spectrum[b].real();
			im[b] = spectrum[b].imag();
		}
	}
	// the original getBand() / getFractional(): a band's value (0 outside [0, bands)) and the linear interpolation
	static Sample bandAt(const Sample *v, int bands, int index) {
		if (index < 0 || index >= bands) return 0;
		return v[index];
	}
	static Sample fractionalAt(const Sample *v, int bands, int lowIndex, Sample fractional) {
		Sample low = bandAt(v, bands, lowIndex);
		Sample high = bandAt(v, bands, lowIndex + 1);
		return low + (high - low)*fractional;
	}
	static void fractionalAt(const Sample *re, const Sample *im, int bands, int lowIndex, Sample fractional, Sample &outR, Sample &outI) {
		Sample lowR = bandAt(re, bands, lowIndex), lowI = bandAt(im, bands, lowIndex);
		Sample highR = bandAt(re, bands, lowIndex + 1), highI = bandAt(im, bands, lowIndex + 1);
		outR = lowR + (highR - lowR)*fractional;
		outI = lowI + (highI - lowI)*fractional;
	}
	static void fractionalAt(const Sample *re, const Sample *im, int bands, Sample inputIndex, Sample &outR, Sample &outI) {
		int lowIndex = static_cast<int>(std::floor(inputIndex));
		Sample fracIndex = inputIndex - lowIndex;
		fractionalAt(re, im, bands, lowIndex, fracIndex, outR, outI);
	}

	struct Peak {
		Sample input, output;
	};
	std::vector<Peak> peaks;
	std::vector<Sample> energy, smoothedEnergy;
	struct PitchMapPoint {
		Sample inputBin, freqGrad;
	};
	std::vector<PitchMapPoint> outputMap;
	
	// the predictions (energy, input), structure-of-arrays: channel c's band b at c*bands + b
	std::vector<Sample> predEnergy, predInR, predInI;
	// Prediction::makeOutput()
	static void makeOutput(Sample energy, Sample inR, Sample inI, Sample phaseR, Sample phaseI, Sample &outR, Sample &outI) {
		Sample phaseNorm = phaseR*phaseR + phaseI*phaseI;
		if (phaseNorm <= noiseFloor) {
			phaseR = inR; // prediction is too weak, fall back to the input
			phaseI = inI;
			phaseNorm = (inR*inR + inI*inI) + noiseFloor;
		}
		Sample scale = std::sqrt(energy/phaseNorm);
		outR = phaseR*scale;
		outI = phaseI*scale;
	}

	// If RandomEngine=void, use std::default_random_engine;
	using RandomEngineImpl = typename std::conditional<
		std::is_void<RandomEngine>::value,
		std::default_random_engine,
		RandomEngine
	>::type;
	RandomEngineImpl randomEngine;

	size_t processSpectrumSteps = 0;
	static constexpr size_t splitMainPrediction = 8; // it's just heavy, since we're blending up to 4 different phase predictions
	void updateProcessSpectrumSteps() {
		processSpectrumSteps = 0;
		if (blockProcess.newSpectrum) processSpectrumSteps += channels;
		if (blockProcess.mappedFrequencies) {
			processSpectrumSteps += smoothEnergySteps;
			processSpectrumSteps += 1; // findPeaks
		}
		processSpectrumSteps += 1; // updating the output map
		processSpectrumSteps += channels; // preliminary phase-vocoder prediction
		processSpectrumSteps += splitMainPrediction;
		if (blockProcess.newSpectrum) processSpectrumSteps += 1; // .input -> .prevInput
		if (blockProcess.processFormants) processSpectrumSteps += 3;
	}
	// LYNX (round 190kr): a mono source in both channels (a mono sample) has the same input in both: the second
	// channel's analysis step copies the first's spectrum instead of a second identical FFT
	bool sameAnalysis(size_t step) const {
		return !_splitComputation && channels == 2 && step == 1 && stft.inputChannelsEqual(0, 1);
	}

	void processSpectrum(size_t step) {
		Sample timeFactor = blockProcess.timeFactor;
		
		Sample smoothingBins = Sample(stft.fftSamples())/stft.defaultInterval();
		int longVerticalStep = static_cast<int>(std::round(smoothingBins));
		timeFactor = std::max<Sample>(timeFactor, 1/maxCleanStretch);
		bool randomTimeFactor = (timeFactor > maxCleanStretch);
		std::uniform_real_distribution<Sample> timeFactorDist(maxCleanStretch*2*randomTimeFactor - timeFactor, timeFactor);

		if (blockProcess.newSpectrum) {
			if (step < size_t(channels)) {
				if (channels == 2 && !blockProcess.mappedFrequencies && !_splitComputation) {
					rotationPending[step] = 1;
				} else {
					rotateBands(int(step));
				}
				return;
			}
			step -= channels;
		}
		if (blockProcess.mappedFrequencies) {
			if (step < smoothEnergySteps) {
				smoothEnergy(step, smoothingBins);
				return;
			}
			step -= smoothEnergySteps;
			if (step-- == 0) {
				findPeaks();
				return;
			}
		}
		if (step-- == 0) {
			if (blockProcess.mappedFrequencies) {
				updateOutputMap();
				identityMap = false;
			} else { // we're not pitch-shifting, so no need to find peaks etc.
				for (int c = 0; c < channels; ++c) {
					inputEnergies(c);
				}

				for (int b = 0; b < bands; ++b) {
					outputMap[b] = {Sample(b), 1};
				}
				identityMap = true;
			}
			return;
		}
		if (blockProcess.processFormants) {
			if (step < 3) {
				updateFormants(step);
				return;
			}
			step -= 3;
		}
		// Preliminary output prediction from phase-vocoder
		if (step < size_t(channels)) {
			int c = int(step);
			if (identityMap && channels == 2 && !_splitComputation) {
				preliminaryIdentityInputs(c);
				preliminaryPending = true;
			} else if (identityMap) {
				preliminaryIdentity(c);
			} else {
				preliminaryMapped(c);
			}
			return;
		}
		step -= channels;

		if (step < splitMainPrediction) {
			// Re-predict using phase differences between frequencies. LYNX (round 190kr): everything that does not
			// depend on the outputs of the bands below is computed first, for the chunk's bands (prepareMainPrediction),
			// so the band-to-band chain below does only the work on it, in the same order as before.
			if (step == 0) {
				prepareMaxChannels();
			}
			size_t chunk = step;
			int startB = int(bands*chunk/splitMainPrediction);
			int endB = int(bands*(chunk + 1)/splitMainPrediction);
			setScratchChunk(startB, endB, longVerticalStep); // (round 190lv: the scratch holds this chunk)
			prepareMainPrediction(startB, endB, timeFactor, longVerticalStep, randomTimeFactor, timeFactorDist);
			if (channels == 2) {
				mainChainStereo(startB, endB, longVerticalStep);
				return;
			}
			for (int b = startB; b < endB; ++b) {
				int maxChannel = maxChannelOf[b];
				Sample *oR = bandOut(maxChannel), *oI = bandOutI(maxChannel);

				Sample phaseR = 0, phaseI = 0;
				// Upwards vertical steps
				if (b > 0) {
					Sample aR = oR[b - 1], aI = oI[b - 1], tR = selTwistRp[b], tI = selTwistIp[b];
					phaseR += aR*tR - aI*tI;
					phaseI += aR*tI + aI*tR;
					if (b >= longVerticalStep) {
						Sample lR = oR[b - longVerticalStep], lI = oI[b - longVerticalStep], ltR = selLongTwistRp[b], ltI = selLongTwistIp[b];
						phaseR += lR*ltR - lI*ltI;
						phaseI += lR*ltI + lI*ltR;
					}
				}
				// Downwards vertical steps
				if (b < bands - 1) {
					phaseR += selUpRp[b];
					phaseI += selUpIp[b];
					if (b < bands - longVerticalStep) {
						phaseR += selLongUpRp[b];
						phaseI += selLongUpIp[b];
					}
				}

				size_t mi = size_t(maxChannel)*bands + b;
				Sample pInR = predInR[mi], pInI = predInI[mi];
				makeOutput(predEnergy[mi], pInR, pInI, phaseR, phaseI, oR[b], oI[b]);
				Sample outR = oR[b], outI = oI[b];
				
				// All other bins are locked in phase
				for (int c = 0; c < channels; ++c) {
					if (c != maxChannel) {
						size_t ci = size_t(c)*bands + b;
						Sample cInR = predInR[ci], cInI = predInI[ci];
						// channelTwist = mul<true>(channelPrediction.input, prediction.input)
						Sample twR = pInR*cInR + pInI*cInI;
						Sample twI = pInR*cInI - pInI*cInR;
						// channelPhase = mul(outputBin.output, channelTwist)
						Sample cpR = outR*twR - outI*twI;
						Sample cpI = outR*twI + outI*twR;
						makeOutput(predEnergy[ci], cInR, cInI, cpR, cpI, bandOut(c)[b], bandOutI(c)[b]);
					}
				}
			}
			return;
		}
		step -= splitMainPrediction;

		if (blockProcess.newSpectrum) {
			if (step-- == 0) {
				std::copy(bandInR.begin(), bandInR.end(), bandPrevR.begin());
				std::copy(bandInIm.begin(), bandInIm.end(), bandPrevIm.begin());
			}
		}
	}

	// The main prediction's band-to-band chain for two channels. A band's phase-locked output of the other channel is
	// computed in the chain only when a band above reads it (as its band below, or its long step below); the others
	// are computed afterwards for the whole chunk, four at a time (lockOtherChannel): the same values.
	void mainChainStereo(int startB, int endB, int L) {
		Sample *outR[2] = {bandOut(0), bandOut(1)}, *outI[2] = {bandOutI(0), bandOutI(1)};
		const Sample *qR[2] = {predInR.data(), predInR.data() + bands}, *qI[2] = {predInI.data(), predInI.data() + bands};
		const Sample *qE[2] = {predEnergy.data(), predEnergy.data() + bands};
		const int *maxOf = maxChannelOf.data();
		// each channel's output at the band below (kept in registers: the chain's latency)
		Sample prev0R = 0, prev0I = 0, prev1R = 0, prev1I = 0;
		if (startB > 0) {
			prev0R = outR[0][startB - 1];
			prev0I = outI[0][startB - 1];
			prev1R = outR[1][startB - 1];
			prev1I = outI[1][startB - 1];
		}
		for (int b = startB; b < endB; ++b) {
			int m = maxOf[b], o = 1 - m;
			Sample phaseR = 0, phaseI = 0;
			// Upwards vertical steps
			if (b > 0) {
				Sample aR = m ? prev1R : prev0R, aI = m ? prev1I : prev0I, tR = selTwistRp[b], tI = selTwistIp[b];
				phaseR += aR*tR - aI*tI;
				phaseI += aR*tI + aI*tR;
				if (b >= L) {
					Sample lR = outR[m][b - L], lI = outI[m][b - L], ltR = selLongTwistRp[b], ltI = selLongTwistIp[b];
					phaseR += lR*ltR - lI*ltI;
					phaseI += lR*ltI + lI*ltR;
				}
			}
			// Downwards vertical steps
			if (b < bands - 1) {
				phaseR += selUpRp[b];
				phaseI += selUpIp[b];
				if (b < bands - L) {
					phaseR += selLongUpRp[b];
					phaseI += selLongUpIp[b];
				}
			}
			Sample pInR = qR[m][b], pInI = qI[m][b], newR, newI;
			makeOutput(qE[m][b], pInR, pInI, phaseR, phaseI, newR, newI);
			outR[m][b] = newR;
			outI[m][b] = newI;
			if (m) {
				prev1R = newR;
				prev1I = newI;
			} else {
				prev0R = newR;
				prev0I = newI;
			}
			bool needed = (b + 1 < bands && maxOf[b + 1] == o) || (b + L < bands && maxOf[b + L] == o);
			if (needed) {
				Sample cInR = qR[o][b], cInI = qI[o][b];
				// channelTwist = mul<true>(channelPrediction.input, prediction.input)
				Sample twR = pInR*cInR + pInI*cInI;
				Sample twI = pInR*cInI - pInI*cInR;
				// channelPhase = mul(outputBin.output, channelTwist)
				Sample cpR = newR*twR - newI*twI;
				Sample cpI = newR*twI + newI*twR;
				Sample lockedR, lockedI;
				makeOutput(qE[o][b], cInR, cInI, cpR, cpI, lockedR, lockedI);
				outR[o][b] = lockedR;
				outI[o][b] = lockedI;
				if (o) {
					prev1R = lockedR;
					prev1I = lockedI;
				} else {
					prev0R = lockedR;
					prev0I = lockedI;
				}
			}
		}
		lockOtherChannel(startB, endB);
	}
	// the other channel's phase-locked output of bands [startB, endB) (two channels)
	void lockOtherChannel(int startB, int endB) {
		Sample *outR[2] = {bandOut(0), bandOut(1)}, *outI[2] = {bandOutI(0), bandOutI(1)};
		const Sample *qR[2] = {predInR.data(), predInR.data() + bands}, *qI[2] = {predInI.data(), predInI.data() + bands};
		const Sample *qE[2] = {predEnergy.data(), predEnergy.data() + bands};
		int b = startB;
#if SIGNALSMITH_STRETCH_AVX
		if (avxOn()) b = lockOtherChannelAvx(startB, endB);
#endif
#if SIGNALSMITH_STRETCH_SSE2
		const __m128 floor = _mm_set1_ps(noiseFloor);
		for (; b + 4 <= endB; b += 4) {
			// lanes whose maximum-energy channel is 1 (the other channel 0)
			__m128 mask = _mm_castsi128_ps(_mm_cmpgt_epi32(_mm_loadu_si128((const __m128i *)(maxChannelOf.data() + b)), _mm_setzero_si128()));
			__m128 o0R = _mm_loadu_ps(outR[0] + b), o0I = _mm_loadu_ps(outI[0] + b);
			__m128 o1R = _mm_loadu_ps(outR[1] + b), o1I = _mm_loadu_ps(outI[1] + b);
			__m128 q0R = _mm_loadu_ps(qR[0] + b), q0I = _mm_loadu_ps(qI[0] + b), q1R = _mm_loadu_ps(qR[1] + b), q1I = _mm_loadu_ps(qI[1] + b);
			__m128 e0 = _mm_loadu_ps(qE[0] + b), e1 = _mm_loadu_ps(qE[1] + b);
			__m128 mOutR = selectV(mask, o1R, o0R), mOutI = selectV(mask, o1I, o0I);
			__m128 pR = selectV(mask, q1R, q0R), pI = selectV(mask, q1I, q0I); // the maximum channel's prediction input
			__m128 cR = selectV(mask, q0R, q1R), cI = selectV(mask, q0I, q1I); // the other channel's
			__m128 cE = selectV(mask, e0, e1);
			__m128 twR, twI, cpR, cpI;
			mulConjV(cR, cI, pR, pI, twR, twI);
			mulV(mOutR, mOutI, twR, twI, cpR, cpI);
			// makeOutput
			__m128 phaseNorm = _mm_add_ps(_mm_mul_ps(cpR, cpR), _mm_mul_ps(cpI, cpI));
			__m128 weak = _mm_cmple_ps(phaseNorm, floor);
			__m128 phR = selectV(weak, cR, cpR), phI = selectV(weak, cI, cpI);
			__m128 inNorm = _mm_add_ps(_mm_add_ps(_mm_mul_ps(cR, cR), _mm_mul_ps(cI, cI)), floor);
			__m128 scale = _mm_sqrt_ps(_mm_div_ps(cE, selectV(weak, inNorm, phaseNorm)));
			__m128 lockedR = _mm_mul_ps(phR, scale), lockedI = _mm_mul_ps(phI, scale);
			_mm_storeu_ps(outR[0] + b, selectV(mask, lockedR, o0R));
			_mm_storeu_ps(outI[0] + b, selectV(mask, lockedI, o0I));
			_mm_storeu_ps(outR[1] + b, selectV(mask, o1R, lockedR));
			_mm_storeu_ps(outI[1] + b, selectV(mask, o1I, lockedI));
		}
#endif
		for (; b < endB; ++b) {
			int m = maxChannelOf[b], o = 1 - m;
			Sample pInR = qR[m][b], pInI = qI[m][b], cInR = qR[o][b], cInI = qI[o][b];
			Sample twR = pInR*cInR + pInI*cInI;
			Sample twI = pInR*cInI - pInI*cInR;
			Sample oR = outR[m][b], oI = outI[m][b];
			Sample cpR = oR*twR - oI*twI;
			Sample cpI = oR*twI + oI*twR;
			makeOutput(qE[o][b], cInR, cInI, cpR, cpI, outR[o][b], outI[o][b]);
		}
	}

#if SIGNALSMITH_STRETCH_SSE2
	// four bands' complex values, split: (aR + i aI)*(bR + i bI) as the original _impl::mul / mul<true> wrote them
	static void mulV(__m128 aR, __m128 aI, __m128 bR, __m128 bI, __m128 &outR, __m128 &outI) {
		outR = _mm_sub_ps(_mm_mul_ps(aR, bR), _mm_mul_ps(aI, bI));
		outI = _mm_add_ps(_mm_mul_ps(aR, bI), _mm_mul_ps(aI, bR));
	}
	static void mulConjV(__m128 aR, __m128 aI, __m128 bR, __m128 bI, __m128 &outR, __m128 &outI) {
		outR = _mm_add_ps(_mm_mul_ps(bR, aR), _mm_mul_ps(bI, aI));
		outI = _mm_sub_ps(_mm_mul_ps(bR, aI), _mm_mul_ps(bI, aR));
	}
#endif

	// new spectrum: the outputs and previous inputs turn by each band's phase advance over one interval
	void rotateBands(int channel) {
		Sample *oR = bandOut(channel), *oI = bandOutI(channel), *pR = bandPrev(channel), *pI = bandPrevI(channel);
		const Sample *rR = rotTableR.data(), *rI = rotTableI.data();
		int b = 0;
#if SIGNALSMITH_STRETCH_AVX
		if (avxOn()) b = rotateBandsAvx(oR, oI, pR, pI, rR, rI);
#endif
#if SIGNALSMITH_STRETCH_SSE2
		for (; b + 4 <= bands; b += 4) {
			__m128 rotR = _mm_loadu_ps(rR + b), rotI = _mm_loadu_ps(rI + b), xR, xI;
			mulV(_mm_loadu_ps(oR + b), _mm_loadu_ps(oI + b), rotR, rotI, xR, xI);
			_mm_storeu_ps(oR + b, xR);
			_mm_storeu_ps(oI + b, xI);
			mulV(_mm_loadu_ps(pR + b), _mm_loadu_ps(pI + b), rotR, rotI, xR, xI);
			_mm_storeu_ps(pR + b, xR);
			_mm_storeu_ps(pI + b, xI);
		}
#endif
		for (; b < bands; ++b) {
			Complex o = _impl::mul(Complex{oR[b], oI[b]}, Complex{rR[b], rI[b]});
			Complex p = _impl::mul(Complex{pR[b], pI[b]}, Complex{rR[b], rI[b]});
			oR[b] = o.real();
			oI[b] = o.imag();
			pR[b] = p.real();
			pI[b] = p.imag();
		}
	}

	void inputEnergies(int c) {
		const Sample *iR = bandIn(c), *iI = bandInI(c);
		Sample *e = bandEnergy(c);
		int b = 0;
#if SIGNALSMITH_STRETCH_AVX
		if (avxOn()) b = inputEnergiesAvx(iR, iI, e);
#endif
#if SIGNALSMITH_STRETCH_SSE2
		for (; b + 4 <= bands; b += 4) {
			__m128 r = _mm_loadu_ps(iR + b), i = _mm_loadu_ps(iI + b);
			_mm_storeu_ps(e + b, _mm_add_ps(_mm_mul_ps(r, r), _mm_mul_ps(i, i)));
		}
#endif
		for (; b < bands; ++b) {
			e[b] = iR[b]*iR[b] + iI[b]*iI[b];
		}
	}

	// the preliminary phase-vocoder prediction of one band (the original loop body), for any output map
	void preliminaryBand(int c, int b, int lowIndex, Sample fracIndex, Sample freqGrad) {
		size_t pi = size_t(c)*bands + b;
		Sample prevEnergy = predEnergy[pi];
		Sample e = fractionalAt(bandEnergy(c), bands, lowIndex, fracIndex);
		e *= std::max<Sample>(0, freqGrad); // scale the energy according to local stretch factor
		predEnergy[pi] = e;
		Sample inR, inI, prR, prI;
		fractionalAt(bandIn(c), bandInI(c), bands, lowIndex, fracIndex, inR, inI);
		predInR[pi] = inR;
		predInI[pi] = inI;
		fractionalAt(bandPrev(c), bandPrevI(c), bands, lowIndex, fracIndex, prR, prI);
		// freqTwist = mul<true>(prediction.input, prevInput)
		Sample ftR = prR*inR + prI*inI;
		Sample ftI = prR*inI - prI*inR;
		// phase = mul(outputBin.output, freqTwist)
		Sample oR = bandOut(c)[b], oI = bandOutI(c)[b];
		Sample phR = oR*ftR - oI*ftI;
		Sample phI = oR*ftI + oI*ftR;
		Sample d = std::max(prevEnergy, e) + noiseFloor;
		bandOut(c)[b] = phR/d;
		bandOutI(c)[b] = phI/d;
	}
	void preliminaryMapped(int c) {
		for (int b = 0; b < bands; ++b) {
			auto mapPoint = outputMap[b];
			int lowIndex = static_cast<int>(std::floor(mapPoint.inputBin));
			Sample fracIndex = mapPoint.inputBin - lowIndex;
			preliminaryBand(c, b, lowIndex, fracIndex, mapPoint.freqGrad);
		}
	}
	// the output map is {b, 1}: band b reads band b with a fraction of exactly 0 (the same arithmetic, in fours)
	void preliminaryIdentity(int c) {
		int b = 0;
#if SIGNALSMITH_STRETCH_AVX
		if (avxOn()) b = preliminaryIdentityAvx(c);
#endif
#if SIGNALSMITH_STRETCH_SSE2
		const Sample *eV = bandEnergy(c), *iR = bandIn(c), *iI = bandInI(c), *pR = bandPrev(c), *pI = bandPrevI(c);
		Sample *oR = bandOut(c), *oI = bandOutI(c);
		Sample *pE = predEnergy.data() + size_t(c)*bands, *qR = predInR.data() + size_t(c)*bands, *qI = predInI.data() + size_t(c)*bands;
		const __m128 zero = _mm_setzero_ps(), one = _mm_set1_ps(Sample(1)), floor = _mm_set1_ps(noiseFloor);
		// (the last band reads band `bands`, a zero of the padding, as getBand() returned 0 there)
		for (; b + 4 <= bands; b += 4) {
			__m128 prevEnergy = _mm_loadu_ps(pE + b);
			__m128 eLow = _mm_loadu_ps(eV + b), eHigh = _mm_loadu_ps(eV + b + 1);
			__m128 e = _mm_mul_ps(_mm_add_ps(eLow, _mm_mul_ps(_mm_sub_ps(eHigh, eLow), zero)), one);
			_mm_storeu_ps(pE + b, e);
			__m128 lowR = _mm_loadu_ps(iR + b), lowI = _mm_loadu_ps(iI + b);
			__m128 inR = _mm_add_ps(lowR, _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(iR + b + 1), lowR), zero));
			__m128 inI = _mm_add_ps(lowI, _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(iI + b + 1), lowI), zero));
			_mm_storeu_ps(qR + b, inR);
			_mm_storeu_ps(qI + b, inI);
			__m128 plR = _mm_loadu_ps(pR + b), plI = _mm_loadu_ps(pI + b);
			__m128 prR = _mm_add_ps(plR, _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(pR + b + 1), plR), zero));
			__m128 prI = _mm_add_ps(plI, _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(pI + b + 1), plI), zero));
			__m128 ftR, ftI, phR, phI;
			mulConjV(inR, inI, prR, prI, ftR, ftI);
			mulV(_mm_loadu_ps(oR + b), _mm_loadu_ps(oI + b), ftR, ftI, phR, phI);
			// std::max(prevEnergy, e) = (prevEnergy < e ? e : prevEnergy) = maxps(e, prevEnergy)
			__m128 d = _mm_add_ps(_mm_max_ps(e, prevEnergy), floor);
			_mm_storeu_ps(oR + b, _mm_div_ps(phR, d));
			_mm_storeu_ps(oI + b, _mm_div_ps(phI, d));
		}
#endif
		for (; b < bands; ++b) {
			preliminaryBand(c, b, b, Sample(b) - b, 1);
		}
	}

	// identity map, two channels: the preliminary prediction's energies and inputs (the outputs: preliminaryIdentityOutputs)
	void preliminaryIdentityInputs(int c) {
		const Sample *eV = bandEnergy(c), *iR = bandIn(c), *iI = bandInI(c);
		Sample *pE = predEnergy.data() + size_t(c)*bands, *saved = savedPredEnergy.data() + size_t(c)*bands;
		Sample *qR = predInR.data() + size_t(c)*bands, *qI = predInI.data() + size_t(c)*bands;
		int b = 0;
#if SIGNALSMITH_STRETCH_AVX
		if (avxOn()) b = preliminaryIdentityInputsAvx(eV, iR, iI, pE, saved, qR, qI);
#endif
#if SIGNALSMITH_STRETCH_SSE2
		const __m128 zero = _mm_setzero_ps(), one = _mm_set1_ps(Sample(1));
		for (; b + 4 <= bands; b += 4) {
			_mm_storeu_ps(saved + b, _mm_loadu_ps(pE + b));
			__m128 eLow = _mm_loadu_ps(eV + b), eHigh = _mm_loadu_ps(eV + b + 1);
			_mm_storeu_ps(pE + b, _mm_mul_ps(_mm_add_ps(eLow, _mm_mul_ps(_mm_sub_ps(eHigh, eLow), zero)), one));
			__m128 lowR = _mm_loadu_ps(iR + b), lowI = _mm_loadu_ps(iI + b);
			_mm_storeu_ps(qR + b, _mm_add_ps(lowR, _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(iR + b + 1), lowR), zero)));
			_mm_storeu_ps(qI + b, _mm_add_ps(lowI, _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(iI + b + 1), lowI), zero)));
		}
#endif
		for (; b < bands; ++b) {
			Sample fracIndex = Sample(b) - b;
			saved[b] = pE[b];
			Sample e = fractionalAt(eV, bands, b, fracIndex);
			e *= std::max<Sample>(0, 1);
			pE[b] = e;
			fractionalAt(iR, iI, bands, b, fracIndex, qR[b], qI[b]);
		}
	}
	void preliminaryIdentityOutputs(int c) {
		const Sample *pR = bandPrev(c), *pI = bandPrevI(c);
		Sample *oR = bandOut(c), *oI = bandOutI(c);
		const Sample *pE = predEnergy.data() + size_t(c)*bands, *saved = savedPredEnergy.data() + size_t(c)*bands;
		const Sample *qR = predInR.data() + size_t(c)*bands, *qI = predInI.data() + size_t(c)*bands;
		int b = 0;
#if SIGNALSMITH_STRETCH_AVX
		if (avxOn()) b = preliminaryIdentityOutputsAvx(pR, pI, oR, oI, pE, saved, qR, qI);
#endif
#if SIGNALSMITH_STRETCH_SSE2
		const __m128 zero = _mm_setzero_ps(), floor = _mm_set1_ps(noiseFloor);
		for (; b + 4 <= bands; b += 4) {
			__m128 inR = _mm_loadu_ps(qR + b), inI = _mm_loadu_ps(qI + b);
			__m128 plR = _mm_loadu_ps(pR + b), plI = _mm_loadu_ps(pI + b);
			__m128 prR = _mm_add_ps(plR, _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(pR + b + 1), plR), zero));
			__m128 prI = _mm_add_ps(plI, _mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(pI + b + 1), plI), zero));
			__m128 ftR, ftI, phR, phI;
			mulConjV(inR, inI, prR, prI, ftR, ftI);
			mulV(_mm_loadu_ps(oR + b), _mm_loadu_ps(oI + b), ftR, ftI, phR, phI);
			__m128 d = _mm_add_ps(_mm_max_ps(_mm_loadu_ps(pE + b), _mm_loadu_ps(saved + b)), floor);
			_mm_storeu_ps(oR + b, _mm_div_ps(phR, d));
			_mm_storeu_ps(oI + b, _mm_div_ps(phI, d));
		}
#endif
		for (; b < bands; ++b) {
			Sample inR = qR[b], inI = qI[b], prR, prI;
			fractionalAt(pR, pI, bands, b, Sample(b) - b, prR, prI);
			Sample ftR = prR*inR + prI*inI;
			Sample ftI = prR*inI - prI*inR;
			Sample phR = oR[b]*ftR - oI[b]*ftI;
			Sample phI = oR[b]*ftI + oI[b]*ftR;
			Sample d = std::max(saved[b], pE[b]) + noiseFloor;
			oR[b] = phR/d;
			oI[b] = phI/d;
		}
	}
	// the outputs the main prediction reads: the pending rotations and preliminary outputs of the channels that are the
	// maximum-energy one of some band (maxChannelOf known); the other channels' are skipped
	void finishPreliminary(const bool *used) {
		for (int c = 0; c < channels; ++c) {
			if (used[c]) {
				if (rotationPending[c]) rotateBands(c);
				if (preliminaryPending) preliminaryIdentityOutputs(c);
			}
			rotationPending[c] = 0;
		}
		preliminaryPending = false;
	}

	// one band's terms of the main prediction with a random time factor (the original loop's reads, any channel count)
	void prepareRandomBand(int b, int longVerticalStep) {
		int m = maxChannelOf[b];
		const Sample *iR = bandIn(m), *iI = bandInI(m), *oR = bandOut(m), *oI = bandOutI(m);
		const Sample *qR = predInR.data() + size_t(m)*bands, *qI = predInI.data() + size_t(m)*bands;
		if (b > 0) {
			Sample binTimeFactor = drawDownp[b];
			Sample dR, dI;
			fractionalAt(iR, iI, bands, outputMap[b].inputBin - binTimeFactor, dR, dI);
			selTwistRp[b] = dR*qR[b] + dI*qI[b];
			selTwistIp[b] = dR*qI[b] - dI*qR[b];
			if (b >= longVerticalStep) {
				fractionalAt(iR, iI, bands, outputMap[b].inputBin - longVerticalStep*binTimeFactor, dR, dI);
				selLongTwistRp[b] = dR*qR[b] + dI*qI[b];
				selLongTwistIp[b] = dR*qI[b] - dI*qR[b];
			}
		}
		if (b < bands - 1) {
			Sample binTimeFactor = drawUpp[b];
			Sample dR, dI;
			fractionalAt(iR, iI, bands, outputMap[b + 1].inputBin - binTimeFactor, dR, dI);
			Sample tR = dR*qR[b + 1] + dI*qI[b + 1];
			Sample tI = dR*qI[b + 1] - dI*qR[b + 1];
			// mul<true>(upBin.output, shortVerticalTwist)
			selUpRp[b] = tR*oR[b + 1] + tI*oI[b + 1];
			selUpIp[b] = tR*oI[b + 1] - tI*oR[b + 1];
			if (b < bands - longVerticalStep) {
				int u = b + longVerticalStep;
				fractionalAt(iR, iI, bands, outputMap[u].inputBin - longVerticalStep*binTimeFactor, dR, dI);
				Sample lR = dR*qR[u] + dI*qI[u];
				Sample lI = dR*qI[u] - dI*qR[u];
				selLongUpRp[b] = lR*oR[u] + lI*oI[u];
				selLongUpIp[b] = lR*oI[u] - lI*oR[u];
			}
		}
	}
	// one band's terms with one time factor for all bands, from each channel's twists (any channel count)
	void selectBand(int b, int longVerticalStep) {
		int m = maxChannelOf[b];
		const Sample *tR = twistRp + size_t(m)*chunkStride, *tI = twistIp + size_t(m)*chunkStride;
		const Sample *lR = longTwistRp + size_t(m)*chunkStride, *lI = longTwistIp + size_t(m)*chunkStride;
		const Sample *oR = bandOut(m), *oI = bandOutI(m);
		selTwistRp[b] = tR[b];
		selTwistIp[b] = tI[b];
		selLongTwistRp[b] = lR[b];
		selLongTwistIp[b] = lI[b];
		if (b < bands - 1) {
			// mul<true>(upBin.output, shortVerticalTwist)
			selUpRp[b] = tR[b + 1]*oR[b + 1] + tI[b + 1]*oI[b + 1];
			selUpIp[b] = tR[b + 1]*oI[b + 1] - tI[b + 1]*oR[b + 1];
			if (b < bands - longVerticalStep) {
				int u = b + longVerticalStep;
				selLongUpRp[b] = lR[u]*oR[u] + lI[u]*oI[u];
				selLongUpIp[b] = lR[u]*oI[u] - lI[u]*oR[u];
			}
		}
	}

#if SIGNALSMITH_STRETCH_SSE2
	// interpolated input at four fractional band positions (getFractional), each lane from channel `lane[j]`'s bands
	// (padded arrays: `re[j]`/`im[j]` point at band -bandPad)
	void fractionalV(__m128 index, const Sample *const *re, const Sample *const *im, bool sameChannel, __m128 &outR, __m128 &outI) const {
		// lowIndex = int(floor(index)), fracIndex = index - lowIndex (truncation, one down when that rounded up)
		__m128i t = _mm_cvttps_epi32(index);
		__m128 tF = _mm_cvtepi32_ps(t);
		t = _mm_add_epi32(t, _mm_castps_si128(_mm_cmpgt_ps(tF, index)));
		__m128 frac = _mm_sub_ps(index, _mm_cvtepi32_ps(t));
		alignas(16) int idx[4];
		_mm_store_si128((__m128i *)idx, t);
		__m128 lowR, lowI, highR, highI;
		// (four neighbouring bands of one channel inside the padded range: plain loads, zeros of the padding included)
		if (sameChannel && idx[1] == idx[0] + 1 && idx[2] == idx[0] + 2 && idx[3] == idx[0] + 3
				&& idx[0] >= -bandPad && idx[0] <= bands + bandPad - 5) {
			int k = idx[0] + bandPad;
			lowR = _mm_loadu_ps(re[0] + k);
			lowI = _mm_loadu_ps(im[0] + k);
			highR = _mm_loadu_ps(re[0] + k + 1);
			highI = _mm_loadu_ps(im[0] + k + 1);
		} else {
			alignas(16) Sample loR[4], loI[4], hiR[4], hiI[4];
			for (int j = 0; j < 4; ++j) {
				// outside [-bandPad, bands + bandPad - 2] both bands read are zeros either way
				int k = std::min(std::max(idx[j], -bandPad), bands + bandPad - 2) + bandPad;
				loR[j] = re[j][k];
				loI[j] = im[j][k];
				hiR[j] = re[j][k + 1];
				hiI[j] = im[j][k + 1];
			}
			lowR = _mm_load_ps(loR);
			lowI = _mm_load_ps(loI);
			highR = _mm_load_ps(hiR);
			highI = _mm_load_ps(hiI);
		}
		outR = _mm_add_ps(lowR, _mm_mul_ps(_mm_sub_ps(highR, lowR), frac));
		outI = _mm_add_ps(lowI, _mm_mul_ps(_mm_sub_ps(highI, lowI), frac));
	}
	// outputMap[b..b+3].inputBin
	__m128 inputBinsV(int b) const {
		__m128 m01 = _mm_loadu_ps(&outputMap[b].inputBin), m23 = _mm_loadu_ps(&outputMap[b + 2].inputBin);
		return _mm_shuffle_ps(m01, m23, _MM_SHUFFLE(2, 0, 2, 0));
	}
	static __m128 selectV(__m128 mask, __m128 ifSet, __m128 ifClear) {
		return _mm_or_ps(_mm_and_ps(mask, ifSet), _mm_andnot_ps(mask, ifClear));
	}
#endif
#if SIGNALSMITH_STRETCH_AVX
	// LYNX (round 190kz): eight-band versions of the four-band loops above and below. Each one is the SSE2 loop with
	// the same operations on eight lanes (_mm_*_ps → _mm256_*_ps, compare predicates as the SSE2 ones: LE/GT, ordered
	// signalling), run first; the bands it leaves go through the SSE2 loop and the scalar loop as before.
	static bool avxOn() { return lynx::cpu::hasAvx2(); }
	static SIGNALSMITH_AVX2_INLINE void mulV8(__m256 aR, __m256 aI, __m256 bR, __m256 bI, __m256 &outR, __m256 &outI) {
		outR = _mm256_sub_ps(_mm256_mul_ps(aR, bR), _mm256_mul_ps(aI, bI));
		outI = _mm256_add_ps(_mm256_mul_ps(aR, bI), _mm256_mul_ps(aI, bR));
	}
	static SIGNALSMITH_AVX2_INLINE void mulConjV8(__m256 aR, __m256 aI, __m256 bR, __m256 bI, __m256 &outR, __m256 &outI) {
		outR = _mm256_add_ps(_mm256_mul_ps(bR, aR), _mm256_mul_ps(bI, aI));
		outI = _mm256_sub_ps(_mm256_mul_ps(bR, aI), _mm256_mul_ps(bI, aR));
	}
	static SIGNALSMITH_AVX2_INLINE __m256 selectV8(__m256 mask, __m256 ifSet, __m256 ifClear) {
		return _mm256_or_ps(_mm256_and_ps(mask, ifSet), _mm256_andnot_ps(mask, ifClear));
	}
	// a value of the maximum-energy channel of each lane at band `at`
	static SIGNALSMITH_AVX2_INLINE __m256 selectedV8(const Sample *const *v, __m256 mask, int at) {
		return selectV8(mask, _mm256_loadu_ps(v[1] + at), _mm256_loadu_ps(v[0] + at));
	}
	// the lanes of bands b..b+7 whose maximum-energy channel is 1
	SIGNALSMITH_AVX2_INLINE __m256 maskAt8(int b) const {
		return _mm256_castsi256_ps(_mm256_cmpgt_epi32(_mm256_loadu_si256((const __m256i *)(maxChannelOf.data() + b)), _mm256_setzero_si256()));
	}
	SIGNALSMITH_AVX2_INLINE __m256 inputBinsV8(int b) const {
		return _mm256_setr_ps(outputMap[b].inputBin, outputMap[b + 1].inputBin, outputMap[b + 2].inputBin, outputMap[b + 3].inputBin,
			outputMap[b + 4].inputBin, outputMap[b + 5].inputBin, outputMap[b + 6].inputBin, outputMap[b + 7].inputBin);
	}
	// fractionalV on eight lanes (the same per-lane arithmetic; the plain-load case needs eight neighbouring bands)
	SIGNALSMITH_AVX2_INLINE void fractionalV8(__m256 index, const Sample *const *re, const Sample *const *im, bool sameChannel, __m256 &outR, __m256 &outI) const {
		__m256i t = _mm256_cvttps_epi32(index);
		__m256 tF = _mm256_cvtepi32_ps(t);
		t = _mm256_add_epi32(t, _mm256_castps_si256(_mm256_cmp_ps(tF, index, _CMP_GT_OS)));
		__m256 frac = _mm256_sub_ps(index, _mm256_cvtepi32_ps(t));
		alignas(32) int idx[8];
		_mm256_store_si256((__m256i *)idx, t);
		__m256 lowR, lowI, highR, highI;
		bool contiguous = sameChannel && idx[0] >= -bandPad && idx[0] <= bands + bandPad - 9;
		for (int j = 1; j < 8 && contiguous; ++j) contiguous = idx[j] == idx[0] + j;
		if (contiguous) {
			int k = idx[0] + bandPad;
			lowR = _mm256_loadu_ps(re[0] + k);
			lowI = _mm256_loadu_ps(im[0] + k);
			highR = _mm256_loadu_ps(re[0] + k + 1);
			highI = _mm256_loadu_ps(im[0] + k + 1);
		} else {
			alignas(32) Sample loR[8], loI[8], hiR[8], hiI[8];
			for (int j = 0; j < 8; ++j) {
				int k = std::min(std::max(idx[j], -bandPad), bands + bandPad - 2) + bandPad;
				loR[j] = re[j][k];
				loI[j] = im[j][k];
				hiR[j] = re[j][k + 1];
				hiI[j] = im[j][k + 1];
			}
			lowR = _mm256_load_ps(loR);
			lowI = _mm256_load_ps(loI);
			highR = _mm256_load_ps(hiR);
			highI = _mm256_load_ps(hiI);
		}
		outR = _mm256_add_ps(lowR, _mm256_mul_ps(_mm256_sub_ps(highR, lowR), frac));
		outI = _mm256_add_ps(lowI, _mm256_mul_ps(_mm256_sub_ps(highI, lowI), frac));
	}

	SIGNALSMITH_AVX2 int rotateBandsAvx(Sample *oR, Sample *oI, Sample *pR, Sample *pI, const Sample *rR, const Sample *rI) {
		int b = 0;
		for (; b + 8 <= bands; b += 8) {
			__m256 rotR = _mm256_loadu_ps(rR + b), rotI = _mm256_loadu_ps(rI + b), xR, xI;
			mulV8(_mm256_loadu_ps(oR + b), _mm256_loadu_ps(oI + b), rotR, rotI, xR, xI);
			_mm256_storeu_ps(oR + b, xR);
			_mm256_storeu_ps(oI + b, xI);
			mulV8(_mm256_loadu_ps(pR + b), _mm256_loadu_ps(pI + b), rotR, rotI, xR, xI);
			_mm256_storeu_ps(pR + b, xR);
			_mm256_storeu_ps(pI + b, xI);
		}
		return b;
	}
	SIGNALSMITH_AVX2 int inputEnergiesAvx(const Sample *iR, const Sample *iI, Sample *e) {
		int b = 0;
		for (; b + 8 <= bands; b += 8) {
			__m256 r = _mm256_loadu_ps(iR + b), i = _mm256_loadu_ps(iI + b);
			_mm256_storeu_ps(e + b, _mm256_add_ps(_mm256_mul_ps(r, r), _mm256_mul_ps(i, i)));
		}
		return b;
	}
	SIGNALSMITH_AVX2 int preliminaryIdentityAvx(int c) {
		int b = 0;
		const Sample *eV = bandEnergy(c), *iR = bandIn(c), *iI = bandInI(c), *pR = bandPrev(c), *pI = bandPrevI(c);
		Sample *oR = bandOut(c), *oI = bandOutI(c);
		Sample *pE = predEnergy.data() + size_t(c)*bands, *qR = predInR.data() + size_t(c)*bands, *qI = predInI.data() + size_t(c)*bands;
		const __m256 zero = _mm256_setzero_ps(), one = _mm256_set1_ps(Sample(1)), floor = _mm256_set1_ps(noiseFloor);
		for (; b + 8 <= bands; b += 8) {
			__m256 prevEnergy = _mm256_loadu_ps(pE + b);
			__m256 eLow = _mm256_loadu_ps(eV + b), eHigh = _mm256_loadu_ps(eV + b + 1);
			__m256 e = _mm256_mul_ps(_mm256_add_ps(eLow, _mm256_mul_ps(_mm256_sub_ps(eHigh, eLow), zero)), one);
			_mm256_storeu_ps(pE + b, e);
			__m256 lowR = _mm256_loadu_ps(iR + b), lowI = _mm256_loadu_ps(iI + b);
			__m256 inR = _mm256_add_ps(lowR, _mm256_mul_ps(_mm256_sub_ps(_mm256_loadu_ps(iR + b + 1), lowR), zero));
			__m256 inI = _mm256_add_ps(lowI, _mm256_mul_ps(_mm256_sub_ps(_mm256_loadu_ps(iI + b + 1), lowI), zero));
			_mm256_storeu_ps(qR + b, inR);
			_mm256_storeu_ps(qI + b, inI);
			__m256 plR = _mm256_loadu_ps(pR + b), plI = _mm256_loadu_ps(pI + b);
			__m256 prR = _mm256_add_ps(plR, _mm256_mul_ps(_mm256_sub_ps(_mm256_loadu_ps(pR + b + 1), plR), zero));
			__m256 prI = _mm256_add_ps(plI, _mm256_mul_ps(_mm256_sub_ps(_mm256_loadu_ps(pI + b + 1), plI), zero));
			__m256 ftR, ftI, phR, phI;
			mulConjV8(inR, inI, prR, prI, ftR, ftI);
			mulV8(_mm256_loadu_ps(oR + b), _mm256_loadu_ps(oI + b), ftR, ftI, phR, phI);
			__m256 d = _mm256_add_ps(_mm256_max_ps(e, prevEnergy), floor);
			_mm256_storeu_ps(oR + b, _mm256_div_ps(phR, d));
			_mm256_storeu_ps(oI + b, _mm256_div_ps(phI, d));
		}
		return b;
	}
	SIGNALSMITH_AVX2 int preliminaryIdentityInputsAvx(const Sample *eV, const Sample *iR, const Sample *iI, Sample *pE, Sample *saved, Sample *qR, Sample *qI) {
		int b = 0;
		const __m256 zero = _mm256_setzero_ps(), one = _mm256_set1_ps(Sample(1));
		for (; b + 8 <= bands; b += 8) {
			_mm256_storeu_ps(saved + b, _mm256_loadu_ps(pE + b));
			__m256 eLow = _mm256_loadu_ps(eV + b), eHigh = _mm256_loadu_ps(eV + b + 1);
			_mm256_storeu_ps(pE + b, _mm256_mul_ps(_mm256_add_ps(eLow, _mm256_mul_ps(_mm256_sub_ps(eHigh, eLow), zero)), one));
			__m256 lowR = _mm256_loadu_ps(iR + b), lowI = _mm256_loadu_ps(iI + b);
			_mm256_storeu_ps(qR + b, _mm256_add_ps(lowR, _mm256_mul_ps(_mm256_sub_ps(_mm256_loadu_ps(iR + b + 1), lowR), zero)));
			_mm256_storeu_ps(qI + b, _mm256_add_ps(lowI, _mm256_mul_ps(_mm256_sub_ps(_mm256_loadu_ps(iI + b + 1), lowI), zero)));
		}
		return b;
	}
	SIGNALSMITH_AVX2 int preliminaryIdentityOutputsAvx(const Sample *pR, const Sample *pI, Sample *oR, Sample *oI, const Sample *pE, const Sample *saved, const Sample *qR, const Sample *qI) {
		int b = 0;
		const __m256 zero = _mm256_setzero_ps(), floor = _mm256_set1_ps(noiseFloor);
		for (; b + 8 <= bands; b += 8) {
			__m256 inR = _mm256_loadu_ps(qR + b), inI = _mm256_loadu_ps(qI + b);
			__m256 plR = _mm256_loadu_ps(pR + b), plI = _mm256_loadu_ps(pI + b);
			__m256 prR = _mm256_add_ps(plR, _mm256_mul_ps(_mm256_sub_ps(_mm256_loadu_ps(pR + b + 1), plR), zero));
			__m256 prI = _mm256_add_ps(plI, _mm256_mul_ps(_mm256_sub_ps(_mm256_loadu_ps(pI + b + 1), plI), zero));
			__m256 ftR, ftI, phR, phI;
			mulConjV8(inR, inI, prR, prI, ftR, ftI);
			mulV8(_mm256_loadu_ps(oR + b), _mm256_loadu_ps(oI + b), ftR, ftI, phR, phI);
			__m256 d = _mm256_add_ps(_mm256_max_ps(_mm256_loadu_ps(pE + b), _mm256_loadu_ps(saved + b)), floor);
			_mm256_storeu_ps(oR + b, _mm256_div_ps(phR, d));
			_mm256_storeu_ps(oI + b, _mm256_div_ps(phI, d));
		}
		return b;
	}
	SIGNALSMITH_AVX2 int prepareMaxChannelsAvx() {
		int b = 0;
		for (; b + 8 <= bands; b += 8) {
			__m256 e0 = _mm256_loadu_ps(predEnergy.data() + b), e1 = _mm256_loadu_ps(predEnergy.data() + bands + b);
			__m256i one = _mm256_and_si256(_mm256_castps_si256(_mm256_cmp_ps(e1, e0, _CMP_GT_OS)), _mm256_set1_epi32(1));
			_mm256_storeu_si256((__m256i *)(maxChannelOf.data() + b), one);
		}
		return b;
	}
	SIGNALSMITH_AVX2 int lockOtherChannelAvx(int startB, int endB) {
		Sample *outR[2] = {bandOut(0), bandOut(1)}, *outI[2] = {bandOutI(0), bandOutI(1)};
		const Sample *qR[2] = {predInR.data(), predInR.data() + bands}, *qI[2] = {predInI.data(), predInI.data() + bands};
		const Sample *qE[2] = {predEnergy.data(), predEnergy.data() + bands};
		int b = startB;
		const __m256 floor = _mm256_set1_ps(noiseFloor);
		for (; b + 8 <= endB; b += 8) {
			__m256 mask = maskAt8(b);
			__m256 o0R = _mm256_loadu_ps(outR[0] + b), o0I = _mm256_loadu_ps(outI[0] + b);
			__m256 o1R = _mm256_loadu_ps(outR[1] + b), o1I = _mm256_loadu_ps(outI[1] + b);
			__m256 q0R = _mm256_loadu_ps(qR[0] + b), q0I = _mm256_loadu_ps(qI[0] + b), q1R = _mm256_loadu_ps(qR[1] + b), q1I = _mm256_loadu_ps(qI[1] + b);
			__m256 e0 = _mm256_loadu_ps(qE[0] + b), e1 = _mm256_loadu_ps(qE[1] + b);
			__m256 mOutR = selectV8(mask, o1R, o0R), mOutI = selectV8(mask, o1I, o0I);
			__m256 pR = selectV8(mask, q1R, q0R), pI = selectV8(mask, q1I, q0I);
			__m256 cR = selectV8(mask, q0R, q1R), cI = selectV8(mask, q0I, q1I);
			__m256 cE = selectV8(mask, e0, e1);
			__m256 twR, twI, cpR, cpI;
			mulConjV8(cR, cI, pR, pI, twR, twI);
			mulV8(mOutR, mOutI, twR, twI, cpR, cpI);
			__m256 phaseNorm = _mm256_add_ps(_mm256_mul_ps(cpR, cpR), _mm256_mul_ps(cpI, cpI));
			__m256 weak = _mm256_cmp_ps(phaseNorm, floor, _CMP_LE_OS);
			__m256 phR = selectV8(weak, cR, cpR), phI = selectV8(weak, cI, cpI);
			__m256 inNorm = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(cR, cR), _mm256_mul_ps(cI, cI)), floor);
			__m256 scale = _mm256_sqrt_ps(_mm256_div_ps(cE, selectV8(weak, inNorm, phaseNorm)));
			__m256 lockedR = _mm256_mul_ps(phR, scale), lockedI = _mm256_mul_ps(phI, scale);
			_mm256_storeu_ps(outR[0] + b, selectV8(mask, lockedR, o0R));
			_mm256_storeu_ps(outI[0] + b, selectV8(mask, lockedI, o0I));
			_mm256_storeu_ps(outR[1] + b, selectV8(mask, o1R, lockedR));
			_mm256_storeu_ps(outI[1] + b, selectV8(mask, o1I, lockedI));
		}
		return b;
	}
	// prepareMainPrediction, one time factor: one channel's twists from band b (returns where the SSE2 loop goes on)
	SIGNALSMITH_AVX2 int twistsAvx(int b, int twistEnd, const Sample *iR, const Sample *iI, const Sample *cqR, const Sample *cqI,
			Sample *tR, Sample *tI, Sample *lR, Sample *lI, Sample timeFactor, Sample longTimeFactor) {
		const __m256 tf = _mm256_set1_ps(timeFactor), ltf = _mm256_set1_ps(longTimeFactor);
		const Sample *re[8], *im[8];
		for (int j = 0; j < 8; ++j) {
			re[j] = iR - bandPad;
			im[j] = iI - bandPad;
		}
		for (; b + 8 <= twistEnd; b += 8) {
			__m256 bin = inputBinsV8(b);
			__m256 pR = _mm256_loadu_ps(cqR + b), pI = _mm256_loadu_ps(cqI + b), dR, dI, xR, xI;
			fractionalV8(_mm256_sub_ps(bin, tf), re, im, true, dR, dI);
			mulConjV8(pR, pI, dR, dI, xR, xI);
			_mm256_storeu_ps(tR + b, xR);
			_mm256_storeu_ps(tI + b, xI);
			fractionalV8(_mm256_sub_ps(bin, ltf), re, im, true, dR, dI);
			mulConjV8(pR, pI, dR, dI, xR, xI);
			_mm256_storeu_ps(lR + b, xR);
			_mm256_storeu_ps(lI + b, xI);
		}
		return b;
	}
	// prepareMainPrediction, one time factor: each band's terms from its maximum-energy channel, from band b
	SIGNALSMITH_AVX2 int selectAvx(int b, int endB, int L) {
		const Sample *oR[2] = {bandOut(0), bandOut(1)}, *oI[2] = {bandOutI(0), bandOutI(1)};
		const Sample *tR[2], *tI[2], *lR[2], *lI[2];
		for (int c = 0; c < 2; ++c) {
			tR[c] = twistRp + size_t(c)*chunkStride;
			tI[c] = twistIp + size_t(c)*chunkStride;
			lR[c] = longTwistRp + size_t(c)*chunkStride;
			lI[c] = longTwistIp + size_t(c)*chunkStride;
		}
		for (; b + 8 <= endB && b + 8 + L <= bands; b += 8) {
			__m256 mask = maskAt8(b), xR, xI;
			_mm256_storeu_ps(selTwistRp + b, selectedV8(tR, mask, b));
			_mm256_storeu_ps(selTwistIp + b, selectedV8(tI, mask, b));
			_mm256_storeu_ps(selLongTwistRp + b, selectedV8(lR, mask, b));
			_mm256_storeu_ps(selLongTwistIp + b, selectedV8(lI, mask, b));
			mulConjV8(selectedV8(oR, mask, b + 1), selectedV8(oI, mask, b + 1), selectedV8(tR, mask, b + 1), selectedV8(tI, mask, b + 1), xR, xI);
			_mm256_storeu_ps(selUpRp + b, xR);
			_mm256_storeu_ps(selUpIp + b, xI);
			mulConjV8(selectedV8(oR, mask, b + L), selectedV8(oI, mask, b + L), selectedV8(lR, mask, b + L), selectedV8(lI, mask, b + L), xR, xI);
			_mm256_storeu_ps(selLongUpRp + b, xR);
			_mm256_storeu_ps(selLongUpIp + b, xI);
		}
		return b;
	}
#endif

	// once per block (the main prediction's first step): the maximum-energy channel of each band, and (two channels,
	// no split computation) the pending rotations / preliminary outputs of the channels that are the maximum somewhere
	void prepareMaxChannels() {
		int b = 0;
#if SIGNALSMITH_STRETCH_SSE2
		if (channels == 2) {
#if SIGNALSMITH_STRETCH_AVX
			if (avxOn()) b = prepareMaxChannelsAvx();
#endif
			for (; b + 4 <= bands; b += 4) {
				// channel 1 only when its energy is greater (`e > maxEnergy`)
				__m128 e0 = _mm_loadu_ps(predEnergy.data() + b), e1 = _mm_loadu_ps(predEnergy.data() + bands + b);
				__m128i one = _mm_and_si128(_mm_castps_si128(_mm_cmpgt_ps(e1, e0)), _mm_set1_epi32(1));
				_mm_storeu_si128((__m128i *)(maxChannelOf.data() + b), one);
			}
		}
#endif
		for (; b < bands; ++b) {
			int maxChannel = 0;
			Sample maxEnergy = predEnergy[b];
			for (int c = 1; c < channels; ++c) {
				Sample e = predEnergy[size_t(c)*bands + b];
				if (e > maxEnergy) {
					maxChannel = c;
					maxEnergy = e;
				}
			}
			maxChannelOf[b] = maxChannel;
		}
		// the channels that are the maximum somewhere (only theirs are read by the main prediction)
		usedChannel[0] = usedChannel[1] = true;
		if (channels == 2) {
			int any1 = 0, all1 = 1;
			for (int b = 0; b < bands; ++b) {
				any1 |= maxChannelOf[b];
				all1 &= maxChannelOf[b];
			}
			usedChannel[0] = !all1 || bands == 0;
			usedChannel[1] = any1 != 0;
			finishPreliminary(usedChannel);
		}
	}
	bool usedChannel[2] = {true, true};

	// the main prediction's terms off its chain for bands [startB, endB) (one chunk: the same reads, at the same point
	// of the block, as the original loop made for those bands)
	template<class Dist>
	void prepareMainPrediction(int startB, int endB, Sample timeFactor, int longVerticalStep, bool randomTimeFactor, Dist &timeFactorDist) {
		const int L = longVerticalStep;
#if SIGNALSMITH_STRETCH_SSE2
		const bool vector = channels == 2 && L >= 1;
		const Sample *inR[2] {}, *inI[2] {}, *qR[2] {}, *qI[2] {}, *oR[2] {}, *oI[2] {};
		if (vector) {
			for (int c = 0; c < 2; ++c) {
				inR[c] = bandIn(c) - bandPad;
				inI[c] = bandInI(c) - bandPad;
				qR[c] = predInR.data() + size_t(c)*bands;
				qI[c] = predInI.data() + size_t(c)*bands;
				oR[c] = bandOut(c);
				oI[c] = bandOutI(c);
			}
		}
		// the channel-1 lanes of bands b..b+3, and a value of the maximum-energy channel at band b + offset
		auto maskAt = [&](int b) {
			return _mm_castsi128_ps(_mm_cmpgt_epi32(_mm_loadu_si128((const __m128i *)(maxChannelOf.data() + b)), _mm_setzero_si128()));
		};
		auto selected = [&](const Sample *const *v, __m128 mask, int at) {
			return selectV(mask, _mm_loadu_ps(v[1] + at), _mm_loadu_ps(v[0] + at));
		};
#endif

		if (randomTimeFactor) {
			// the same draws in the same order as the original loop: per band, the downward one then the upward one
			for (int b = startB; b < endB; ++b) {
				if (b > 0) drawDownp[b] = timeFactorDist(randomEngine);
				if (b < bands - 1) drawUpp[b] = timeFactorDist(randomEngine);
			}
			int b = startB;
#if SIGNALSMITH_STRETCH_SSE2
			if (vector) {
				// the edge bands (some terms absent) one by one, the others in fours
				for (; b < std::min(L, endB); ++b) prepareRandomBand(b, L);
				const __m128 longStep = _mm_set1_ps(Sample(L));
				for (; b + 4 <= endB && b + 4 + L <= bands; b += 4) {
					__m128 mask = maskAt(b);
					alignas(16) int lane[4];
					_mm_store_si128((__m128i *)lane, _mm_loadu_si128((const __m128i *)(maxChannelOf.data() + b)));
					bool same = lane[0] == lane[1] && lane[0] == lane[2] && lane[0] == lane[3];
					const Sample *re[4], *im[4];
					for (int j = 0; j < 4; ++j) {
						re[j] = inR[lane[j]];
						im[j] = inI[lane[j]];
					}
					__m128 down = _mm_loadu_ps(drawDownp + b), up = _mm_loadu_ps(drawUpp + b);
					__m128 pR = selected(qR, mask, b), pI = selected(qI, mask, b), dR, dI, xR, xI;
					__m128 bin = inputBinsV(b);
					fractionalV(_mm_sub_ps(bin, down), re, im, same, dR, dI);
					mulConjV(pR, pI, dR, dI, xR, xI);
					_mm_storeu_ps(selTwistRp + b, xR);
					_mm_storeu_ps(selTwistIp + b, xI);
					fractionalV(_mm_sub_ps(bin, _mm_mul_ps(longStep, down)), re, im, same, dR, dI);
					mulConjV(pR, pI, dR, dI, xR, xI);
					_mm_storeu_ps(selLongTwistRp + b, xR);
					_mm_storeu_ps(selLongTwistIp + b, xI);
					// upwards: the twist at b + 1 (b + L) from this band's draw, onto that band's preliminary output
					__m128 tR, tI;
					fractionalV(_mm_sub_ps(inputBinsV(b + 1), up), re, im, same, dR, dI);
					mulConjV(selected(qR, mask, b + 1), selected(qI, mask, b + 1), dR, dI, tR, tI);
					mulConjV(selected(oR, mask, b + 1), selected(oI, mask, b + 1), tR, tI, xR, xI);
					_mm_storeu_ps(selUpRp + b, xR);
					_mm_storeu_ps(selUpIp + b, xI);
					fractionalV(_mm_sub_ps(inputBinsV(b + L), _mm_mul_ps(longStep, up)), re, im, same, dR, dI);
					mulConjV(selected(qR, mask, b + L), selected(qI, mask, b + L), dR, dI, tR, tI);
					mulConjV(selected(oR, mask, b + L), selected(oI, mask, b + L), tR, tI, xR, xI);
					_mm_storeu_ps(selLongUpRp + b, xR);
					_mm_storeu_ps(selLongUpIp + b, xI);
				}
			}
#endif
			for (; b < endB; ++b) prepareRandomBand(b, L);
			return;
		}
		// one time factor for all bands: each channel's twists for the chunk's bands and the ones its upward terms
		// read (up to endB + L), then each band takes its channel's
		Sample longTimeFactor = L*timeFactor;
		const int twistEnd = int(std::min<long>(bands, long(endB) + std::max(L, 1)));
		for (int c = 0; c < channels; ++c) {
			if (channels == 2 && !usedChannel[c]) continue; // (never selected below)
			const Sample *iR = bandIn(c), *iI = bandInI(c);
			const Sample *cqR = predInR.data() + size_t(c)*bands, *cqI = predInI.data() + size_t(c)*bands;
			Sample *tR = twistRp + size_t(c)*chunkStride, *tI = twistIp + size_t(c)*chunkStride;
			Sample *lR = longTwistRp + size_t(c)*chunkStride, *lI = longTwistIp + size_t(c)*chunkStride;
			int b = startB;
#if SIGNALSMITH_STRETCH_SSE2
			const __m128 tf = _mm_set1_ps(timeFactor), ltf = _mm_set1_ps(longTimeFactor);
			const Sample *re[4], *im[4];
			for (int j = 0; j < 4; ++j) {
				re[j] = iR - bandPad;
				im[j] = iI - bandPad;
			}
#if SIGNALSMITH_STRETCH_AVX
			if (avxOn()) b = twistsAvx(b, twistEnd, iR, iI, cqR, cqI, tR, tI, lR, lI, timeFactor, longTimeFactor);
#endif
			for (; b + 4 <= twistEnd; b += 4) {
				__m128 bin = inputBinsV(b);
				__m128 pR = _mm_loadu_ps(cqR + b), pI = _mm_loadu_ps(cqI + b), dR, dI, xR, xI;
				fractionalV(_mm_sub_ps(bin, tf), re, im, true, dR, dI);
				mulConjV(pR, pI, dR, dI, xR, xI);
				_mm_storeu_ps(tR + b, xR);
				_mm_storeu_ps(tI + b, xI);
				fractionalV(_mm_sub_ps(bin, ltf), re, im, true, dR, dI);
				mulConjV(pR, pI, dR, dI, xR, xI);
				_mm_storeu_ps(lR + b, xR);
				_mm_storeu_ps(lI + b, xI);
			}
#endif
			for (; b < twistEnd; ++b) {
				Sample dR, dI;
				fractionalAt(iR, iI, bands, outputMap[b].inputBin - timeFactor, dR, dI);
				tR[b] = dR*cqR[b] + dI*cqI[b];
				tI[b] = dR*cqI[b] - dI*cqR[b];
				fractionalAt(iR, iI, bands, outputMap[b].inputBin - longTimeFactor, dR, dI);
				lR[b] = dR*cqR[b] + dI*cqI[b];
				lI[b] = dR*cqI[b] - dI*cqR[b];
			}
		}
		// each band's terms from its maximum-energy channel: the twists at b, and the upward predictions from the
		// (preliminary) outputs at b + 1 and b + longVerticalStep
		int b = startB;
#if SIGNALSMITH_STRETCH_SSE2
		if (vector) {
			const Sample *tR[2], *tI[2], *lR[2], *lI[2];
			for (int c = 0; c < 2; ++c) {
				tR[c] = twistRp + size_t(c)*chunkStride;
				tI[c] = twistIp + size_t(c)*chunkStride;
				lR[c] = longTwistRp + size_t(c)*chunkStride;
				lI[c] = longTwistIp + size_t(c)*chunkStride;
			}
#if SIGNALSMITH_STRETCH_AVX
			if (avxOn()) b = selectAvx(b, endB, L);
#endif
			for (; b + 4 <= endB && b + 4 + L <= bands; b += 4) {
				__m128 mask = maskAt(b), xR, xI;
				_mm_storeu_ps(selTwistRp + b, selected(tR, mask, b));
				_mm_storeu_ps(selTwistIp + b, selected(tI, mask, b));
				_mm_storeu_ps(selLongTwistRp + b, selected(lR, mask, b));
				_mm_storeu_ps(selLongTwistIp + b, selected(lI, mask, b));
				mulConjV(selected(oR, mask, b + 1), selected(oI, mask, b + 1), selected(tR, mask, b + 1), selected(tI, mask, b + 1), xR, xI);
				_mm_storeu_ps(selUpRp + b, xR);
				_mm_storeu_ps(selUpIp + b, xI);
				mulConjV(selected(oR, mask, b + L), selected(oI, mask, b + L), selected(lR, mask, b + L), selected(lI, mask, b + L), xR, xI);
				_mm_storeu_ps(selLongUpRp + b, xR);
				_mm_storeu_ps(selLongUpIp + b, xI);
			}
		}
#endif
		for (; b < endB; ++b) selectBand(b, L);
	}

	// Produces smoothed energy across all channels
	static constexpr size_t smoothEnergySteps = 3;
	Sample smoothEnergyState = 0;
	void smoothEnergy(size_t step, Sample smoothingBins) {
		Sample smoothingSlew = 1/(1 + smoothingBins*Sample(0.5));
		if (step-- == 0) {
			for (auto &e : energy) e = 0;
			for (int c = 0; c < channels; ++c) {
				const Sample *iR = bandIn(c), *iI = bandInI(c);
				Sample *inputEnergy = bandEnergy(c);
				for (int b = 0; b < bands; ++b) {
					Sample e = iR[b]*iR[b] + iI[b]*iI[b];
					inputEnergy[b] = e; // Used for interpolating prediction energy
					energy[b] += e;
				}
			}
			for (int b = 0; b < bands; ++b) {
				smoothedEnergy[b] = energy[b];
			}
			smoothEnergyState = 0;
			return;
		}

		// The two other steps are repeated smoothing passes, down and up
		Sample e = smoothEnergyState;
		for (int b = bands - 1; b >= 0; --b) {
			e += (smoothedEnergy[b] - e)*smoothingSlew;
			smoothedEnergy[b] = e;
		}
		for (int b = 0; b < bands; ++b) {
			e += (smoothedEnergy[b] - e)*smoothingSlew;
			smoothedEnergy[b] = e;
		}
		smoothEnergyState = e;
	}
	
	Sample mapFreq(Sample freq) const {
		if (customFreqMap) return customFreqMap(freq);
		if (freq > freqTonalityLimit) {
			return freq + (freqMultiplier - 1)*freqTonalityLimit;
		}
		return freq*freqMultiplier;
	}
	
	// Identifies spectral peaks using energy across all channels
	void findPeaks() {
		peaks.resize(0);
		
		int start = 0;
		while (start < bands) {
			if (energy[start] > smoothedEnergy[start]) {
				int end = start;
				Sample bandSum = 0, energySum = 0;
				while (end < bands && energy[end] > smoothedEnergy[end]) {
					bandSum += end*energy[end];
					energySum += energy[end];
					++end;
				}
				Sample avgBand = bandSum/energySum;
				Sample avgFreq = bandToFreq(avgBand);
				peaks.emplace_back(Peak{avgBand, freqToBand(mapFreq(avgFreq))});

				start = end;
			}
			++start;
		}
	}
	
	void updateOutputMap() {
		if (peaks.empty()) {
			for (int b = 0; b < bands; ++b) {
				outputMap[b] = {Sample(b), 1};
			}
			return;
		}
		Sample bottomOffset = peaks[0].input - peaks[0].output;
		for (int b = 0; b < std::min(bands, static_cast<int>(std::ceil(peaks[0].output))); ++b) {
			outputMap[b] = {b + bottomOffset, 1};
		}
		// Interpolate between points
		for (size_t p = 1; p < peaks.size(); ++p) {
			const Peak &prev = peaks[p - 1], &next = peaks[p];
			Sample rangeScale = 1/(next.output - prev.output);
			Sample outOffset = prev.input - prev.output;
			Sample outScale = next.input - next.output - prev.input + prev.output;
			Sample gradScale = outScale*rangeScale;
			int startBin = std::max(0, static_cast<int>(std::ceil(prev.output)));
			int endBin = std::min(bands, static_cast<int>(std::ceil(next.output)));
			for (int b = startBin; b < endBin; ++b) {
				Sample r = (b - prev.output)*rangeScale;
				Sample h = r*r*(3 - 2*r);
				Sample outB = b + outOffset + h*outScale;
				
				Sample gradH = 6*r*(1 - r);
				Sample gradB = 1 + gradH*gradScale;
				
				outputMap[b] = {outB, gradB};
			}
		}
		Sample topOffset = peaks.back().input - peaks.back().output;
		for (int b = std::max(0, static_cast<int>(peaks.back().output)); b < bands; ++b) {
			outputMap[b] = {b + topOffset, 1};
		}
	}

	// If we mapped formants the same way as mapFreq(), this would be the inverse
	Sample invMapFormant(Sample freq) const {
		if (freq*invFormantMultiplier > freqTonalityLimit) {
			return freq + (1 - formantMultiplier)*freqTonalityLimit;
		}
		return freq*invFormantMultiplier;
	}

	Sample freqEstimateWeighted = 0;
	Sample freqEstimateWeight = 0;
	Sample estimateFrequency() {
		// 3 highest peaks in the input
		std::array<int, 3> peakIndices{0, 0, 0};
		for (int b = 1; b < bands - 1; ++b) {
			Sample e = formantMetric[b];
			// local maxima only
			if (e < formantMetric[b - 1] || e <= formantMetric[b + 1]) continue;
			
			if (e > formantMetric[peakIndices[0]]) {
				if (e > formantMetric[peakIndices[1]]) {
					if (e > formantMetric[peakIndices[2]]) {
						peakIndices = {peakIndices[1], peakIndices[2], b};
					} else {
						peakIndices = {peakIndices[1], b, peakIndices[2]};
					}
				} else {
					peakIndices[0] = b;
				}
			}
		}
		
		// VERY rough pitch estimation
		int peakEstimate = peakIndices[2];
		if (formantMetric[peakIndices[1]] > formantMetric[peakIndices[2]]*0.1) {
			int diff = std::abs(peakEstimate - peakIndices[1]);
			if (diff > peakEstimate/8 && diff < peakEstimate*7/8) peakEstimate = peakEstimate%diff;
			if (formantMetric[peakIndices[0]] > formantMetric[peakIndices[2]]*0.01) {
				diff = std::abs(peakEstimate - peakIndices[0]);
				if (diff > peakEstimate/8 && diff < peakEstimate*7/8) peakEstimate = peakEstimate%diff;
			}
		}
		Sample weight = formantMetric[peakIndices[2]];
		// Smooth it out a bit
		freqEstimateWeighted += (peakEstimate*weight - freqEstimateWeighted)*Sample(0.25);
		freqEstimateWeight += (weight - freqEstimateWeight)*Sample(0.25);
		
		return freqEstimateWeighted/(freqEstimateWeight + Sample(1e-30));
	}
	
	Sample freqEstimate;
	
	std::vector<Sample> formantMetric;
	Sample formantBaseFreq = 0;
	void updateFormants(size_t step) {
		if (step-- == 0) {
			for (auto &e : formantMetric) e = 0;
			for (int c = 0; c < channels; ++c) {
				const Sample *inputEnergy = bandEnergy(c);
				for (int b = 0; b < bands; ++b) {
					formantMetric[b] += inputEnergy[b];
				}
			}

			freqEstimate = freqToBand(formantBaseFreq);
			if (formantBaseFreq <= 0) freqEstimate = estimateFrequency();
		} else if (step-- == 0) {
			Sample decay = 1 - 1/(freqEstimate*Sample(0.5) + 1);
			Sample e = 0;
			for (size_t repeat = 0; repeat < 2; ++repeat) {
				for (int b = bands - 1; b >= 0; --b) {
					e = std::max(formantMetric[b], e*decay);
					formantMetric[b] = e;
				}
				for (int b = 0; b < bands; ++b) {
					e = std::max(formantMetric[b], e*decay);
					formantMetric[b] = e;
				}
			}
			decay = 1/decay;
			for (size_t repeat = 0; repeat < 2; ++repeat) {
				for (int b = bands - 1; b >= 0; --b) {
					e = std::min(formantMetric[b], e*decay);
					formantMetric[b] = e;
				}
				for (int b = 0; b < bands; ++b) {
					e = std::min(formantMetric[b], e*decay);
					formantMetric[b] = e;
				}
			}
		} else {
			auto getFormant = [&](Sample band) -> Sample {
				if (band < 0) return 0;
				band = std::min(band, static_cast<Sample>(bands));
				int floorBand = std::floor(band);
				Sample fracBand = band - floorBand;
				Sample low = formantMetric[floorBand], high = formantMetric[floorBand + 1];
				return low + (high - low)*fracBand;
			};

			for (int b = 0; b < bands; ++b) {
				Sample inputF = bandToFreq(static_cast<Sample>(b));
				Sample outputF = formantCompensation ? mapFreq(inputF) : inputF;
				outputF = invMapFormant(outputF);

				Sample inputE = formantMetric[b];
				Sample targetE = getFormant(freqToBand(outputF));

				Sample formantRatio = targetE/(inputE + Sample(1e-30));
				Sample energyRatio = formantRatio;

				for (int c = 0; c < channels; ++c) {
					// This is what's used to decide the output energy, so this affects the output
					bandEnergy(c)[b] *= energyRatio;
				}
			}
		}
	}

	// Proxy class to avoid copying/allocating anything
	template<class Io>
	struct OffsetIO {
		Io &io;
		int offset;

		struct Channel {
			Io &io;
			int channel;
			int offset;
			
			auto operator[](int i) -> decltype(io[0][0]) {
				return io[channel][i + offset];
			}
		};
		Channel operator[](int c) {
			return {io, c, offset};
		}
	};
};

}} // namespace
#endif // include guard

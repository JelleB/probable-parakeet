#include "AudioEngine.hpp"

#include "LogBins.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool isPowerOfTwo(int n) { return n > 0 && (n & (n - 1)) == 0; }

void fftInPlace(std::vector<std::complex<float>>& a) {
  const std::size_t n = a.size();
  if (n == 0) return;

  // Bit reversal permutation
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }

  // Cooley-Tukey
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const float ang = -2.0f * kPi / static_cast<float>(len);
    const std::complex<float> wlen(std::cos(ang), std::sin(ang));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<float> w(1.0f, 0.0f);
      for (std::size_t j = 0; j < len / 2; ++j) {
        const std::complex<float> u = a[i + j];
        const std::complex<float> v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wlen;
      }
    }
  }
}

void applyHannWindow(std::vector<float>& x) {
  const std::size_t n = x.size();
  if (n <= 1) return;
  for (std::size_t i = 0; i < n; ++i) {
    const float w = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) /
                                           static_cast<float>(n - 1)));
    x[i] *= w;
  }
}

// Convert a linear magnitude to a 0..1 "meter" value.
float magToMeter(float mag) {
  const float db = 20.0f * std::log10(std::max(1.0e-9f, mag));
  const float floorDb = -80.0f;
  const float clamped = std::min(0.0f, std::max(floorDb, db));
  return (clamped - floorDb) / (0.0f - floorDb);
}

} // namespace

AudioEngine::AudioEngine(int sampleRate_, int fftSize_, int logBins_, const std::string& flacOutputPath)
    : sampleRate(sampleRate_), fftSize(fftSize_), logBins(logBins_), flacPath(flacOutputPath) {
  if (sampleRate <= 0) sampleRate = 48000;
  if (fftSize <= 0) fftSize = 2048;
  if (logBins <= 0) logBins = 64;
  if (!isPowerOfTwo(fftSize)) {
    throw std::invalid_argument("fftSize must be a power of two");
  }

  latestLog.assign(static_cast<std::size_t>(logBins), 0.0f);
  captureBuffer.assign(static_cast<std::size_t>(fftSize), 0.0f);

  flacEnabled = !flacPath.empty();
  if (flacEnabled) initFlac();
}

AudioEngine::~AudioEngine() {
  stop();
  closeFlac();
}

void AudioEngine::start() {
  bool expected = false;
  if (!running.compare_exchange_strong(expected, true)) return;
  audioThread = std::thread(&AudioEngine::audioThreadFunc, this);
}

void AudioEngine::stop() {
  bool expected = true;
  if (!running.compare_exchange_strong(expected, false)) return;
  if (audioThread.joinable()) audioThread.join();
}

std::vector<float> AudioEngine::getLogBins() {
  std::scoped_lock lk(logMutex);
  return latestLog;
}

std::vector<float> AudioEngine::getLogBinCenters() const {
  LogBins lb(LogBins::Config{.sampleRate = sampleRate, .fftSize = fftSize, .bins = logBins, .minHz = 20.0f});
  return lb.centersHz();
}

std::vector<std::pair<float, float>> AudioEngine::computeLogBinFreqs() const {
  LogBins lb(LogBins::Config{.sampleRate = sampleRate, .fftSize = fftSize, .bins = logBins, .minHz = 20.0f});
  return lb.edgesHz();
}

void AudioEngine::audioThreadFunc() {
  using clock = std::chrono::steady_clock;

  // Simple synthetic signal: sine tone that sweeps slowly.
  float phase = 0.0f;
  float toneHz = 220.0f;
  float sweepDir = 1.0f;
  const float amp = 0.2f;

  LogBins lb(LogBins::Config{.sampleRate = sampleRate, .fftSize = fftSize, .bins = logBins, .minHz = 20.0f});

  std::vector<float> frame(static_cast<std::size_t>(fftSize), 0.0f);
  std::vector<std::complex<float>> fftBuf(static_cast<std::size_t>(fftSize));
  std::vector<float> mags(static_cast<std::size_t>(fftSize / 2 + 1), 0.0f);
  std::vector<std::int32_t> pcm32(static_cast<std::size_t>(fftSize), 0);

  const auto bufferDur =
      std::chrono::duration<double>(static_cast<double>(fftSize) / static_cast<double>(sampleRate));

  auto nextTick = clock::now();
  while (running.load()) {
    nextTick += std::chrono::duration_cast<clock::duration>(bufferDur);

    // Sweep between 110Hz and 1760Hz for a more interesting display.
    toneHz += sweepDir * 0.5f;
    if (toneHz > 1760.0f) { toneHz = 1760.0f; sweepDir = -1.0f; }
    if (toneHz < 110.0f)  { toneHz = 110.0f;  sweepDir =  1.0f; }

    const float phaseInc = 2.0f * kPi * toneHz / static_cast<float>(sampleRate);

    for (int i = 0; i < fftSize; ++i) {
      const float s = amp * std::sin(phase);
      phase += phaseInc;
      if (phase > 2.0f * kPi) phase -= 2.0f * kPi;
      frame[static_cast<std::size_t>(i)] = s;
    }

    // Capture buffer for potential external use later.
    captureBuffer = frame;

    // Optional FLAC dump (mono).
    if (flacEnabled && flacEncoder) {
      for (int i = 0; i < fftSize; ++i) {
        const float s = std::max(-1.0f, std::min(1.0f, frame[static_cast<std::size_t>(i)]));
        // libFLAC expects signed int32 samples. We'll store 16-bit aligned in int32.
        const int v = static_cast<int>(std::lrint(s * 32767.0f));
        pcm32[static_cast<std::size_t>(i)] = static_cast<std::int32_t>(v);
      }
#if AUDIOENGINE_HAS_FLAC
      (void)FLAC__stream_encoder_process_interleaved(flacEncoder, pcm32.data(), static_cast<unsigned>(fftSize));
#endif
    }

    // FFT analysis
    std::vector<float> windowed = frame;
    applyHannWindow(windowed);
    for (int i = 0; i < fftSize; ++i) {
      fftBuf[static_cast<std::size_t>(i)] = std::complex<float>(windowed[static_cast<std::size_t>(i)], 0.0f);
    }
    fftInPlace(fftBuf);

    // Magnitudes for bins 0..N/2 (inclusive)
    const float norm = 1.0f / static_cast<float>(fftSize);
    for (int k = 0; k <= fftSize / 2; ++k) {
      const float mag = std::abs(fftBuf[static_cast<std::size_t>(k)]) * norm;
      mags[static_cast<std::size_t>(k)] = mag;
    }

    auto logLinear = lb.compute(mags);
    for (auto& v : logLinear) v = magToMeter(v);

    {
      std::scoped_lock lk(logMutex);
      latestLog = std::move(logLinear);
    }

    std::this_thread::sleep_until(nextTick);
  }
}

void AudioEngine::initFlac() {
  if (!flacEnabled) return;
#if AUDIOENGINE_HAS_FLAC
  flacEncoder = FLAC__stream_encoder_new();
  if (!flacEncoder) {
    flacEnabled = false;
    return;
  }
  FLAC__stream_encoder_set_channels(flacEncoder, 1);
  FLAC__stream_encoder_set_bits_per_sample(flacEncoder, 16);
  FLAC__stream_encoder_set_sample_rate(flacEncoder, static_cast<unsigned>(sampleRate));
  FLAC__stream_encoder_set_compression_level(flacEncoder, 5);

  const FLAC__StreamEncoderInitStatus st =
      FLAC__stream_encoder_init_file(flacEncoder, flacPath.c_str(), nullptr, nullptr);
  if (st != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
    FLAC__stream_encoder_delete(flacEncoder);
    flacEncoder = nullptr;
    flacEnabled = false;
    return;
  }
#else
  // Built without FLAC headers/library; silently disable.
  flacEnabled = false;
#endif
}

void AudioEngine::closeFlac() {
#if AUDIOENGINE_HAS_FLAC
  if (flacEncoder) {
    (void)FLAC__stream_encoder_finish(flacEncoder);
    FLAC__stream_encoder_delete(flacEncoder);
    flacEncoder = nullptr;
  }
#endif
  flacEnabled = false;
}


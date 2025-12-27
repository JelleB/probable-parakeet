#include "AudioEngine.hpp"
#include "LogBins.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

#if __has_include(<portaudio.h>)
#include <portaudio.h>
#define AUDIOENGINE_HAS_PORTAUDIO 1
#else
#define AUDIOENGINE_HAS_PORTAUDIO 0
struct PaStream;
struct PaStreamCallbackTimeInfo;
using PaStreamCallbackFlags = unsigned long;
#endif

#if __has_include(<kissfft/kiss_fftr.h>)
#include <kissfft/kiss_fftr.h>
#define AUDIOENGINE_HAS_KISSFFT 1
#else
#define AUDIOENGINE_HAS_KISSFFT 0
using kiss_fftr_cfg = void*;
struct kiss_fft_cpx { float r; float i; };
#endif

namespace {
constexpr float kPi = 3.14159265358979323846f;
} // namespace

AudioEngine::AudioEngine(int sampleRate_, int fftSize_, int logBins_, const std::string& flacOutputPath)
    : sampleRate(sampleRate_),
      fftSize(fftSize_),
      logBins(logBins_),
      flacPath(flacOutputPath),
      flacEnabled(!flacOutputPath.empty()) {
    if (sampleRate <= 0) sampleRate = 48000;
    if (fftSize <= 0) fftSize = 2048;
    if (logBins <= 0) logBins = 64;

    latestLog.resize(static_cast<std::size_t>(logBins), 0.0f);
    captureBuffer.resize(static_cast<std::size_t>(fftSize), 0.0f);
}

AudioEngine::~AudioEngine() {
  stop();
}

void AudioEngine::start() {
    if (running.load()) return;
    running = true;

#if AUDIOENGINE_HAS_PORTAUDIO
    Pa_Initialize();
#else
    // Still start a thread so the app doesn't hang, but it will error immediately.
#endif

    if (flacEnabled) initFlac();
    audioThread = std::thread(&AudioEngine::audioThreadFunc, this);
}

void AudioEngine::stop() {
    if (!running.load()) return;
    running = false;

    if (audioThread.joinable()) audioThread.join();

    if (flacEnabled) closeFlac();

#if AUDIOENGINE_HAS_PORTAUDIO
    Pa_Terminate();
#endif
}

std::vector<float> AudioEngine::getLogBins() {
    std::lock_guard<std::mutex> lock(logMutex);
    return latestLog;
}

std::vector<std::pair<float, float>> AudioEngine::computeLogBinFreqs() const {
    std::vector<std::pair<float, float>> out;
    out.reserve(static_cast<std::size_t>(logBins));

    const float fMin = 20.0f;
    const float fMax = static_cast<float>(sampleRate) * 0.5f;

    for (int i = 0; i < logBins; i++) {
        const float a = static_cast<float>(i) / static_cast<float>(logBins);
        const float b = static_cast<float>(i + 1) / static_cast<float>(logBins);

        const float fLow  = fMin * std::pow(fMax / fMin, a);
        const float fHigh = fMin * std::pow(fMax / fMin, b);

        out.emplace_back(fLow, fHigh);
    }
    return out;
}

std::vector<float> AudioEngine::getLogBinCenters() const {
    auto ranges = computeLogBinFreqs();
    std::vector<float> centers;
    centers.reserve(ranges.size());

    for (const auto& r : ranges)
        centers.push_back(std::sqrt(r.first * r.second));

    return centers;
}

#if AUDIOENGINE_HAS_PORTAUDIO
static int paCallback(
    const void* input,
    void*,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo*,
    PaStreamCallbackFlags,
    void* userData
) {
    auto* eng = static_cast<AudioEngine*>(userData);
    const float* in = static_cast<const float*>(input);
    if (!in) return paContinue;

    // Write to ring buffer (mono).
    {
        std::lock_guard<std::mutex> lock(eng->captureMutex);
        std::size_t writeIdx = eng->captureWriteIdx.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < static_cast<std::size_t>(frameCount); i++) {
            eng->captureBuffer[writeIdx] = in[i];
            writeIdx = (writeIdx + 1) % static_cast<std::size_t>(eng->fftSize);
        }
        eng->captureWriteIdx.store(writeIdx, std::memory_order_relaxed);
    }

    if (eng->flacEnabled && eng->flacEncoder) {
#if AUDIOENGINE_HAS_FLAC
        static thread_local std::vector<FLAC__int32> pcm;
        pcm.resize(static_cast<std::size_t>(frameCount));
        for (std::size_t i = 0; i < static_cast<std::size_t>(frameCount); i++)
            pcm[i] = static_cast<FLAC__int32>(in[i] * 32767.0f);

        FLAC__stream_encoder_process_interleaved(
            eng->flacEncoder, pcm.data(), static_cast<unsigned>(frameCount)
        );
#endif
    }

    return paContinue;
}
#endif

void AudioEngine::audioThreadFunc() {
#if !(AUDIOENGINE_HAS_PORTAUDIO && AUDIOENGINE_HAS_KISSFFT)
    // Deps not present in this build environment; keep running but produce zeros.
    // (Throwing from a std::thread would call std::terminate.)
    std::vector<float> zeros(static_cast<std::size_t>(logBins), 0.0f);
    while (running.load()) {
        {
            std::lock_guard<std::mutex> lock(logMutex);
            latestLog = zeros;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
#else
    PaStream* stream = nullptr;

    Pa_OpenDefaultStream(
        &stream,
        1, 0,
        paFloat32,
        sampleRate,
        256,
        paCallback,
        this
    );
    Pa_StartStream(stream);

    kiss_fftr_cfg cfg = kiss_fftr_alloc(fftSize, 0, nullptr, nullptr);

    std::vector<float> window(static_cast<std::size_t>(fftSize));
    for (int i = 0; i < fftSize; i++)
        window[static_cast<std::size_t>(i)] =
            0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(fftSize - 1));

    std::vector<float> block(static_cast<std::size_t>(fftSize));
    std::vector<kiss_fft_cpx> out(static_cast<std::size_t>(fftSize / 2 + 1));
    std::vector<float> mag(static_cast<std::size_t>(fftSize / 2));

    while (running.load()) {
        // Snapshot last fftSize samples from ring buffer into chronological order.
        {
            std::lock_guard<std::mutex> lock(captureMutex);
            const std::size_t w = captureWriteIdx.load(std::memory_order_relaxed);
            for (int i = 0; i < fftSize; ++i) {
                const std::size_t idx =
                    (w + static_cast<std::size_t>(i)) % static_cast<std::size_t>(fftSize);
                block[static_cast<std::size_t>(i)] = captureBuffer[idx];
            }
        }

        for (int i = 0; i < fftSize; i++)
            block[static_cast<std::size_t>(i)] *= window[static_cast<std::size_t>(i)];

        kiss_fftr(cfg, block.data(), out.data());

        for (int i = 0; i < fftSize / 2; i++) {
            const float r = out[static_cast<std::size_t>(i)].r;
            const float im = out[static_cast<std::size_t>(i)].i;
            mag[static_cast<std::size_t>(i)] = std::sqrt(r * r + im * im);
        }

        auto log = LogBins::compute(mag, sampleRate, fftSize, logBins);

        {
            std::lock_guard<std::mutex> lock(logMutex);
            latestLog = std::move(log);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::free(cfg);
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
#endif
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


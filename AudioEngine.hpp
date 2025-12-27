#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// A tiny "audio engine" skeleton intended for demos/tests.
// It does NOT talk to any OS audio API; it simulates a realtime audio callback
// by generating audio buffers on a background thread at the configured rate.
//
// This is useful for demonstrating DSP/analyzers (like LogBins) without pulling
// in dependencies such as PortAudio/ALSA/CoreAudio.
class AudioEngine final {
public:
  struct Config {
    int sampleRate = 48000;
    int channels = 1;
    std::size_t framesPerBuffer = 1024;
  };

  using Analyzer = std::function<void(const float* interleaved,
                                      std::size_t frames,
                                      int channels,
                                      int sampleRate)>;

  explicit AudioEngine(Config cfg = {});
  ~AudioEngine();

  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  // Starts the simulation thread. Safe to call multiple times.
  void start();

  // Stops the simulation thread. Safe to call multiple times.
  void stop();

  bool isRunning() const noexcept { return m_running.load(); }
  const Config& config() const noexcept { return m_cfg; }

  // Simple signal generator parameters (sine wave).
  void setToneHz(double hz);
  void setAmplitude(double amp01);

  // Adds an analyzer that is invoked once per buffer.
  // Analyzers must be thread-safe and non-blocking in real engines; here we
  // still recommend keeping them fast.
  void addAnalyzer(Analyzer analyzer);
  void clearAnalyzers();

private:
  void runThread_();

  Config m_cfg;

  std::atomic<bool> m_running{false};
  std::thread m_thread;

  // Generator state
  std::mutex m_genMutex;
  double m_toneHz = 440.0;
  double m_amp = 0.2;
  double m_phase = 0.0;

  std::mutex m_analyzersMutex;
  std::vector<Analyzer> m_analyzers;
};


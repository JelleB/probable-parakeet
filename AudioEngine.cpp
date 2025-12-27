#include "AudioEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

AudioEngine::AudioEngine(Config cfg) : m_cfg(cfg) {
  if (m_cfg.sampleRate <= 0) m_cfg.sampleRate = 48000;
  if (m_cfg.channels <= 0) m_cfg.channels = 1;
  if (m_cfg.framesPerBuffer == 0) m_cfg.framesPerBuffer = 1024;
}

AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::start() {
  bool expected = false;
  if (!m_running.compare_exchange_strong(expected, true)) {
    return; // already running
  }
  m_thread = std::thread(&AudioEngine::runThread_, this);
}

void AudioEngine::stop() {
  bool expected = true;
  if (!m_running.compare_exchange_strong(expected, false)) {
    return; // already stopped
  }
  if (m_thread.joinable()) m_thread.join();
}

void AudioEngine::setToneHz(double hz) {
  std::scoped_lock lk(m_genMutex);
  m_toneHz = std::max(0.0, hz);
}

void AudioEngine::setAmplitude(double amp01) {
  std::scoped_lock lk(m_genMutex);
  m_amp = std::clamp(amp01, 0.0, 1.0);
}

void AudioEngine::addAnalyzer(Analyzer analyzer) {
  std::scoped_lock lk(m_analyzersMutex);
  m_analyzers.push_back(std::move(analyzer));
}

void AudioEngine::clearAnalyzers() {
  std::scoped_lock lk(m_analyzersMutex);
  m_analyzers.clear();
}

void AudioEngine::runThread_() {
  using clock = std::chrono::steady_clock;

  const auto bufferDuration =
      std::chrono::duration<double>(static_cast<double>(m_cfg.framesPerBuffer) /
                                    static_cast<double>(m_cfg.sampleRate));

  std::vector<float> interleaved(m_cfg.framesPerBuffer *
                                 static_cast<std::size_t>(m_cfg.channels));

  auto nextTick = clock::now();
  while (m_running.load()) {
    nextTick += std::chrono::duration_cast<clock::duration>(bufferDuration);

    double toneHz = 0.0;
    double amp = 0.0;
    {
      std::scoped_lock lk(m_genMutex);
      toneHz = m_toneHz;
      amp = m_amp;
    }

    const double twoPi = 2.0 * std::numbers::pi_v<double>;
    const double phaseInc = (toneHz <= 0.0)
                                ? 0.0
                                : (twoPi * toneHz /
                                   static_cast<double>(m_cfg.sampleRate));

    // Generate one buffer (interleaved).
    for (std::size_t f = 0; f < m_cfg.framesPerBuffer; ++f) {
      const float s =
          static_cast<float>(amp * std::sin(m_phase));
      m_phase += phaseInc;
      if (m_phase > twoPi) m_phase -= twoPi;

      for (int c = 0; c < m_cfg.channels; ++c) {
        interleaved[f * static_cast<std::size_t>(m_cfg.channels) +
                    static_cast<std::size_t>(c)] = s;
      }
    }

    // Snapshot analyzers to avoid holding a lock while calling user code.
    std::vector<Analyzer> analyzersCopy;
    {
      std::scoped_lock lk(m_analyzersMutex);
      analyzersCopy = m_analyzers;
    }
    for (const auto& a : analyzersCopy) {
      if (a) a(interleaved.data(), m_cfg.framesPerBuffer, m_cfg.channels, m_cfg.sampleRate);
    }

    std::this_thread::sleep_until(nextTick);
  }
}


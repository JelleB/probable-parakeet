#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

// Helper for mapping an FFT magnitude spectrum into log-spaced frequency bins.
// This is intentionally dependency-free and "good enough" for visualization.
class LogBins final {
public:
  struct Config {
    int sampleRate = 48000;
    int fftSize = 2048;
    int bins = 64;
    float minHz = 20.0f;
  };

  explicit LogBins(Config cfg) : m_cfg(cfg) {
    if (m_cfg.sampleRate <= 0) m_cfg.sampleRate = 48000;
    if (m_cfg.fftSize <= 0) m_cfg.fftSize = 2048;
    if (m_cfg.bins <= 0) m_cfg.bins = 64;
    if (!(m_cfg.minHz > 0.0f)) m_cfg.minHz = 20.0f;
  }

  std::vector<std::pair<float, float>> edgesHz() const {
    const float nyquist = 0.5f * static_cast<float>(m_cfg.sampleRate);
    const float minHz = std::min(std::max(1.0f, m_cfg.minHz), nyquist);
    const float maxHz = std::max(minHz, nyquist);

    std::vector<std::pair<float, float>> out;
    out.reserve(static_cast<std::size_t>(m_cfg.bins));

    const float logMin = std::log10(minHz);
    const float logMax = std::log10(maxHz);
    const float step = (logMax - logMin) / static_cast<float>(m_cfg.bins);

    for (int i = 0; i < m_cfg.bins; ++i) {
      const float a = std::pow(10.0f, logMin + step * static_cast<float>(i));
      const float b = std::pow(10.0f, logMin + step * static_cast<float>(i + 1));
      out.emplace_back(a, std::min(b, nyquist));
    }
    // Ensure monotonicity and non-empty bands
    for (auto& p : out) {
      if (p.second < p.first) p.second = p.first;
    }
    return out;
  }

  std::vector<float> centersHz() const {
    const auto e = edgesHz();
    std::vector<float> c;
    c.reserve(e.size());
    for (const auto& [lo, hi] : e) {
      // geometric center is better for log spacing
      c.push_back(std::sqrt(std::max(1.0e-6f, lo) * std::max(1.0e-6f, hi)));
    }
    return c;
  }

  // Map a magnitude spectrum (bins 0..N/2) into log bins.
  // magnitude.size() should be (fftSize/2 + 1).
  std::vector<float> compute(const std::vector<float>& magnitude) const {
    const auto bands = edgesHz();
    std::vector<float> out(bands.size(), 0.0f);
    if (magnitude.empty()) return out;

    const float hzPerBin = static_cast<float>(m_cfg.sampleRate) /
                           static_cast<float>(m_cfg.fftSize);

    for (std::size_t i = 0; i < bands.size(); ++i) {
      const float lo = bands[i].first;
      const float hi = bands[i].second;

      const int binLo = std::max(0, static_cast<int>(std::floor(lo / hzPerBin)));
      const int binHi = std::min(static_cast<int>(magnitude.size()) - 1,
                                 static_cast<int>(std::ceil(hi / hzPerBin)));
      if (binHi < binLo) continue;

      float acc = 0.0f;
      int count = 0;
      for (int b = binLo; b <= binHi; ++b) {
        acc += magnitude[static_cast<std::size_t>(b)];
        ++count;
      }
      out[i] = (count > 0) ? (acc / static_cast<float>(count)) : 0.0f;
    }
    return out;
  }

private:
  Config m_cfg;
};


#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

// Log-spaced binning for FFT magnitudes (visualization friendly).
struct LogBins final {
  // Returns logBins values derived from `mag`, which is typically `fftSize/2`
  // bins of linear magnitude (k = 0..fftSize/2-1).
  static std::vector<float> compute(const std::vector<float>& mag,
                                    int sampleRate,
                                    int fftSize,
                                    int logBins,
                                    float fMin = 20.0f) {
    if (sampleRate <= 0) sampleRate = 48000;
    if (fftSize <= 0) fftSize = 2048;
    if (logBins <= 0) logBins = 64;

    const float nyquist = 0.5f * static_cast<float>(sampleRate);
    const float minHz = std::min(std::max(1.0f, fMin), nyquist);
    const float maxHz = std::max(minHz, nyquist);

    std::vector<float> out(static_cast<std::size_t>(logBins), 0.0f);
    if (mag.empty()) return out;

    const float logMin = std::log10(minHz);
    const float logMax = std::log10(maxHz);
    const float step = (logMax - logMin) / static_cast<float>(logBins);
    const float hzPerBin = static_cast<float>(sampleRate) / static_cast<float>(fftSize);

    for (int i = 0; i < logBins; ++i) {
      const float a = static_cast<float>(i) / static_cast<float>(logBins);
      const float b = static_cast<float>(i + 1) / static_cast<float>(logBins);
      (void)a;
      (void)b;

      const float fLow = std::pow(10.0f, logMin + step * static_cast<float>(i));
      const float fHigh = std::pow(10.0f, logMin + step * static_cast<float>(i + 1));

      const int binLo = std::max(0, static_cast<int>(std::floor(fLow / hzPerBin)));
      const int binHi = std::min(static_cast<int>(mag.size()) - 1,
                                 static_cast<int>(std::ceil(std::min(fHigh, nyquist) / hzPerBin)));
      if (binHi < binLo) continue;

      float acc = 0.0f;
      int count = 0;
      for (int k = binLo; k <= binHi; ++k) {
        acc += mag[static_cast<std::size_t>(k)];
        ++count;
      }
      out[static_cast<std::size_t>(i)] = (count > 0) ? (acc / static_cast<float>(count)) : 0.0f;
    }

    return out;
  }
};


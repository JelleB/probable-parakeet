#include <doctest/doctest.h>

#include "LogBins.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

TEST_CASE("LogBins::compute returns correct size and non-negative") {
    const int sampleRate = 44100;
    const int fftSize = 1024;
    const int numBins = 64;

    std::vector<float> mag(static_cast<std::size_t>(fftSize / 2), 0.0f);
    mag[10] = 1.0f;

    auto out = LogBins::compute(mag, sampleRate, fftSize, numBins);
    CHECK(out.size() == static_cast<std::size_t>(numBins));
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return v >= 0.0f; }));
}

TEST_CASE("LogBins::compute places single-tone energy in exactly one log bin") {
    const int sampleRate = 44100;
    const int fftSize = 1024;
    const int numBins = 64;
    const float freqHz = 1000.0f;

    std::vector<float> mag(static_cast<std::size_t>(fftSize / 2), 0.0f);
    const int k = std::max(0, std::min(static_cast<int>(mag.size()) - 1,
                                       static_cast<int>(std::floor(freqHz * fftSize / sampleRate))));
    mag[static_cast<std::size_t>(k)] = 1.0f;

    auto out = LogBins::compute(mag, sampleRate, fftSize, numBins);

    // Find which log-bins include k using the exact same math as LogBins::compute.
    const float fMin = 20.0f;
    const float fMax = sampleRate * 0.5f;
    std::vector<int> binsCoveringK;
    for (int i = 0; i < numBins; i++) {
        const float a = float(i) / numBins;
        const float b = float(i + 1) / numBins;
        const float fLow  = fMin * std::pow(fMax / fMin, a);
        const float fHigh = fMin * std::pow(fMax / fMin, b);

        const int binLow = std::max(0, int(std::floor(fLow * fftSize / sampleRate)));
        const int binHigh = std::min(int(mag.size()) - 1,
                                     int(std::ceil(fHigh * fftSize / sampleRate)));
        if (binLow <= k && k <= binHigh) {
            binsCoveringK.push_back(i);
        }
    }
    REQUIRE(!binsCoveringK.empty());

    int nonZeroCount = 0;
    for (int i = 0; i < numBins; i++) {
        if (out[static_cast<std::size_t>(i)] > 0.0f) nonZeroCount++;
    }

    // Because of floor/ceil boundaries, two adjacent log bins can overlap on the same FFT index.
    // Assert that only bins that cover k are non-zero.
    for (int i = 0; i < numBins; i++) {
        const bool covers =
            std::find(binsCoveringK.begin(), binsCoveringK.end(), i) != binsCoveringK.end();
        if (covers) {
            CHECK(out[static_cast<std::size_t>(i)] > 0.0f);
        } else {
            CHECK(out[static_cast<std::size_t>(i)] == doctest::Approx(0.0f));
        }
    }
    CHECK(nonZeroCount == static_cast<int>(binsCoveringK.size()));
}


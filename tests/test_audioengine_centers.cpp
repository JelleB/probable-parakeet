#include <doctest/doctest.h>

#include "AudioEngine.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

TEST_CASE("AudioEngine::getLogBinCenters matches expected geometric centers") {
    const int sampleRate = 44100;
    const int fftSize = 1024;
    const int numBins = 64;
    AudioEngine engine(sampleRate, fftSize, numBins, "");

    const auto centers = engine.getLogBinCenters();
    REQUIRE(centers.size() == static_cast<std::size_t>(numBins));

    // Monotonic and within expected range.
    CHECK(std::is_sorted(centers.begin(), centers.end()));
    CHECK(centers.front() >= 20.0f);
    CHECK(centers.back() <= static_cast<float>(sampleRate) * 0.5f + 1.0f);

    // Exact expected centers using the same math as computeLogBinFreqs() + sqrt(lo*hi).
    std::vector<float> expected;
    expected.reserve(static_cast<std::size_t>(numBins));

    const float fMin = 20.0f;
    const float fMax = static_cast<float>(sampleRate) * 0.5f;
    for (int i = 0; i < numBins; i++) {
        const float a = static_cast<float>(i) / static_cast<float>(numBins);
        const float b = static_cast<float>(i + 1) / static_cast<float>(numBins);
        const float fLow  = fMin * std::pow(fMax / fMin, a);
        const float fHigh = fMin * std::pow(fMax / fMin, b);
        expected.push_back(std::sqrt(fLow * fHigh));
    }

    for (int i = 0; i < numBins; i++) {
        CHECK(centers[static_cast<std::size_t>(i)] == doctest::Approx(expected[static_cast<std::size_t>(i)]).epsilon(1e-5));
    }
}


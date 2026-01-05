#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

class LogBins {
public:
    static std::vector<float> compute(
        const std::vector<float>& fftMag,
        int sampleRate,
        int fftSize,
        int numBins
    ) {
        std::vector<float> out(numBins, 0.0f);

        float fMin = 20.0f;
        float fMax = sampleRate * 0.5f;

        for (int i = 0; i < numBins; i++) {
            float a = float(i) / numBins;
            float b = float(i + 1) / numBins;

            float fLow  = fMin * std::pow(fMax / fMin, a);
            float fHigh = fMin * std::pow(fMax / fMin, b);

            int binLow = std::max(
                0, int(std::floor(fLow * fftSize / sampleRate))
            );
            int binHigh = std::min(
                int(fftMag.size()) - 1,
                int(std::ceil(fHigh * fftSize / sampleRate))
            );

            float sum = 0.0f;
            int count = 0;

            for (int b = binLow; b <= binHigh; b++) {
                sum += fftMag[b];
                count++;
            }

            out[i] = (count > 0) ? sum / count : 0.0f;
        }

        return out;
    }
};


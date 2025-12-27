#include <iostream>
#include <thread>
#include <chrono>

#include "AudioEngine.hpp"

int main() {
    AudioEngine engine(
        44100,
        1024,
        64,
        "test.flac"   // "" disables FLAC
    );

    engine.start();

    auto centers = engine.getLogBinCenters();
    for (int i = 0; i < static_cast<int>(centers.size()); i++)
        std::cout << i << ": " << centers[static_cast<std::size_t>(i)] << " Hz\n";

    for (int i = 0; i < 20; i++) {
        auto bins = engine.getLogBins();
        std::cout << "Frame " << i << " bin[10]=" << bins[10] << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    engine.stop();
    return 0;
}


#include "AudioEngine.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

static void printBars(const std::vector<float>& bins, int width = 50) {
  for (float v : bins) {
    const int n = static_cast<int>(v * static_cast<float>(width));
    for (int i = 0; i < n; ++i) std::cout << '#';
    std::cout << '\n';
  }
}

int main() {
  // Example usage:
  // - generate synthetic audio in a background thread
  // - compute FFT -> log-spaced bins
  // - poll bins every ~200ms
  //
  // If you have libFLAC available and want to dump audio, pass a path:
  // AudioEngine engine(48000, 2048, 64, "out.flac");
  AudioEngine engine(48000, 2048, 64);
  engine.start();

  const auto centers = engine.getLogBinCenters();
  std::cout << "Log-bin centers (Hz): ";
  for (std::size_t i = 0; i < centers.size(); ++i) {
    if (i) std::cout << ", ";
    std::cout << std::fixed << std::setprecision(1) << centers[i];
  }
  std::cout << "\n\n";

  for (int t = 0; t < 25; ++t) { // ~5 seconds at 200ms
    const auto bins = engine.getLogBins();
    std::cout << "Frame " << t << " (0..1 meter per bin)\n";
    printBars(bins, 40);
    std::cout << "----\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  engine.stop();
  return 0;
}


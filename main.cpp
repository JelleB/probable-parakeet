#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>

#include "AudioEngine.hpp"
#include "WebSocketServer.hpp"

static std::string jsonArray(const std::vector<float>& v) {
    std::ostringstream oss;
    oss << '[';
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) oss << ',';
        oss << v[i];
    }
    oss << ']';
    return oss.str();
}

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

    // WebSocket server for Node.js visualization
    // - Connect to ws://localhost:8787 from Node and parse JSON frames.
    WebSocketServer ws(8787);
    ws.start([&]() {
        const auto bins = engine.getLogBins();
        std::ostringstream oss;
        oss << "{\"centers\":" << jsonArray(centers)
            << ",\"bins\":" << jsonArray(bins)
            << "}";
        return oss.str();
    }, 100);

    for (int i = 0; i < 20; i++) {
        auto bins = engine.getLogBins();
        std::cout << "Frame " << i << " bin[10]=" << bins[10] << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    ws.stop();
    engine.stop();
    return 0;
}


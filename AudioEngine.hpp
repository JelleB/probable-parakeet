#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if __has_include(<FLAC/stream_encoder.h>)
#include <FLAC/stream_encoder.h>
#define AUDIOENGINE_HAS_FLAC 1
#else
// Allow building without libFLAC headers installed.
#define AUDIOENGINE_HAS_FLAC 0
struct FLAC__StreamEncoder;
#endif

class AudioEngine {
public:
    AudioEngine(
        int sampleRate,
        int fftSize,
        int logBins,
        const std::string& flacOutputPath = ""
    );

    ~AudioEngine();

    void start();
    void stop();

    std::vector<float> getLogBins();             // 64/128 bins, call every 200 ms
    std::vector<float> getLogBinCenters() const; // center frequency per bin

private:
    void audioThreadFunc();
    void initFlac();
    void closeFlac();

    std::vector<std::pair<float,float>> computeLogBinFreqs() const;

private:
    int sampleRate;
    int fftSize;
    int logBins;

    std::thread audioThread;
    std::atomic<bool> running{false};

    std::vector<float> latestLog;
    std::mutex logMutex;

    std::vector<float> captureBuffer;
    std::atomic<std::size_t> captureWriteIdx{0};
    std::mutex captureMutex;

    std::string flacPath;
    bool flacEnabled{false};
    FLAC__StreamEncoder* flacEncoder{nullptr};
};


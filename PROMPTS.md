# Prompts / requirements log

This file records the prompts and requirements that led to the current version of this repository.

## Initial request

- “AudioEngine.hpp / AudioEngine.cpp / LogBins.hpp / main.cpp (example usage)”

## AudioEngine interface (provided header)

- Provide an `AudioEngine` with:
  - ctor: `(int sampleRate, int fftSize, int logBins, const std::string& flacOutputPath = "")`
  - `start()`, `stop()`
  - `getLogBins()` (call every ~200ms)
  - `getLogBinCenters() const`
  - FLAC output support via `FLAC/stream_encoder.h`

## AudioEngine.cpp implementation (provided body)

- Implement `AudioEngine.cpp` using:
  - PortAudio capture callback into a ring buffer
  - kissFFT (`kiss_fftr`) for FFT
  - Hann window
  - `LogBins::compute(mag, sampleRate, fftSize, logBins)`
  - optional FLAC writing in the PortAudio callback

## Log binning implementation (provided LogBins.hpp)

- Implement `LogBins.hpp` with:
  - `static std::vector<float> compute(const std::vector<float>& fftMag, int sampleRate, int fftSize, int numBins)`
  - log-spaced bands from 20Hz..Nyquist
  - each output bin is the average of FFT magnitudes in the corresponding frequency range

## main.cpp example usage (provided)

- Example `main.cpp`:
  - construct `AudioEngine(44100, 1024, 64, "test.flac")` (or `""` to disable FLAC)
  - print bin center frequencies
  - poll `getLogBins()` in a loop every 200ms

## “Make sure I did not forget a file; analyze; check it in”

- Confirm all expected files exist.
- Analyze supplied code.
- Commit changes.

## Unit tests

- Suggest a C++ unit test framework, implement it, and create unit tests for the most important functions:
  - `LogBins::compute()`
  - `AudioEngine::getLogBinCenters()`

## WebSocket + Node visualization

- “To the demo, add a websocket to serve up the result to a nodejs graph”
- Add C++ WebSocket broadcasting JSON:
  - `{ "centers": [...], "bins": [...] }`
- Provide a Node.js viewer that connects and plots.

## “For my daughter… add a lot of love”

- Add a small dedication message for the author’s daughter.

## Hearts + personalization

- “My daughter Meike is 7 now, so add a lot of hearts … to the nodejs view”
- Add lots of hearts to the Node viewer output and mention Meike (7).

## Browser waterfall widget (Chart.js v3)

- Find/add a widget that can print/plot a **waterfall** graph, preferably based on Chart.js v3:
  - take JSON with 64 bin values
  - convert each bin to a colored dot (color based on value)
  - draw dots on a line; each new sample is a line below
- Implement a browser viewer using Chart.js v3 + a matrix/heatmap-style rendering.

## Repository operations requested

- Create a new branch `develop`
- Merge this feature branch into `develop`
- Merge into `main`


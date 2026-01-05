# probable-parakeet

## Notice (AI-generated)

All code in this repository has been generated with AI assistance.

For traceability, the prompts/requirements that led to this version are recorded in `PROMPTS.md`.

For my daughter Meike (7): you are deeply loved. ❤️❤️❤️❤️❤️

## Build + run (C++)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j 2
./build/example
```

The demo prints log-bin centers and also broadcasts JSON over WebSocket on **port 8787**:

- **URL**: `ws://127.0.0.1:8787`
- **Payload**: `{"centers":[...],"bins":[...]}`

## View the bins (Node.js terminal graph)

```bash
cd node
npm install
node viewer.js
```

## Waterfall view (browser, Chart.js v3)

1) Start the C++ demo so it serves WebSocket on `ws://127.0.0.1:8787`.

2) In another terminal:

```bash
cd node
npm install
npm run waterfall
```

Open the printed URL (defaults to `http://127.0.0.1:8080/waterfall/`).

If needed:

```bash
WS_URL=ws://127.0.0.1:8787 node viewer.js
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```
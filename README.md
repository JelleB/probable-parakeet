# probable-parakeet

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

If needed:

```bash
WS_URL=ws://127.0.0.1:8787 node viewer.js
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```
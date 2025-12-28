const qs = new URLSearchParams(location.search);
const wsUrl = qs.get("ws") ?? "ws://127.0.0.1:8787";
const maxRows = Number(qs.get("rows") ?? "200");

const wsUrlEl = document.getElementById("wsUrl");
const statusEl = document.getElementById("status");
const rowsEl = document.getElementById("rows");
const binsEl = document.getElementById("bins");

wsUrlEl.textContent = wsUrl;
rowsEl.textContent = String(Number.isFinite(maxRows) ? maxRows : 200);

// History as rows of 64 values (oldest at top, newest appended).
let history = [];
let nBins = 64;

// Auto scale for color mapping (simple peak-hold with decay)
let colorMax = 1e-6;

function clamp01(x) {
  if (x < 0) return 0;
  if (x > 1) return 1;
  return x;
}

function hsvToRgb(h, s, v) {
  const c = v * s;
  const hp = (h / 60);
  const x = c * (1 - Math.abs((hp % 2) - 1));
  let r = 0, g = 0, b = 0;
  if (0 <= hp && hp < 1) [r, g, b] = [c, x, 0];
  else if (1 <= hp && hp < 2) [r, g, b] = [x, c, 0];
  else if (2 <= hp && hp < 3) [r, g, b] = [0, c, x];
  else if (3 <= hp && hp < 4) [r, g, b] = [0, x, c];
  else if (4 <= hp && hp < 5) [r, g, b] = [x, 0, c];
  else if (5 <= hp && hp < 6) [r, g, b] = [c, 0, x];
  const m = v - c;
  r = Math.round((r + m) * 255);
  g = Math.round((g + m) * 255);
  b = Math.round((b + m) * 255);
  return `rgb(${r},${g},${b})`;
}

function valueToColor(v) {
  // Normalize using a soft log curve so low values still show.
  // t in [0..1]
  const t = clamp01(Math.log10(1 + 9 * (v / colorMax)) / Math.log10(10));
  // Hue: 240 (blue) -> 0 (red)
  const hue = (1 - t) * 240;
  return hsvToRgb(hue, 1.0, 0.95);
}

function rebuildMatrixData() {
  const data = [];
  for (let y = 0; y < history.length; y++) {
    const row = history[y];
    for (let x = 0; x < nBins; x++) {
      data.push({ x, y, v: row[x] ?? 0 });
    }
  }
  return data;
}

const ctx = document.getElementById("chart").getContext("2d");
const chart = new Chart(ctx, {
  type: "matrix",
  data: {
    datasets: [
      {
        label: "Waterfall",
        data: [],
        backgroundColor: (c) => {
          const v = c?.raw?.v ?? 0;
          return valueToColor(v);
        },
        borderWidth: 0,
        width: (c) => {
          const a = c.chart.chartArea;
          return a ? a.width / nBins : 10;
        },
        height: (c) => {
          const a = c.chart.chartArea;
          const rows = Math.max(1, maxRows);
          return a ? a.height / rows : 4;
        },
      },
    ],
  },
  options: {
    responsive: true,
    maintainAspectRatio: false,
    animation: false,
    plugins: {
      legend: { display: false },
      tooltip: { enabled: false },
    },
    scales: {
      x: {
        type: "linear",
        min: -0.5,
        max: nBins - 0.5,
        grid: { display: false },
        ticks: { display: false },
      },
      y: {
        type: "linear",
        min: -0.5,
        max: maxRows - 0.5,
        grid: { display: false },
        ticks: { display: false },
      },
    },
  },
});

function setStatus(s) {
  statusEl.textContent = s;
}

function onBins(bins) {
  if (!Array.isArray(bins)) return;
  if (bins.length !== nBins) {
    nBins = bins.length;
    binsEl.textContent = String(nBins);
    chart.options.scales.x.max = nBins - 0.5;
  }

  // Update color scale target.
  const rowMax = bins.reduce((m, x) => Math.max(m, Number(x) || 0), 0);
  colorMax = Math.max(colorMax * 0.98, rowMax, 1e-6);

  history.push(bins.map((x) => Number(x) || 0));
  const limit = Number.isFinite(maxRows) ? Math.max(1, maxRows) : 200;
  while (history.length > limit) history.shift();

  chart.data.datasets[0].data = rebuildMatrixData();
  chart.update("none");
}

function connect() {
  setStatus("connecting…");
  const ws = new WebSocket(wsUrl);

  ws.onopen = () => setStatus("connected");
  ws.onclose = () => {
    setStatus("disconnected (reconnecting…)"); 
    setTimeout(connect, 500);
  };
  ws.onerror = () => {
    setStatus("error (reconnecting…)"); 
    try { ws.close(); } catch {}
  };
  ws.onmessage = (ev) => {
    let msg;
    try {
      msg = JSON.parse(ev.data);
    } catch {
      return;
    }
    onBins(msg.bins);
  };
}

connect();


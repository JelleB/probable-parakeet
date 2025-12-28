import WebSocket from "ws";
import asciichart from "asciichart";

const url = process.env.WS_URL ?? "ws://127.0.0.1:8787";
const daughter = process.env.DAUGHTER_NAME ?? "Meike";
const age = Number(process.env.DAUGHTER_AGE ?? "7");
const hearts = "❤️".repeat(40);

let centers = null;

const ws = new WebSocket(url);

ws.on("open", () => {
  process.stdout.write(`Connected: ${url}\n`);
});

ws.on("message", (buf) => {
  let msg;
  try {
    msg = JSON.parse(buf.toString("utf8"));
  } catch {
    return;
  }

  if (!centers && Array.isArray(msg.centers)) centers = msg.centers;
  if (!Array.isArray(msg.bins)) return;

  // Render bins as an ASCII chart.
  // (Values are raw log-bin averages from the C++ side.)
  process.stdout.write("\x1b[2J\x1b[H"); // clear screen, home
  process.stdout.write(`${hearts}\n`);
  process.stdout.write(`For ${daughter} (${Number.isFinite(age) ? age : "?"}) — with lots of love\n`);
  process.stdout.write(`${hearts}\n\n`);
  process.stdout.write(`Audio log bins (${msg.bins.length})\n`);

  // Downsample slightly for terminal width if needed.
  const bins = msg.bins;
  const maxPoints = 64;
  const step = Math.max(1, Math.floor(bins.length / maxPoints));
  const series = [];
  for (let i = 0; i < bins.length; i += step) series.push(bins[i]);

  process.stdout.write(asciichart.plot(series, { height: 15 }) + "\n");

  if (centers) {
    const lo = centers[0]?.toFixed?.(1) ?? "?";
    const hi = centers[centers.length - 1]?.toFixed?.(1) ?? "?";
    process.stdout.write(`Centers: ${lo} Hz .. ${hi} Hz\n`);
  }

  process.stdout.write(`\n${"💖".repeat(24)}\n`);
});

ws.on("close", () => {
  process.stdout.write("Disconnected\n");
  process.exit(0);
});

ws.on("error", (e) => {
  process.stderr.write(String(e) + "\n");
  process.exit(1);
});


import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const port = Number(process.env.PORT ?? "8080");
const root = __dirname; // serve from /workspace/node

const mime = {
  ".html": "text/html; charset=utf-8",
  ".js": "application/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".png": "image/png",
  ".svg": "image/svg+xml",
};

function send(res, code, body, type = "text/plain; charset=utf-8") {
  res.writeHead(code, {
    "Content-Type": type,
    "Cache-Control": "no-store",
  });
  res.end(body);
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url ?? "/", "http://localhost");
  let rel = decodeURIComponent(url.pathname);

  // Convenience: redirect / -> /waterfall/
  if (rel === "/") {
    res.writeHead(302, { Location: "/waterfall/" });
    res.end();
    return;
  }

  if (rel.endsWith("/")) rel += "index.html";
  const filePath = path.join(root, rel);

  // Prevent path traversal.
  if (!filePath.startsWith(root)) {
    send(res, 403, "Forbidden");
    return;
  }

  fs.readFile(filePath, (err, data) => {
    if (err) {
      send(res, 404, "Not found");
      return;
    }
    const ext = path.extname(filePath).toLowerCase();
    send(res, 200, data, mime[ext] ?? "application/octet-stream");
  });
});

server.listen(port, "127.0.0.1", () => {
  process.stdout.write(`Viewer server: http://127.0.0.1:${port}/waterfall/?ws=${encodeURIComponent("ws://127.0.0.1:8787")}\n`);
});


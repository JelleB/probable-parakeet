#include "WebSocketServer.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string trim(const std::string& s) {
  std::size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
  std::size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
  return s.substr(a, b - a);
}

// Very small HTTP header parser: returns value for a key (case-sensitive).
std::string headerValue(const std::string& headers, const std::string& key) {
  std::istringstream iss(headers);
  std::string line;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto pos = line.find(':');
    if (pos == std::string::npos) continue;
    const std::string k = line.substr(0, pos);
    if (k == key) return trim(line.substr(pos + 1));
  }
  return {};
}

} // namespace

WebSocketServer::WebSocketServer(int port) : m_port(port) {}

WebSocketServer::~WebSocketServer() { stop(); }

void WebSocketServer::start(PayloadProvider provider, int intervalMs) {
  if (m_running.load()) return;
  m_provider = std::move(provider);
  m_intervalMs = std::max(10, intervalMs);
  m_stopRequested = false;
  m_running = true;

  m_acceptThread = std::thread(&WebSocketServer::acceptLoop_, this);
  m_broadcastThread = std::thread(&WebSocketServer::broadcastLoop_, this);
}

void WebSocketServer::stop() {
  if (!m_running.exchange(false)) return;
  m_stopRequested = true;

  // Closing the listen FD breaks accept().
  closeFd_(m_listenFd);

  if (m_acceptThread.joinable()) m_acceptThread.join();
  if (m_broadcastThread.joinable()) m_broadcastThread.join();

  std::lock_guard<std::mutex> lk(m_clientsMutex);
  for (int& fd : m_clients) closeFd_(fd);
  m_clients.clear();
}

void WebSocketServer::acceptLoop_() {
  m_listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (m_listenFd < 0) return;

  int yes = 1;
  (void)::setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(m_port));

  if (::bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closeFd_(m_listenFd);
    return;
  }
  if (::listen(m_listenFd, 16) != 0) {
    closeFd_(m_listenFd);
    return;
  }

  while (m_running.load() && !m_stopRequested.load()) {
    int client = ::accept(m_listenFd, nullptr, nullptr);
    if (client < 0) {
      if (m_stopRequested.load()) break;
      continue;
    }

    // Read HTTP request (best-effort, capped).
    std::string req;
    req.resize(8192);
    const ssize_t n = ::recv(client, req.data(), req.size(), 0);
    if (n <= 0) {
      closeFd_(client);
      continue;
    }
    req.resize(static_cast<std::size_t>(n));

    const auto key = headerValue(req, "Sec-WebSocket-Key");
    if (key.empty()) {
      closeFd_(client);
      continue;
    }

    const std::string acceptKey = makeAcceptKey_(key);
    const std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
        "\r\n";

    if (!sendAll_(client, resp.data(), resp.size())) {
      closeFd_(client);
      continue;
    }

    {
      std::lock_guard<std::mutex> lk(m_clientsMutex);
      m_clients.push_back(client);
    }
  }
}

void WebSocketServer::broadcastLoop_() {
  while (m_running.load() && !m_stopRequested.load()) {
    const auto payload = m_provider ? m_provider() : std::string{};

    std::vector<int> clientsCopy;
    {
      std::lock_guard<std::mutex> lk(m_clientsMutex);
      clientsCopy = m_clients;
    }

    std::vector<int> dead;
    for (int fd : clientsCopy) {
      if (!sendTextFrame_(fd, payload)) dead.push_back(fd);
    }

    if (!dead.empty()) {
      std::lock_guard<std::mutex> lk(m_clientsMutex);
      for (int d : dead) {
        auto it = std::find(m_clients.begin(), m_clients.end(), d);
        if (it != m_clients.end()) {
          int fd = *it;
          closeFd_(fd);
          m_clients.erase(it);
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMs));
  }
}

bool WebSocketServer::sendAll_(int fd, const void* data, std::size_t len) {
  const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
  std::size_t sent = 0;
  while (sent < len) {
    const ssize_t n = ::send(fd, p + sent, len - sent, 0);
    if (n <= 0) return false;
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

bool WebSocketServer::sendTextFrame_(int fd, const std::string& text) {
  // Server-to-client frames are not masked.
  // FIN=1, opcode=1
  std::vector<std::uint8_t> frame;
  frame.reserve(2 + 8 + text.size());
  frame.push_back(0x81);

  const std::size_t n = text.size();
  if (n <= 125) {
    frame.push_back(static_cast<std::uint8_t>(n));
  } else if (n <= 0xFFFF) {
    frame.push_back(126);
    frame.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFF));
    frame.push_back(static_cast<std::uint8_t>(n & 0xFF));
  } else {
    frame.push_back(127);
    for (int i = 7; i >= 0; --i) frame.push_back(static_cast<std::uint8_t>((n >> (i * 8)) & 0xFF));
  }

  frame.insert(frame.end(), text.begin(), text.end());
  return sendAll_(fd, frame.data(), frame.size());
}

void WebSocketServer::closeFd_(int& fd) {
  if (fd >= 0) {
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
    fd = -1;
  }
}

std::string WebSocketServer::makeAcceptKey_(const std::string& secWebSocketKey) {
  const std::string guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  const auto digest = sha1_(secWebSocketKey + guid);
  return base64Encode_(digest);
}

std::string WebSocketServer::base64Encode_(const std::vector<std::uint8_t>& data) {
  static constexpr char kTbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);

  std::size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | uint32_t(data[i + 2]);
    out.push_back(kTbl[(v >> 18) & 0x3F]);
    out.push_back(kTbl[(v >> 12) & 0x3F]);
    out.push_back(kTbl[(v >> 6) & 0x3F]);
    out.push_back(kTbl[v & 0x3F]);
  }
  if (i < data.size()) {
    uint32_t v = uint32_t(data[i]) << 16;
    out.push_back(kTbl[(v >> 18) & 0x3F]);
    if (i + 1 < data.size()) {
      v |= uint32_t(data[i + 1]) << 8;
      out.push_back(kTbl[(v >> 12) & 0x3F]);
      out.push_back(kTbl[(v >> 6) & 0x3F]);
      out.push_back('=');
    } else {
      out.push_back(kTbl[(v >> 12) & 0x3F]);
      out.push_back('=');
      out.push_back('=');
    }
  }
  return out;
}

std::vector<std::uint8_t> WebSocketServer::sha1_(const std::string& s) {
  // Minimal SHA-1 implementation (for WebSocket handshake only).
  auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };

  std::vector<std::uint8_t> msg(s.begin(), s.end());
  const uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8ULL;
  msg.push_back(0x80);
  while ((msg.size() % 64) != 56) msg.push_back(0x00);
  for (int i = 7; i >= 0; --i) msg.push_back(static_cast<std::uint8_t>((bitLen >> (i * 8)) & 0xFF));

  uint32_t h0 = 0x67452301u;
  uint32_t h1 = 0xEFCDAB89u;
  uint32_t h2 = 0x98BADCFEu;
  uint32_t h3 = 0x10325476u;
  uint32_t h4 = 0xC3D2E1F0u;

  for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      const std::size_t o = chunk + static_cast<std::size_t>(i) * 4;
      w[i] = (uint32_t(msg[o]) << 24) | (uint32_t(msg[o + 1]) << 16) |
             (uint32_t(msg[o + 2]) << 8) | uint32_t(msg[o + 3]);
    }
    for (int i = 16; i < 80; ++i) {
      w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (int i = 0; i < 80; ++i) {
      uint32_t f = 0, k = 0;
      if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6u; }

      const uint32_t temp = rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = temp;
    }

    h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
  }

  std::vector<std::uint8_t> out(20);
  auto put = [&](int idx, uint32_t v) {
    out[static_cast<std::size_t>(idx) + 0] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    out[static_cast<std::size_t>(idx) + 1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    out[static_cast<std::size_t>(idx) + 2] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    out[static_cast<std::size_t>(idx) + 3] = static_cast<std::uint8_t>(v & 0xFF);
  };
  put(0, h0); put(4, h1); put(8, h2); put(12, h3); put(16, h4);
  return out;
}


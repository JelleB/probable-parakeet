#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Minimal WebSocket (RFC6455) server for local demos.
// - Supports a single text broadcast to all connected clients
// - Implements HTTP Upgrade + Sec-WebSocket-Accept
// - Ignores incoming frames (clients may send pings; we don't parse them)
//
// Intended for visualization/telemetry, not production.
class WebSocketServer final {
public:
  using PayloadProvider = std::function<std::string()>;

  explicit WebSocketServer(int port);
  ~WebSocketServer();

  WebSocketServer(const WebSocketServer&) = delete;
  WebSocketServer& operator=(const WebSocketServer&) = delete;

  void start(PayloadProvider provider, int intervalMs = 100);
  void stop();

  bool isRunning() const noexcept { return m_running.load(); }

private:
  void acceptLoop_();
  void broadcastLoop_();

  static std::string makeAcceptKey_(const std::string& secWebSocketKey);
  static std::string base64Encode_(const std::vector<std::uint8_t>& data);
  static std::vector<std::uint8_t> sha1_(const std::string& s);

  static bool sendAll_(int fd, const void* data, std::size_t len);
  static bool sendTextFrame_(int fd, const std::string& text);
  static void closeFd_(int& fd);

  const int m_port;
  std::atomic<bool> m_running{false};
  std::atomic<bool> m_stopRequested{false};

  std::thread m_acceptThread;
  std::thread m_broadcastThread;

  PayloadProvider m_provider;
  int m_intervalMs = 100;

  int m_listenFd = -1;

  std::mutex m_clientsMutex;
  std::vector<int> m_clients;
};


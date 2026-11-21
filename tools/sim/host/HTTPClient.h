// HTTP for the host build. Every request goes to the mock internet the test
// harness runs (sim::board.httpMock) as GET /<host><path>, with the scheme in
// X-Sim-Scheme. With no mock, or the internet down, a request fails like a
// dead line: after the connect timeout.
#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#define HTTP_CODE_OK 200
#define HTTP_CODE_NOT_FOUND 404
#define HTTPC_ERROR_CONNECTION_REFUSED (-1)
#define HTTPC_ERROR_READ_TIMEOUT (-11)
class HTTPClient {
 public:
  bool begin(WiFiClient &client, const String &url);
  bool begin(const String &url) { WiFiClient c; return begin(c, url); }
  void end() { body.clear(); }
  void setTimeout(uint32_t ms) { timeoutMs = ms; }
  void setConnectTimeout(int32_t ms) { connectTimeoutMs = ms; }
  void setUserAgent(const String &ua) { userAgent = ua.v; }
  void setReuse(bool) {}
  void addHeader(const String &name, const String &value) { if (name == "User-Agent") userAgent = value.v; else headers += name.v + ": " + value.v + "\r\n"; }
  int GET();
  int getSize() { return (int)body.size(); }
  String getString() { return String(cutBody ? body.substr(0, body.size() / 2) : body); }   // a dropped line: half a body
  int writeToStream(Stream *stream);
  static String errorToString(int) { return String("connection failed"); }
 private:
  std::string url, userAgent = "ESP32HTTPClient", headers, body;
  uint32_t timeoutMs = 5000;
  int32_t connectTimeoutMs = 5000;
  bool cutBody = false;
};

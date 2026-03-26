#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebSocketsClient.h>

// ── WiFi Credentials ──────────────────────────────────────────────
const char* ssid     = "Unknown";
const char* password = "123456789"; // <-- Paste your password here

// ── Server Config ─────────────────────────────────────────────────
const char* websocket_host = "newaudiocluod.onrender.com";
const uint16_t websocket_port = 443;
const char* websocket_path = "/";

// ── State ─────────────────────────────────────────────────────────
WebSocketsClient webSocket;

// ── WebSocket Event Handler ───────────────────────────────────────
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    
    case WStype_CONNECTED:
      // We don't want to print too much here, otherwise it corrupts the audio stream!
      // But a simple connection log is fine before the audio starts.
      Serial.printf("\n✅ RECEIVER Connected to server: wss://%s:%d%s\n", 
                    websocket_host, websocket_port, websocket_path);
      break;

    case WStype_DISCONNECTED:
      // Re-initialize a blank line if we disconnect so the serial monitor stays clean
      Serial.println("\n❌ RECEIVER Disconnected from server");
      break;

    case WStype_BIN:
      // 🚨 CRITICAL PATH 🚨
      // This is the raw audio coming from your Sender ESP32 via Render.
      // We instantly dump the raw bytes over USB so play_audio.py can read them.
      Serial.write(payload, length);
      break;

    case WStype_TEXT:
    case WStype_ERROR:
    case WStype_PING:
    case WStype_PONG:
      // Ignore these to keep the serial line clean for audio data
      break;
      
    default:
      break;
  }
}

// ── Setup ─────────────────────────────────────────────────────────
void setup() {
  // MUST be 921600 to match your play_audio.py script!
  Serial.begin(921600);
  delay(500);

  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  // Disable WiFi power save to reduce audio latency
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Configure Secure WebSocket with Render Proxy Bypass
  webSocket.beginSSL(websocket_host, websocket_port, websocket_path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  
  // The Origin header tricks Render into thinking this is a web browser
  webSocket.setExtraHeaders("Origin: https://newaudiocluod.onrender.com\r\nUser-Agent: ESP32-Receiver");
  
  webSocket.enableHeartbeat(15000, 3000, 2);
}

// ── Loop ──────────────────────────────────────────────────────────
void loop() {
  webSocket.loop();
}
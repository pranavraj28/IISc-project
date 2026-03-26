#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>

// ── WiFi Credentials ──────────────────────────────────────────────
const char* ssid     = "TP-Link_0F81";
const char* password = "84568328";

// ── Server Config ─────────────────────────────────────────────────
const char* websocket_host = "newaudiocluod.onrender.com";
const uint16_t websocket_port = 443;
const char* websocket_path = "/";

// ── I2S Mic Pins ──────────────────────────────────────────────────
#define I2S_WS   15
#define I2S_SCK  14
#define I2S_SD   32
#define I2S_PORT I2S_NUM_0

// ── Audio Config ──────────────────────────────────────────────────
#define SAMPLES_PER_PACKET 320        // 20ms of audio @ 16kHz
#define BYTES_PER_PACKET   (SAMPLES_PER_PACKET * 2)  // int16 = 2 bytes

// ── State ─────────────────────────────────────────────────────────
WebSocketsClient webSocket;
SemaphoreHandle_t wsMutex;

volatile bool isConnected  = false;
volatile bool micStarted   = false;
volatile bool i2sInstalled = false;
volatile bool startMicFlag = false;

int16_t audioBuffer[SAMPLES_PER_PACKET];

// ── I2S Setup / Teardown ──────────────────────────────────────────
void teardownI2S() {
  if (i2sInstalled) {
    i2s_driver_uninstall(I2S_PORT);
    i2sInstalled = false;
    Serial.println("🔇 I2S uninstalled");
  }
}

bool setupI2S() {
  teardownI2S();
  i2s_config_t i2s_config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = 16000,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 4,
    .dma_buf_len          = 64
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num   = I2S_SCK,
    .ws_io_num    = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_SD
  };
  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("❌ I2S install failed: %d\n", err);
    return false;
  }

  i2s_set_pin(I2S_PORT, &pin_config);
  i2sInstalled = true;
  Serial.println("🎤 Mic Started");
  return true;
}

// ── WebSocket Event Handler ───────────────────────────────────────
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("✅ Connected to server: wss://%s:%d%s\n",
                    websocket_host, websocket_port, websocket_path);
      isConnected  = true;
      startMicFlag = true;   
      break;
    case WStype_DISCONNECTED:
      Serial.println("❌ Disconnected from server");
      isConnected = false;
      break;
    case WStype_BIN:
      Serial.printf("📦 Received audio: %d bytes\n", length);
      break;
    case WStype_TEXT:
      Serial.printf("📨 Server message: %s\n", payload);
      break;
    case WStype_ERROR:
      Serial.printf("⚠️  WebSocket error: %s\n", payload ? (char*)payload : "unknown");
      break;
    case WStype_PING:
    case WStype_PONG:
      break;
    default:
      break;
  }
}

// ── Audio Task (runs on Core 0) ───────────────────────────────────
void audioTask(void* param) {
  // Enhanced DSP State Variables
  float last_raw = 0, last_hp = 0, last_lp = 0;
  
  // 🎛️ TUNING KNOB: Increase this if you still hear static, decrease if it cuts off your voice
  const float NOISE_GATE_THRESHOLD = 150.0f; 
  
  int   bufferIndex  = 0;
  unsigned long lastSendTime = 0;
  
  while (true) {
    if (startMicFlag && !micStarted) {
      startMicFlag = false;
      if (setupI2S()) {
        micStarted = true;
      }
    }

    if (!isConnected && micStarted) {
      teardownI2S();
      micStarted  = false;
      bufferIndex = 0;
      last_raw    = 0;
      last_hp     = 0;
      last_lp     = 0;
    }

    if (!isConnected || !micStarted) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    int32_t rawSamples[64];
    size_t  bytesRead = 0;
    esp_err_t result = i2s_read(I2S_PORT, rawSamples, sizeof(rawSamples),
                                &bytesRead, pdMS_TO_TICKS(20));
                                
    if (result != ESP_OK || bytesRead == 0) continue;

    int samplesRead = bytesRead / 4;

    for (int i = 0; i < samplesRead; i++) {
      // 1. Scale down raw 32-bit sample
      float raw = (float)(rawSamples[i] >> 16);
      
      // 2. High-Pass Filter (Removes DC offset & low rumble)
      float hp = 0.95f * (last_hp + raw - last_raw);
      last_raw = raw;
      last_hp  = hp;

      // 3. Low-Pass Filter (Removes high-frequency WiFi hiss/static)
      float lp = last_lp + 0.3f * (hp - last_lp);
      last_lp = lp;

      // 4. Hardware Noise Gate (Silences quiet background noise)
      int16_t finalSample = 0;
      if (abs(lp) > NOISE_GATE_THRESHOLD) {
         finalSample = (int16_t)lp;
      }

      audioBuffer[bufferIndex++] = finalSample;
      
      if (bufferIndex >= SAMPLES_PER_PACKET) {
        unsigned long now = millis();
        if (now - lastSendTime >= 20) {
          if (xSemaphoreTake(wsMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            webSocket.sendBIN((uint8_t*)audioBuffer, BYTES_PER_PACKET);
            xSemaphoreGive(wsMutex);
            lastSendTime = now;
          }
        }
        bufferIndex = 0;
      }
    }
  }
}

// ── Setup ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  wsMutex = xSemaphoreCreateMutex();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n✅ WiFi Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Configure WebSocket with the Origin Header Fix
  webSocket.beginSSL(websocket_host, websocket_port, websocket_path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  webSocket.setExtraHeaders("Origin: https://newaudiocluod.onrender.com\r\nUser-Agent: ESP32");
  webSocket.enableHeartbeat(15000, 3000, 2);

  Serial.println("🔌 Connecting to WebSocket server...");

  xTaskCreatePinnedToCore(audioTask, "AudioTask", 8192, NULL, 1, NULL, 0);
}

// ── Loop (Core 1 — WebSocket only) ───────────────────────────────
void loop() {
  if (xSemaphoreTake(wsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    webSocket.loop();
    xSemaphoreGive(wsMutex);
  }
  delay(1);
}
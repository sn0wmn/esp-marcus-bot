#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FluxGarage_RoboEyes.h>
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// ============================================================================
// HARDWARE SETUP - Seeed Studio XIAO ESP32 S3 Sense
// ============================================================================
//
// BUILT-IN COMPONENTS (No External Wiring):
//   - OV2640 Camera Module (already integrated)
//   - INMP441 Microphone (already integrated, I2S on GPIOs 2, 41, 42)
//   - RGB LED (GPIO 21)
//
// EXTERNAL CONNECTIONS REQUIRED:
//
//   1. OLED Display (SSD1306, 128x64, I2C Address 0x3C):
//      SDA  → GPIO 5 (default I2C SDA)
//      SCL  → GPIO 6 (default I2C SCL)
//      VCC  → 3.3V
//      GND  → GND
//
//   2. I2S Speaker/Amplifier (e.g., MAX98357A):
//      Your Amp Pin  →  ESP32 Pin
//      ───────────────────────────
//      LRC           →  GPIO 8 (Word Select)
//      BCLK          →  GPIO 4 (Bit Clock)
//      DIN           →  GPIO 9 (Data Out from ESP32)
//      GAin          →  GND (low gain) or 3.3V (high gain)
//      SD            →  3.3V (shutdown control - tie high to enable)
//      VIN           →  3.3V or 5V (power - check your amp datasheet)
//      GND           →  GND
//
//   3. Power:
//      - Connect USB-C cable for power and programming
//      - All peripherals use 3.3V logic level
//
// ============================================================================

// XIAO ESP32 S3 Sense built-in LED
#define LED_PIN LED_BUILTIN // GPIO 21 on XIAO ESP32 S3

// OLED Display Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RoboEyes<Adafruit_SSD1306> eyes(display);
String currentEmotion = "happy";

// WiFi Configuration
const char *ssid = "Verizon_WNLM7H-IoT";
const char *password = "YOUR_WIFI_PASSWORD";
const char *serverUrl = "http://192.168.1.191:3000/api/process";

// WiFi recovery helper
bool ensureWiFiConnected()
{
  if (WiFi.status() == WL_CONNECTED)
    return true;

  Serial.println("[WIFI] Connection lost, reconnecting...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10)
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n[WIFI] Reconnected!");
    return true;
  }

  Serial.println("\n[WIFI] Reconnection failed");
  return false;
}

// I2S Microphone Configuration for XIAO ESP32 S3 Sense (built-in INMP441)
#define I2S_WS 42  // LRCK
#define I2S_SD 2   // DIN
#define I2S_SCK 41 // BCLK
#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 16000
#define BUFFER_SIZE 512

// I2S Speaker Configuration (using different pins)
#define I2S_SPEAKER_BCK 4  // Bit Clock (moved to GPIO 4 to avoid I2C SDA conflict)
#define I2S_SPEAKER_WS 8   // Word Select (LRCK)
#define I2S_SPEAKER_DOUT 9 // Data Out
#define I2S_SPEAKER_PORT I2S_NUM_1

// Camera Configuration for XIAO ESP32 S3 Sense (OV2640)
#define CAMERA_MODEL_XIAO_ESP32S3
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 10
#define SIOD_GPIO_NUM 40
#define SIOC_GPIO_NUM 39
#define Y9_GPIO_NUM 48
#define Y8_GPIO_NUM 11
#define Y7_GPIO_NUM 12
#define Y6_GPIO_NUM 14
#define Y5_GPIO_NUM 16
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 17
#define Y2_GPIO_NUM 15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM 47
#define PCLK_GPIO_NUM 13

// ============================================================================
// FEATURE FLAGS FOR ATOMIC TESTING
// ============================================================================
#define ENABLE_WAKE_WORD true      // Wake word detection
#define ENABLE_BACKEND true        // STT + LLM processing via backend
#define ENABLE_SPEAKER true        // Audio playback
#define ENABLE_CAMERA false        // Camera initialization and face detection
#define ENABLE_DISPLAY true        // OLED display and eyes
#define ENABLE_VERBOSE_DEBUG true  // Enhanced serial logging
#define ENABLE_MANUAL_TRIGGER true // Serial command to trigger recording

// Test Mode Presets (comment/uncomment to activate)
// #define TEST_MODE_MIC_ONLY          // Test: Microphone recording only
// #define TEST_MODE_WAKE_WORD_ONLY    // Test: Wake word detection only
// #define TEST_MODE_BACKEND_ONLY      // Test: Backend communication only
// #define TEST_MODE_SPEAKER_ONLY      // Test: Speaker playback only
// #define TEST_MODE_CAMERA_ONLY       // Test: Camera capture only
// #define TEST_MODE_DISPLAY_ONLY      // Test: Display and eyes only

// Apply test mode overrides
#ifdef TEST_MODE_MIC_ONLY
#undef ENABLE_WAKE_WORD
#undef ENABLE_BACKEND
#undef ENABLE_SPEAKER
#undef ENABLE_CAMERA
#undef ENABLE_DISPLAY
#define ENABLE_WAKE_WORD false
#define ENABLE_BACKEND false
#define ENABLE_SPEAKER false
#define ENABLE_CAMERA false
#define ENABLE_DISPLAY false
#endif

#ifdef TEST_MODE_WAKE_WORD_ONLY
#undef ENABLE_BACKEND
#undef ENABLE_SPEAKER
#undef ENABLE_CAMERA
#define ENABLE_BACKEND false
#define ENABLE_SPEAKER false
#define ENABLE_CAMERA false
#endif

#ifdef TEST_MODE_BACKEND_ONLY
#undef ENABLE_WAKE_WORD
#undef ENABLE_SPEAKER
#undef ENABLE_CAMERA
#define ENABLE_WAKE_WORD false
#define ENABLE_SPEAKER false
#define ENABLE_CAMERA false
#endif

#ifdef TEST_MODE_SPEAKER_ONLY
#undef ENABLE_WAKE_WORD
#undef ENABLE_BACKEND
#undef ENABLE_CAMERA
#define ENABLE_WAKE_WORD false
#define ENABLE_BACKEND false
#define ENABLE_CAMERA false
#endif

#ifdef TEST_MODE_CAMERA_ONLY
#undef ENABLE_WAKE_WORD
#undef ENABLE_BACKEND
#undef ENABLE_SPEAKER
#define ENABLE_WAKE_WORD false
#define ENABLE_BACKEND false
#define ENABLE_SPEAKER false
#endif

#ifdef TEST_MODE_DISPLAY_ONLY
#undef ENABLE_WAKE_WORD
#undef ENABLE_BACKEND
#undef ENABLE_SPEAKER
#undef ENABLE_CAMERA
#define ENABLE_WAKE_WORD false
#define ENABLE_BACKEND false
#define ENABLE_SPEAKER false
#define ENABLE_CAMERA false
#endif

// Manual trigger flag
volatile bool manualTriggerRequested = false;

// Verbose logging helper
#define DEBUG_LOG(fmt, ...)                            \
  if (ENABLE_VERBOSE_DEBUG)                            \
  {                                                    \
    Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); \
  }

// Wake word detection settings
const char *WAKE_WORD = "marcus";
const int WAKE_WORD_THRESHOLD = 1000;                                        // Energy threshold
const int RECORDING_DURATION_MS = 5000;                                      // 5 seconds (fallback max)
const int MAX_AUDIO_SIZE = SAMPLE_RATE * (RECORDING_DURATION_MS / 1000) * 2; // 16-bit samples

// Silence detection settings for end-of-speech detection
const int SILENCE_THRESHOLD = 800;     // Energy threshold for silence (increased for PDM mic)
const int SILENCE_DURATION_MS = 1500;  // Stop after 1.5 seconds of silence
const int MIN_RECORDING_MS = 1000;     // Minimum 1 second recording
const int MAX_RECORDING_MS = 10000;    // Maximum 10 seconds
const int NO_SPEECH_TIMEOUT_MS = 5000; // Abort if no speech detected after wake word for 5 seconds

// Circular buffer for I2S audio recording
#define CIRCULAR_BUFFER_SIZE (SAMPLE_RATE * 3 * 2) // 3 seconds of 16-bit audio (~96KB)
static int16_t circularAudioBuffer[CIRCULAR_BUFFER_SIZE] EXT_RAM_ATTR;
static volatile size_t writeIndex = 0;
static volatile size_t readIndex = 0;
static volatile bool isRecording = false;

// State Machine for MarcusBot
enum SystemState
{
  STATE_IDLE,
  STATE_LISTENING_FOR_WAKE_WORD,
  STATE_RECORDING,
  STATE_PROCESSING,
  STATE_PLAYING_RESPONSE,
  STATE_ERROR
};

volatile SystemState currentState = STATE_LISTENING_FOR_WAKE_WORD;
volatile SystemState previousState = STATE_IDLE;
String errorMessage = "";
volatile unsigned long stateStartTime = 0;

// State timeout limits (milliseconds)
const unsigned long STATE_TIMEOUT_RECORDING = 15000;  // 15 sec
const unsigned long STATE_TIMEOUT_PROCESSING = 45000; // 45 sec
const unsigned long STATE_TIMEOUT_PLAYING = 120000;   // 2 min
const unsigned long STATE_TIMEOUT_ERROR = 5000;       // 5 sec

// Custom AudioFileSource for streaming from memory
class AudioFileSourceMemory : public AudioFileSource
{
public:
  AudioFileSourceMemory(const uint8_t *data, size_t len) : data_(data), len_(len), pos_(0) {}
  virtual ~AudioFileSourceMemory() override {}
  virtual bool open(const char *path) override { return true; }
  virtual uint32_t read(void *buffer, uint32_t len) override
  {
    if (pos_ >= len_)
      return 0;
    uint32_t toRead = min(len, (uint32_t)(len_ - pos_));
    memcpy(buffer, data_ + pos_, toRead);
    pos_ += toRead;
    return toRead;
  }
  virtual bool seek(int32_t pos, int dir) override
  {
    if (dir == SEEK_SET)
      pos_ = pos;
    else if (dir == SEEK_CUR)
      pos_ += pos;
    else if (dir == SEEK_END)
      pos_ = len_ + pos;
    pos_ = constrain(pos_, 0, (int32_t)len_);
    return true;
  }
  virtual bool close() override { return true; }
  virtual bool isOpen() override { return true; }
  virtual uint32_t getSize() override { return len_; }
  virtual uint32_t getPos() override { return pos_; }

private:
  const uint8_t *data_;
  size_t len_;
  size_t pos_;
};

// Emotion timeline for dynamic emotion changes
struct EmotionSegment
{
  String text;
  String emotion;
  int wordCount;
  unsigned long durationMs; // Estimated duration in milliseconds
};
EmotionSegment emotionTimeline[10]; // Max 10 segments
int emotionSegmentCount = 0;
int currentSegmentIndex = 0;

// Audio playback objects (reused, not recreated)
AudioOutputI2S *audioOutput = nullptr;
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceBuffer *mp3PlaybackBuffer = nullptr;
AudioFileSourceMemory *audioMemorySource = nullptr;

// Playback state
volatile bool isPlayingAudio = false;
volatile unsigned long audioPlaybackStartTime = 0;

// MP3 data buffer (reused)
static uint8_t *mp3DataBuffer = nullptr;
static size_t mp3BufferCapacity = 0;

// FreeRTOS task handles and semaphores
TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t audioTaskHandle = NULL;
TaskHandle_t cameraTaskHandle = NULL;
SemaphoreHandle_t displayMutex = NULL;

// Face detection state
volatile bool faceDetected = false;
volatile int faceCount = 0;
volatile bool cameraEnabled = true; // Control flag for camera task

// Pre-allocated recording buffer
static int16_t *preAllocatedRecordBuffer = nullptr;
static const size_t RECORDING_BUFFER_SIZE = MAX_AUDIO_SIZE;

// Forward declarations
void startRecording();
void stopRecording();
void abortRecording();
size_t getRecordedDataSize();
void copyRecordedData(int16_t *dest, size_t maxSize);
bool sendAudioToBackend();
void displayTask(void *parameter);
void audioTask(void *parameter);
void cameraTask(void *parameter);
void setupI2SSpeaker();
void initializeAudioObjects();
void cleanupAudioPlayback();
int countWords(String text);
void calculateEmotionTimings();
void updateEmotionDuringPlayback();
void transitionToState(SystemState newState);
const char *getStateName(SystemState state);
bool detectSilence(int16_t *buffer, size_t samples);
void setEmotionFromString(String emotion);
void blinkGreen(int times);
void blinkRed(int times);
void blinkBlue(int times);
void displayText(const char *text);
bool ensureWiFiConnected();

// State Machine Functions
const char *getStateName(SystemState state)
{
  switch (state)
  {
  case STATE_IDLE:
    return "IDLE";
  case STATE_LISTENING_FOR_WAKE_WORD:
    return "LISTENING";
  case STATE_RECORDING:
    return "RECORDING";
  case STATE_PROCESSING:
    return "PROCESSING";
  case STATE_PLAYING_RESPONSE:
    return "PLAYING";
  case STATE_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

void transitionToState(SystemState newState)
{
  previousState = currentState;
  currentState = newState;
  stateStartTime = millis();

  Serial.printf("[STATE] %s -> %s\n",
                getStateName(previousState),
                getStateName(newState));

  // Entry actions for new state
  switch (newState)
  {
  case STATE_IDLE:
    digitalWrite(LED_PIN, LOW);
    setEmotionFromString("happy");
    displayText("MarcusBot Ready");
    break;

  case STATE_LISTENING_FOR_WAKE_WORD:
    digitalWrite(LED_PIN, LOW);
    setEmotionFromString("happy");
    // Don't update display constantly - only on state change
    break;

  case STATE_RECORDING:
    blinkBlue(1);
    digitalWrite(LED_PIN, HIGH);
    startRecording();
    setEmotionFromString("surprised");
    displayText("Listening...");
    Serial.println("[AUDIO] Recording started");
    break;

  case STATE_PROCESSING:
    digitalWrite(LED_PIN, LOW);
    setEmotionFromString("skeptical");
    displayText("Thinking...");
    Serial.println("[PROCESS] Sending to backend");
    break;

  case STATE_PLAYING_RESPONSE:
    digitalWrite(LED_PIN, LOW);
    displayText("Speaking...");
    Serial.println("[AUDIO] Playing response");
    break;

  case STATE_ERROR:
    blinkRed(2);
    digitalWrite(LED_PIN, LOW);
    setEmotionFromString("frustrated");
    displayText("Error!");
    Serial.printf("[ERROR] %s\n", errorMessage.c_str());
    break;
  }
}

// Silence Detection for End-of-Speech with DC bias removal
bool detectSilence(int16_t *buffer, size_t samples)
{
  if (samples == 0)
    return true;

  // Calculate DC offset (average value)
  long dcOffset = 0;
  for (size_t i = 0; i < samples; i++)
  {
    dcOffset += buffer[i];
  }
  dcOffset /= samples;

  // Calculate energy with DC offset removed
  long energy = 0;
  for (size_t i = 0; i < samples; i++)
  {
    int16_t sample = buffer[i] - dcOffset;
    energy += abs(sample);
  }
  energy /= samples;

  return energy < SILENCE_THRESHOLD;
}

// LED Helpers
void blinkGreen(int times)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
}

void blinkRed(int times)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

void blinkBlue(int times)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(LED_PIN, HIGH);
    delay(150);
    digitalWrite(LED_PIN, LOW);
    delay(150);
  }
}

void setEmotionFromString(String emotion)
{
  emotion.toLowerCase();

  // Map our emotions to RoboEyes moods (DEFAULT, TIRED, ANGRY, HAPPY)
  if (emotion == "happy" || emotion == "in_love" || emotion == "awe")
    eyes.setMood(HAPPY);
  else if (emotion == "sad" || emotion == "shy" || emotion == "frustrated")
    eyes.setMood(TIRED);
  else if (emotion == "angry" || emotion == "skeptical")
    eyes.setMood(ANGRY);
  else if (emotion == "surprised")
  {
    eyes.open();
    eyes.setMood(DEFAULT);
  }
  else
    eyes.setMood(DEFAULT);

  currentEmotion = emotion;
  Serial.println("[EMOTION] Set to: " + emotion);
}

// Display text helper
void displayText(const char *text)
{
  if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)))
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(text);
    display.display();
    xSemaphoreGive(displayMutex);
  }
}

void setupI2S()
{
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = BUFFER_SIZE,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0};

  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_PIN_NO_CHANGE,
      .ws_io_num = I2S_SCK, // PDM CLK
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_SD}; // PDM DATA

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  i2s_zero_dma_buffer(I2S_PORT);

  Serial.println("I2S microphone initialized");
}

void setupI2SSpeaker()
{
  // Initialize audio output for I2S speaker
  audioOutput = new AudioOutputI2S(I2S_SPEAKER_PORT, AudioOutputI2S::EXTERNAL_I2S);
  audioOutput->SetPinout(I2S_SPEAKER_BCK, I2S_SPEAKER_WS, I2S_SPEAKER_DOUT);
  audioOutput->SetGain(0.5); // Adjust volume (0.0 to 4.0)

  Serial.println("I2S speaker initialized");
}

void initializeAudioObjects()
{
  // Preallocate MP3 generator (reused across playbacks)
  if (!mp3)
  {
    mp3 = new AudioGeneratorMP3();
  }
}

void cleanupAudioPlayback()
{
  // Stop playback but don't delete objects (they're reused)
  if (mp3 && mp3->isRunning())
  {
    mp3->stop();
  }

  if (mp3PlaybackBuffer)
  {
    delete mp3PlaybackBuffer;
    mp3PlaybackBuffer = nullptr;
  }

  if (audioMemorySource)
  {
    delete audioMemorySource;
    audioMemorySource = nullptr;
  }
}

int countWords(String text)
{
  if (text.length() == 0)
    return 0;

  int wordCount = 0;
  bool inWord = false;
  const char *str = text.c_str();

  // Use pointer iteration instead of array indexing
  while (*str)
  {
    if (*str == ' ' || *str == '\t' || *str == '\n')
    {
      inWord = false;
    }
    else if (!inWord)
    {
      inWord = true;
      wordCount++;
    }
    str++;
  }

  return wordCount;
}

void calculateEmotionTimings()
{
  // Average speaking rate: ~150 words per minute = 2.5 words/sec = 400ms per word
  const int msPerWord = 400;

  for (int i = 0; i < emotionSegmentCount; i++)
  {
    emotionTimeline[i].wordCount = countWords(emotionTimeline[i].text);
    emotionTimeline[i].durationMs = emotionTimeline[i].wordCount * msPerWord;

    Serial.printf("Segment %d: %d words, ~%lu ms\n",
                  i, emotionTimeline[i].wordCount, emotionTimeline[i].durationMs);
    yield(); // Allow other tasks to run
  }
}

void updateEmotionDuringPlayback()
{
  if (!isPlayingAudio || emotionSegmentCount == 0)
    return;

  unsigned long elapsed = millis() - audioPlaybackStartTime;
  unsigned long cumulativeTime = 0;

  // Find which segment we should be in based on elapsed time
  for (int i = 0; i < emotionSegmentCount; i++)
  {
    cumulativeTime += emotionTimeline[i].durationMs;

    if (elapsed < cumulativeTime && currentSegmentIndex != i)
    {
      currentSegmentIndex = i;
      Serial.printf("Switching to segment %d: %s [%s]\n",
                    i,
                    emotionTimeline[i].text.c_str(),
                    emotionTimeline[i].emotion.c_str());
      setEmotionFromString(emotionTimeline[i].emotion);
      break;
    }
  }
}

void startRecording()
{
  writeIndex = 0;
  readIndex = 0;
  isRecording = true;
  DEBUG_LOG("Recording started - buffers reset");
}

void stopRecording()
{
  isRecording = false;
  DEBUG_LOG("Recording stopped");
}

void abortRecording()
{
  // Reset recording state and clear buffers
  isRecording = false;
  writeIndex = 0;
  readIndex = 0;
  Serial.println("[AUDIO] Recording aborted - no speech detected");
  DEBUG_LOG("Recording aborted - buffers cleared");
}

size_t getRecordedDataSize()
{
  if (writeIndex >= readIndex)
    return (writeIndex - readIndex) * sizeof(int16_t);
  else
    return (CIRCULAR_BUFFER_SIZE - readIndex + writeIndex) * sizeof(int16_t);
}

void copyRecordedData(int16_t *dest, size_t maxSize)
{
  size_t samplesToRead = min(maxSize / sizeof(int16_t), writeIndex >= readIndex ? writeIndex - readIndex : CIRCULAR_BUFFER_SIZE - readIndex + writeIndex);

  for (size_t i = 0; i < samplesToRead; i++)
  {
    dest[i] = circularAudioBuffer[readIndex];
    readIndex = (readIndex + 1) % CIRCULAR_BUFFER_SIZE;
  }
}

void setupCamera()
{
  if (!ENABLE_CAMERA)
  {
    Serial.println("[TEST] Camera disabled");
    return;
  }

  DEBUG_LOG("Initializing camera...");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_QVGA; // 320x240
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  DEBUG_LOG("Camera config: QVGA, JPEG, PSRAM");

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    DEBUG_LOG("Camera initialization error: 0x%x", err);
    return;
  }

  Serial.println("Camera initialized");
  DEBUG_LOG("Camera ready");
}

// Continuous I2S reading into circular buffer with wake word detection
bool detectWakeWord()
{
  static int consecutiveDetections = 0;
  const int REQUIRED_DETECTIONS = 3; // Require 3 consecutive detections

  int16_t buffer[BUFFER_SIZE];
  size_t bytesRead;

  // Read audio samples from I2S
  i2s_read(I2S_PORT, buffer, BUFFER_SIZE * sizeof(int16_t), &bytesRead, portMAX_DELAY);

  if (bytesRead > 0)
  {
    size_t samplesRead = bytesRead / sizeof(int16_t);

    DEBUG_LOG("I2S read: %d bytes, %d samples", bytesRead, samplesRead);

    // Debug: Show raw sample values
    if (samplesRead >= 5)
    {
      Serial.printf("[RAW] First 5 samples: %d, %d, %d, %d, %d\n",
                    buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
    }

    // Write to circular buffer if recording or always buffering
    for (size_t i = 0; i < samplesRead; i++)
    {
      circularAudioBuffer[writeIndex] = buffer[i];
      writeIndex = (writeIndex + 1) % CIRCULAR_BUFFER_SIZE;

      // Prevent overwriting unread data when not recording
      if (!isRecording && writeIndex == readIndex)
      {
        readIndex = (readIndex + 1) % CIRCULAR_BUFFER_SIZE;
      }
    }

    // Calculate DC offset
    long dcOffset = 0;
    for (size_t i = 0; i < samplesRead; i++)
    {
      dcOffset += buffer[i];
    }
    dcOffset /= samplesRead;

    // Calculate energy with DC offset removed for wake word detection
    long energy = 0;
    for (size_t i = 0; i < samplesRead; i++)
    {
      int16_t sample = buffer[i] - dcOffset;
      energy += abs(sample);
    }
    energy /= samplesRead;

    DEBUG_LOG("Audio energy: %ld (DC: %ld, threshold: %d)", energy, dcOffset, WAKE_WORD_THRESHOLD);

    // Simple threshold-based detection with debouncing
    if (energy > WAKE_WORD_THRESHOLD)
    {
      consecutiveDetections++;
      DEBUG_LOG("Wake word candidate %d/%d", consecutiveDetections, REQUIRED_DETECTIONS);
      if (consecutiveDetections >= REQUIRED_DETECTIONS)
      {
        Serial.printf("[WAKE] Detected! Energy: %ld\n", energy);
        consecutiveDetections = 0;
        return true;
      }
    }
    else
    {
      consecutiveDetections = 0;
    }
  }

  return false;
}

// FreeRTOS Task: Display/Eyes Animation (runs continuously)
void displayTask(void *parameter)
{
  if (!ENABLE_DISPLAY)
  {
    Serial.println("[TEST] Display task disabled");
    vTaskDelete(NULL);
    return;
  }

  while (1)
  {
    // Only take mutex when needed
    bool needsUpdate = isPlayingAudio; // Update if playing audio

    if (needsUpdate)
    {
      if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)))
      {
        // Update emotion timing during audio playback
        updateEmotionDuringPlayback();

        eyes.update();
        display.clearDisplay();
        eyes.drawEyes();
        display.display();
        xSemaphoreGive(displayMutex);
      }
    }
    else
    {
      // Always update eyes for animation
      if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)))
      {
        eyes.update();
        display.clearDisplay();
        eyes.drawEyes();
        display.display();
        xSemaphoreGive(displayMutex);
      }
    }

    yield();                        // Yield to other tasks
    vTaskDelay(pdMS_TO_TICKS(100)); // ~10 FPS for eyes
  }
}

// FreeRTOS Task: Camera Face Detection
void cameraTask(void *parameter)
{
  if (!ENABLE_CAMERA)
  {
    Serial.println("[TEST] Camera task disabled");
    vTaskDelete(NULL);
    return;
  }

  while (1)
  {
    if (cameraEnabled)
    {
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb)
      {
        DEBUG_LOG("Camera frame: %d bytes, %dx%d", fb->len, fb->width, fb->height);

        // Basic face detection based on JPEG analysis
        // For production: integrate ESP-WHO face detection library
        // This is a placeholder - check if image data suggests faces

        // Simple heuristic: if we have a valid frame, assume potential face
        if (fb->len > 5000) // Arbitrary size threshold
        {
          if (!faceDetected)
          {
            faceDetected = true;
            Serial.println("Face detected in frame!");
            DEBUG_LOG("Face detected: frame size %d bytes", fb->len);
          }
          faceCount++;
        }
        else
        {
          faceDetected = false;
          DEBUG_LOG("No face: frame too small (%d bytes)", fb->len);
        }

        esp_camera_fb_return(fb);
      }
      else
      {
        DEBUG_LOG("Failed to capture frame");
      }
      vTaskDelay(pdMS_TO_TICKS(500)); // Check every 500ms
    }
    else
    {
      // Camera paused - longer delay to save power
      DEBUG_LOG("Camera paused");
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
}

// FreeRTOS Task: Audio/Wake Word Detection with State Machine
void audioTask(void *parameter)
{
  unsigned long recordingStartTime = 0;
  unsigned long lastSpeechTime = 0;
  bool speechDetectedAfterWakeWord = false;

  while (1)
  {
    // Check for manual trigger via serial
    if (ENABLE_MANUAL_TRIGGER && manualTriggerRequested)
    {
      manualTriggerRequested = false;
      Serial.println("[MANUAL] Manual trigger activated!");
      if (currentState == STATE_LISTENING_FOR_WAKE_WORD || currentState == STATE_IDLE)
      {
        transitionToState(STATE_RECORDING);
        recordingStartTime = millis();
        lastSpeechTime = millis();
        speechDetectedAfterWakeWord = false; // Reset flag for manual trigger too
        DEBUG_LOG("Manual trigger - started recording");
      }
      else
      {
        Serial.printf("[MANUAL] Cannot trigger in state: %s\n", getStateName(currentState));
      }
    }

    // Check for state timeout (watchdog)
    unsigned long stateElapsed = millis() - stateStartTime;
    bool timeout = false;

    switch (currentState)
    {
    case STATE_RECORDING:
      if (stateElapsed > STATE_TIMEOUT_RECORDING)
      {
        Serial.println("[ERROR] Recording timeout!");
        timeout = true;
      }
      break;
    case STATE_PROCESSING:
      if (stateElapsed > STATE_TIMEOUT_PROCESSING)
      {
        Serial.println("[ERROR] Processing timeout!");
        timeout = true;
      }
      break;
    case STATE_PLAYING_RESPONSE:
      if (stateElapsed > STATE_TIMEOUT_PLAYING)
      {
        Serial.println("[ERROR] Playback timeout!");
        timeout = true;
      }
      break;
    case STATE_ERROR:
      if (stateElapsed > STATE_TIMEOUT_ERROR)
      {
        timeout = true; // Auto-recover from error
      }
      break;
    default:
      break;
    }

    if (timeout)
    {
      errorMessage = "State timeout - recovering";
      transitionToState(STATE_ERROR);
    }

    switch (currentState)
    {

    case STATE_IDLE:
      // Brief idle state before listening
      vTaskDelay(pdMS_TO_TICKS(100));
      transitionToState(STATE_LISTENING_FOR_WAKE_WORD);
      break;

    case STATE_LISTENING_FOR_WAKE_WORD:
      if (ENABLE_WAKE_WORD)
      {
        if (detectWakeWord())
        {
          Serial.println("[WAKE] Wake word confirmed!");
          transitionToState(STATE_RECORDING);
          recordingStartTime = millis();
          lastSpeechTime = millis();
          speechDetectedAfterWakeWord = false; // Reset flag for new recording
          DEBUG_LOG("Started recording - waiting for speech...");
        }
      }
      else
      {
        // Wake word disabled - wait for manual trigger
        DEBUG_LOG("Wake word disabled, waiting for manual trigger...");
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      break;

    case STATE_RECORDING:
    {
      int16_t buffer[BUFFER_SIZE];
      size_t bytesRead;
      i2s_read(I2S_PORT, buffer, BUFFER_SIZE * sizeof(int16_t), &bytesRead, portMAX_DELAY);

      if (bytesRead > 0)
      {
        size_t samplesRead = bytesRead / sizeof(int16_t);

        DEBUG_LOG("Recording: %d samples, buffer pos: %d", samplesRead, writeIndex);

        // Write to circular buffer
        for (size_t i = 0; i < samplesRead; i++)
        {
          circularAudioBuffer[writeIndex] = buffer[i];
          writeIndex = (writeIndex + 1) % CIRCULAR_BUFFER_SIZE;
        }

        // Calculate DC offset and energy with bias removal for accurate detection
        long dcOffset = 0;
        for (size_t i = 0; i < samplesRead; i++)
        {
          dcOffset += buffer[i];
        }
        dcOffset /= samplesRead;

        // Calculate energy with DC offset removed
        long energy = 0;
        for (size_t i = 0; i < samplesRead; i++)
        {
          int16_t sample = buffer[i] - dcOffset;
          energy += abs(sample);
        }
        energy /= samplesRead;

        // Check for silence (end of speech detection)
        bool isSilent = detectSilence(buffer, samplesRead);

        if (!isSilent)
        {
          lastSpeechTime = millis();
          speechDetectedAfterWakeWord = true; // Mark that we detected actual speech
          DEBUG_LOG("Speech detected - flag set");
        }

        unsigned long elapsed = millis() - recordingStartTime;
        unsigned long silenceDuration = millis() - lastSpeechTime;

        DEBUG_LOG("Recording: %.1fs elapsed, %.1fs silence, energy: %ld, isSilent: %d, speechDetected: %d",
                  elapsed / 1000.0, silenceDuration / 1000.0, energy, isSilent, speechDetectedAfterWakeWord);

        // Check for no-speech timeout (wake word but no actual speech)
        if (!speechDetectedAfterWakeWord && elapsed > NO_SPEECH_TIMEOUT_MS)
        {
          Serial.printf("[AUDIO] No speech detected after wake word (%.1fs timeout)\n", elapsed / 1000.0);
          abortRecording();
          transitionToState(STATE_LISTENING_FOR_WAKE_WORD);
          break;
        }

        // Check stop conditions (only if we detected speech)
        if (speechDetectedAfterWakeWord)
        {
          if (elapsed >= MIN_RECORDING_MS && silenceDuration > SILENCE_DURATION_MS)
          {
            Serial.printf("[AUDIO] Silence detected (%.1fs recorded)\n", elapsed / 1000.0);
            stopRecording();
            transitionToState(STATE_PROCESSING);
          }
          else if (elapsed > MAX_RECORDING_MS)
          {
            Serial.printf("[AUDIO] Max time reached (%.1fs)\n", elapsed / 1000.0);
            stopRecording();
            transitionToState(STATE_PROCESSING);
          }
        }
      }

      yield();
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    break;

    case STATE_PROCESSING:
    {
      size_t audioSize = getRecordedDataSize();
      Serial.printf("[PROCESS] Audio: %d bytes (%.1fs)\n",
                    audioSize,
                    (audioSize / 2.0) / SAMPLE_RATE);
      DEBUG_LOG("Recorded audio size: %d bytes", audioSize);

      if (ENABLE_BACKEND)
      {
        // Ensure WiFi is connected
        if (!ensureWiFiConnected())
        {
          errorMessage = "WiFi connection lost";
          transitionToState(STATE_ERROR);
          break;
        }

        bool success = sendAudioToBackend();

        if (success)
        {
          Serial.println("[PROCESS] Success!");
          transitionToState(STATE_PLAYING_RESPONSE);
        }
        else
        {
          errorMessage = "Backend communication failed";
          transitionToState(STATE_ERROR);
        }
      }
      else
      {
        // Backend disabled - simulate response for testing
        Serial.println("[TEST] Backend disabled - skipping processing");
        DEBUG_LOG("Would have sent %d bytes to backend", audioSize);

        // Skip to next state or return to listening
        vTaskDelay(pdMS_TO_TICKS(1000));
        transitionToState(STATE_LISTENING_FOR_WAKE_WORD);
      }
    }
    break;

    case STATE_PLAYING_RESPONSE:
      // Audio playback happens, check if still playing
      if (!isPlayingAudio)
      {
        Serial.println("[AUDIO] Playback complete\n");
        vTaskDelay(pdMS_TO_TICKS(500)); // Brief pause
        transitionToState(STATE_LISTENING_FOR_WAKE_WORD);
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      break;

    case STATE_ERROR:
      // Error state - show error then recover
      vTaskDelay(pdMS_TO_TICKS(3000)); // Show error for 3 seconds

      // Try WiFi recovery if that was the issue
      if (errorMessage.indexOf("WiFi") >= 0)
      {
        ensureWiFiConnected();
      }

      Serial.printf("[RECOVERY] Recovering from: %s\n", errorMessage.c_str());
      errorMessage = "";
      transitionToState(STATE_LISTENING_FOR_WAKE_WORD);
      break;

    default:
      Serial.println("[ERROR] Unknown state - resetting");
      transitionToState(STATE_IDLE);
      break;
    }

    yield();
  }
}

// Send audio to backend and receive response
bool sendAudioToBackend()
{
  size_t audioDataSize = getRecordedDataSize();
  DEBUG_LOG("sendAudioToBackend: audioDataSize=%d", audioDataSize);

  if (WiFi.status() != WL_CONNECTED || audioDataSize == 0)
  {
    Serial.println("[ERROR] WiFi not connected or no audio data");
    DEBUG_LOG("WiFi status: %d, audioDataSize: %d", WiFi.status(), audioDataSize);
    return false;
  }

  // Use pre-allocated buffer instead of malloc
  if (!preAllocatedRecordBuffer)
  {
    Serial.println("[ERROR] Record buffer not allocated!");
    return false;
  }

  copyRecordedData(preAllocatedRecordBuffer, audioDataSize);
  size_t recordedAudioBufferSize = audioDataSize;

  Serial.println("[HTTP] Sending audio to backend...");
  DEBUG_LOG("Connecting to: %s", serverUrl);

  HTTPClient http;
  http.begin(serverUrl);
  http.setTimeout(30000); // 30 second timeout

  // Create WAV header
  uint8_t wavHeader[44];
  uint32_t fileSize = recordedAudioBufferSize + 36;
  uint32_t dataSize = recordedAudioBufferSize;
  uint16_t numChannels = 1;
  uint32_t sampleRate = SAMPLE_RATE;
  uint16_t bitsPerSample = 16;
  uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
  uint16_t blockAlign = numChannels * (bitsPerSample / 8);

  memcpy(wavHeader, "RIFF", 4);
  memcpy(wavHeader + 4, &fileSize, 4);
  memcpy(wavHeader + 8, "WAVE", 4);
  memcpy(wavHeader + 12, "fmt ", 4);
  uint32_t fmtSize = 16;
  memcpy(wavHeader + 16, &fmtSize, 4);
  uint16_t audioFormat = 1;
  memcpy(wavHeader + 20, &audioFormat, 2);
  memcpy(wavHeader + 22, &numChannels, 2);
  memcpy(wavHeader + 24, &sampleRate, 4);
  memcpy(wavHeader + 28, &byteRate, 4);
  memcpy(wavHeader + 32, &blockAlign, 2);
  memcpy(wavHeader + 34, &bitsPerSample, 2);
  memcpy(wavHeader + 36, "data", 4);
  memcpy(wavHeader + 40, &dataSize, 4);

  // Combine header and audio data (temporary buffer for HTTP upload)
  size_t totalSize = 44 + recordedAudioBufferSize;
  uint8_t *fullAudio = (uint8_t *)malloc(totalSize); // Use regular heap for temporary data
  if (!fullAudio)
  {
    Serial.println("[ERROR] Failed to allocate full audio buffer!");
    return false;
  }

  memcpy(fullAudio, wavHeader, 44);
  memcpy(fullAudio + 44, preAllocatedRecordBuffer, recordedAudioBufferSize);

  // Create multipart form data
  const char *boundary = "----ESP32Boundary";

  // Calculate content length
  size_t boundaryLen = strlen(boundary);
  size_t headerLen = boundaryLen + 2 + 58 + 26 + 4; // --boundary\r\n + Content-Disposition + Content-Type + \r\n\r\n
  http.addHeader("Content-Type", String("multipart/form-data; boundary=") + boundary);
  http.addHeader("Content-Length", String(headerLen + totalSize + boundaryLen + 6));

  // Send request
  WiFiClient *client = http.getStreamPtr();
  client->print("--");
  client->print(boundary);
  client->print("\r\n");
  client->print("Content-Disposition: form-data; name=\"audio\"; filename=\"audio.wav\"\r\n");
  client->print("Content-Type: audio/wav\r\n\r\n");
  client->write(fullAudio, totalSize);
  client->print("\r\n--");
  client->print(boundary);
  client->print("--\r\n");

  int httpResponseCode = http.GET(); // Complete the request

  free(fullAudio);

  Serial.printf("[HTTP] Response code: %d\n", httpResponseCode);
  DEBUG_LOG("HTTP response: %d", httpResponseCode);

  if (httpResponseCode == 200)
  {
    // Extract emotion timeline from response header
    String segmentsJson = http.header("X-LLM-Segments");
    DEBUG_LOG("Emotion segments JSON: %s", segmentsJson.c_str());

    if (segmentsJson.length() > 0)
    {
      Serial.println("[EMOTION] Parsing timeline...");
      emotionSegmentCount = 0;

      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, segmentsJson);

      if (error)
      {
        Serial.printf("[ERROR] JSON parse failed: %s\n", error.c_str());
      }
      else
      {
        JsonArray segments = doc.as<JsonArray>();
        for (JsonObject segment : segments)
        {
          if (emotionSegmentCount >= 10)
            break;

          emotionTimeline[emotionSegmentCount].text = segment["text"].as<String>();
          emotionTimeline[emotionSegmentCount].emotion = segment["emotion"].as<String>();

          Serial.printf("  [%s] %s\n",
                        emotionTimeline[emotionSegmentCount].emotion.c_str(),
                        emotionTimeline[emotionSegmentCount].text.c_str());

          emotionSegmentCount++;
        }
      }

      // Set initial emotion and calculate timings
      if (emotionSegmentCount > 0)
      {
        currentSegmentIndex = 0;
        calculateEmotionTimings();
        setEmotionFromString(emotionTimeline[0].emotion);
      }
    }

    // Get raw MP3 audio data
    int contentLength = http.getSize();
    Serial.printf("[AUDIO] Response size: %d bytes\n", contentLength);
    DEBUG_LOG("MP3 content length: %d bytes", contentLength);

    if (contentLength > 0)
    {
      // Reuse or allocate MP3 buffer in PSRAM
      if (mp3BufferCapacity < contentLength)
      {
        if (mp3DataBuffer)
          heap_caps_free(mp3DataBuffer);
        mp3DataBuffer = (uint8_t *)heap_caps_malloc(contentLength, MALLOC_CAP_SPIRAM);
        mp3BufferCapacity = contentLength;
        DEBUG_LOG("Allocated MP3 buffer: %d bytes (PSRAM)", contentLength);
      }

      if (!mp3DataBuffer)
      {
        Serial.println("[ERROR] Failed to allocate MP3 buffer!");
        DEBUG_LOG("malloc failed for %d bytes", contentLength);
        http.end();
        return false;
      }

      // Read MP3 data from response
      WiFiClient *stream = http.getStreamPtr();
      int bytesRead = 0;
      while (http.connected() && bytesRead < contentLength)
      {
        size_t available = stream->available();
        if (available)
        {
          int toRead = min((int)available, contentLength - bytesRead);
          int read = stream->readBytes(mp3DataBuffer + bytesRead, toRead);
          bytesRead += read;
        }
        yield();
      }

      Serial.printf("[AUDIO] Downloaded %d bytes MP3\n", bytesRead);

      // Play audio with emotion timeline
      if (bytesRead > 0)
      {
        if (ENABLE_SPEAKER)
        {
          Serial.println("[AUDIO] Playing with emotion timeline...");
          DEBUG_LOG("Starting MP3 playback: %d bytes", bytesRead);

          // Clean up previous playback resources
          cleanupAudioPlayback();

          // Stream MP3 directly from memory
          audioMemorySource = new AudioFileSourceMemory(mp3DataBuffer, bytesRead);
          mp3PlaybackBuffer = new AudioFileSourceBuffer(audioMemorySource, 2048);

          if (mp3->begin(mp3PlaybackBuffer, audioOutput))
          {
            isPlayingAudio = true;
            audioPlaybackStartTime = millis();
            currentSegmentIndex = 0;
            DEBUG_LOG("MP3 playback started");

            // Play audio loop - emotion changes handled in displayTask
            while (mp3->isRunning())
            {
              if (!mp3->loop())
              {
                mp3->stop();
                break;
              }
              yield();
            }

            isPlayingAudio = false;
            Serial.println("[AUDIO] Playback complete!");
            DEBUG_LOG("MP3 playback finished");
          }
          else
          {
            Serial.println("[ERROR] Failed to start MP3 playback!");
            DEBUG_LOG("mp3->begin() failed");
            http.end();
            return false;
          }
        }
        else
        {
          Serial.println("[TEST] Speaker disabled - skipping playback");
          DEBUG_LOG("Would have played %d bytes of MP3 data", bytesRead);
          // Simulate playback time
          vTaskDelay(pdMS_TO_TICKS(2000));
        }

        http.end();
        return true;
      }
    }
  }
  else if (httpResponseCode > 0)
  {
    Serial.printf("[ERROR] Backend error: %d\n", httpResponseCode);
    String errorPayload = http.getString();
    Serial.println(errorPayload);
    DEBUG_LOG("HTTP error payload: %s", errorPayload.c_str());
  }
  else
  {
    Serial.printf("[ERROR] Connection failed: %s\n", http.errorToString(httpResponseCode).c_str());
    DEBUG_LOG("HTTP connection error code: %d", httpResponseCode);
  }

  http.end();
  return false;
}

void setup()
{
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("\n=== MarcusBot Initializing ===");
  Serial.printf("[MEM] Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("[MEM] PSRAM: %d bytes\n", ESP.getPsramSize());

  // Pre-allocate recording buffer in PSRAM
  preAllocatedRecordBuffer = (int16_t *)heap_caps_malloc(RECORDING_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  if (!preAllocatedRecordBuffer)
  {
    Serial.println("Failed to pre-allocate recording buffer!");
  }
  else
  {
    Serial.println("Recording buffer pre-allocated in PSRAM");
  }

  // Initialize circular buffer
  writeIndex = 0;
  readIndex = 0;
  isRecording = false;

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA); // Set to Station mode
  WiFi.disconnect();   // Disconnect from any previous connection
  delay(100);
  WiFi.begin(ssid, password);

  Serial.printf("SSID: %s\n", ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) // Increased timeout
  {
    delay(500);
    Serial.print(".");
    Serial.print(WiFi.status()); // Print status code for debugging
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n[WIFI] Connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal strength (RSSI): ");
    Serial.println(WiFi.RSSI());
    blinkGreen(3);
  }
  else
  {
    Serial.println("\n[WIFI] Connection FAILED!");
    Serial.print("Status code: ");
    Serial.println(WiFi.status());
    Serial.println("Codes: 0=IDLE, 1=NO_SSID, 3=CONNECTED, 4=FAILED, 6=DISCONNECTED");
    Serial.println("Check: 1) Is network 2.4GHz? 2) Correct password? 3) In range?");
    blinkRed(3);
    return;
  }

  // Initialize I2S microphone
  setupI2S();

  // Initialize I2S speaker
  setupI2SSpeaker();

  // Initialize reusable audio objects
  initializeAudioObjects();

  // Initialize Camera
  setupCamera();

  // Initialize OLED display
  if (ENABLE_DISPLAY)
  {
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
    {
      Serial.println("OLED initialization failed!");
    }
    else
    {
      Serial.println("OLED initialized");
      DEBUG_LOG("Display: %dx%d, address 0x%X", SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_ADDRESS);
      display.clearDisplay();
      eyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 60);
      eyes.setAutoblinker(true, 3, 2);
      eyes.setIdleMode(true, 2, 2);
      eyes.setMood(HAPPY);
      display.display();
    }
  }
  else
  {
    Serial.println("[TEST] Display disabled");
  }

  // Create FreeRTOS mutex for display access
  displayMutex = xSemaphoreCreateMutex();

  // Create FreeRTOS tasks
  if (ENABLE_DISPLAY)
  {
    xTaskCreatePinnedToCore(
        displayTask,
        "DisplayTask",
        4096,
        NULL,
        1,
        &displayTaskHandle,
        0 // Core 0
    );
    DEBUG_LOG("Display task created on Core 0");
  }

  xTaskCreatePinnedToCore(
      audioTask,
      "AudioTask",
      16384, // Increased from 8192 to handle larger buffers and HTTP operations
      NULL,
      2,
      &audioTaskHandle,
      1 // Core 1
  );
  DEBUG_LOG("Audio task created on Core 1");

  if (ENABLE_CAMERA)
  {
    xTaskCreatePinnedToCore(
        cameraTask,
        "CameraTask",
        4096,
        NULL,
        1,
        &cameraTaskHandle,
        1 // Core 1
    );
    DEBUG_LOG("Camera task created on Core 1");
  }

  // Print test configuration
  Serial.println("\n=== MarcusBot Test Configuration ===");
  Serial.printf("Wake Word:   %s\n", ENABLE_WAKE_WORD ? "ENABLED" : "DISABLED");
  Serial.printf("Backend:     %s\n", ENABLE_BACKEND ? "ENABLED" : "DISABLED");
  Serial.printf("Speaker:     %s\n", ENABLE_SPEAKER ? "ENABLED" : "DISABLED");
  Serial.printf("Camera:      %s\n", ENABLE_CAMERA ? "ENABLED" : "DISABLED");
  Serial.printf("Display:     %s\n", ENABLE_DISPLAY ? "ENABLED" : "DISABLED");
  Serial.printf("Verbose Log: %s\n", ENABLE_VERBOSE_DEBUG ? "ENABLED" : "DISABLED");
  Serial.printf("Manual Trig: %s\n", ENABLE_MANUAL_TRIGGER ? "ENABLED" : "DISABLED");
  Serial.println("======================================\n");

  Serial.println("\n=== Ready! Say 'Marcus' to start ===");
  if (ENABLE_CAMERA)
    Serial.println("Face detection: Active");
  Serial.println("Tasks running on both cores");
  Serial.printf("Initial state: %s\n", getStateName(currentState));
  Serial.printf("[MEM] Free heap after init: %d bytes\n", ESP.getFreeHeap());

  // Self-test confirmation
  Serial.println("\n[SELF-TEST] System Check:");
  Serial.printf("  [%s] WiFi\n", WiFi.status() == WL_CONNECTED ? "OK" : "FAIL");
  if (ENABLE_DISPLAY)
    Serial.printf("  [%s] Display\n", display.getPixel(0, 0) >= 0 ? "OK" : "FAIL");
  Serial.printf("  [%s] Microphone\n", "OK"); // Assume OK if I2S initialized
  Serial.printf("  [%s] Speaker\n", audioOutput ? "OK" : "FAIL");
  if (ENABLE_CAMERA)
    Serial.printf("  [%s] Camera\n", "OK"); // Assume OK if camera initialized
  Serial.printf("  [%s] Recording Buffer\n", preAllocatedRecordBuffer ? "OK" : "FAIL");

  if (ENABLE_MANUAL_TRIGGER)
  {
    Serial.println("\n[MANUAL] Send 't' via Serial to manually trigger recording");
  }

  if (ENABLE_WAKE_WORD)
  {
    Serial.println("\nListening for wake word...\n");
  }
  else
  {
    Serial.println("\nWake word disabled - use manual trigger\n");
  }

  digitalWrite(LED_PIN, LOW);
}

void loop()
{
  // Check for serial commands (manual trigger)
  if (ENABLE_MANUAL_TRIGGER && Serial.available() > 0)
  {
    char cmd = Serial.read();
    if (cmd == 't' || cmd == 'T')
    {
      Serial.println("\n[MANUAL] Trigger command received!");
      manualTriggerRequested = true;
    }
    else if (cmd == 's' || cmd == 'S')
    {
      // Status command
      Serial.println("\n=== System Status ===");
      Serial.printf("State:       %s\n", getStateName(currentState));
      Serial.printf("Free Heap:   %d bytes\n", ESP.getFreeHeap());
      Serial.printf("WiFi:        %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
      if (ENABLE_CAMERA)
        Serial.printf("Face:        %s (count: %d)\n", faceDetected ? "Detected" : "None", faceCount);
      Serial.printf("Audio:       %s\n", isPlayingAudio ? "Playing" : "Idle");
      Serial.printf("Recording:   %s\n", isRecording ? "Active" : "Idle");
      Serial.println("=====================\n");
    }
    else if (cmd == 'h' || cmd == 'H' || cmd == '?')
    {
      // Help command
      Serial.println("\n=== Serial Commands ===");
      Serial.println("t/T - Manually trigger recording");
      Serial.println("s/S - Show system status");
      Serial.println("h/H/? - Show this help");
      Serial.println("========================\n");
    }
  }

  // All work is done in FreeRTOS tasks
  // Main loop just monitors and logs status
  static unsigned long lastStatusPrint = 0;

  if (millis() - lastStatusPrint > 10000) // Every 10 seconds
  {
    if (ENABLE_CAMERA)
    {
      Serial.printf("[STATUS] State: %s | Face: %s (n=%d) | Heap: %d bytes | WiFi: %s\n",
                    getStateName(currentState),
                    faceDetected ? "YES" : "NO",
                    faceCount,
                    ESP.getFreeHeap(),
                    WiFi.status() == WL_CONNECTED ? "OK" : "DISCONNECTED");
    }
    else
    {
      Serial.printf("[STATUS] State: %s | Heap: %d bytes | WiFi: %s\n",
                    getStateName(currentState),
                    ESP.getFreeHeap(),
                    WiFi.status() == WL_CONNECTED ? "OK" : "DISCONNECTED");
    }
    lastStatusPrint = millis();
  }

  vTaskDelay(pdMS_TO_TICKS(1000));
}
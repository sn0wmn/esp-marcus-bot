# MarcusBot - ESP32 S3 Voice Assistant

An AI-powered voice assistant robot using Seeed Studio XIAO ESP32 S3 Sense with animated eyes, voice interaction, and face detection.

---

## 📌 Pin Configuration - XIAO ESP32 S3 Sense
### External Components (Require Wiring)

| Component | Pin | XIAO S3 Pin Name | Usage | ✅ Valid? |
|-----------|-----|------------------|-------|----------|
| **I2C OLED Display** | | | 128x64 SSD1306 | |
| OLED SDA | Default I2C | D4 (GPIO 6) | I2C Data | ✅ Yes |
| OLED SCL | Default I2C | D5 (GPIO 7) | I2C Clock | ✅ Yes |
| OLED VCC | 3.3V | 3V3 | Power | ✅ Yes |
| OLED GND | GND | GND | Ground | ✅ Yes |
| **I2S Speaker (MAX98357A)** | | | Audio Output | |
| Speaker BCK | GPIO 5 | D3 | I2S Bit Clock | ✅ Yes |
| Speaker WS | GPIO 8 | D6 | I2S Word Select | ✅ Yes |
| Speaker DOUT | GPIO 9 | D7 | I2S Data Out | ✅ Yes |
| Speaker VCC | 5V | 5V | Power | ✅ Yes |
| Speaker GND | GND | GND | Ground | ✅ Yes |

---

## 🔧 Hardware Requirements

### Main Board
- **Seeed Studio XIAO ESP32 S3 Sense** (with camera and microphone)
  - Built-in OV2640 camera
  - Built-in INMP441 I2S microphone
  - ESP32-S3 dual-core processor
  - 8MB PSRAM
  - WiFi & Bluetooth

### External Components
1. **OLED Display**: 128x64 I2C SSD1306
2. **Speaker**: MAX98357A I2S Amplifier
3. **Power**: USB-C or 5V supply

---

## 🌟 Features

- 🎤 **Voice Activation**: Wake word detection ("Marcus")
- 🤖 **AI Conversation**: GPT-4o integration via backend API
- 🗣️ **Text-to-Speech**: Fish Audio TTS with MP3 playback
- 👀 **Animated Eyes**: Dynamic emotion-driven eye animations
- 📷 **Face Detection**: Built-in OV2640 camera
- 🎭 **Emotion System**: 9 emotions (happy, sad, angry, surprised, etc.)
- 🔄 **Real-time Updates**: Emotions change during speech playback

---

## 🚀 Getting Started

### 1. Backend Setup

The MarcusBot requires the MarcusBotBE backend server running. See `../MarcusBotBE/README.md`.

Quick start:
```bash
cd ../MarcusBotBE
npm install
# Configure .env with API keys
npm run dev
```

### 2. Hardware Wiring

Connect external components according to the pin table above.

**OLED Display (I2C)**:
- SDA → D4 (GPIO 6)
- SCL → D5 (GPIO 7) 
- VCC → 3.3V
- GND → GND

**MAX98357A Speaker (I2S)**:
- BCLK → D3 (GPIO 5)
- LRC → D6 (GPIO 8)
- DIN → D7 (GPIO 9)
- VCC → 5V
- GND → GND

### 3. Configuration

Edit `src/main.cpp`:

```cpp
// WiFi Configuration
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";
const char *serverUrl = "http://YOUR_BACKEND_IP:3000/api/process";
```

### 4. Build & Upload

```bash
# Install PlatformIO
pip install platformio

# Build and upload
pio run --target upload

# Monitor serial output
pio device monitor
```

---

## 📊 System Architecture

```
MarcusBot (ESP32 S3) → WiFi → MarcusBotBE (Node.js)
                                    ↓
                              OpenAI GPT-4o
                                    ↓
                              Fish Audio TTS
                                    ↓
                              MP3 Audio Stream
                                    ↓
MarcusBot Speaker ← WiFi ← MarcusBotBE
```

---

## 🎮 Usage

1. **Power on** - Eyes initialize in "happy" mode
2. **Say wake word** - "Marcus" (LED blinks blue)
3. **Speak your query** - Records for 5 seconds
4. **Wait for response** - Processing indicator on display
5. **Listen & Watch** - Audio plays with synchronized eye emotions

---

## 🎭 Emotion System

Supported emotions:
- `happy` - Default, friendly eyes
- `sad` - Droopy, sympathetic eyes
- `angry` - Intense, furrowed eyes
- `surprised` - Wide, shocked eyes
- `skeptical` - Raised eyebrow, questioning
- `frustrated` - Annoyed expression
- `shy` - Bashful, looking away
- `awe` - Wonder, amazed eyes
- `in_love` - Heart eyes, adoring

---

## 🐛 Troubleshooting

### Camera Fails to Initialize
- Check PSRAM is enabled: `-DBOARD_HAS_PSRAM`
- Verify camera pins match XIAO S3 Sense pinout

### No Audio Output
- Verify MAX98357A connections (BCK→GPIO5, WS→GPIO8, DOUT→GPIO9)
- Check audio gain on amplifier
- Monitor serial for MP3 decode errors
- Ensure 5V power supply is adequate

### WiFi Connection Issues
- Verify SSID and password
- Check backend server is running
- Confirm IP address is reachable

### OLED Display Not Working
- Verify I2C address (usually 0x3C)
- Check SDA (GPIO 6) and SCL (GPIO 7) connections
- Ensure Wire library initializes properly
- Monitor serial output for initialization errors

---

## 📚 Dependencies

See `platformio.ini` for full list:
- ESP32 Arduino framework
- ESP32 Camera driver
- Adafruit GFX & SSD1306
- FluxGarage RoboEyes
- ESP8266Audio (MP3 decoder)
- ArduinoJson
- FastLED

---

## 📖 Related Documentation

- Backend API: `../MarcusBotBE/README.md`
- Performance Improvements: `../MarcusBotBE/PERFORMANCE_IMPROVEMENTS.md`
- GPT Model Analysis: `../MarcusBotBE/GPT_MODEL_ANALYSIS.md`

---

## 🔗 Hardware Links

- [XIAO ESP32 S3 Sense](https://www.seeedstudio.com/XIAO-ESP32S3-Sense-p-5639.html)
- [SSD1306 OLED Display](https://www.adafruit.com/product/326)
- [MAX98357A I2S Amplifier](https://www.adafruit.com/product/3006)

---

## 📝 License

MIT License - See LICENSE file for details

---

## 🤝 Contributing

Issues and pull requests welcome!

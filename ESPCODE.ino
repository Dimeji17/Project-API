#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// ========== CHANGE THESE VALUES ==========
const char* ssid = "Dimeji";          
const char* password = "Dimeji_17";   
const char* serverIP = "192.168.109.219";       
// =========================================

const int serverPort = 5000;

// LCD pins
#define SDA_PIN 8
#define SCL_PIN 9

// RS485 pins
#define SSerialRX 16
#define SSerialTX 17
#define SSerialTxControl 4
#define RS485Transmit HIGH
#define RS485Receive LOW
#define Pin13LED 13

// Soil sensor data structure
struct SoilData {
  unsigned int soilHumidity;
  unsigned int soilTemperature;
  unsigned int soilConductivity;
  unsigned int soilPH;
  unsigned int nitrogen;
  unsigned int phosphorus;
  unsigned int potassium;
};

// Global variables
HardwareSerial mySerial(1);
LiquidCrystal_I2C lcd(0x27, 16, 2);
bool wifiConnected = false;
bool lcdConnected = false;
unsigned long lastSensorRead = 0;
unsigned long lastLcdUpdate = 0;
int lcdScreen = 0;
const unsigned long SENSOR_INTERVAL = 30000; // 30 seconds
const unsigned long LCD_UPDATE_INTERVAL = 3000; // 3 seconds

// Current data for LCD display
SoilData currentSensorData = {0, 0, 0, 0, 0, 0, 0};
String lastCropPrediction = "Waiting...";
String lastSoilFertility = "Waiting...";
float lastCropConfidence = 0.0;
float lastSoilConfidence = 0.0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("🌱 ESP32 Crop Prediction Client Starting...");
  
  // Initialize I2C for LCD
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcdConnected = true;
  
  lcd.setCursor(0, 0);
  lcd.print("ESP32 Starting..");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  
  Serial.println("✅ LCD initialized");
  
  // Setup RS485
  mySerial.begin(4800, SERIAL_8N1, SSerialRX, SSerialTX);
  pinMode(SSerialTxControl, OUTPUT);
  pinMode(Pin13LED, OUTPUT);
  digitalWrite(SSerialTxControl, RS485Receive);
  digitalWrite(Pin13LED, LOW);
  
  Serial.println("✅ RS485 setup complete");
  
  if (lcdConnected) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
    lcd.setCursor(0, 1);
    lcd.print("Please wait...");
  }
  
  connectToWiFi();
  
  Serial.println("✅ Setup complete!");
  if (wifiConnected) {
    Serial.println("📡 Server: http://" + String(serverIP) + ":" + String(serverPort));
  }
  
  if (lcdConnected) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ESP32 Ready!");
    lcd.setCursor(0, 1);
    if (wifiConnected) {
      lcd.print("WiFi Connected");
    } else {
      lcd.print("WiFi Failed");
    }
    delay(2000);
  }
  
  lastSensorRead = millis();
  lastLcdUpdate = millis();
}

void loop() {
  yield();
  
  // Update LCD display regularly
  if (lcdConnected && millis() - lastLcdUpdate >= LCD_UPDATE_INTERVAL) {
    updateLCDDisplay();
    lastLcdUpdate = millis();
  }
  
  // Check if it's time to read sensors
  if (millis() - lastSensorRead < SENSOR_INTERVAL) {
    delay(1000);
    return;
  }
  
  checkWiFi();
  
  Serial.println("\n========================================");
  Serial.println("📊 Reading Sensors...");
  
  // Get sensor data
  currentSensorData = getSensorDataSafe();
  
  // Show readings with more debug info
  Serial.println("=== RAW SENSOR VALUES ===");
  Serial.printf("🌡️  Temperature: %d (%.1f°C)\n", currentSensorData.soilTemperature, currentSensorData.soilTemperature / 10.0);
  Serial.printf("💧 Humidity: %d (%.1f%%)\n", currentSensorData.soilHumidity, currentSensorData.soilHumidity / 10.0);
  Serial.printf("🧪 pH: %d (%.1f)\n", currentSensorData.soilPH, currentSensorData.soilPH / 10.0);
  Serial.printf("🟢 Nitrogen: %d mg/kg\n", currentSensorData.nitrogen);
  Serial.printf("🔴 Phosphorus: %d mg/kg\n", currentSensorData.phosphorus);
  Serial.printf("🟡 Potassium: %d mg/kg\n", currentSensorData.potassium);
  Serial.printf("⚡ Conductivity: %d µS/cm\n", currentSensorData.soilConductivity);
  
  // Check if NPK values are all zero and update predictions accordingly
  if (currentSensorData.nitrogen == 0 && currentSensorData.phosphorus == 0 && currentSensorData.potassium == 0) {
    Serial.println("⚠️  All NPK values are 0 - Setting to 'No NPK' status");
    lastCropPrediction = "No NPK";
    lastSoilFertility = "No NPK";
    lastCropConfidence = 0.0;
    lastSoilConfidence = 0.0;
  } else if (wifiConnected) {
    // Send to server if WiFi is connected and we have NPK data
    sendToServerSafe(currentSensorData);
  } else {
    Serial.println("❌ No WiFi - skipping server upload");
  }
  
  lastSensorRead = millis();
  Serial.println("⏰ Next reading in 30 seconds...\n");
}

void updateLCDDisplay() {
  lcd.clear();
  
  switch (lcdScreen) {
    case 0: // Temperature & Humidity
      {
        lcd.setCursor(0, 0);
        lcd.print("Temp:");
        lcd.print(currentSensorData.soilTemperature / 10.0, 1);
        lcd.print("C");
        lcd.setCursor(0, 1);
        lcd.print("Humid:");
        lcd.print(currentSensorData.soilHumidity / 10.0, 1);
        lcd.print("%");
        break;
      }
      
    case 1: // pH and Conductivity
      {
        lcd.setCursor(0, 0);
        lcd.print("pH: ");
        lcd.print(currentSensorData.soilPH / 10.0, 1);
        lcd.setCursor(0, 1);
        lcd.print("Conduct:");
        lcd.print(currentSensorData.soilConductivity);
        break;
      }
      
    case 2: // Nitrogen
      {
        lcd.setCursor(0, 0);
        lcd.print("Nitrogen (N)");
        lcd.setCursor(0, 1);
        lcd.print(currentSensorData.nitrogen);
        lcd.print(" mg/kg");
        break;
      }
      
    case 3: // Phosphorus
      {
        lcd.setCursor(0, 0);
        lcd.print("Phosphorus (P)");
        lcd.setCursor(0, 1);
        lcd.print(currentSensorData.phosphorus);
        lcd.print(" mg/kg");
        break;
      }
      
    case 4: // Potassium
      {
        lcd.setCursor(0, 0);
        lcd.print("Potassium (K)");
        lcd.setCursor(0, 1);
        lcd.print(currentSensorData.potassium);
        lcd.print(" mg/kg");
        break;
      }
      
    case 5: // Crop Prediction - Simple Text
      {
        lcd.setCursor(0, 0);
        lcd.print("Crop: ");
        String cropShort = lastCropPrediction;
        if (cropShort.length() > 10) {
          cropShort = cropShort.substring(0, 10);
        }
        lcd.print(cropShort);
        
        lcd.setCursor(0, 1);
        if (lastCropConfidence > 0) {
          lcd.print("Conf: ");
          lcd.print(lastCropConfidence, 1);
          lcd.print("%");
        } else {
          lcd.print("No prediction");
        }
        break;
      }
      
    case 6: // Soil Fertility - Simple Text
      {
        lcd.setCursor(0, 0);
        lcd.print("Soil: ");
        lcd.print(lastSoilFertility);
        
        lcd.setCursor(0, 1);
        lcd.print("Conf: ");
        lcd.print(lastSoilConfidence, 1);
        lcd.print("%");
        break;
      }
      
    case 7: // WiFi Status
      {
        lcd.setCursor(0, 0);
        if (wifiConnected) {
          lcd.print("WiFi: Connected");
          lcd.setCursor(0, 1);
          String ip = WiFi.localIP().toString();
          if (ip.length() > 16) ip = ip.substring(0, 16);
          lcd.print(ip);
        } else {
          lcd.print("WiFi: Disconn.");
          lcd.setCursor(0, 1);
          lcd.print("Check connection");
        }
        break;
      }
  }
  
  // Move to next screen
  lcdScreen = (lcdScreen + 1) % 8;
}

void connectToWiFi() {
  Serial.print("📡 Connecting to WiFi");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  const int maxAttempts = 30;
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempts++;
    yield();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("✅ WiFi connected!");
    Serial.println("📍 ESP32 IP: " + WiFi.localIP().toString());
    wifiConnected = true;
  } else {
    Serial.println();
    Serial.println("❌ WiFi connection failed!");
    wifiConnected = false;
  }
}

void checkWiFi() {
  if (WiFi.status() != WL_CONNECTED && wifiConnected) {
    Serial.println("📡 WiFi disconnected. Attempting reconnection...");
    wifiConnected = false;
    WiFi.reconnect();
    
    int quickAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && quickAttempts < 10) {
      delay(500);
      quickAttempts++;
      yield();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✅ WiFi reconnected!");
      wifiConnected = true;
    }
  }
}

void sendToServerSafe(SoilData data) {
  HTTPClient http;
  String url = "http://" + String(serverIP) + ":" + String(serverPort) + "/predict";
  
  http.setTimeout(10000); // 10 seconds timeout
  
  if (!http.begin(url)) {
    Serial.println("❌ HTTP client initialization failed");
    return;
  }
  
  http.addHeader("Content-Type", "application/json");
  
  // Create JSON data
  StaticJsonDocument<512> json;
  json["nitrogen"] = data.nitrogen;
  json["phosphorus"] = data.phosphorus;
  json["potassium"] = data.potassium;
  json["ph"] = data.soilPH;
  json["temperature"] = data.soilTemperature;
  json["humidity"] = data.soilHumidity;
  json["rainfall"] = 200;  // Default rainfall
  
  String jsonString;
  serializeJson(json, jsonString);
  
  Serial.println("📤 Sending to server...");
  Serial.println("JSON: " + jsonString);
  
  int responseCode = http.POST(jsonString);
  
  if (responseCode > 0) {
    String response = http.getString();
    
    if (responseCode == 200) {
      Serial.println("✅ Server Response:");
      
      // Parse response
      DynamicJsonDocument responseJson(1024);
      DeserializationError error = deserializeJson(responseJson, response);
      
      if (error) {
        Serial.println("❌ Failed to parse server response");
      } else {
        // Extract predictions
        if (responseJson["predictions"]["recommended_crop"]) {
          lastCropPrediction = responseJson["predictions"]["recommended_crop"].as<String>();
          lastCropConfidence = responseJson["predictions"]["crop_confidence"];
        }
        
        if (responseJson["predictions"]["soil_fertility"]) {
          lastSoilFertility = responseJson["predictions"]["soil_fertility"].as<String>();
          lastSoilConfidence = responseJson["predictions"]["soil_confidence"];
        }
        
        Serial.println("🎯 PREDICTIONS:");
        Serial.printf("   🌾 Crop: %s (%.1f%% sure)\n", lastCropPrediction.c_str(), lastCropConfidence);
        Serial.printf("   🌱 Soil: %s fertility (%.1f%% sure)\n", lastSoilFertility.c_str(), lastSoilConfidence);
        
        // Show recommendations
        if (responseJson["recommendations"]) {
          Serial.println("💡 ADVICE:");
          JsonArray recommendations = responseJson["recommendations"];
          for (size_t i = 0; i < recommendations.size(); i++) {
            Serial.printf("   • %s\n", recommendations[i].as<String>().c_str());
          }
        }
      }
    } else {
      Serial.printf("❌ Server error: %d\n", responseCode);
      Serial.println("Response: " + response);
    }
  } else {
    Serial.printf("❌ Connection failed: %d\n", responseCode);
    Serial.println("   Check if server is running!");
  }
  
  http.end();
}

SoilData getSensorDataSafe() {
  byte queryData[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};
  byte receivedData[19];
  SoilData soilData = {0, 0, 0, 0, 0, 0, 0};

  // Send sensor query
  digitalWrite(Pin13LED, RS485Transmit);
  digitalWrite(SSerialTxControl, RS485Transmit);
  delay(10);
  
  mySerial.write(queryData, sizeof(queryData));
  mySerial.flush();
  
  digitalWrite(Pin13LED, LOW);
  digitalWrite(SSerialTxControl, RS485Receive);

  delay(500);

  int len = mySerial.available();
  if (len > 0) {
    Serial.printf("📡 Received %d bytes from sensor\n", len);
    mySerial.readBytes(receivedData, len);
    
    // Print raw bytes for debugging
    Serial.print("Raw bytes: ");
    for (int i = 0; i < len; i++) {
      Serial.printf("%02X ", receivedData[i]);
    }
    Serial.println();

    if (len >= 17) {
      soilData.soilHumidity    = (receivedData[3] << 8) | receivedData[4];
      soilData.soilTemperature = (receivedData[5] << 8) | receivedData[6];
      soilData.soilConductivity= (receivedData[7] << 8) | receivedData[8];
      soilData.soilPH          = (receivedData[9] << 8) | receivedData[10];
      soilData.nitrogen        = (receivedData[11] << 8) | receivedData[12];
      soilData.phosphorus      = (receivedData[13] << 8) | receivedData[14];
      soilData.potassium       = (receivedData[15] << 8) | receivedData[16];
      
      Serial.println("✅ Valid sensor data received and parsed");
    } else {
      Serial.println("⚠️  Incomplete sensor data received");
    }
  } else {
    Serial.println("⚠️  No sensor response received");
  }

  return soilData;
}
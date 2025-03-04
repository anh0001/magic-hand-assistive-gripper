/*
Testing Instructions:
1. On macOS, open Terminal and run:
   nc 192.168.1.83 9123
2. To test "start": type start and press Enter to reset the game state.
3. To test "finish": reconnect and type finish to receive the current game state.
4. to test "batt": reconnect and type batt to receive the battery percentage and voltage.
Ensure both devices are on the same network and the fixed IP is reachable.
*/

#include <M5StickCPlus2.h>
#include <WiFi.h>  // Added WiFi library

const uint16_t DARKBLUE = 0x001F;  // Custom dark blue color

// Constants for motion detection
const float MOTION_THRESHOLD = 0.5;  // Lower threshold for detecting subtle jerky movements
const int BUZZER_PIN = 2;           // Built-in buzzer pin (G2)
// Removed: const int GRIP_BUTTON = 37;  // No longer needed
const int HOLD_PIN = 4;             // Hold pin for power management

// New PWM definitions for buzzer (ESP32 LEDC API)
const int BUZZER_CHANNEL    = 0;   // PWM channel for buzzer
const int BUZZER_RESOLUTION = 8;   // 8-bit resolution
const int INITIAL_FREQUENCY = 2000; // Initial frequency in Hz

#define PIN_BATTERY_VOLTAGE 38

// Function to read the battery voltage in volts
float readBatteryVoltage() {
    int adcValue = analogRead(PIN_BATTERY_VOLTAGE); // Read raw ADC value (0-4095)
    float voltage = ((float)adcValue / 4095.0) * 4.2 * 2.0;
    return voltage;
}

// Function to map voltage to a battery percentage (3.7V -> 0%, 4.2V -> 100%)
int readBatteryPercentage() {
    float voltage = readBatteryVoltage();
    if (voltage < 3.7) return 0;
    if (voltage > 4.2) return 100;
    return (int)(((voltage - 3.7) / (4.2 - 3.7)) * 100);
}

// ------------------ New Game Variables ------------------
const int MAX_LIFE = 100;
int life = MAX_LIFE;
bool powerOn = true;

// Variables for motion detection
float accX = 0.0F;
float accY = 0.0F;
float accZ = 0.0F;
float prevAccX = 0.0F;
float prevAccY = 0.0F;
float prevAccZ = 0.0F;

// Variables for feedback control
unsigned long lastFeedbackTime = 0;
const unsigned long FEEDBACK_INTERVAL = 50;  // Reduced interval for more frequent audio feedback

// Variable to track when to disable the buzzer tone
unsigned long toneEndTime = 0;

// WiFi and TCP server configuration
const char* ssid = "TLLMS";         // Replace with your WiFi name
const char* password = "mobimobi";  // Replace with your WiFi password
const uint16_t TCP_PORT = 9123;     // TCP server port

WiFiServer tcpServer(TCP_PORT);
WiFiClient tcpClient;

// Add global intensity variable to hold the most recent sensor intensity if needed
float currentIntensity = 0.0F;

// Add fixed IP configuration
IPAddress local_IP(192, 168, 1, 83);   // Set a free IP
IPAddress gateway(192, 168, 1, 1);     // Your router's IP
IPAddress subnet(255, 255, 255, 0);    // Subnet mask

// Optionally, add DNS servers if needed
// IPAddress primaryDNS(8, 8, 8, 8);    
// IPAddress secondaryDNS(8, 8, 4, 4);

// UI helper functions:

// Draws an elegant header with rounded corners and centered title.
void drawHeader() {
  M5.Lcd.fillRoundRect(0, 0, 240, 30, 5, DARKBLUE);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(2);
  
  // Draw centered title
  const char* title = "Magic Hand";
  uint16_t w = M5.Lcd.textWidth(title);
  uint16_t h = M5.Lcd.fontHeight();
  int titleX = (240 - w) / 2;
  int titleY = (30 - h) / 2;
  M5.Lcd.setCursor(titleX, titleY);
  M5.Lcd.print(title);
  
  // Draw battery percentage on the top right corner
  int batteryPercentage = readBatteryPercentage();
  uint16_t batteryColor = (batteryPercentage <= 20) ? RED : GREEN;
  char battStr[10];
  sprintf(battStr, "%d%%", batteryPercentage);
  uint16_t battW = M5.Lcd.textWidth(battStr);
  int battX = 240 - battW - 5;
  int battY = (30 - h) / 2;
  M5.Lcd.setCursor(battX, battY);
  M5.Lcd.setTextColor(batteryColor);
  M5.Lcd.print(battStr);
}

// battery indicator update function.
void updateBatteryIndicator() {
    // Ensure the text size is consistent with initialization.
    M5.Lcd.setTextSize(2);
    uint16_t h = M5.Lcd.fontHeight();
    
    int batteryPercentage = readBatteryPercentage();
    uint16_t batteryColor = (batteryPercentage <= 20) ? RED : GREEN;
    char battStr[10];
    sprintf(battStr, "%d%%", batteryPercentage);

    // Calculate the fixed width using the expected maximum string ("100%")
    uint16_t fixedBattW = M5.Lcd.textWidth("100%");
    int battX = 240 - fixedBattW - 5;
    int battY = (30 - h) / 2;
    
    // Clear the battery area with the fixed width to avoid residual artifacts.
    M5.Lcd.fillRect(battX, battY, fixedBattW, h, DARKBLUE);
    
    // Reposition and print the battery indicator.
    M5.Lcd.setCursor(battX, battY);
    M5.Lcd.setTextColor(batteryColor);
    M5.Lcd.print(battStr);
}

// Modify drawStatus() to show "Game Over" when life is 0.
void drawStatus(float totalDelta, int life) {
  const int boxY = 40, boxHeight = 60;
  uint16_t statusColor;
  const char* statusText;
  if (life <= 0) {
    statusColor = RED;
    statusText = "Game Over";
  } else {
    statusColor = (totalDelta > MOTION_THRESHOLD) ? RED : GREEN;
    statusText = (totalDelta > MOTION_THRESHOLD) ? "Jerky Movement" : "Smooth Movement";
  }
  // ...existing drawing code...
  M5.Lcd.fillRect(0, boxY, 240, boxHeight, BLACK);
  M5.Lcd.fillRoundRect(10, boxY + 10, 220, boxHeight - 20, 5, statusColor);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(2);
  uint16_t w = M5.Lcd.textWidth(statusText);
  uint16_t h = M5.Lcd.fontHeight();
  int textX = 10 + ((220 - w) / 2);
  int textY = boxY + 10 + ((boxHeight - 20 - h) / 2);
  M5.Lcd.setCursor(textX, textY);
  M5.Lcd.print(statusText);
}

// Modified drawFooter() to accept an extra intensity parameter and rearrange the footer layout.
void drawFooter(float accX, float accY, float accZ, int life, float intensity) {
  // Clear footer area (y=115, height 25)
  M5.Lcd.fillRect(0, 115, 240, 25, BLACK);

  // Draw life bar at the top of the footer (y = 115, height = 7)
  int barX = 10, barY = 115, barWidth = 220, barHeight = 7;
  int filledWidth = map(life, 0, MAX_LIFE, 0, barWidth);
  M5.Lcd.drawRect(barX, barY, barWidth, barHeight, WHITE);
  M5.Lcd.fillRect(barX, barY, filledWidth, barHeight, YELLOW);

  // Draw accelerometer readings on the bottom left (at y = 123)
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(5, 123);
  M5.Lcd.printf("X:%.2f Y:%.2f Z:%.2f", accX, accY, accZ);

  // Draw intensity on the bottom right corner
  char intensityStr[20];
  sprintf(intensityStr, "Int: %.2f", intensity);
  uint16_t textW = M5.Lcd.textWidth(intensityStr);
  M5.Lcd.setCursor(240 - textW - 5, 123);
  M5.Lcd.print(intensityStr);
}

// Add new UI timing globals:
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_INTERVAL = 1000;  // Update UI every 1000ms

// Add near other global variables
bool chargingMode = false;  // New flag to indicate charging mode

// Add WiFi management constants
const unsigned long WIFI_TIMEOUT = 10000;  // 10 seconds timeout for WiFi connection
bool wifiConnected = false;                // Track WiFi connection status

// Define device states
enum DeviceState {
  ACTIVE,
  POWER_OFF,
  CHARGING
};

// Global variables
DeviceState currentState = ACTIVE;

void setup() {
    // Initialize the M5StickC Plus2
    M5.begin();
    
    // Uncomment if your board requires explicit speaker initialization
    // M5.Speaker.begin();
    
    // Set hold pin high to maintain power
    pinMode(HOLD_PIN, OUTPUT);
    digitalWrite(HOLD_PIN, HIGH);
    
    // Configure display
    M5.Lcd.setRotation(3);
    M5.Lcd.fillScreen(BLACK);
    
    drawHeader();
    
    // Display startup message in footer area
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(WHITE);
    // M5.Lcd.setCursor(10, 127);
    // M5.Lcd.print("Initializing...");
    
    // Initialize IMU
    M5.Imu.begin();
    
    // ----- Initialize PWM for buzzer -----
    ledcSetup(BUZZER_CHANNEL, INITIAL_FREQUENCY, BUZZER_RESOLUTION);
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);

    // Configure WiFi with fixed IP settings
    if (!WiFi.config(local_IP, gateway, subnet)) {
        Serial.println("STA Failed to configure fixed IP");
    }
    
    // Start WiFi connection with timeout
    WiFi.begin(ssid, password);
    M5.Lcd.setCursor(10, 127);
    M5.Lcd.print("Connecting WiFi...");
    
    unsigned long wifiStartTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStartTime) < WIFI_TIMEOUT) {
        delay(500);
        M5.Lcd.print(".");
    }
    
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (wifiConnected) {
        tcpServer.begin();
        M5.Lcd.fillScreen(BLACK);
        drawHeader();
        M5.Lcd.setTextSize(1);
        M5.Lcd.setCursor(10, 127);
        M5.Lcd.printf("IP: %s\nPort: %d", WiFi.localIP().toString().c_str(), TCP_PORT);
    } else {
        M5.Lcd.fillScreen(BLACK);
        drawHeader();
        M5.Lcd.setTextSize(1);
        M5.Lcd.setCursor(10, 127);
        M5.Lcd.print("WiFi Not Found");
    }
}

// Updated drawChargingMode() implementation:
void drawChargingMode() {
    static int animationFrame = 0;
    static unsigned long lastFrameTime = 0;
    
    // Update animation every 500ms
    if (millis() - lastFrameTime > 500) {
        lastFrameTime = millis();
        animationFrame = (animationFrame + 1) % 4; // 4 animation frames
        
        M5.Lcd.fillScreen(BLACK);

        // Draw centered main title
        M5.Lcd.setTextSize(2);
        M5.Lcd.setTextColor(GREEN);
        const char* title = "CHARGING MODE";
        int titleW = M5.Lcd.textWidth(title);
        M5.Lcd.setCursor((240 - titleW) / 2, 10);
        M5.Lcd.print(title);

        // Draw centered subtitle
        const char* subTitle = "WiFi Disabled";
        int subTitleW = M5.Lcd.textWidth(subTitle);
        M5.Lcd.setCursor((240 - subTitleW) / 2, 35);
        M5.Lcd.print(subTitle);

        // Battery animation box parameters
        int batW = 70;
        int batH = 20;
        int batX = (240 - batW) / 2;
        int batY = 60;
        M5.Lcd.drawRect(batX, batY, batW, batH, GREEN);
        // Draw battery terminal on the right
        M5.Lcd.fillRect(batX + batW, batY + 5, 5, batH - 10, GREEN);

        // Animate battery fill: fill in 4 steps
        int maxFillWidth = batW - 4; // leave margin inside the battery box
        int fillWidth = (maxFillWidth * (animationFrame + 1)) / 4;
        M5.Lcd.fillRect(batX + 2, batY + 2, fillWidth, batH - 4, GREEN);

        // Draw battery percentage below the battery animation
        int batteryPercentage = readBatteryPercentage();
        char battStr[10];
        sprintf(battStr, "%d%%", batteryPercentage);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(WHITE);
        int battTextW = M5.Lcd.textWidth(battStr);
        M5.Lcd.setCursor((240 - battTextW) / 2, batY + batH + 5);
        M5.Lcd.print(battStr);

        // Instruction text at bottom (moved down to avoid overlap)
        const char* instr = "Press button A to exit charging mode";
        int instrW = M5.Lcd.textWidth(instr);
        M5.Lcd.setCursor((240 - instrW) / 2, batY + batH + 20);
        M5.Lcd.print(instr);
    }
}

void loop() {
  M5.update();
  bool btnAPressed = M5.BtnA.wasPressed();

  if (btnAPressed) {
    switch (currentState) {
      case ACTIVE:
        currentState = POWER_OFF;
        powerOn = false;
        // Reset IMU baseline and feedback timer on state change
        M5.Imu.getAccelData(&accX, &accY, &accZ);
        prevAccX = accX; prevAccY = accY; prevAccZ = accZ;
        lastFeedbackTime = millis();
        // Display power off message
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setTextColor(WHITE);
        M5.Lcd.setCursor(60, 60);
        M5.Lcd.print("Power Off");
        break;
      case POWER_OFF:
        currentState = CHARGING;
        chargingMode = true;
        // Reset IMU baseline and feedback timer on state change
        M5.Imu.getAccelData(&accX, &accY, &accZ);
        prevAccX = accX; prevAccY = accY; prevAccZ = accZ;
        lastFeedbackTime = millis();
        // Ensure WiFi is disconnected
        if (WiFi.status() == WL_CONNECTED) {
          WiFi.disconnect(true);
          wifiConnected = false;
        }
        // Show charging mode UI
        drawChargingMode();
        break;
      case CHARGING:
        currentState = ACTIVE;
        chargingMode = false;
        powerOn = true;
        // Reset IMU baseline and feedback timer on state change
        M5.Imu.getAccelData(&accX, &accY, &accZ);
        prevAccX = accX; prevAccY = accY; prevAccZ = accZ;
        lastFeedbackTime = millis();
        // Reconnect WiFi and reinitialize display
        WiFi.begin(ssid, password);
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_TIMEOUT) {
          delay(100);
        }
        if (WiFi.status() == WL_CONNECTED) {
          tcpServer.begin();
          wifiConnected = true;
        } else {
          wifiConnected = false;
        }
        // Reinitialize display
        M5.Lcd.fillScreen(BLACK);
        drawHeader();
        lastDisplayUpdate = millis();
        break;
    }
    return;
  }

  // Act based on current state
  if (currentState == CHARGING) {
    // In charging mode, continuously update the charging UI and skip further processing.
    drawChargingMode();
    delay(50);
    return;
  }
  else if (currentState == POWER_OFF) {
    // In power off state, halt most processing.
    delay(50);
    return;
  }

  // Original loop code for ACTIVE state - remove duplicated button handling
  // Only process TCP if WiFi is connected
  if (wifiConnected) {
    if (tcpServer.hasClient()) {
      tcpClient = tcpServer.available();
    }
    
    if (tcpClient && tcpClient.connected() && tcpClient.available()) {
      String command = tcpClient.readStringUntil('\n');
      command.trim();
      if (command == "start") {
        // Reset game state
        life = MAX_LIFE;
        // Optionally reset intensity and accelerometer data if needed
        accX = accY = accZ = 0.0F;
        currentIntensity = 0.0F;
        tcpClient.println("Game state reset");
      } else if (command == "finish") {
        // Send current game state: life and intensity (here intensity = totalDelta)
        tcpClient.printf("Life: %d, Intensity: %.2f\n", life, currentIntensity);
      } else if (command == "batt") {
        // Read battery voltage and calculate battery percentage
        int batteryPercentage = readBatteryPercentage();
        float batteryVoltage = readBatteryVoltage();
        tcpClient.printf("Battery: %d%%, Voltage: %.2fV\n", batteryPercentage, batteryVoltage);
      }
      tcpClient.stop();
    }
  }

  // ----- Reset Life (BtnB) -----
  if (M5.BtnB.wasPressed()) {
    life = MAX_LIFE;
  }

  // ----- IMU Data & Motion Detection -----
  M5.Imu.getAccelData(&accX, &accY, &accZ);
  float deltaX = abs(accX - prevAccX);
  float deltaY = abs(accY - prevAccY);
  float deltaZ = abs(accZ - prevAccZ);
  float totalDelta = deltaX + deltaY + deltaZ;
  currentIntensity = totalDelta;  // Update intensity for TCP output
  prevAccX = accX; prevAccY = accY; prevAccZ = accZ;

  // If a jerky movement is detected and there is remaining life, trigger beep and decrement life.
  if (totalDelta > MOTION_THRESHOLD && (millis() - lastFeedbackTime >= FEEDBACK_INTERVAL) && life > 0) {
    int beepFreq = map(constrain(totalDelta * 100, 0, 1000), 0, 1000, 500, 2000);
    ledcWriteTone(BUZZER_CHANNEL, beepFreq);
    toneEndTime = millis() + 100;
    lastFeedbackTime = millis();
    int decrement = map(constrain(totalDelta * 100, 0, 1000), 0, 1000, 1, 5);
    life = max(0, life - decrement);
  }

  if (toneEndTime != 0 && millis() >= toneEndTime) {
    ledcWriteTone(BUZZER_CHANNEL, 0);
    toneEndTime = 0;
  }

  // Replace immediate UI updates with a timing check:
  if (millis() - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    updateBatteryIndicator();  // Refresh battery indicator area
    drawStatus(totalDelta, life);
    drawFooter(accX, accY, accZ, life, totalDelta);
    lastDisplayUpdate = millis();
  }

  delay(10);
}

void provideFeedback(int frequency) {
    unsigned long currentTime = millis();
    if (currentTime - lastFeedbackTime >= FEEDBACK_INTERVAL) {
        ledcWriteTone(BUZZER_CHANNEL, frequency);
        toneEndTime = currentTime + 100;
        lastFeedbackTime = currentTime;
    }
}
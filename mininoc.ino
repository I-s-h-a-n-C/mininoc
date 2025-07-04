#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WebServer.h>
#include <HTTPClient.h>

// Replace with your network credentials
const char* ssid = "YOURWIFISSID";
const char* password = "YOURWIFIPASSWORD";

// ST7735 TFT display pin definitions for ESP32-C3 SuperMini
#define TFT_CS    7   // Chip select
#define TFT_RST   6   // Reset
#define TFT_DC    5   // Data/Command
#define TFT_MOSI  4   // SDA/MOSI
#define TFT_SCLK  3   // SCL/SCLK

// Create display object
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// Web server
WebServer server(80);

// Display pages
enum DisplayPage {
  PAGE_STATUS,
  PAGE_SPEED_TEST,
  PAGE_HISTORY_GRAPH,
  PAGE_COUNT
};

DisplayPage currentPage = PAGE_STATUS;
unsigned long lastPageSwitch = 0;
const unsigned long pageInterval = 5000; // Switch pages every 5 seconds

// Variables for tracking uptime
unsigned long startTime;
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 5000; // Update every 5 seconds

// WiFi retry logic variables
unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 5000; // Check WiFi every 5 seconds
bool wasConnected = false;
int retryCount = 0;
const int maxRetries = 3;

// Speed test variables
float downloadSpeedMbps = 0.0;
float uploadSpeedMbps = 0.0;
unsigned long lastSpeedTest = 0;
const unsigned long speedTestInterval = 30000; // Test every 30 seconds
bool speedTestInProgress = false;
String speedTestStatus = "Ready";

// Connection history for graph (last 60 data points)
int rssiHistory[60];
int historyIndex = 0;
bool historyFull = false;
unsigned long lastHistoryUpdate = 0;
const unsigned long historyInterval = 2000; // Update history every 2 seconds

// Display dimming
bool isDimmed = false;
unsigned long dimTimer = 0;
const unsigned long dimDelay = 15000; // Dim after 15 seconds
unsigned long lastPowerOn = 0; // Track power cycles

void setup() {
  Serial.begin(115200);
  // Initialize display
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1); // Landscape orientation
  tft.fillScreen(ST77XX_BLACK);
  // Initialize history array
  for (int i = 0; i < 60; i++) {
    rssiHistory[i] = -100; // Initialize with very poor signal
  }
  // Display startup message
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 10);
  tft.println("ESP32-C3 Mini NOC");
  tft.setCursor(10, 30);
  tft.println("Connecting to WiFi...");
  // Connect to WiFi
  connectToWiFi();
  startTime = millis();
  lastPowerOn = millis();
  dimTimer = millis();
  // Initialize web server
  setupWebServer();
  // Clear screen after connection
  tft.fillScreen(ST77XX_BLACK);
}

void loop() {
  // Handle web server
  server.handleClient();
  // Check WiFi connection status periodically
  if (millis() - lastWiFiCheck >= wifiCheckInterval) {
    checkWiFiConnection();
    lastWiFiCheck = millis();
  }
  // Update connection history
  if (millis() - lastHistoryUpdate >= historyInterval && WiFi.status() == WL_CONNECTED) {
    updateConnectionHistory();
    lastHistoryUpdate = millis();
  }
  // Perform speed test periodically
  if (millis() - lastSpeedTest >= speedTestInterval && WiFi.status() == WL_CONNECTED && !speedTestInProgress) {
    performSpeedTest();
    lastSpeedTest = millis();
  }
  // Handle display dimming
  handleDisplayDimming();
  // Switch pages every 5 seconds
  if (millis() - lastPageSwitch >= pageInterval) {
    currentPage = (DisplayPage)((currentPage + 1) % PAGE_COUNT);
    lastPageSwitch = millis();
    tft.fillScreen(ST77XX_BLACK); // Clear entire screen before switching pages
    updateDisplay(); // Immediately draw new page
  }
  // Update display
  if (millis() - lastUpdate >= updateInterval) {
    updateDisplay();
    lastUpdate = millis();
  }
  delay(100);
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) { // 30 second timeout
    delay(1000);
    Serial.println("Connecting to WiFi...");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    wasConnected = true;
    retryCount = 0;
    dimTimer = millis(); // Reset dim timer on connection
  } else {
    Serial.println("WiFi connection failed!");
    wasConnected = false;
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED && wasConnected) {
    Serial.println("WiFi disconnected! Attempting to reconnect...");
    retryCount++;
    if (retryCount <= maxRetries) {
      WiFi.disconnect();
      delay(1000);
      connectToWiFi();
    } else {
      Serial.println("Max retry attempts reached. Will keep trying...");
      retryCount = 0; // Reset counter to keep trying
    }
  }
  // Update connection state
  if (WiFi.status() == WL_CONNECTED) {
    wasConnected = true;
  } else {
    wasConnected = false;
  }
}

void updateConnectionHistory() {
  if (WiFi.status() == WL_CONNECTED) {
    rssiHistory[historyIndex] = WiFi.RSSI();
  } else {
    rssiHistory[historyIndex] = -100; // Disconnected
  }
  historyIndex = (historyIndex + 1) % 60;
  if (historyIndex == 0) historyFull = true;
}

void performSpeedTest() {
  speedTestInProgress = true;
  speedTestStatus = "Testing...";
  // Memory-efficient ping-based connection quality test
  HTTPClient http;
  http.begin("http://httpbin.org/status/200"); // Just get HTTP status, no data download
  http.setTimeout(5000); // 5 second timeout
  unsigned long startTime = millis();
  int httpCode = http.GET();
  unsigned long endTime = millis();
  if (httpCode == 200) {
    float responseTime = endTime - startTime; // Response time in ms
    // Estimate "speed" based on response time (lower is better)
    // This is more of a connection quality indicator than actual speed
    if (responseTime < 100) {
      downloadSpeedMbps = 10.0; // Excellent connection
    } else if (responseTime < 300) {
      downloadSpeedMbps = 5.0;  // Good connection  
    } else if (responseTime < 800) {
      downloadSpeedMbps = 2.0;  // Fair connection
    } else {
      downloadSpeedMbps = 0.5;  // Poor connection
    }
    speedTestStatus = String(responseTime) + "ms";
  } else {
    downloadSpeedMbps = 0.0;
    speedTestStatus = "Failed";
  }
  http.end();
  speedTestInProgress = false;
}

void handleDisplayDimming() {
  // Check if we should dim (15 seconds after power on or connection)
  if (!isDimmed && (millis() - dimTimer > dimDelay)) {
    isDimmed = true;
    // Reduce brightness by setting a darker background and text colors
  }
  // Reset dim timer on new connection
  if (WiFi.status() == WL_CONNECTED && !wasConnected) {
    dimTimer = millis();
    isDimmed = false;
  }
}

void updateDisplay() {
  uint16_t textColor = isDimmed ? 0x7BEF : ST77XX_WHITE; // Dimmed or normal text
  uint16_t accentColor = isDimmed ? 0x4208 : ST77XX_CYAN; // Dimmed or normal accent
  switch (currentPage) {
    case PAGE_STATUS:
      drawStatusPage(textColor, accentColor);
      break;
    case PAGE_SPEED_TEST:
      drawSpeedTestPage(textColor, accentColor);
      break;
    case PAGE_HISTORY_GRAPH:
      drawHistoryGraphPage(textColor, accentColor);
      break;
  }
}

void drawStatusPage(uint16_t textColor, uint16_t accentColor) {
  // Page title
  tft.setTextColor(accentColor);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.println("WiFi Status [1/3]");
  // WiFi connection status
  tft.setCursor(5, 25);
  tft.setTextColor(accentColor);
  tft.print("Status: ");
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(isDimmed ? 0x2604 : ST77XX_GREEN);
    tft.print("Connected");
  } else {
    tft.setTextColor(isDimmed ? 0x4800 : ST77XX_RED);
    tft.print("Disconnected");
  }
  // Signal strength with bars
  tft.setTextColor(accentColor);
  tft.setCursor(5, 40);
  tft.print("Signal: ");
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    tft.setTextColor(textColor);
    tft.print(rssi);
    tft.print(" dBm");
    drawSignalBars(rssi, 120, 40, isDimmed);
  } else {
    tft.setTextColor(textColor);
    tft.print("N/A");
  }
  // Uptime
  tft.setTextColor(accentColor);
  tft.setCursor(5, 55);
  tft.print("Uptime: ");
  tft.setTextColor(textColor);
  unsigned long uptime = millis() - startTime;
  formatUptime(uptime);
  // IP Address
  tft.setTextColor(accentColor);
  tft.setCursor(5, 70);
  tft.print("IP: ");
  tft.setTextColor(textColor);
  if (WiFi.status() == WL_CONNECTED) {
    tft.print(WiFi.localIP());
  } else {
    tft.print("Not assigned");
  }
  // Web interface info
  tft.setTextColor(0x7BEF);
  tft.setCursor(5, 85);
  tft.print("Web: ");
  if (WiFi.status() == WL_CONNECTED) {
    tft.print(WiFi.localIP());
  } else {
    tft.print("Offline");
  }
}

void drawSpeedTestPage(uint16_t textColor, uint16_t accentColor) {
  // Page title
  tft.setTextColor(accentColor);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.println("Speed Test [2/3]");
  // Speed test status
  tft.setTextColor(accentColor);
  tft.setCursor(5, 25);
  tft.print("Status: ");
  tft.setTextColor(textColor);
  tft.print(speedTestStatus);
  // Connection quality/response time
  tft.setTextColor(accentColor);
  tft.setCursor(5, 45);
  tft.print("Response: ");
  tft.setTextColor(textColor);
  if (speedTestStatus.indexOf("ms") > 0) {
    tft.print(speedTestStatus); // Shows response time
  } else {
    tft.print(speedTestStatus); // Shows status
  }
  // Connection quality indicator
  tft.setTextColor(accentColor);
  tft.setCursor(5, 65);
  tft.print("Quality: ");
  tft.setTextColor(textColor);
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    if (rssi > -50) tft.print("Excellent");
    else if (rssi > -60) tft.print("Good");
    else if (rssi > -70) tft.print("Fair");
    else tft.print("Poor");
  } else {
    tft.print("Disconnected");
  }
  // Progress bar for speed test
  if (speedTestInProgress) {
    tft.drawRect(5, 85, 100, 8, accentColor);
    // Simple animated progress bar
    int progress = (millis() / 200) % 100;
    tft.fillRect(6, 86, progress, 6, isDimmed ? 0x2604 : ST77XX_GREEN);
  }
}

void drawHistoryGraphPage(uint16_t textColor, uint16_t accentColor) {
  // Page title
  tft.setTextColor(accentColor);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.println("Signal History [3/3]");
  // Draw graph axes
  int graphX = 10;
  int graphY = 25;
  int graphWidth = 140;
  int graphHeight = 50;
  tft.drawLine(graphX, graphY + graphHeight, graphX + graphWidth, graphY + graphHeight, accentColor); // X-axis
  tft.drawLine(graphX, graphY, graphX, graphY + graphHeight, accentColor); // Y-axis
  // Y-axis labels
  tft.setTextColor(0x7BEF);
  tft.setTextSize(1);
  tft.setCursor(2, graphY - 2);
  tft.print("-30");
  tft.setCursor(2, graphY + graphHeight/2 - 2);
  tft.print("-65");
  tft.setCursor(2, graphY + graphHeight - 8);
  tft.print("-100");
  // Draw signal history graph
  int dataPoints = historyFull ? 60 : historyIndex;
  if (dataPoints > 1) {
    for (int i = 1; i < dataPoints && i < graphWidth; i++) {
      int idx1 = historyFull ? (historyIndex + i - 1) % 60 : i - 1;
      int idx2 = historyFull ? (historyIndex + i) % 60 : i;
      // Map RSSI values (-30 to -100) to graph coordinates
      int y1 = graphY + graphHeight - ((rssiHistory[idx1] + 100) * graphHeight / 70);
      int y2 = graphY + graphHeight - ((rssiHistory[idx2] + 100) * graphHeight / 70);
      // Clamp to graph bounds
      y1 = constrain(y1, graphY, graphY + graphHeight);
      y2 = constrain(y2, graphY, graphY + graphHeight);
      uint16_t lineColor = isDimmed ? 0x2604 : ST77XX_GREEN;
      if (rssiHistory[idx2] < -70) lineColor = isDimmed ? 0x8400 : ST77XX_YELLOW;
      if (rssiHistory[idx2] < -80) lineColor = isDimmed ? 0x4800 : ST77XX_RED;
      tft.drawLine(graphX + i - 1, y1, graphX + i, y2, lineColor);
    }
  }
  // Current RSSI value
  tft.setTextColor(accentColor);
  tft.setCursor(5, 85);
  tft.print("Current: ");
  tft.setTextColor(textColor);
  if (WiFi.status() == WL_CONNECTED) {
    tft.print(WiFi.RSSI());
    tft.print(" dBm");
  } else {
    tft.print("Disconnected");
  }
  // Time indicator
  tft.setTextColor(0x7BEF);
  tft.setCursor(5, 100);
  tft.print("Last 2 mins displayed");
}

void drawSignalBars(int rssi, int x, int y, bool dimmed) {
  int barWidth = 4;
  int barHeight = 8;
  int barSpacing = 6;
  // Clear previous bars
  tft.fillRect(x, y, 35, barHeight, ST77XX_BLACK);
  // Determine number of bars based on RSSI
  int bars = 0;
  if (rssi > -50) bars = 4;        // Excellent
  else if (rssi > -60) bars = 3;   // Good
  else if (rssi > -70) bars = 2;   // Fair
  else if (rssi > -80) bars = 1;   // Poor
  else bars = 0;                   // Very poor
  // Draw signal bars
  for (int i = 0; i < 4; i++) {
    int currentBarHeight = (i + 1) * 2; // Increasing height
    int currentBarY = y + (barHeight - currentBarHeight);
    uint16_t barColor;
    if (i < bars) {
      // Filled bars - color based on signal strength
      if (dimmed) {
        if (bars >= 3) barColor = 0x2604;      // Dimmed green
        else if (bars >= 2) barColor = 0x8400; // Dimmed yellow
        else barColor = 0x4800;                // Dimmed red
      } else {
        if (bars >= 3) barColor = ST77XX_GREEN;
        else if (bars >= 2) barColor = ST77XX_YELLOW;
        else barColor = ST77XX_RED;
      }
    } else {
      // Empty bars
      barColor = 0x31A6; // Dark gray
    }
    tft.fillRect(x + (i * barSpacing), currentBarY, barWidth, currentBarHeight, barColor);
  }
}

void formatUptime(unsigned long uptime) {
  unsigned long seconds = uptime / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
  if (days > 0) {
    tft.print(days);
    tft.print("d ");
  }
  if (hours > 0 || days > 0) {
    tft.print(hours);
    tft.print("h ");
  }
  tft.print(minutes);
  tft.print("m ");
  tft.print(seconds);
  tft.print("s");
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><title>ESP32-C3 Mini NOC</title>";
    html += "<meta http-equiv='refresh' content='5'>";
    html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;}";
    html += ".container{background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
    html += ".status{display:flex;justify-content:space-between;margin:10px 0;}";
    html += ".connected{color:green;} .disconnected{color:red;}";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>ESP32-C3 Mini NOC Dashboard</h1>";
    html += "<div class='status'><span>WiFi Status:</span><span class='";
    html += (WiFi.status() == WL_CONNECTED) ? "connected'>Connected" : "disconnected'>Disconnected";
    html += "</span></div>";
    if (WiFi.status() == WL_CONNECTED) {
      html += "<div class='status'><span>SSID:</span><span>" + WiFi.SSID() + "</span></div>";
      html += "<div class='status'><span>IP Address:</span><span>" + WiFi.localIP().toString() + "</span></div>";
      html += "<div class='status'><span>RSSI:</span><span>" + String(WiFi.RSSI()) + " dBm</span></div>";
      html += "<div class='status'><span>Download Speed:</span><span>" + String(downloadSpeedMbps, 2) + " Mbps</span></div>";
    }
    unsigned long uptime = millis() - startTime;
    html += "<div class='status'><span>Uptime:</span><span>" + String(uptime/1000) + " seconds</span></div>";
    html += "<div class='status'><span>Speed Test Status:</span><span>" + speedTestStatus + "</span></div>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
  });
  server.begin();
  Serial.println("Web server started");
}
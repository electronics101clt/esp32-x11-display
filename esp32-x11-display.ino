#include <WiFi.h>

const char* ssid = "CarNet1";
const char* password = "password";

WiFiClient x11;
const char* xserver = "192.168.4.2";
const int xport = 6000;

uint32_t rootWindow = 0;
uint32_t rootVisual = 0;
uint32_t windowID = 0;
uint32_t gcID = 0;

// Colors
#define COL_BG        0x001A1A2E  // Dark blue-gray background
#define COL_PANEL     0x0016213E  // Panel background
#define COL_POWER_OFF 0x00AA0000  // Red power
#define COL_POWER_ON  0x0000AA00  // Green power
#define COL_MUTE      0x00FF6600  // Orange mute
#define COL_VOL       0x002E7D32  // Green volume
#define COL_CH        0x001565C0  // Blue channel
#define COL_NUM       0x00424242  // Gray numbers
#define COL_NAV       0x00546E7A  // Blue-gray nav
#define COL_OK        0x00FF8F00  // Amber OK
#define COL_MEDIA     0x006A1B9A  // Purple media
#define COL_RED       0x00D32F2F  // Red
#define COL_GREEN     0x00388E3C  // Green
#define COL_YELLOW    0x00FBC02D  // Yellow
#define COL_BLUE      0x001976D2  // Blue
#define COL_MENU      0x00455A64  // Menu gray
#define COL_SOURCE    0x00795548  // Brown source
#define COL_STATUS_BG 0x00263238  // Status bar bg
#define COL_STATUS_FG 0x0000E676  // Status text (bright green)
#define COL_BORDER_LT 0x00666666  // Light border (3D effect)
#define COL_BORDER_DK 0x00222222  // Dark border (3D effect)

// Button structure
struct Button {
  int16_t x, y;
  uint16_t w, h;
  uint32_t color;
  const char* name;
  const char* label;
};

// Full remote control layout
Button buttons[] = {
  // Row 1: Power, Mute, Source (top row)
  {20, 60, 80, 50, COL_POWER_OFF, "POWER", "PWR"},
  {110, 60, 80, 50, COL_MUTE, "MUTE", "MUTE"},
  {200, 60, 80, 50, COL_SOURCE, "SOURCE", "SRC"},

  // Row 2: Volume and Channel controls
  {20, 130, 60, 50, COL_VOL, "VOL+", "V+"},
  {90, 130, 60, 50, COL_VOL, "VOL-", "V-"},
  {170, 130, 60, 50, COL_CH, "CH+", "C+"},
  {240, 130, 60, 50, COL_CH, "CH-", "C-"},

  // Number pad (3x4 grid) - right side
  {350, 60, 55, 45, COL_NUM, "1", "1"},
  {415, 60, 55, 45, COL_NUM, "2", "2"},
  {480, 60, 55, 45, COL_NUM, "3", "3"},
  {350, 115, 55, 45, COL_NUM, "4", "4"},
  {415, 115, 55, 45, COL_NUM, "5", "5"},
  {480, 115, 55, 45, COL_NUM, "6", "6"},
  {350, 170, 55, 45, COL_NUM, "7", "7"},
  {415, 170, 55, 45, COL_NUM, "8", "8"},
  {480, 170, 55, 45, COL_NUM, "9", "9"},
  {350, 225, 55, 45, COL_NUM, "0", "0"},
  {415, 225, 55, 45, COL_NUM, "PREV", "<-"},
  {480, 225, 55, 45, COL_NUM, "INFO", "i"},

  // Navigation D-pad (center-left area)
  {100, 210, 60, 45, COL_NAV, "UP", "UP"},
  {100, 305, 60, 45, COL_NAV, "DOWN", "DN"},
  {30, 257, 60, 45, COL_NAV, "LEFT", "LT"},
  {170, 257, 60, 45, COL_NAV, "RIGHT", "RT"},
  {100, 257, 60, 45, COL_OK, "OK", "OK"},

  // Menu row (below nav)
  {30, 370, 60, 40, COL_MENU, "MENU", "MNU"},
  {100, 370, 60, 40, COL_MENU, "HOME", "HOM"},
  {170, 370, 60, 40, COL_MENU, "BACK", "BCK"},

  // Media controls
  {350, 290, 55, 40, COL_MEDIA, "REW", "<<"},
  {415, 290, 55, 40, COL_MEDIA, "PLAY", ">"},
  {480, 290, 55, 40, COL_MEDIA, "FF", ">>"},
  {350, 340, 55, 40, COL_MEDIA, "STOP", "[]"},
  {415, 340, 55, 40, COL_MEDIA, "PAUSE", "||"},
  {480, 340, 55, 40, COL_MEDIA, "REC", "O"},

  // Color buttons (bottom)
  {350, 400, 45, 35, COL_RED, "RED", ""},
  {405, 400, 45, 35, COL_GREEN, "GREEN", ""},
  {460, 400, 45, 35, COL_YELLOW, "YELLOW", ""},
  {515, 400, 45, 35, COL_BLUE, "BLUE", ""},

  // Quick buttons
  {560, 60, 55, 45, COL_MENU, "GUIDE", "EPG"},
  {560, 115, 55, 45, COL_MENU, "EXIT", "X"},
};

const int numButtons = sizeof(buttons) / sizeof(Button);

// State
bool powerOn = false;
bool muted = false;
String lastCommand = "Ready";
unsigned long lastCommandTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(ssid, password);

  Serial.println("ESP32 Remote Control v2.0");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Waiting for Android...");

  while (WiFi.softAPgetStationNum() == 0) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nAndroid connected!");
  delay(2000);

  connectToXServer();
}

void connectToXServer() {
  Serial.println("\n=== Connecting to X Server ===");

  if (!x11.connect(xserver, xport)) {
    Serial.println("Failed to connect");
    delay(5000);
    ESP.restart();
    return;
  }

  Serial.println("TCP connected");

  // X11 Setup - 12 bytes required
  uint8_t setup[] = {
    0x6C, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };

  x11.write(setup, sizeof(setup));
  x11.flush();
  delay(500);

  if (!x11.available()) {
    Serial.println("No response");
    return;
  }

  uint8_t status = x11.read();
  if (status != 1) {
    Serial.println("X11 setup failed");
    return;
  }

  x11.read();
  uint16_t protoMajor = x11.read() | (x11.read() << 8);
  uint16_t protoMinor = x11.read() | (x11.read() << 8);
  Serial.printf("X11 %d.%d\n", protoMajor, protoMinor);

  uint16_t additionalLen = x11.read() | (x11.read() << 8);
  int totalBytes = additionalLen * 4;
  uint8_t* data = (uint8_t*)malloc(totalBytes);
  if (!data) return;

  int bytesRead = 0;
  while (bytesRead < totalBytes) {
    if (x11.available()) data[bytesRead++] = x11.read();
    else delay(10);
  }

  uint32_t ridBase = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
  uint16_t vendorLen = data[16] | (data[17] << 8);
  uint8_t numFormats = data[21];

  int vendorPad = (4 - (vendorLen % 4)) % 4;
  int screenOffset = 32 + vendorLen + vendorPad + (numFormats * 8);

  rootWindow = data[screenOffset] | (data[screenOffset+1] << 8) |
               (data[screenOffset+2] << 16) | (data[screenOffset+3] << 24);
  uint32_t rootVisual = data[screenOffset+32] | (data[screenOffset+33] << 8) |
               (data[screenOffset+34] << 16) | (data[screenOffset+35] << 24);

  windowID = ridBase | 0x00000001;
  gcID = ridBase | 0x00000002;

  Serial.printf("Root: 0x%08X\n", rootWindow);
  free(data);

  createGC();
  createWindow(rootVisual);
  mapWindow();
  raiseWindow();

  Serial.println("Waiting for Expose...");
  waitForExpose();
  clearWindow();
  drawUI();

  Serial.println("Remote Control v2.0 Ready!");
}

void createGC() {
  uint8_t req[20];
  req[0] = 55; req[1] = 0; req[2] = 5; req[3] = 0;
  writeU32(&req[4], gcID);
  writeU32(&req[8], rootWindow);
  req[12] = 0x04; req[13] = 0; req[14] = 0; req[15] = 0;
  writeU32(&req[16], 0xFFFFFFFF);
  x11.write(req, 20);
  x11.flush();
}

void createWindow(uint32_t visual) {
  uint8_t req[44];
  req[0] = 1; req[1] = 0; req[2] = 11; req[3] = 0;
  writeU32(&req[4], windowID);
  writeU32(&req[8], rootWindow);
  writeU16(&req[12], 0); writeU16(&req[14], 0);
  writeU16(&req[16], 640); writeU16(&req[18], 480);
  writeU16(&req[20], 0);
  writeU16(&req[22], 1);
  writeU32(&req[24], visual);
  writeU32(&req[28], 0x00000A02);  // bg + override + events
  writeU32(&req[32], COL_BG);      // background
  writeU32(&req[36], 0x00000001);  // override-redirect
  writeU32(&req[40], 0x0000800C);  // Exposure + ButtonPress + ButtonRelease
  x11.write(req, 44);
  x11.flush();
}

void mapWindow() {
  uint8_t req[8];
  req[0] = 8; req[1] = 0; req[2] = 2; req[3] = 0;
  writeU32(&req[4], windowID);
  x11.write(req, 8);
  x11.flush();
  delay(100);
}

void raiseWindow() {
  uint8_t req[16];
  req[0] = 12; req[1] = 0; req[2] = 4; req[3] = 0;
  writeU32(&req[4], windowID);
  writeU32(&req[8], 0x00000040);  // stack-mode
  writeU32(&req[12], 0);          // Above
  x11.write(req, 16);
  x11.flush();
}

void clearWindow() {
  uint8_t req[16];
  req[0] = 61; req[1] = 1; req[2] = 4; req[3] = 0;
  writeU32(&req[4], windowID);
  writeU16(&req[8], 0); writeU16(&req[10], 0);
  writeU16(&req[12], 640); writeU16(&req[14], 480);
  x11.write(req, 16);
  x11.flush();
  delay(50);
}

void waitForExpose() {
  unsigned long start = millis();
  while (millis() - start < 5000) {
    if (x11.available() >= 32) {
      uint8_t code = x11.read();
      for (int i = 0; i < 31; i++) if (x11.available()) x11.read();
      if (code == 12) return;
    }
    delay(10);
  }
}

// Helper functions
void writeU16(uint8_t* buf, uint16_t val) {
  buf[0] = val & 0xFF;
  buf[1] = (val >> 8) & 0xFF;
}

void writeU32(uint8_t* buf, uint32_t val) {
  buf[0] = val & 0xFF;
  buf[1] = (val >> 8) & 0xFF;
  buf[2] = (val >> 16) & 0xFF;
  buf[3] = (val >> 24) & 0xFF;
}

void setColor(uint32_t color) {
  uint8_t req[16];
  req[0] = 56; req[1] = 0; req[2] = 4; req[3] = 0;
  writeU32(&req[4], gcID);
  writeU32(&req[8], 0x00000004);
  writeU32(&req[12], color);
  x11.write(req, 16);
  x11.flush();
}

void fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h) {
  uint8_t req[20];
  req[0] = 70; req[1] = 0; req[2] = 5; req[3] = 0;
  writeU32(&req[4], windowID);
  writeU32(&req[8], gcID);
  writeU16(&req[12], x); writeU16(&req[14], y);
  writeU16(&req[16], w); writeU16(&req[18], h);
  x11.write(req, 20);
  x11.flush();
}

void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
  uint8_t req[20];
  req[0] = 65; req[1] = 0; req[2] = 5; req[3] = 0;  // PolyLine
  writeU32(&req[4], windowID);
  writeU32(&req[8], gcID);
  writeU16(&req[12], x1); writeU16(&req[14], y1);
  writeU16(&req[16], x2); writeU16(&req[18], y2);
  x11.write(req, 20);
  x11.flush();
}

void draw3DButton(int16_t x, int16_t y, uint16_t w, uint16_t h, uint32_t color, bool pressed) {
  // Main button fill
  setColor(color);
  fillRect(x, y, w, h);

  // 3D borders
  if (!pressed) {
    setColor(COL_BORDER_LT);
    fillRect(x, y, w, 2);        // top
    fillRect(x, y, 2, h);        // left
    setColor(COL_BORDER_DK);
    fillRect(x, y + h - 2, w, 2); // bottom
    fillRect(x + w - 2, y, 2, h); // right
  } else {
    setColor(COL_BORDER_DK);
    fillRect(x, y, w, 2);
    fillRect(x, y, 2, h);
    setColor(COL_BORDER_LT);
    fillRect(x, y + h - 2, w, 2);
    fillRect(x + w - 2, y, 2, h);
  }
}

void drawStatusBar() {
  // Status bar background
  setColor(COL_STATUS_BG);
  fillRect(0, 0, 640, 45);

  // Status indicators
  setColor(powerOn ? COL_POWER_ON : COL_POWER_OFF);
  fillRect(10, 12, 20, 20);

  setColor(muted ? COL_MUTE : COL_STATUS_BG);
  fillRect(40, 12, 20, 20);

  // Divider line
  setColor(COL_BORDER_LT);
  fillRect(0, 44, 640, 1);
}

void drawUI() {
  // Draw status bar
  drawStatusBar();

  // Draw all buttons with 3D effect
  for (int i = 0; i < numButtons; i++) {
    uint32_t color = buttons[i].color;
    // Special case for power button
    if (strcmp(buttons[i].name, "POWER") == 0) {
      color = powerOn ? COL_POWER_ON : COL_POWER_OFF;
    }
    draw3DButton(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, color, false);
  }

  // Draw panel separators
  setColor(COL_BORDER_DK);
  fillRect(320, 50, 2, 400);  // Vertical divider

  x11.flush();
}

void updateStatusBar() {
  drawStatusBar();
  x11.flush();
}

void handleButtonPress(int16_t x, int16_t y) {
  for (int i = 0; i < numButtons; i++) {
    if (x >= buttons[i].x && x < buttons[i].x + buttons[i].w &&
        y >= buttons[i].y && y < buttons[i].y + buttons[i].h) {

      const char* name = buttons[i].name;
      Serial.printf("Button: %s\n", name);

      // Visual feedback - pressed state
      uint32_t color = buttons[i].color;
      if (strcmp(name, "POWER") == 0) color = powerOn ? COL_POWER_ON : COL_POWER_OFF;
      draw3DButton(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, color, true);
      x11.flush();

      // Execute command
      executeCommand(name);
      lastCommand = name;
      lastCommandTime = millis();

      // Restore button
      delay(150);
      if (strcmp(name, "POWER") == 0) color = powerOn ? COL_POWER_ON : COL_POWER_OFF;
      draw3DButton(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, color, false);
      x11.flush();

      return;
    }
  }
}

void executeCommand(const char* cmd) {
  Serial.printf(">>> CMD: %s\n", cmd);

  // Handle state changes
  if (strcmp(cmd, "POWER") == 0) {
    powerOn = !powerOn;
    Serial.printf("Power: %s\n", powerOn ? "ON" : "OFF");
    updateStatusBar();
    // Redraw power button with new color
    draw3DButton(buttons[0].x, buttons[0].y, buttons[0].w, buttons[0].h,
                 powerOn ? COL_POWER_ON : COL_POWER_OFF, false);
  }
  else if (strcmp(cmd, "MUTE") == 0) {
    muted = !muted;
    Serial.printf("Mute: %s\n", muted ? "ON" : "OFF");
    updateStatusBar();
  }
  else if (strcmp(cmd, "VOL+") == 0) {
    Serial.println("Volume UP");
  }
  else if (strcmp(cmd, "VOL-") == 0) {
    Serial.println("Volume DOWN");
  }
  else if (strcmp(cmd, "CH+") == 0) {
    Serial.println("Channel UP");
  }
  else if (strcmp(cmd, "CH-") == 0) {
    Serial.println("Channel DOWN");
  }
  else if (cmd[0] >= '0' && cmd[0] <= '9') {
    Serial.printf("Number: %c\n", cmd[0]);
  }
  else if (strcmp(cmd, "PLAY") == 0) {
    Serial.println("Play");
  }
  else if (strcmp(cmd, "PAUSE") == 0) {
    Serial.println("Pause");
  }
  else if (strcmp(cmd, "STOP") == 0) {
    Serial.println("Stop");
  }
  else if (strcmp(cmd, "REW") == 0) {
    Serial.println("Rewind");
  }
  else if (strcmp(cmd, "FF") == 0) {
    Serial.println("Fast Forward");
  }
  else if (strcmp(cmd, "REC") == 0) {
    Serial.println("Record");
  }
  else if (strcmp(cmd, "RED") == 0 || strcmp(cmd, "GREEN") == 0 ||
           strcmp(cmd, "YELLOW") == 0 || strcmp(cmd, "BLUE") == 0) {
    Serial.printf("Color: %s\n", cmd);
  }
  else {
    Serial.printf("Action: %s\n", cmd);
  }

  // Send command via serial for external processing
  Serial.printf("!CMD:%s\n", cmd);
}

void loop() {
  // Handle X11 events
  if (x11.available() >= 32) {
    uint8_t event[32];
    for (int i = 0; i < 32; i++) {
      event[i] = x11.available() ? x11.read() : 0;
    }

    uint8_t code = event[0] & 0x7F;

    if (code == 4) {  // ButtonPress
      int16_t x = event[24] | (event[25] << 8);
      int16_t y = event[26] | (event[27] << 8);
      handleButtonPress(x, y);
    }
    else if (code == 12) {  // Expose
      drawUI();
    }
  }

  // Serial command input
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      executeCommand(cmd.c_str());
    }
  }

  // Connection check
  if (!x11.connected()) {
    Serial.println("Disconnected");
    delay(3000);
    ESP.restart();
  }

  delay(10);
}

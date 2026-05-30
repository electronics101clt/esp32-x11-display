#include <WiFi.h>

const char* ssid = "CarNet1";
const char* password = "password";

WiFiClient x11;
const char* xserver = "192.168.4.2";
const int xport = 6000;

uint32_t rootWindow = 0;
uint32_t rootVisual = 0;
uint32_t windowID = 0x01000001;
uint32_t gcID = 0x01000002;
uint32_t cursorID = 0x01000003;
uint32_t cursorFont = 0;

// Remote control button structure
struct Button {
  int16_t x, y;
  uint16_t w, h;
  uint32_t color;
  const char* name;
};

// Define remote control buttons
Button buttons[] = {
  // Power button at top
  {20, 20, 200, 80, 0x00FF0000, "POWER"},

  // Volume controls
  {20, 120, 90, 60, 0x0000FF00, "VOL+"},
  {130, 120, 90, 60, 0x0000FF00, "VOL-"},

  // Channel controls
  {20, 200, 90, 60, 0x000088FF, "CH+"},
  {130, 200, 90, 60, 0x000088FF, "CH-"},

  // Number pad (3x4 grid)
  {250, 20, 60, 60, 0x00888888, "1"},
  {330, 20, 60, 60, 0x00888888, "2"},
  {410, 20, 60, 60, 0x00888888, "3"},

  {250, 100, 60, 60, 0x00888888, "4"},
  {330, 100, 60, 60, 0x00888888, "5"},
  {410, 100, 60, 60, 0x00888888, "6"},

  {250, 180, 60, 60, 0x00888888, "7"},
  {330, 180, 60, 60, 0x00888888, "8"},
  {410, 180, 60, 60, 0x00888888, "9"},

  {250, 260, 60, 60, 0x00888888, "0"},

  // Navigation controls
  {550, 100, 60, 60, 0x00FFFF00, "UP"},
  {550, 180, 60, 60, 0x00FFFF00, "DOWN"},
  {490, 140, 60, 60, 0x00FFFF00, "LEFT"},
  {610, 140, 60, 60, 0x00FFFF00, "RIGHT"},
  {550, 140, 60, 60, 0x00FF8800, "OK"}
};

const int numButtons = sizeof(buttons) / sizeof(Button);

// Command queue for bidirectional communication
String commandQueue[10];
int queueHead = 0;
int queueTail = 0;

void setup() {
  Serial.begin(9600);
  delay(1000);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(ssid, password);

  Serial.println("ESP32 Remote Control");
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

  Serial.println("TCP connected to X server");

  // X11 Setup - 12 bytes required
  uint8_t setup[] = {
    0x6C, 0x00,  // 'l' = little endian, pad
    0x0B, 0x00,  // major version 11
    0x00, 0x00,  // minor version 0
    0x00, 0x00,  // auth proto name length
    0x00, 0x00,  // auth proto data length
    0x00, 0x00   // pad (required!)
  };

  x11.write(setup, sizeof(setup));
  x11.flush();

  delay(500);

  if (!x11.available()) {
    Serial.println("No response from X server");
    return;
  }

  // Read setup response
  uint8_t status = x11.read();
  if (status != 1) {
    Serial.println("X11 setup failed");
    return;
  }

  x11.read();

  uint16_t protoMajor = x11.read() | (x11.read() << 8);
  uint16_t protoMinor = x11.read() | (x11.read() << 8);
  Serial.printf("X11 Protocol: %d.%d\n", protoMajor, protoMinor);

  uint16_t additionalLen = x11.read() | (x11.read() << 8);
  int totalBytes = additionalLen * 4;
  uint8_t* setupData = (uint8_t*)malloc(totalBytes);
  if (!setupData) {
    Serial.println("Failed to allocate setup buffer");
    return;
  }

  int bytesRead = 0;
  while (bytesRead < totalBytes) {
    if (x11.available()) {
      setupData[bytesRead++] = x11.read();
    } else {
      delay(10);
    }
  }

  // Parse setup data
  uint32_t ridBase = setupData[4] | (setupData[5] << 8) | (setupData[6] << 16) | (setupData[7] << 24);
  uint16_t vendorLen = setupData[16] | (setupData[17] << 8);
  uint8_t numFormats = setupData[21];

  int vendorPad = (4 - (vendorLen % 4)) % 4;
  int screenOffset = 32 + vendorLen + vendorPad + (numFormats * 8);

  rootWindow = setupData[screenOffset] | (setupData[screenOffset+1] << 8) |
               (setupData[screenOffset+2] << 16) | (setupData[screenOffset+3] << 24);
  rootVisual = setupData[screenOffset+32] | (setupData[screenOffset+33] << 8) |
               (setupData[screenOffset+34] << 16) | (setupData[screenOffset+35] << 24);

  windowID = ridBase | 0x00000001;
  gcID = ridBase | 0x00000002;
  cursorID = ridBase | 0x00000003;
  cursorFont = ridBase | 0x00000004;

  Serial.printf("Root window: 0x%08X\n", rootWindow);
  free(setupData);

  createGC();
  createWindow();
  mapWindow();

  Serial.println("Waiting for Expose...");
  waitForExpose();

  clearWindow();
  drawRemoteControl();
  Serial.println("Remote Control Ready!");
}

void createGC() {
  uint8_t req[20];
  req[0] = 55; req[1] = 0; req[2] = 5; req[3] = 0;

  req[4] = gcID & 0xFF;
  req[5] = (gcID >> 8) & 0xFF;
  req[6] = (gcID >> 16) & 0xFF;
  req[7] = (gcID >> 24) & 0xFF;

  req[8] = rootWindow & 0xFF;
  req[9] = (rootWindow >> 8) & 0xFF;
  req[10] = (rootWindow >> 16) & 0xFF;
  req[11] = (rootWindow >> 24) & 0xFF;

  req[12] = 0x04; req[13] = 0x00; req[14] = 0x00; req[15] = 0x00;
  req[16] = 0xFF; req[17] = 0xFF; req[18] = 0xFF; req[19] = 0xFF;

  x11.write(req, 20);
  x11.flush();
  Serial.println("Created GC");
}

void createWindow() {
  uint8_t req[44];
  req[0] = 1; req[1] = 0; req[2] = 11; req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  req[8] = rootWindow & 0xFF;
  req[9] = (rootWindow >> 8) & 0xFF;
  req[10] = (rootWindow >> 16) & 0xFF;
  req[11] = (rootWindow >> 24) & 0xFF;

  req[12] = 0; req[13] = 0;
  req[14] = 0; req[15] = 0;

  req[16] = 0x00; req[17] = 0x04;  // width = 1024
  req[18] = 0x58; req[19] = 0x02;  // height = 600

  req[20] = 0; req[21] = 0;
  req[22] = 1; req[23] = 0;

  req[24] = rootVisual & 0xFF;
  req[25] = (rootVisual >> 8) & 0xFF;
  req[26] = (rootVisual >> 16) & 0xFF;
  req[27] = (rootVisual >> 24) & 0xFF;

  // Value mask: background + override-redirect + event-mask
  // 0x0A02 = bits 1, 9, 11
  req[28] = 0x02; req[29] = 0x0A; req[30] = 0x00; req[31] = 0x00;

  // Background: dark gray 0x00202020
  req[32] = 0x20; req[33] = 0x20; req[34] = 0x20; req[35] = 0x00;

  // Override-redirect: true
  req[36] = 0x01; req[37] = 0x00; req[38] = 0x00; req[39] = 0x00;

  // Event mask: Exposure (0x8000) + ButtonPress (0x0004) + ButtonRelease (0x0008)
  // Total: 0x800C
  req[40] = 0x0C; req[41] = 0x80; req[42] = 0x00; req[43] = 0x00;

  x11.write(req, 44);
  x11.flush();
  Serial.println("Created window");
}

void mapWindow() {
  uint8_t req[8];
  req[0] = 8; req[1] = 0; req[2] = 2; req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  x11.write(req, 8);
  x11.flush();
  Serial.println("Mapped window");

  delay(100);
  raiseWindow();
}

void raiseWindow() {
  // ConfigureWindow request - raise window to top (above XSDL instruction screen)
  uint8_t req[16];
  req[0] = 12;  // opcode: ConfigureWindow
  req[1] = 0;
  req[2] = 4;   // length: 4 words
  req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  // Value mask: 0x0040 = stack-mode (bit 6)
  req[8] = 0x40;
  req[9] = 0x00;
  req[10] = 0x00;
  req[11] = 0x00;

  // Stack mode: 0 = Above (raise to top)
  req[12] = 0x00;
  req[13] = 0x00;
  req[14] = 0x00;
  req[15] = 0x00;

  x11.write(req, 16);
  x11.flush();
  Serial.println("Raised window to top");
}

void clearWindow() {
  // ClearArea request
  uint8_t req[16];
  req[0] = 61;  // opcode: ClearArea
  req[1] = 1;   // exposures = true
  req[2] = 4;   // length: 4 words
  req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  req[8] = 0; req[9] = 0;    // x = 0
  req[10] = 0; req[11] = 0;  // y = 0
  req[12] = 0x00; req[13] = 0x04;  // width = 1024
  req[14] = 0x58; req[15] = 0x02;  // height = 600

  x11.write(req, 16);
  x11.flush();
  Serial.println("Cleared window");
  delay(100);
}

void waitForExpose() {
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    if (x11.available() >= 32) {
      uint8_t eventCode = x11.read();

      if (eventCode == 0) {
        uint8_t errorCode = x11.read();
        Serial.printf("X11 ERROR: code=%d\n", errorCode);
        for (int i = 0; i < 30; i++) if (x11.available()) x11.read();
        continue;
      }

      if (eventCode == 12) {
        for (int i = 0; i < 31; i++) if (x11.available()) x11.read();
        Serial.println("Received Expose");
        return;
      }

      for (int i = 0; i < 31; i++) if (x11.available()) x11.read();
    }
    delay(10);
  }
  Serial.println("Timeout - continuing anyway");
}

void setGCForeground(uint32_t color) {
  uint8_t req[16];
  req[0] = 56; req[1] = 0; req[2] = 4; req[3] = 0;

  req[4] = gcID & 0xFF;
  req[5] = (gcID >> 8) & 0xFF;
  req[6] = (gcID >> 16) & 0xFF;
  req[7] = (gcID >> 24) & 0xFF;

  req[8] = 0x04; req[9] = 0x00; req[10] = 0x00; req[11] = 0x00;

  req[12] = color & 0xFF;
  req[13] = (color >> 8) & 0xFF;
  req[14] = (color >> 16) & 0xFF;
  req[15] = (color >> 24) & 0xFF;

  x11.write(req, 16);
  x11.flush();
}

void fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h) {
  uint8_t req[20];
  req[0] = 70; req[1] = 0; req[2] = 5; req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  req[8] = gcID & 0xFF;
  req[9] = (gcID >> 8) & 0xFF;
  req[10] = (gcID >> 16) & 0xFF;
  req[11] = (gcID >> 24) & 0xFF;

  req[12] = x & 0xFF; req[13] = (x >> 8) & 0xFF;
  req[14] = y & 0xFF; req[15] = (y >> 8) & 0xFF;
  req[16] = w & 0xFF; req[17] = (w >> 8) & 0xFF;
  req[18] = h & 0xFF; req[19] = (h >> 8) & 0xFF;

  x11.write(req, 20);
  x11.flush();
}

void drawRemoteControl() {
  Serial.println("Drawing remote control...");

  for (int i = 0; i < numButtons; i++) {
    setGCForeground(buttons[i].color);
    fillRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h);
  }

  Serial.println("Remote control drawn!");
}

void handleButtonPress(int16_t x, int16_t y) {
  // Find which button was pressed
  for (int i = 0; i < numButtons; i++) {
    if (x >= buttons[i].x && x < buttons[i].x + buttons[i].w &&
        y >= buttons[i].y && y < buttons[i].y + buttons[i].h) {

      Serial.printf("Button pressed: %s at (%d, %d)\n", buttons[i].name, x, y);

      // Visual feedback - brighten button
      uint32_t brightColor = buttons[i].color | 0x00404040;
      setGCForeground(brightColor);
      fillRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h);
      x11.flush();

      // Execute command
      executeCommand(buttons[i].name);

      // Restore original color after 200ms
      delay(200);
      setGCForeground(buttons[i].color);
      fillRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h);
      x11.flush();

      return;
    }
  }

  Serial.printf("Click outside buttons: (%d, %d)\n", x, y);
}

void executeCommand(const char* cmd) {
  Serial.printf(">>> COMMAND: %s\n", cmd);

  // Add command to queue for bidirectional communication
  if ((queueTail + 1) % 10 != queueHead) {
    commandQueue[queueTail] = cmd;
    queueTail = (queueTail + 1) % 10;
  }

  // Execute actions based on command
  if (strcmp(cmd, "POWER") == 0) {
    Serial.println("Action: Toggle power");
  } else if (strcmp(cmd, "VOL+") == 0) {
    Serial.println("Action: Volume up");
  } else if (strcmp(cmd, "VOL-") == 0) {
    Serial.println("Action: Volume down");
  } else if (strcmp(cmd, "CH+") == 0) {
    Serial.println("Action: Channel up");
  } else if (strcmp(cmd, "CH-") == 0) {
    Serial.println("Action: Channel down");
  } else if (strcmp(cmd, "OK") == 0) {
    Serial.println("Action: Select/OK");
  } else if (cmd[0] >= '0' && cmd[0] <= '9') {
    Serial.printf("Action: Number %c\n", cmd[0]);
  } else if (strcmp(cmd, "UP") == 0 || strcmp(cmd, "DOWN") == 0 ||
             strcmp(cmd, "LEFT") == 0 || strcmp(cmd, "RIGHT") == 0) {
    Serial.printf("Action: Navigate %s\n", cmd);
  }
}

void loop() {
  // Handle X11 events
  if (x11.available() >= 32) {
    uint8_t event[32];
    for (int i = 0; i < 32; i++) {
      event[i] = x11.available() ? x11.read() : 0;
    }

    uint8_t code = event[0] & 0x7F;

    if (code == 0) {
      Serial.printf("X11 ERROR: %d\n", event[1]);
    } else if (code == 4) {
      // ButtonPress event
      int16_t x = event[24] | (event[25] << 8);
      int16_t y = event[26] | (event[27] << 8);
      handleButtonPress(x, y);
    } else if (code == 12) {
      // Expose - redraw
      Serial.println("Redrawing...");
      drawRemoteControl();
    }
  }

  // Check for commands from serial (bidirectional)
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial.printf("Serial command received: %s\n", cmd.c_str());
      executeCommand(cmd.c_str());
    }
  }

  if (!x11.connected()) {
    Serial.println("Disconnected - restarting");
    delay(5000);
    ESP.restart();
  }

  delay(10);
}

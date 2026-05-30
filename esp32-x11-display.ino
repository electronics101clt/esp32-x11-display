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

// Track if we need to redraw
bool needsRedraw = false;

void setup() {
  Serial.begin(9600);
  delay(1000);

  // Configure hotspot
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(ssid, password);

  Serial.println("ESP32 Hotspot Started");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Waiting for Android to connect...");

  // Wait for Android
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

  // X11 Setup - byte order 'l' (little endian), protocol 11.0, no auth
  uint8_t setup[] = {
    0x6C, 0x00,  // 'l' = little endian, pad
    0x0B, 0x00,  // major version 11
    0x00, 0x00,  // minor version 0
    0x00, 0x00,  // auth proto name length
    0x00, 0x00,  // auth proto data length
    0x00, 0x00   // pad
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
  Serial.print("Setup status: ");
  Serial.println(status);

  if (status != 1) {
    Serial.println("X11 setup failed");
    return;
  }

  // Skip unused byte
  x11.read();

  // Read protocol version
  uint16_t protoMajor = x11.read() | (x11.read() << 8);
  uint16_t protoMinor = x11.read() | (x11.read() << 8);
  Serial.printf("X11 Protocol: %d.%d\n", protoMajor, protoMinor);

  // Read additional data length
  uint16_t additionalLen = x11.read() | (x11.read() << 8);
  Serial.printf("Additional data: %d words (%d bytes)\n", additionalLen, additionalLen * 4);

  // Read the entire additional data into buffer
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
  Serial.printf("Read %d bytes of setup data\n", bytesRead);

  // Parse setup data according to X11 protocol spec
  uint32_t ridBase = setupData[4] | (setupData[5] << 8) | (setupData[6] << 16) | (setupData[7] << 24);
  uint32_t ridMask = setupData[8] | (setupData[9] << 8) | (setupData[10] << 16) | (setupData[11] << 24);
  Serial.printf("Resource ID base: 0x%08X mask: 0x%08X\n", ridBase, ridMask);

  uint16_t vendorLen = setupData[16] | (setupData[17] << 8);
  uint8_t numFormats = setupData[21];
  Serial.printf("Vendor length: %d, Formats: %d\n", vendorLen, numFormats);

  // Calculate screen offset
  int vendorPad = (4 - (vendorLen % 4)) % 4;
  int screenOffset = 32 + vendorLen + vendorPad + (numFormats * 8);
  Serial.printf("Screen data at offset: %d\n", screenOffset);

  // Read root window
  rootWindow = setupData[screenOffset] | (setupData[screenOffset+1] << 8) |
               (setupData[screenOffset+2] << 16) | (setupData[screenOffset+3] << 24);
  Serial.printf("Root window: 0x%08X\n", rootWindow);

  // Root visual at offset +32
  rootVisual = setupData[screenOffset+32] | (setupData[screenOffset+33] << 8) |
               (setupData[screenOffset+34] << 16) | (setupData[screenOffset+35] << 24);
  Serial.printf("Root visual: 0x%08X\n", rootVisual);

  uint8_t rootDepth = setupData[screenOffset+38];
  Serial.printf("Root depth: %d\n", rootDepth);

  // Use resource ID base for our IDs
  windowID = ridBase | 0x00000001;
  gcID = ridBase | 0x00000002;
  cursorID = ridBase | 0x00000003;
  cursorFont = ridBase | 0x00000004;
  Serial.printf("Using window ID: 0x%08X\n", windowID);

  free(setupData);

  Serial.println("X11 setup complete!");

  // Setup desktop environment
  openCursorFont();
  createCursor();
  setupRootWindow();

  // Create window and GC
  createGC();
  createWindow();

  // Set window title for window manager
  setWMName("ESP32 Display");

  mapWindow();

  // Wait for initial Expose event
  Serial.println("Waiting for Expose event...");
  if (waitForExpose()) {
    drawContent();
  }
}

void openCursorFont() {
  const char* fontName = "cursor";
  uint8_t nameLen = strlen(fontName);
  uint8_t pad = (4 - (nameLen % 4)) % 4;
  uint8_t reqLen = 12 + nameLen + pad;

  uint8_t req[32];
  req[0] = 45;  // opcode: OpenFont
  req[1] = 0;
  req[2] = (reqLen / 4) & 0xFF;
  req[3] = 0;

  req[4] = cursorFont & 0xFF;
  req[5] = (cursorFont >> 8) & 0xFF;
  req[6] = (cursorFont >> 16) & 0xFF;
  req[7] = (cursorFont >> 24) & 0xFF;

  req[8] = nameLen & 0xFF;
  req[9] = (nameLen >> 8) & 0xFF;
  req[10] = 0;
  req[11] = 0;

  for (int i = 0; i < nameLen; i++) {
    req[12 + i] = fontName[i];
  }
  for (int i = 0; i < pad; i++) {
    req[12 + nameLen + i] = 0;
  }

  x11.write(req, reqLen);
  x11.flush();
  Serial.println("Opened cursor font");
  delay(100);
}

void createCursor() {
  uint8_t req[32];
  req[0] = 94;  // opcode: CreateGlyphCursor
  req[1] = 0;
  req[2] = 8;   // length: 8 words
  req[3] = 0;

  req[4] = cursorID & 0xFF;
  req[5] = (cursorID >> 8) & 0xFF;
  req[6] = (cursorID >> 16) & 0xFF;
  req[7] = (cursorID >> 24) & 0xFF;

  req[8] = cursorFont & 0xFF;
  req[9] = (cursorFont >> 8) & 0xFF;
  req[10] = (cursorFont >> 16) & 0xFF;
  req[11] = (cursorFont >> 24) & 0xFF;

  req[12] = cursorFont & 0xFF;
  req[13] = (cursorFont >> 8) & 0xFF;
  req[14] = (cursorFont >> 16) & 0xFF;
  req[15] = (cursorFont >> 24) & 0xFF;

  req[16] = 68; req[17] = 0;  // source char (left_ptr)
  req[18] = 69; req[19] = 0;  // mask char

  // Foreground (black)
  req[20] = 0x00; req[21] = 0x00;
  req[22] = 0x00; req[23] = 0x00;
  req[24] = 0x00; req[25] = 0x00;

  // Background (white)
  req[26] = 0xFF; req[27] = 0xFF;
  req[28] = 0xFF; req[29] = 0xFF;
  req[30] = 0xFF; req[31] = 0xFF;

  x11.write(req, 32);
  x11.flush();
  Serial.println("Created cursor");
  delay(100);
}

void setupRootWindow() {
  uint8_t req[16];
  req[0] = 2;   // opcode: ChangeWindowAttributes
  req[1] = 0;
  req[2] = 4;   // length: 4 words
  req[3] = 0;

  req[4] = rootWindow & 0xFF;
  req[5] = (rootWindow >> 8) & 0xFF;
  req[6] = (rootWindow >> 16) & 0xFF;
  req[7] = (rootWindow >> 24) & 0xFF;

  // Value mask: 0x4000 = cursor (bit 14)
  req[8] = 0x00;
  req[9] = 0x40;
  req[10] = 0x00;
  req[11] = 0x00;

  req[12] = cursorID & 0xFF;
  req[13] = (cursorID >> 8) & 0xFF;
  req[14] = (cursorID >> 16) & 0xFF;
  req[15] = (cursorID >> 24) & 0xFF;

  x11.write(req, 16);
  x11.flush();
  Serial.println("Set cursor on root window");
  delay(100);
}

void createGC() {
  uint8_t req[20];
  req[0] = 55;  // opcode: CreateGC
  req[1] = 0;
  req[2] = 5;   // length: 5 words
  req[3] = 0;

  req[4] = gcID & 0xFF;
  req[5] = (gcID >> 8) & 0xFF;
  req[6] = (gcID >> 16) & 0xFF;
  req[7] = (gcID >> 24) & 0xFF;

  req[8] = rootWindow & 0xFF;
  req[9] = (rootWindow >> 8) & 0xFF;
  req[10] = (rootWindow >> 16) & 0xFF;
  req[11] = (rootWindow >> 24) & 0xFF;

  // Value mask: foreground (bit 2)
  req[12] = 0x04;
  req[13] = 0x00;
  req[14] = 0x00;
  req[15] = 0x00;

  // Foreground color (white)
  req[16] = 0xFF;
  req[17] = 0xFF;
  req[18] = 0xFF;
  req[19] = 0xFF;

  x11.write(req, 20);
  x11.flush();
  Serial.println("Created GC");
}

void createWindow() {
  // CreateWindow with background-pixel, backing-store, and event-mask
  // NO override-redirect - let window manager handle decorations
  uint8_t req[44];
  req[0] = 1;   // opcode: CreateWindow
  req[1] = 0;   // depth: CopyFromParent
  req[2] = 11;  // length: 11 words
  req[3] = 0;

  // Window ID
  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  // Parent (root)
  req[8] = rootWindow & 0xFF;
  req[9] = (rootWindow >> 8) & 0xFF;
  req[10] = (rootWindow >> 16) & 0xFF;
  req[11] = (rootWindow >> 24) & 0xFF;

  // x, y position (let WM decide, but start at 0,0)
  req[12] = 0; req[13] = 0;
  req[14] = 0; req[15] = 0;

  // width=1024, height=600
  req[16] = 0x00; req[17] = 0x04;  // 1024
  req[18] = 0x58; req[19] = 0x02;  // 600

  // border width = 1 (visible border)
  req[20] = 1; req[21] = 0;

  // class: InputOutput (1)
  req[22] = 1; req[23] = 0;

  // visual: copy from parent
  req[24] = rootVisual & 0xFF;
  req[25] = (rootVisual >> 8) & 0xFF;
  req[26] = (rootVisual >> 16) & 0xFF;
  req[27] = (rootVisual >> 24) & 0xFF;

  // Value mask: 0x0842
  // bit 1 (0x0002): background-pixel
  // bit 6 (0x0040): backing-store
  // bit 11 (0x0800): event-mask
  req[28] = 0x42;
  req[29] = 0x08;
  req[30] = 0x00;
  req[31] = 0x00;

  // Values in bit order (low to high):

  // 1. background-pixel (bit 1) = dark gray
  req[32] = 0x40;
  req[33] = 0x40;
  req[34] = 0x40;
  req[35] = 0x00;

  // 2. backing-store (bit 6) = WhenMapped (1)
  req[36] = 0x01;
  req[37] = 0x00;
  req[38] = 0x00;
  req[39] = 0x00;

  // 3. event-mask (bit 11) = ExposureMask (0x8000) + StructureNotifyMask (0x20000)
  req[40] = 0x00;
  req[41] = 0x80;
  req[42] = 0x02;
  req[43] = 0x00;

  x11.write(req, 44);
  x11.flush();
  Serial.printf("Created managed window 0x%08X (with backing-store)\n", windowID);
}

void setWMName(const char* name) {
  // ChangeProperty request (opcode 18) to set WM_NAME
  uint8_t nameLen = strlen(name);
  uint8_t pad = (4 - (nameLen % 4)) % 4;
  uint8_t reqLen = 24 + nameLen + pad;  // header + data

  uint8_t req[64];
  req[0] = 18;  // opcode: ChangeProperty
  req[1] = 0;   // mode: Replace
  req[2] = (reqLen / 4) & 0xFF;
  req[3] = (reqLen / 4) >> 8;

  // Window
  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  // Property atom: WM_NAME = 39
  req[8] = 39;
  req[9] = 0;
  req[10] = 0;
  req[11] = 0;

  // Type atom: STRING = 31
  req[12] = 31;
  req[13] = 0;
  req[14] = 0;
  req[15] = 0;

  // Format: 8 (bytes)
  req[16] = 8;
  req[17] = 0;
  req[18] = 0;
  req[19] = 0;

  // Length of data (in format units, so bytes here)
  req[20] = nameLen;
  req[21] = 0;
  req[22] = 0;
  req[23] = 0;

  // The name string
  for (int i = 0; i < nameLen; i++) {
    req[24 + i] = name[i];
  }
  for (int i = 0; i < pad; i++) {
    req[24 + nameLen + i] = 0;
  }

  x11.write(req, reqLen);
  x11.flush();
  Serial.printf("Set WM_NAME to '%s'\n", name);
}

void mapWindow() {
  uint8_t req[8];
  req[0] = 8;   // opcode: MapWindow
  req[1] = 0;
  req[2] = 2;   // length: 2 words
  req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  x11.write(req, 8);
  x11.flush();
  Serial.println("Mapped window (waiting for WM to show it)");
}

bool waitForExpose() {
  unsigned long startTime = millis();
  while (millis() - startTime < 10000) {  // 10 second timeout
    if (x11.available() >= 32) {
      uint8_t eventCode = x11.read();

      if (eventCode == 0) {  // X11 Error
        uint8_t errorCode = x11.read();
        uint16_t seqNum = x11.read() | (x11.read() << 8);
        Serial.printf("X11 ERROR: code=%d seq=%d\n", errorCode, seqNum);
        for (int i = 0; i < 28; i++) x11.read();
        continue;
      }

      // Drain rest of event
      uint8_t eventData[31];
      for (int i = 0; i < 31; i++) {
        eventData[i] = x11.available() ? x11.read() : 0;
      }

      if (eventCode == 12) {  // Expose
        Serial.println("Received Expose event!");
        return true;
      } else if (eventCode == 19) {  // MapNotify
        Serial.println("Window mapped by WM");
      } else if (eventCode == 22) {  // ConfigureNotify
        Serial.println("Window configured by WM");
      } else {
        Serial.printf("Event: %d\n", eventCode);
      }
    }
    delay(10);
  }
  Serial.println("Timeout waiting for Expose");
  return false;
}

void setGCForeground(uint32_t color) {
  uint8_t req[16];
  req[0] = 56;  // opcode: ChangeGC
  req[1] = 0;
  req[2] = 4;   // length: 4 words
  req[3] = 0;

  req[4] = gcID & 0xFF;
  req[5] = (gcID >> 8) & 0xFF;
  req[6] = (gcID >> 16) & 0xFF;
  req[7] = (gcID >> 24) & 0xFF;

  req[8] = 0x04;
  req[9] = 0x00;
  req[10] = 0x00;
  req[11] = 0x00;

  req[12] = color & 0xFF;
  req[13] = (color >> 8) & 0xFF;
  req[14] = (color >> 16) & 0xFF;
  req[15] = (color >> 24) & 0xFF;

  x11.write(req, 16);
  x11.flush();
}

void fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h) {
  uint8_t req[20];
  req[0] = 70;  // opcode: PolyFillRectangle
  req[1] = 0;
  req[2] = 5;   // length: 5 words
  req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  req[8] = gcID & 0xFF;
  req[9] = (gcID >> 8) & 0xFF;
  req[10] = (gcID >> 16) & 0xFF;
  req[11] = (gcID >> 24) & 0xFF;

  req[12] = x & 0xFF;
  req[13] = (x >> 8) & 0xFF;
  req[14] = y & 0xFF;
  req[15] = (y >> 8) & 0xFF;
  req[16] = w & 0xFF;
  req[17] = (w >> 8) & 0xFF;
  req[18] = h & 0xFF;
  req[19] = (h >> 8) & 0xFF;

  x11.write(req, 20);
  x11.flush();
}

void drawContent() {
  Serial.println("Drawing content...");

  // Draw colored rectangles
  setGCForeground(0x00FF0000);  // Red
  fillRect(50, 50, 200, 200);

  setGCForeground(0x0000FF00);  // Green
  fillRect(150, 100, 200, 200);

  setGCForeground(0x000000FF);  // Blue
  fillRect(250, 150, 200, 200);

  // Draw a yellow rectangle
  setGCForeground(0x00FFFF00);  // Yellow
  fillRect(350, 200, 200, 200);

  // Draw white border around window content area
  setGCForeground(0x00FFFFFF);  // White
  fillRect(0, 0, 1024, 5);      // Top
  fillRect(0, 595, 1024, 5);    // Bottom
  fillRect(0, 0, 5, 600);       // Left
  fillRect(1019, 0, 5, 600);    // Right

  Serial.println("Drawing complete!");
}

void loop() {
  // Handle X11 events
  if (x11.available() >= 32) {
    uint8_t code = x11.read();

    // Read rest of event
    uint8_t eventData[31];
    for (int i = 0; i < 31; i++) {
      eventData[i] = x11.available() ? x11.read() : 0;
    }

    if (code == 0) {
      // X11 Error
      uint8_t errorCode = eventData[0];
      uint16_t seqNum = eventData[1] | (eventData[2] << 8);
      Serial.printf("X11 ERROR: code=%d seq=%d\n", errorCode, seqNum);
    }
    else if (code == 12) {
      // Expose event - need to redraw
      // eventData[0] = window (4 bytes), [4-5] = x, [6-7] = y, [8-9] = width, [10-11] = height, [12-13] = count
      uint16_t count = eventData[12] | (eventData[13] << 8);
      Serial.printf("Expose event (count=%d)\n", count);

      // Only redraw when count reaches 0 (last expose in series)
      if (count == 0) {
        drawContent();
      }
    }
    else if (code == 22) {
      // ConfigureNotify - window was moved/resized
      Serial.println("ConfigureNotify - window reconfigured");
    }
    else if (code == 19) {
      // MapNotify
      Serial.println("MapNotify - window is now visible");
    }
    else if (code == 18) {
      // UnmapNotify
      Serial.println("UnmapNotify - window is now hidden");
    }
    else {
      Serial.printf("Event: 0x%02X\n", code);
    }
  }

  if (!x11.connected()) {
    Serial.println("X11 disconnected - restarting");
    delay(5000);
    ESP.restart();
  }

  delay(10);
}

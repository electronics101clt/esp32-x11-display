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

void setup() {
  Serial.begin(9600);
  delay(1000);

  // Configure hotspot - Android needs static IP 192.168.4.8 configured manually
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(ssid, password);

  Serial.println("ESP32 Hotspot Started");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Waiting for Android to connect...");
  Serial.println("Configure Android WiFi with static IP 192.168.4.8");

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
  // Offset 0-3: release number
  // Offset 4-7: resource-id-base
  uint32_t ridBase = setupData[4] | (setupData[5] << 8) | (setupData[6] << 16) | (setupData[7] << 24);
  // Offset 8-11: resource-id-mask
  uint32_t ridMask = setupData[8] | (setupData[9] << 8) | (setupData[10] << 16) | (setupData[11] << 24);
  Serial.printf("Resource ID base: 0x%08X mask: 0x%08X\n", ridBase, ridMask);

  // Offset 16-17: vendor length
  uint16_t vendorLen = setupData[16] | (setupData[17] << 8);
  // Offset 21: number of FORMATs
  uint8_t numFormats = setupData[21];
  Serial.printf("Vendor length: %d, Formats: %d\n", vendorLen, numFormats);

  // Calculate screen offset
  // Fixed header = 32 bytes, then vendor string (padded to 4 bytes), then formats (8 bytes each)
  int vendorPad = (4 - (vendorLen % 4)) % 4;
  int screenOffset = 32 + vendorLen + vendorPad + (numFormats * 8);
  Serial.printf("Screen data at offset: %d\n", screenOffset);

  // Read root window (first 4 bytes of screen structure)
  rootWindow = setupData[screenOffset] | (setupData[screenOffset+1] << 8) |
               (setupData[screenOffset+2] << 16) | (setupData[screenOffset+3] << 24);
  Serial.printf("Root window: 0x%08X\n", rootWindow);

  // Root visual is at offset +32 from screen start
  rootVisual = setupData[screenOffset+32] | (setupData[screenOffset+33] << 8) |
               (setupData[screenOffset+34] << 16) | (setupData[screenOffset+35] << 24);
  Serial.printf("Root visual: 0x%08X\n", rootVisual);

  // Root depth is at offset +38 from screen start
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

  // Now create window and GC
  createGC();
  createWindow();
  mapWindow();

  // Wait for Expose event before drawing
  Serial.println("Waiting for Expose event...");
  waitForExpose();

  // Clear window area to ensure it's ready
  clearWindow();

  drawTest();
}

void openCursorFont() {
  // OpenFont request to open the standard cursor font
  const char* fontName = "cursor";
  uint8_t nameLen = strlen(fontName);
  uint8_t pad = (4 - (nameLen % 4)) % 4;
  uint8_t reqLen = 12 + nameLen + pad;

  uint8_t req[32];
  req[0] = 45;  // opcode: OpenFont
  req[1] = 0;   // pad
  req[2] = (reqLen / 4) & 0xFF;  // length in 4-byte units
  req[3] = 0;

  // Font ID (use the ID set from ridBase in connectToXServer)
  req[4] = cursorFont & 0xFF;
  req[5] = (cursorFont >> 8) & 0xFF;
  req[6] = (cursorFont >> 16) & 0xFF;
  req[7] = (cursorFont >> 24) & 0xFF;

  // Name length
  req[8] = nameLen & 0xFF;
  req[9] = (nameLen >> 8) & 0xFF;
  req[10] = 0;  // pad
  req[11] = 0;  // pad

  // Font name
  for (int i = 0; i < nameLen; i++) {
    req[12 + i] = fontName[i];
  }

  // Padding
  for (int i = 0; i < pad; i++) {
    req[12 + nameLen + i] = 0;
  }

  x11.write(req, reqLen);
  x11.flush();
  Serial.println("Opened cursor font");
  delay(100);
}

void createCursor() {
  // CreateGlyphCursor request - creates cursor from font glyphs
  uint8_t req[32];
  req[0] = 94;  // opcode: CreateGlyphCursor
  req[1] = 0;   // pad
  req[2] = 8;   // length: 8 words
  req[3] = 0;

  // Cursor ID
  req[4] = cursorID & 0xFF;
  req[5] = (cursorID >> 8) & 0xFF;
  req[6] = (cursorID >> 16) & 0xFF;
  req[7] = (cursorID >> 24) & 0xFF;

  // Source font (cursor font)
  req[8] = cursorFont & 0xFF;
  req[9] = (cursorFont >> 8) & 0xFF;
  req[10] = (cursorFont >> 16) & 0xFF;
  req[11] = (cursorFont >> 24) & 0xFF;

  // Mask font (same as source)
  req[12] = cursorFont & 0xFF;
  req[13] = (cursorFont >> 8) & 0xFF;
  req[14] = (cursorFont >> 16) & 0xFF;
  req[15] = (cursorFont >> 24) & 0xFF;

  // Source char (left_ptr = 68)
  req[16] = 68;
  req[17] = 0;

  // Mask char (left_ptr mask = 69)
  req[18] = 69;
  req[19] = 0;

  // Foreground RGB (black)
  req[20] = 0x00; req[21] = 0x00;  // red
  req[22] = 0x00; req[23] = 0x00;  // green
  req[24] = 0x00; req[25] = 0x00;  // blue

  // Background RGB (white)
  req[26] = 0xFF; req[27] = 0xFF;  // red
  req[28] = 0xFF; req[29] = 0xFF;  // green
  req[30] = 0xFF; req[31] = 0xFF;  // blue

  x11.write(req, 32);
  x11.flush();
  Serial.println("Created cursor (left_ptr)");
  delay(100);
}

void setupRootWindow() {
  // ChangeWindowAttributes - set cursor on root window
  uint8_t req[16];
  req[0] = 2;   // opcode: ChangeWindowAttributes
  req[1] = 0;   // pad
  req[2] = 4;   // length: 4 words (header + mask + 1 value)
  req[3] = 0;

  // Root window ID
  req[4] = rootWindow & 0xFF;
  req[5] = (rootWindow >> 8) & 0xFF;
  req[6] = (rootWindow >> 16) & 0xFF;
  req[7] = (rootWindow >> 24) & 0xFF;

  // Value mask: 0x4000 = cursor (bit 14)
  req[8] = 0x00;
  req[9] = 0x40;
  req[10] = 0x00;
  req[11] = 0x00;

  // Cursor ID
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
  // CreateGC request with foreground color
  uint8_t req[20];
  req[0] = 55;  // opcode: CreateGC
  req[1] = 0;   // pad
  req[2] = 5;   // length: 5 words (increased for color value)
  req[3] = 0;

  // GC ID
  req[4] = gcID & 0xFF;
  req[5] = (gcID >> 8) & 0xFF;
  req[6] = (gcID >> 16) & 0xFF;
  req[7] = (gcID >> 24) & 0xFF;

  // Drawable (root window)
  req[8] = rootWindow & 0xFF;
  req[9] = (rootWindow >> 8) & 0xFF;
  req[10] = (rootWindow >> 16) & 0xFF;
  req[11] = (rootWindow >> 24) & 0xFF;

  // Value mask (0x00000004 = foreground)
  req[12] = 0x04;
  req[13] = 0x00;
  req[14] = 0x00;
  req[15] = 0x00;

  // Foreground color (white = 0xFFFFFFFF)
  req[16] = 0xFF;
  req[17] = 0xFF;
  req[18] = 0xFF;
  req[19] = 0xFF;

  x11.write(req, 20);
  x11.flush();
  Serial.println("Created GC with white foreground");
}

void createWindow() {
  // CreateWindow request with background, override-redirect, and event mask
  uint8_t req[44];
  req[0] = 1;   // opcode: CreateWindow
  req[1] = 0;   // depth: 0 = CopyFromParent (inherit from root)
  req[2] = 11;  // length: 11 words (background + override-redirect + event mask)
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

  // x, y position
  req[12] = 0; req[13] = 0;  // x = 0
  req[14] = 0; req[15] = 0;  // y = 0

  // width, height - fullscreen 1024x600
  req[16] = 0x00; req[17] = 0x04;  // width = 1024
  req[18] = 0x58; req[19] = 0x02;  // height = 600

  // border width
  req[20] = 0; req[21] = 0;  // no border

  // class: InputOutput (1)
  req[22] = 1; req[23] = 0;

  // visual: copy from parent
  req[24] = rootVisual & 0xFF;
  req[25] = (rootVisual >> 8) & 0xFF;
  req[26] = (rootVisual >> 16) & 0xFF;
  req[27] = (rootVisual >> 24) & 0xFF;

  // value mask: 0x0A02 = background-pixel (bit 1) + override-redirect (bit 9) + event-mask (bit 11)
  req[28] = 0x02;
  req[29] = 0x0A;
  req[30] = 0x00;
  req[31] = 0x00;

  // Values in order of bits (low to high):
  // Background pixel (black = 0x00000000)
  req[32] = 0x00;
  req[33] = 0x00;
  req[34] = 0x00;
  req[35] = 0x00;

  // Override-redirect: 1 = true (bypass window manager)
  req[36] = 0x01;
  req[37] = 0x00;
  req[38] = 0x00;
  req[39] = 0x00;

  // Event mask: 0x8000 = ExposureMask (bit 15)
  req[40] = 0x00;
  req[41] = 0x80;
  req[42] = 0x00;
  req[43] = 0x00;

  x11.write(req, 44);
  x11.flush();
  Serial.printf("Created window 0x%08X (override-redirect, black background)\n", windowID);
}

void waitForExpose() {
  // Wait for Expose event (event code 12)
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {  // 5 second timeout
    if (x11.available() >= 32) {  // Events/errors are 32 bytes
      uint8_t eventCode = x11.read();

      if (eventCode == 0) {  // X11 Error
        uint8_t errorCode = x11.read();
        uint16_t seqNum = x11.read() | (x11.read() << 8);
        Serial.printf("X11 ERROR: code=%d seq=%d\n", errorCode, seqNum);
        // Drain rest (28 bytes)
        for (int i = 0; i < 28; i++) {
          if (x11.available()) x11.read();
        }
        continue;
      }

      if (eventCode == 12) {  // Expose event
        // Drain rest of event (31 more bytes)
        for (int i = 0; i < 31; i++) {
          if (x11.available()) x11.read();
        }
        Serial.println("Received Expose event - window is visible!");
        return;
      }

      // Drain rest of unknown event
      for (int i = 0; i < 31; i++) {
        if (x11.available()) x11.read();
      }

      Serial.printf("Received event code: %d\n", eventCode);
    }
    delay(10);
  }
  Serial.println("Timeout waiting for Expose event");
}

void raiseWindow() {
  // ConfigureWindow request - raise window to top
  uint8_t req[16];
  req[0] = 12;  // opcode: ConfigureWindow
  req[1] = 0;   // pad
  req[2] = 4;   // length: 4 words
  req[3] = 0;

  // Window ID
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

void mapWindow() {
  // MapWindow request
  uint8_t req[8];
  req[0] = 8;   // opcode: MapWindow
  req[1] = 0;   // pad
  req[2] = 2;   // length: 2 words
  req[3] = 0;

  // Window ID
  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  x11.write(req, 8);
  x11.flush();
  Serial.println("Mapped window");

  delay(100);

  // Raise window above instruction screen
  raiseWindow();
}

void clearWindow() {
  // ClearArea request - exposures = true to generate Expose events
  uint8_t req[16];
  req[0] = 61;  // opcode: ClearArea
  req[1] = 1;   // exposures = true
  req[2] = 4;   // length: 4 words
  req[3] = 0;

  // Window ID
  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  // x, y position (0, 0)
  req[8] = 0; req[9] = 0;
  req[10] = 0; req[11] = 0;

  // width, height (1024, 600)
  req[12] = 0x00; req[13] = 0x04;
  req[14] = 0x58; req[15] = 0x02;

  x11.write(req, 16);
  x11.flush();
  Serial.println("Cleared window area");
  delay(100);
}

void setGCForeground(uint32_t color) {
  // ChangeGC request - opcode 56
  uint8_t req[16];
  req[0] = 56;  // opcode: ChangeGC
  req[1] = 0;   // pad
  req[2] = 4;   // length: 4 words
  req[3] = 0;

  // GC ID
  req[4] = gcID & 0xFF;
  req[5] = (gcID >> 8) & 0xFF;
  req[6] = (gcID >> 16) & 0xFF;
  req[7] = (gcID >> 24) & 0xFF;

  // Value mask (0x04 = foreground)
  req[8] = 0x04;
  req[9] = 0x00;
  req[10] = 0x00;
  req[11] = 0x00;

  // Foreground color
  req[12] = color & 0xFF;
  req[13] = (color >> 8) & 0xFF;
  req[14] = (color >> 16) & 0xFF;
  req[15] = (color >> 24) & 0xFF;

  x11.write(req, 16);
  x11.flush();
}

void drawTest() {
  Serial.println("Drawing content...");

  // Draw red rectangle on our window
  setGCForeground(0x00FF0000);  // Red
  uint8_t req[20];
  req[0] = 70;  // opcode: PolyFillRectangle
  req[1] = 0;   // pad
  req[2] = 5;   // length: 5 words
  req[3] = 0;

  // Drawable (our window, not root)
  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  // GC
  req[8] = gcID & 0xFF;
  req[9] = (gcID >> 8) & 0xFF;
  req[10] = (gcID >> 16) & 0xFF;
  req[11] = (gcID >> 24) & 0xFF;

  // Red rectangle
  req[12] = 50; req[13] = 0;    // x = 50
  req[14] = 50; req[15] = 0;    // y = 50
  req[16] = 200; req[17] = 0;   // width = 200
  req[18] = 200; req[19] = 0;   // height = 200

  x11.write(req, 20);
  x11.flush();
  Serial.println("Drew red rectangle");

  delay(100);

  // Draw green rectangle
  setGCForeground(0x0000FF00);  // Green
  req[12] = 100; req[13] = 0;   // x = 100
  req[14] = 100; req[15] = 0;   // y = 100
  req[16] = 200; req[17] = 0;   // width = 200
  req[18] = 200; req[19] = 0;   // height = 200

  x11.write(req, 20);
  x11.flush();
  Serial.println("Drew green rectangle");

  delay(100);

  // Draw blue rectangle
  setGCForeground(0x000000FF);  // Blue
  req[12] = 150; req[13] = 0;   // x = 150
  req[14] = 150; req[15] = 0;   // y = 150
  req[16] = 200; req[17] = 0;   // width = 200
  req[18] = 200; req[19] = 0;   // height = 200

  x11.write(req, 20);
  x11.flush();
  Serial.println("Drew blue rectangle");

  Serial.println("All drawing complete!");
}

void loop() {
  // Handle X11 events and errors
  if (x11.available() >= 32) {
    uint8_t code = x11.read();

    if (code == 0) {
      // X11 Error
      uint8_t errorCode = x11.read();
      uint16_t seqNum = x11.read() | (x11.read() << 8);
      Serial.printf("X11 ERROR in loop: code=%d seq=%d\n", errorCode, seqNum);
      for (int i = 0; i < 28; i++) x11.read();
    } else {
      Serial.printf("X11 event: 0x%02X\n", code);
      for (int i = 0; i < 31; i++) x11.read();
    }
  }

  if (!x11.connected()) {
    Serial.println("X11 disconnected - restarting");
    delay(5000);
    ESP.restart();
  }

  delay(100);
}

#include <WiFi.h>

const char* ssid = "CarNet1";
const char* password = "password";

WiFiClient x11;
const char* xserver = "192.168.4.2";
const int xport = 6000;

// X11 server resources
uint32_t rootWindow = 0;
uint32_t rootVisual = 0;
uint32_t ridBase = 0;

// Our resources
uint32_t windowID = 0;
uint32_t gcID = 0;

// Atoms (obtained via InternAtom)
uint32_t atomWmProtocols = 0;
uint32_t atomWmDeleteWindow = 0;

// Window dimensions
uint16_t winWidth = 800;
uint16_t winHeight = 480;

// Sequence number for tracking requests
uint16_t seqNum = 0;

void setup() {
  Serial.begin(9600);
  delay(1000);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(ssid, password);

  Serial.println("ESP32 X11 Application");
  Serial.print("Hotspot IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Waiting for X server to connect...");

  while (WiFi.softAPgetStationNum() == 0) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nClient connected!");
  delay(2000);

  connectToXServer();
}

void connectToXServer() {
  Serial.println("\n=== X11 Connection ===");

  if (!x11.connect(xserver, xport)) {
    Serial.println("Failed to connect to X server");
    delay(5000);
    ESP.restart();
    return;
  }

  Serial.println("TCP connected");

  // X11 connection setup
  uint8_t setup[] = {
    0x6C, 0x00,  // little endian
    0x0B, 0x00,  // protocol 11.0
    0x00, 0x00,  // no auth name
    0x00, 0x00,  // no auth data
    0x00, 0x00   // pad
  };
  x11.write(setup, sizeof(setup));
  x11.flush();
  seqNum = 0;

  delay(500);

  if (!x11.available()) {
    Serial.println("No response from X server");
    return;
  }

  // Read setup response
  uint8_t status = x11.read();
  if (status != 1) {
    Serial.printf("X11 setup failed: %d\n", status);
    return;
  }
  x11.read(); // skip pad

  uint16_t protoMajor = x11.read() | (x11.read() << 8);
  uint16_t protoMinor = x11.read() | (x11.read() << 8);
  Serial.printf("X11 Protocol %d.%d\n", protoMajor, protoMinor);

  uint16_t additionalLen = x11.read() | (x11.read() << 8);
  int totalBytes = additionalLen * 4;

  uint8_t* data = (uint8_t*)malloc(totalBytes);
  if (!data) {
    Serial.println("Memory allocation failed");
    return;
  }

  int bytesRead = 0;
  while (bytesRead < totalBytes) {
    if (x11.available()) {
      data[bytesRead++] = x11.read();
    } else {
      delay(10);
    }
  }

  // Parse setup data
  ridBase = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
  uint16_t vendorLen = data[16] | (data[17] << 8);
  uint8_t numFormats = data[21];

  int vendorPad = (4 - (vendorLen % 4)) % 4;
  int screenOffset = 32 + vendorLen + vendorPad + (numFormats * 8);

  rootWindow = data[screenOffset] | (data[screenOffset+1] << 8) |
               (data[screenOffset+2] << 16) | (data[screenOffset+3] << 24);
  rootVisual = data[screenOffset+32] | (data[screenOffset+33] << 8) |
               (data[screenOffset+34] << 16) | (data[screenOffset+35] << 24);

  Serial.printf("Root window: 0x%08X\n", rootWindow);
  Serial.printf("Resource base: 0x%08X\n", ridBase);

  free(data);

  // Allocate our resource IDs
  windowID = ridBase | 0x00000001;
  gcID = ridBase | 0x00000002;

  // Get required atoms for ICCCM compliance
  atomWmProtocols = internAtom("WM_PROTOCOLS", false);
  atomWmDeleteWindow = internAtom("WM_DELETE_WINDOW", false);
  Serial.printf("WM_PROTOCOLS atom: %d\n", atomWmProtocols);
  Serial.printf("WM_DELETE_WINDOW atom: %d\n", atomWmDeleteWindow);

  // Create graphics context
  createGC();

  // Create main window
  createWindow();

  // Set ICCCM properties for window manager
  setWmName("ESP32 App");
  setWmClass("esp32app", "ESP32App");
  setWmHints();
  setWmProtocols();

  // Map (show) the window
  mapWindow();

  Serial.println("Window created - waiting for Expose event...");

  // Wait for initial expose
  if (waitForExpose()) {
    Serial.println("Window is ready for drawing!");
  }
}

uint32_t internAtom(const char* name, bool onlyIfExists) {
  // InternAtom request (opcode 16)
  uint8_t nameLen = strlen(name);
  uint8_t pad = (4 - (nameLen % 4)) % 4;
  uint8_t reqLen = 8 + nameLen + pad;

  uint8_t req[64];
  req[0] = 16;  // opcode
  req[1] = onlyIfExists ? 1 : 0;
  req[2] = (reqLen / 4) & 0xFF;
  req[3] = (reqLen / 4) >> 8;
  req[4] = nameLen & 0xFF;
  req[5] = (nameLen >> 8) & 0xFF;
  req[6] = 0;
  req[7] = 0;

  for (int i = 0; i < nameLen; i++) {
    req[8 + i] = name[i];
  }
  for (int i = 0; i < pad; i++) {
    req[8 + nameLen + i] = 0;
  }

  x11.write(req, reqLen);
  x11.flush();
  seqNum++;

  // Wait for reply
  unsigned long start = millis();
  while (millis() - start < 2000) {
    if (x11.available() >= 32) {
      uint8_t reply[32];
      for (int i = 0; i < 32; i++) {
        reply[i] = x11.read();
      }

      if (reply[0] == 1) {  // Reply
        uint32_t atom = reply[8] | (reply[9] << 8) | (reply[10] << 16) | (reply[11] << 24);
        return atom;
      } else if (reply[0] == 0) {  // Error
        Serial.printf("InternAtom error: %d\n", reply[1]);
        return 0;
      }
    }
    delay(10);
  }
  Serial.println("InternAtom timeout");
  return 0;
}

void createGC() {
  uint8_t req[16];
  req[0] = 55;  // CreateGC
  req[1] = 0;
  req[2] = 4;   // 4 words (no values)
  req[3] = 0;

  req[4] = gcID & 0xFF;
  req[5] = (gcID >> 8) & 0xFF;
  req[6] = (gcID >> 16) & 0xFF;
  req[7] = (gcID >> 24) & 0xFF;

  req[8] = rootWindow & 0xFF;
  req[9] = (rootWindow >> 8) & 0xFF;
  req[10] = (rootWindow >> 16) & 0xFF;
  req[11] = (rootWindow >> 24) & 0xFF;

  // No value mask - use defaults
  req[12] = 0;
  req[13] = 0;
  req[14] = 0;
  req[15] = 0;

  x11.write(req, 16);
  x11.flush();
  seqNum++;
  Serial.println("Created GC");
}

void createWindow() {
  // CreateWindow with full event mask for input
  uint8_t req[44];
  req[0] = 1;   // CreateWindow
  req[1] = 0;   // depth: CopyFromParent
  req[2] = 11;  // length: 11 words
  req[3] = 0;

  // Window ID
  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  // Parent
  req[8] = rootWindow & 0xFF;
  req[9] = (rootWindow >> 8) & 0xFF;
  req[10] = (rootWindow >> 16) & 0xFF;
  req[11] = (rootWindow >> 24) & 0xFF;

  // Position (x=0, y=0 - let WM decide)
  req[12] = 0; req[13] = 0;
  req[14] = 0; req[15] = 0;

  // Size
  req[16] = winWidth & 0xFF;
  req[17] = (winWidth >> 8) & 0xFF;
  req[18] = winHeight & 0xFF;
  req[19] = (winHeight >> 8) & 0xFF;

  // Border width
  req[20] = 0; req[21] = 0;

  // Class: InputOutput
  req[22] = 1; req[23] = 0;

  // Visual: CopyFromParent (0)
  req[24] = 0;
  req[25] = 0;
  req[26] = 0;
  req[27] = 0;

  // Value mask: 0x0842
  // bit 1: background-pixel
  // bit 6: backing-store
  // bit 11: event-mask
  req[28] = 0x42;
  req[29] = 0x08;
  req[30] = 0x00;
  req[31] = 0x00;

  // Background pixel (light gray = 0xD0D0D0)
  req[32] = 0xD0;
  req[33] = 0xD0;
  req[34] = 0xD0;
  req[35] = 0x00;

  // Backing store = WhenMapped (1)
  req[36] = 0x01;
  req[37] = 0x00;
  req[38] = 0x00;
  req[39] = 0x00;

  // Event mask: comprehensive for app input
  // KeyPress(0) | KeyRelease(1) | ButtonPress(2) | ButtonRelease(3) |
  // PointerMotion(6) | Exposure(15) | StructureNotify(17) | FocusChange(21)
  // = 0x22804F
  req[40] = 0x4F;
  req[41] = 0x80;
  req[42] = 0x22;
  req[43] = 0x00;

  x11.write(req, 44);
  x11.flush();
  seqNum++;
  Serial.printf("Created window 0x%08X (%dx%d)\n", windowID, winWidth, winHeight);
}

void setWmName(const char* name) {
  // ChangeProperty for WM_NAME (atom 39)
  uint8_t nameLen = strlen(name);
  uint8_t pad = (4 - (nameLen % 4)) % 4;
  uint8_t reqLen = 24 + nameLen + pad;

  uint8_t req[64];
  req[0] = 18;  // ChangeProperty
  req[1] = 0;   // Replace
  req[2] = (reqLen / 4) & 0xFF;
  req[3] = (reqLen / 4) >> 8;

  // Window
  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  // Property: WM_NAME = 39
  req[8] = 39; req[9] = 0; req[10] = 0; req[11] = 0;

  // Type: STRING = 31
  req[12] = 31; req[13] = 0; req[14] = 0; req[15] = 0;

  // Format: 8
  req[16] = 8; req[17] = 0; req[18] = 0; req[19] = 0;

  // Data length
  req[20] = nameLen; req[21] = 0; req[22] = 0; req[23] = 0;

  for (int i = 0; i < nameLen; i++) req[24 + i] = name[i];
  for (int i = 0; i < pad; i++) req[24 + nameLen + i] = 0;

  x11.write(req, reqLen);
  x11.flush();
  seqNum++;
  Serial.printf("Set WM_NAME: %s\n", name);
}

void setWmClass(const char* instance, const char* className) {
  // WM_CLASS is two null-terminated strings
  uint8_t instLen = strlen(instance);
  uint8_t classLen = strlen(className);
  uint8_t totalLen = instLen + 1 + classLen + 1;  // +1 for null terminators
  uint8_t pad = (4 - (totalLen % 4)) % 4;
  uint8_t reqLen = 24 + totalLen + pad;

  uint8_t req[64];
  req[0] = 18;  // ChangeProperty
  req[1] = 0;   // Replace
  req[2] = (reqLen / 4) & 0xFF;
  req[3] = (reqLen / 4) >> 8;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  // Property: WM_CLASS = 67
  req[8] = 67; req[9] = 0; req[10] = 0; req[11] = 0;

  // Type: STRING = 31
  req[12] = 31; req[13] = 0; req[14] = 0; req[15] = 0;

  // Format: 8
  req[16] = 8; req[17] = 0; req[18] = 0; req[19] = 0;

  // Data length
  req[20] = totalLen; req[21] = 0; req[22] = 0; req[23] = 0;

  int pos = 24;
  for (int i = 0; i < instLen; i++) req[pos++] = instance[i];
  req[pos++] = 0;  // null terminator
  for (int i = 0; i < classLen; i++) req[pos++] = className[i];
  req[pos++] = 0;  // null terminator
  for (int i = 0; i < pad; i++) req[pos++] = 0;

  x11.write(req, reqLen);
  x11.flush();
  seqNum++;
  Serial.printf("Set WM_CLASS: %s/%s\n", instance, className);
}

void setWmHints() {
  // WM_HINTS is 9 CARD32 values (36 bytes)
  // Format: flags, input, initial_state, icon_pixmap, icon_window, icon_x, icon_y, icon_mask, window_group
  uint8_t req[60];
  req[0] = 18;  // ChangeProperty
  req[1] = 0;   // Replace
  req[2] = 15;  // length: (24 + 36) / 4 = 15 words
  req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  // Property: WM_HINTS = 35
  req[8] = 35; req[9] = 0; req[10] = 0; req[11] = 0;

  // Type: WM_HINTS = 35
  req[12] = 35; req[13] = 0; req[14] = 0; req[15] = 0;

  // Format: 32
  req[16] = 32; req[17] = 0; req[18] = 0; req[19] = 0;

  // Data length (9 CARD32s)
  req[20] = 9; req[21] = 0; req[22] = 0; req[23] = 0;

  // flags: InputHint(1) | StateHint(2) = 3
  req[24] = 3; req[25] = 0; req[26] = 0; req[27] = 0;

  // input: True (1) - we want keyboard input
  req[28] = 1; req[29] = 0; req[30] = 0; req[31] = 0;

  // initial_state: NormalState (1)
  req[32] = 1; req[33] = 0; req[34] = 0; req[35] = 0;

  // icon_pixmap, icon_window, icon_x, icon_y, icon_mask, window_group (all 0)
  for (int i = 36; i < 60; i++) req[i] = 0;

  x11.write(req, 60);
  x11.flush();
  seqNum++;
  Serial.println("Set WM_HINTS");
}

void setWmProtocols() {
  // Set WM_PROTOCOLS with WM_DELETE_WINDOW
  if (atomWmProtocols == 0 || atomWmDeleteWindow == 0) {
    Serial.println("Missing atoms for WM_PROTOCOLS");
    return;
  }

  uint8_t req[32];
  req[0] = 18;  // ChangeProperty
  req[1] = 0;   // Replace
  req[2] = 7;   // length: (24 + 4) / 4 = 7 words
  req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  // Property: WM_PROTOCOLS (obtained via InternAtom)
  req[8] = atomWmProtocols & 0xFF;
  req[9] = (atomWmProtocols >> 8) & 0xFF;
  req[10] = (atomWmProtocols >> 16) & 0xFF;
  req[11] = (atomWmProtocols >> 24) & 0xFF;

  // Type: ATOM = 4
  req[12] = 4; req[13] = 0; req[14] = 0; req[15] = 0;

  // Format: 32
  req[16] = 32; req[17] = 0; req[18] = 0; req[19] = 0;

  // Data length: 1 atom
  req[20] = 1; req[21] = 0; req[22] = 0; req[23] = 0;

  // WM_DELETE_WINDOW atom
  req[24] = atomWmDeleteWindow & 0xFF;
  req[25] = (atomWmDeleteWindow >> 8) & 0xFF;
  req[26] = (atomWmDeleteWindow >> 16) & 0xFF;
  req[27] = (atomWmDeleteWindow >> 24) & 0xFF;

  x11.write(req, 28);
  x11.flush();
  seqNum++;
  Serial.println("Set WM_PROTOCOLS (WM_DELETE_WINDOW)");
}

void mapWindow() {
  uint8_t req[8];
  req[0] = 8;  // MapWindow
  req[1] = 0;
  req[2] = 2;
  req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  x11.write(req, 8);
  x11.flush();
  seqNum++;
  Serial.println("Mapped window");
}

bool waitForExpose() {
  unsigned long start = millis();
  while (millis() - start < 10000) {
    if (x11.available() >= 32) {
      uint8_t event[32];
      for (int i = 0; i < 32; i++) {
        event[i] = x11.available() ? x11.read() : 0;
      }

      uint8_t code = event[0] & 0x7F;  // mask off "sent" bit

      if (code == 0) {
        Serial.printf("X11 Error: code=%d seq=%d\n", event[1], event[2] | (event[3] << 8));
      } else if (code == 12) {  // Expose
        return true;
      } else if (code == 19) {  // MapNotify
        Serial.println("MapNotify received");
      } else if (code == 22) {  // ConfigureNotify
        Serial.println("ConfigureNotify received");
      } else {
        Serial.printf("Event: %d\n", code);
      }
    }
    delay(10);
  }
  return false;
}

// === Drawing Functions (for your app) ===

void setForeground(uint32_t color) {
  uint8_t req[16];
  req[0] = 56;  // ChangeGC
  req[1] = 0;
  req[2] = 4;
  req[3] = 0;

  req[4] = gcID & 0xFF;
  req[5] = (gcID >> 8) & 0xFF;
  req[6] = (gcID >> 16) & 0xFF;
  req[7] = (gcID >> 24) & 0xFF;

  // foreground (bit 2)
  req[8] = 0x04; req[9] = 0; req[10] = 0; req[11] = 0;

  req[12] = color & 0xFF;
  req[13] = (color >> 8) & 0xFF;
  req[14] = (color >> 16) & 0xFF;
  req[15] = (color >> 24) & 0xFF;

  x11.write(req, 16);
  x11.flush();
}

void fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h) {
  uint8_t req[20];
  req[0] = 70;  // PolyFillRectangle
  req[1] = 0;
  req[2] = 5;
  req[3] = 0;

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

void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
  uint8_t req[20];
  req[0] = 65;  // PolyLine (CoordModeOrigin = 0)
  req[1] = 0;   // CoordModeOrigin
  req[2] = 5;
  req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  req[8] = gcID & 0xFF;
  req[9] = (gcID >> 8) & 0xFF;
  req[10] = (gcID >> 16) & 0xFF;
  req[11] = (gcID >> 24) & 0xFF;

  req[12] = x1 & 0xFF; req[13] = (x1 >> 8) & 0xFF;
  req[14] = y1 & 0xFF; req[15] = (y1 >> 8) & 0xFF;
  req[16] = x2 & 0xFF; req[17] = (x2 >> 8) & 0xFF;
  req[18] = y2 & 0xFF; req[19] = (y2 >> 8) & 0xFF;

  x11.write(req, 20);
  x11.flush();
}

// === Event Handling ===

void handleExpose(uint8_t* event) {
  // Expose event - redraw window content
  // Your app drawing code goes here
  Serial.println("Expose - redrawing");

  // Example: just clear to background color - window is empty and ready
  // You can add your app's drawing code here
}

void handleKeyPress(uint8_t* event) {
  // event[1] = keycode
  uint8_t keycode = event[1];
  uint16_t x = event[24] | (event[25] << 8);
  uint16_t y = event[26] | (event[27] << 8);
  Serial.printf("KeyPress: keycode=%d at (%d,%d)\n", keycode, x, y);

  // Your keyboard handling code here
}

void handleKeyRelease(uint8_t* event) {
  uint8_t keycode = event[1];
  Serial.printf("KeyRelease: keycode=%d\n", keycode);
}

void handleButtonPress(uint8_t* event) {
  uint8_t button = event[1];
  uint16_t x = event[24] | (event[25] << 8);
  uint16_t y = event[26] | (event[27] << 8);
  Serial.printf("ButtonPress: button=%d at (%d,%d)\n", button, x, y);

  // Your mouse button handling code here
}

void handleButtonRelease(uint8_t* event) {
  uint8_t button = event[1];
  uint16_t x = event[24] | (event[25] << 8);
  uint16_t y = event[26] | (event[27] << 8);
  Serial.printf("ButtonRelease: button=%d at (%d,%d)\n", button, x, y);
}

void handleMotionNotify(uint8_t* event) {
  uint16_t x = event[24] | (event[25] << 8);
  uint16_t y = event[26] | (event[27] << 8);
  // Uncomment if you want motion events logged:
  // Serial.printf("Motion: (%d,%d)\n", x, y);
}

void handleClientMessage(uint8_t* event) {
  // Check for WM_DELETE_WINDOW
  uint32_t messageType = event[8] | (event[9] << 8) | (event[10] << 16) | (event[11] << 24);
  uint32_t data0 = event[12] | (event[13] << 8) | (event[14] << 16) | (event[15] << 24);

  if (messageType == atomWmProtocols && data0 == atomWmDeleteWindow) {
    Serial.println("Window close requested - restarting");
    delay(1000);
    ESP.restart();
  }
}

void handleFocusIn(uint8_t* event) {
  Serial.println("FocusIn - window has focus");
}

void handleFocusOut(uint8_t* event) {
  Serial.println("FocusOut - window lost focus");
}

void loop() {
  // Process X11 events
  if (x11.available() >= 32) {
    uint8_t event[32];
    for (int i = 0; i < 32; i++) {
      event[i] = x11.available() ? x11.read() : 0;
    }

    uint8_t code = event[0] & 0x7F;

    switch (code) {
      case 0:  // Error
        Serial.printf("X11 Error: code=%d seq=%d\n", event[1], event[2] | (event[3] << 8));
        break;
      case 2:  // KeyPress
        handleKeyPress(event);
        break;
      case 3:  // KeyRelease
        handleKeyRelease(event);
        break;
      case 4:  // ButtonPress
        handleButtonPress(event);
        break;
      case 5:  // ButtonRelease
        handleButtonRelease(event);
        break;
      case 6:  // MotionNotify
        handleMotionNotify(event);
        break;
      case 9:  // FocusIn
        handleFocusIn(event);
        break;
      case 10: // FocusOut
        handleFocusOut(event);
        break;
      case 12: // Expose
        handleExpose(event);
        break;
      case 19: // MapNotify
        Serial.println("MapNotify");
        break;
      case 22: // ConfigureNotify
        Serial.println("ConfigureNotify");
        break;
      case 33: // ClientMessage
        handleClientMessage(event);
        break;
      default:
        Serial.printf("Event: %d\n", code);
    }
  }

  if (!x11.connected()) {
    Serial.println("X11 disconnected - restarting");
    delay(5000);
    ESP.restart();
  }

  delay(10);
}

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

uint16_t winWidth = 800;
uint16_t winHeight = 480;

void setup() {
  Serial.begin(9600);
  delay(1000);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(ssid, password);

  Serial.println("ESP32 X11 App");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  while (WiFi.softAPgetStationNum() == 0) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nClient connected!");
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

  // X11 Setup
  uint8_t setup[] = {
    0x6C, 0x00,
    0x0B, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00
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
    Serial.printf("Setup failed: %d\n", status);
    return;
  }
  x11.read();

  uint16_t protoMajor = x11.read() | (x11.read() << 8);
  uint16_t protoMinor = x11.read() | (x11.read() << 8);
  Serial.printf("X11 %d.%d\n", protoMajor, protoMinor);

  uint16_t additionalLen = x11.read() | (x11.read() << 8);
  int totalBytes = additionalLen * 4;

  uint8_t* data = (uint8_t*)malloc(totalBytes);
  if (!data) {
    Serial.println("Malloc failed");
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

  uint32_t ridBase = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
  uint16_t vendorLen = data[16] | (data[17] << 8);
  uint8_t numFormats = data[21];

  int vendorPad = (4 - (vendorLen % 4)) % 4;
  int screenOffset = 32 + vendorLen + vendorPad + (numFormats * 8);

  rootWindow = data[screenOffset] | (data[screenOffset+1] << 8) |
               (data[screenOffset+2] << 16) | (data[screenOffset+3] << 24);
  rootVisual = data[screenOffset+32] | (data[screenOffset+33] << 8) |
               (data[screenOffset+34] << 16) | (data[screenOffset+35] << 24);

  Serial.printf("Root: 0x%08X\n", rootWindow);

  free(data);

  windowID = ridBase | 0x00000001;
  gcID = ridBase | 0x00000002;

  createGC();
  createWindow();
  setWmName("ESP32 App");
  mapWindow();

  Serial.println("Waiting for Expose...");
  waitForExpose();

  Serial.println("Window ready!");
}

void createGC() {
  uint8_t req[16];
  req[0] = 55;
  req[1] = 0;
  req[2] = 4;
  req[3] = 0;

  req[4] = gcID & 0xFF;
  req[5] = (gcID >> 8) & 0xFF;
  req[6] = (gcID >> 16) & 0xFF;
  req[7] = (gcID >> 24) & 0xFF;

  req[8] = rootWindow & 0xFF;
  req[9] = (rootWindow >> 8) & 0xFF;
  req[10] = (rootWindow >> 16) & 0xFF;
  req[11] = (rootWindow >> 24) & 0xFF;

  req[12] = 0;
  req[13] = 0;
  req[14] = 0;
  req[15] = 0;

  x11.write(req, 16);
  x11.flush();
  Serial.println("GC created");
}

void createWindow() {
  uint8_t req[44];
  req[0] = 1;
  req[1] = 0;
  req[2] = 11;
  req[3] = 0;

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

  req[16] = winWidth & 0xFF;
  req[17] = (winWidth >> 8) & 0xFF;
  req[18] = winHeight & 0xFF;
  req[19] = (winHeight >> 8) & 0xFF;

  req[20] = 0; req[21] = 0;
  req[22] = 1; req[23] = 0;

  req[24] = rootVisual & 0xFF;
  req[25] = (rootVisual >> 8) & 0xFF;
  req[26] = (rootVisual >> 16) & 0xFF;
  req[27] = (rootVisual >> 24) & 0xFF;

  // background-pixel + backing-store + event-mask
  req[28] = 0x42;
  req[29] = 0x08;
  req[30] = 0x00;
  req[31] = 0x00;

  // Background: light gray
  req[32] = 0xC0;
  req[33] = 0xC0;
  req[34] = 0xC0;
  req[35] = 0x00;

  // Backing store = WhenMapped
  req[36] = 0x01;
  req[37] = 0x00;
  req[38] = 0x00;
  req[39] = 0x00;

  // Event mask: Exposure + StructureNotify + Key + Button + Motion
  req[40] = 0x4F;
  req[41] = 0x80;
  req[42] = 0x22;
  req[43] = 0x00;

  x11.write(req, 44);
  x11.flush();
  Serial.printf("Window 0x%08X created\n", windowID);
}

void setWmName(const char* name) {
  uint8_t nameLen = strlen(name);
  uint8_t pad = (4 - (nameLen % 4)) % 4;
  uint8_t reqLen = 24 + nameLen + pad;

  uint8_t req[48];
  req[0] = 18;
  req[1] = 0;
  req[2] = (reqLen / 4) & 0xFF;
  req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  req[8] = 39; req[9] = 0; req[10] = 0; req[11] = 0;
  req[12] = 31; req[13] = 0; req[14] = 0; req[15] = 0;
  req[16] = 8; req[17] = 0; req[18] = 0; req[19] = 0;
  req[20] = nameLen; req[21] = 0; req[22] = 0; req[23] = 0;

  for (int i = 0; i < nameLen; i++) req[24 + i] = name[i];
  for (int i = 0; i < pad; i++) req[24 + nameLen + i] = 0;

  x11.write(req, reqLen);
  x11.flush();
}

void mapWindow() {
  uint8_t req[8];
  req[0] = 8;
  req[1] = 0;
  req[2] = 2;
  req[3] = 0;

  req[4] = windowID & 0xFF;
  req[5] = (windowID >> 8) & 0xFF;
  req[6] = (windowID >> 16) & 0xFF;
  req[7] = (windowID >> 24) & 0xFF;

  x11.write(req, 8);
  x11.flush();
  Serial.println("Window mapped");
}

void waitForExpose() {
  unsigned long start = millis();
  while (millis() - start < 10000) {
    if (x11.available() >= 32) {
      uint8_t event[32];
      for (int i = 0; i < 32; i++) {
        event[i] = x11.available() ? x11.read() : 0;
      }

      uint8_t code = event[0] & 0x7F;

      if (code == 0) {
        Serial.printf("Error: %d\n", event[1]);
      } else if (code == 12) {
        Serial.println("Expose received");
        return;
      } else {
        Serial.printf("Event: %d\n", code);
      }
    }
    delay(10);
  }
  Serial.println("Timeout");
}

// Drawing functions for your app
void setForeground(uint32_t color) {
  uint8_t req[16];
  req[0] = 56;
  req[1] = 0;
  req[2] = 4;
  req[3] = 0;

  req[4] = gcID & 0xFF;
  req[5] = (gcID >> 8) & 0xFF;
  req[6] = (gcID >> 16) & 0xFF;
  req[7] = (gcID >> 24) & 0xFF;

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
  req[0] = 70;
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

void loop() {
  if (x11.available() >= 32) {
    uint8_t event[32];
    for (int i = 0; i < 32; i++) {
      event[i] = x11.available() ? x11.read() : 0;
    }

    uint8_t code = event[0] & 0x7F;

    if (code == 12) {
      // Expose - redraw if needed
      Serial.println("Expose");
    } else if (code == 2) {
      // KeyPress
      Serial.printf("Key: %d\n", event[1]);
    } else if (code == 4) {
      // ButtonPress
      uint16_t x = event[24] | (event[25] << 8);
      uint16_t y = event[26] | (event[27] << 8);
      Serial.printf("Click: %d,%d\n", x, y);
    }
  }

  if (!x11.connected()) {
    Serial.println("Disconnected");
    delay(5000);
    ESP.restart();
  }

  delay(10);
}

#include <WiFi.h>

const char* ssid = "CarNet1";
const char* password = "password";

WiFiClient x11;
const char* xserver = "192.168.4.2";
const int xport = 6000;

uint32_t rootWindow = 0;
uint32_t windowID = 0;
uint32_t gcID = 0;
uint32_t fontID = 0;

// Material Design 3 Color Palette
#define MD_SURFACE       0x001C1B1F
#define MD_SURFACE_CONT  0x002B2930
#define MD_ON_SURFACE    0x00E6E1E5
#define MD_OUTLINE       0x00938F99
#define MD_PRIMARY       0x00D0BCFF

// Button colors
#define COL_POWER_OFF    0x00CF6679
#define COL_POWER_ON     0x0066BB6A
#define COL_MUTE         0x00FF9800
#define COL_VOL          0x004CAF50
#define COL_CH           0x002196F3
#define COL_NUM          0x00424242
#define COL_NAV          0x00607D8B
#define COL_OK           0x00FF9800
#define COL_MEDIA        0x009C27B0
#define COL_RED          0x00F44336
#define COL_GREEN        0x004CAF50
#define COL_YELLOW       0x00FFEB3B
#define COL_BLUE         0x002196F3

struct Button {
  int16_t x, y;
  uint16_t w, h;
  uint32_t color;
  const char* cmd;
  const char* label;
};

// Button layout with text labels - 1024x600 fullscreen
Button buttons[] = {
  // Top row
  {30, 70, 100, 60, COL_POWER_OFF, "POWER", "POWER"},
  {150, 70, 90, 60, COL_MUTE, "MUTE", "MUTE"},
  {260, 70, 90, 60, MD_SURFACE_CONT, "SOURCE", "SRC"},

  // Volume & Channel
  {30, 150, 80, 55, COL_VOL, "VOL+", "VOL+"},
  {120, 150, 80, 55, COL_VOL, "VOL-", "VOL-"},
  {220, 150, 80, 55, COL_CH, "CH+", "CH+"},
  {310, 150, 80, 55, COL_CH, "CH-", "CH-"},

  // Number pad - right side
  {550, 70, 70, 55, COL_NUM, "1", "1"},
  {640, 70, 70, 55, COL_NUM, "2", "2"},
  {730, 70, 70, 55, COL_NUM, "3", "3"},
  {550, 140, 70, 55, COL_NUM, "4", "4"},
  {640, 140, 70, 55, COL_NUM, "5", "5"},
  {730, 140, 70, 55, COL_NUM, "6", "6"},
  {550, 210, 70, 55, COL_NUM, "7", "7"},
  {640, 210, 70, 55, COL_NUM, "8", "8"},
  {730, 210, 70, 55, COL_NUM, "9", "9"},
  {550, 280, 70, 55, MD_SURFACE_CONT, "INFO", "INFO"},
  {640, 280, 70, 55, COL_NUM, "0", "0"},
  {730, 280, 70, 55, MD_SURFACE_CONT, "BACK", "BACK"},

  // D-pad - center left
  {150, 240, 70, 55, COL_NAV, "UP", "UP"},
  {150, 370, 70, 55, COL_NAV, "DOWN", "DOWN"},
  {60, 305, 70, 55, COL_NAV, "LEFT", "LEFT"},
  {240, 305, 70, 55, COL_NAV, "RIGHT", "RIGHT"},
  {150, 305, 70, 55, COL_OK, "OK", "OK"},

  // Menu row
  {60, 450, 80, 50, MD_SURFACE_CONT, "MENU", "MENU"},
  {160, 450, 80, 50, MD_SURFACE_CONT, "HOME", "HOME"},
  {260, 450, 80, 50, MD_SURFACE_CONT, "EXIT", "EXIT"},

  // Media controls
  {550, 360, 70, 50, COL_MEDIA, "REW", "<<"},
  {640, 360, 70, 50, COL_MEDIA, "PLAY", "PLAY"},
  {730, 360, 70, 50, COL_MEDIA, "FF", ">>"},
  {550, 420, 70, 50, COL_MEDIA, "STOP", "STOP"},
  {640, 420, 70, 50, COL_MEDIA, "PAUSE", "PAUSE"},
  {730, 420, 70, 50, COL_MEDIA, "REC", "REC"},

  // Color buttons - bottom right
  {850, 70, 60, 45, COL_RED, "RED", ""},
  {850, 130, 60, 45, COL_GREEN, "GREEN", ""},
  {850, 190, 60, 45, COL_YELLOW, "YELLOW", ""},
  {850, 250, 60, 45, COL_BLUE, "BLUE", ""},

  // Guide/EPG
  {930, 70, 70, 55, MD_SURFACE_CONT, "GUIDE", "GUIDE"},
  {930, 140, 70, 55, MD_SURFACE_CONT, "LIST", "LIST"},
};

const int numButtons = sizeof(buttons) / sizeof(Button);
bool powerOn = false;
bool muted = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(ssid, password);

  Serial.println("ESP32 Material Remote v3.1");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  while (WiFi.softAPgetStationNum() == 0) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected!");
  delay(2000);
  connectToXServer();
}

void connectToXServer() {
  if (!x11.connect(xserver, xport)) {
    Serial.println("Connection failed");
    delay(5000);
    ESP.restart();
  }

  // X11 Setup
  uint8_t setup[] = {0x6C, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  x11.write(setup, 12);
  x11.flush();
  delay(500);

  if (!x11.available() || x11.read() != 1) {
    Serial.println("X11 setup failed");
    return;
  }

  x11.read();
  for (int i = 0; i < 4; i++) x11.read();

  uint16_t addLen = x11.read() | (x11.read() << 8);
  int total = addLen * 4;
  uint8_t* data = (uint8_t*)malloc(total);

  int n = 0;
  while (n < total) {
    if (x11.available()) data[n++] = x11.read();
    else delay(5);
  }

  uint32_t ridBase = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
  uint16_t vendorLen = data[16] | (data[17] << 8);
  uint8_t numFormats = data[21];
  int vendorPad = (4 - (vendorLen % 4)) % 4;
  int screenOff = 32 + vendorLen + vendorPad + (numFormats * 8);

  rootWindow = data[screenOff] | (data[screenOff+1] << 8) | (data[screenOff+2] << 16) | (data[screenOff+3] << 24);
  uint32_t rootVisual = data[screenOff+32] | (data[screenOff+33] << 8) | (data[screenOff+34] << 16) | (data[screenOff+35] << 24);

  windowID = ridBase | 1;
  gcID = ridBase | 2;
  fontID = ridBase | 3;
  free(data);

  Serial.printf("Root: 0x%08X, Font ID: 0x%08X\n", rootWindow, fontID);

  // Open font first
  openFont();
  delay(100);

  createGC();
  createWindow(rootVisual);
  mapWindow();
  raiseWindow();
  waitForExpose();
  drawUI();

  Serial.println("Material Remote Ready!");
}

// Helpers
void w16(uint8_t* b, uint16_t v) { b[0] = v & 0xFF; b[1] = (v >> 8) & 0xFF; }
void w32(uint8_t* b, uint32_t v) { b[0] = v & 0xFF; b[1] = (v >> 8) & 0xFF; b[2] = (v >> 16) & 0xFF; b[3] = (v >> 24) & 0xFF; }

void openFont() {
  // OpenFont request - opcode 45
  // Try a simple fixed-width font
  const char* fontName = "fixed";
  uint8_t nameLen = strlen(fontName);
  uint8_t pad = (4 - (nameLen % 4)) % 4;
  uint16_t reqLen = (12 + nameLen + pad) / 4;

  uint8_t req[32];
  req[0] = 45;  // OpenFont opcode
  req[1] = 0;   // unused
  w16(&req[2], reqLen);
  w32(&req[4], fontID);
  w16(&req[8], nameLen);
  w16(&req[10], 0);  // unused

  for (int i = 0; i < nameLen; i++) req[12 + i] = fontName[i];
  for (int i = 0; i < pad; i++) req[12 + nameLen + i] = 0;

  x11.write(req, 12 + nameLen + pad);
  x11.flush();
  Serial.println("Opened font: fixed");
}

void createGC() {
  // CreateGC with foreground and font
  uint8_t r[24] = {55, 0, 6, 0};  // opcode 55, length 6 words
  w32(&r[4], gcID);
  w32(&r[8], rootWindow);
  w32(&r[12], 0x04 | 0x4000);  // foreground (bit 2) + font (bit 14)
  w32(&r[16], 0xFFFFFF);       // foreground color
  w32(&r[20], fontID);         // font ID
  x11.write(r, 24);
  x11.flush();
  Serial.println("Created GC with font");
}

void createWindow(uint32_t vis) {
  uint8_t r[44] = {1, 0, 11, 0};
  w32(&r[4], windowID);
  w32(&r[8], rootWindow);
  w16(&r[12], 0); w16(&r[14], 0);
  w16(&r[16], 1024); w16(&r[18], 600);  // Fullscreen
  w16(&r[20], 0); w16(&r[22], 1);
  w32(&r[24], vis);
  w32(&r[28], 0x0A02);
  w32(&r[32], MD_SURFACE);
  w32(&r[36], 1);
  w32(&r[40], 0x800C);
  x11.write(r, 44);
  x11.flush();
}

void mapWindow() {
  uint8_t r[8] = {8, 0, 2, 0};
  w32(&r[4], windowID);
  x11.write(r, 8);
  x11.flush();
  delay(100);
}

void raiseWindow() {
  uint8_t r[16] = {12, 0, 4, 0};
  w32(&r[4], windowID);
  w32(&r[8], 0x40);
  w32(&r[12], 0);
  x11.write(r, 16);
  x11.flush();
}

void waitForExpose() {
  unsigned long t = millis();
  while (millis() - t < 5000) {
    if (x11.available() >= 32) {
      uint8_t c = x11.read();
      for (int i = 0; i < 31; i++) if (x11.available()) x11.read();
      if (c == 12) return;
      if (c == 0) Serial.println("X11 Error received");
    }
    delay(10);
  }
}

void setColor(uint32_t c) {
  uint8_t r[16] = {56, 0, 4, 0};
  w32(&r[4], gcID);
  w32(&r[8], 0x04);
  w32(&r[12], c);
  x11.write(r, 16);
  x11.flush();
}

void fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h) {
  uint8_t r[20] = {70, 0, 5, 0};
  w32(&r[4], windowID);
  w32(&r[8], gcID);
  w16(&r[12], x); w16(&r[14], y);
  w16(&r[16], w); w16(&r[18], h);
  x11.write(r, 20);
  x11.flush();
}

void drawText(int16_t x, int16_t y, const char* text) {
  // ImageText8 - opcode 76
  uint8_t len = strlen(text);
  if (len == 0) return;

  uint8_t pad = (4 - ((16 + len) % 4)) % 4;
  uint16_t reqLen = (16 + len + pad) / 4;

  uint8_t req[64];
  req[0] = 76;   // ImageText8 opcode
  req[1] = len;  // string length
  w16(&req[2], reqLen);
  w32(&req[4], windowID);
  w32(&req[8], gcID);
  w16(&req[12], x);
  w16(&req[14], y);

  for (int i = 0; i < len; i++) req[16 + i] = text[i];
  for (int i = 0; i < pad; i++) req[16 + len + i] = 0;

  x11.write(req, 16 + len + pad);
  x11.flush();
}

void drawButton(int16_t x, int16_t y, uint16_t w, uint16_t h, uint32_t color, const char* label, bool pressed) {
  // Shadow
  if (!pressed) {
    setColor(0x00000000);
    fillRect(x + 3, y + 3, w, h);
  }

  // Button background
  setColor(pressed ? (color & 0x00808080) : color);
  fillRect(x, y, w, h);

  // Highlight (3D effect)
  if (!pressed) {
    setColor(color | 0x00404040);
    fillRect(x, y, w, 2);
    fillRect(x, y, 2, h);
  }

  // Draw label centered
  if (label && strlen(label) > 0) {
    setColor(MD_ON_SURFACE);
    // Approximate centering: each char ~6px wide, 10px tall
    int textLen = strlen(label);
    int16_t tx = x + (w - textLen * 6) / 2;
    int16_t ty = y + h / 2 + 4;  // baseline offset
    drawText(tx, ty, label);
  }
}

void drawStatusBar() {
  setColor(MD_SURFACE_CONT);
  fillRect(0, 0, 1024, 48);

  // Power indicator
  setColor(powerOn ? COL_POWER_ON : COL_POWER_OFF);
  fillRect(16, 16, 16, 16);

  // Status text
  setColor(MD_ON_SURFACE);
  drawText(40, 28, powerOn ? "ON" : "OFF");

  // Mute indicator
  if (muted) {
    setColor(COL_MUTE);
    fillRect(90, 16, 16, 16);
    setColor(MD_ON_SURFACE);
    drawText(112, 28, "MUTE");
  }

  // Divider
  setColor(MD_OUTLINE);
  fillRect(0, 47, 1024, 1);
}

void drawUI() {
  drawStatusBar();

  // Draw all buttons
  for (int i = 0; i < numButtons; i++) {
    Button& b = buttons[i];
    uint32_t color = b.color;

    if (strcmp(b.cmd, "POWER") == 0) {
      color = powerOn ? COL_POWER_ON : COL_POWER_OFF;
    }

    drawButton(b.x, b.y, b.w, b.h, color, b.label, false);
  }

  x11.flush();
}

void handlePress(int16_t px, int16_t py) {
  for (int i = 0; i < numButtons; i++) {
    Button& b = buttons[i];
    if (px >= b.x && px < b.x + b.w && py >= b.y && py < b.y + b.h) {
      Serial.printf("Button: %s\n", b.cmd);

      uint32_t color = b.color;
      if (strcmp(b.cmd, "POWER") == 0) color = powerOn ? COL_POWER_ON : COL_POWER_OFF;

      // Pressed state
      drawButton(b.x, b.y, b.w, b.h, color, b.label, true);
      x11.flush();

      executeCmd(b.cmd);

      delay(120);

      // Restore
      if (strcmp(b.cmd, "POWER") == 0) color = powerOn ? COL_POWER_ON : COL_POWER_OFF;
      drawButton(b.x, b.y, b.w, b.h, color, b.label, false);
      x11.flush();

      return;
    }
  }
}

void executeCmd(const char* cmd) {
  Serial.printf("!CMD:%s\n", cmd);

  if (strcmp(cmd, "POWER") == 0) {
    powerOn = !powerOn;
    drawStatusBar();
    // Redraw power button
    drawButton(buttons[0].x, buttons[0].y, buttons[0].w, buttons[0].h,
               powerOn ? COL_POWER_ON : COL_POWER_OFF, buttons[0].label, false);
    x11.flush();
  }
  else if (strcmp(cmd, "MUTE") == 0) {
    muted = !muted;
    drawStatusBar();
    x11.flush();
  }
}

void loop() {
  if (x11.available() >= 32) {
    uint8_t ev[32];
    for (int i = 0; i < 32; i++) ev[i] = x11.available() ? x11.read() : 0;

    uint8_t code = ev[0] & 0x7F;
    if (code == 4) {
      int16_t px = ev[24] | (ev[25] << 8);
      int16_t py = ev[26] | (ev[27] << 8);
      handlePress(px, py);
    }
    else if (code == 12) {
      drawUI();
    }
    else if (code == 0) {
      Serial.printf("X11 Error: %d\n", ev[1]);
    }
  }

  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s.length() > 0) executeCmd(s.c_str());
  }

  if (!x11.connected()) {
    Serial.println("Disconnected");
    delay(3000);
    ESP.restart();
  }

  delay(10);
}

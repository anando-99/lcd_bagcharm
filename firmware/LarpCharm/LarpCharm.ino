/*
  LARP CHARM firmware v3.0
  Board : Waveshare ESP32-S3-Touch-LCD-1.69 (240x280 ST7789V2, CST816T touch)
  Control: Bluetooth LE from the Larp Charm web app

  Screens (swipe left/right):  CLOCK · TEXT · PHOTO · CLIPS · MUSIC · PET · TOYS · CARD
  Shake: Clock = next background · Pet = dizzy · Toys = roll · asleep = wake

  Touch:
    swipe left / right  -> next / previous screen (with slide animation)
    swipe up / down     -> brightness up / down
    tap                 -> Clock: 12/24h · Text: style · Clips: next clip · Pet: pet it
    double tap          -> feed pet
    long press          -> CARD screen (NFC "tap here")
  PWR button: short = wake / next screen, hold 2 s = power off (battery only)

  Libraries (Library Manager):
    GFX Library for Arduino (moononournation) 1.6.x
    NimBLE-Arduino (h2zero) 2.x
    JPEGDEC (Larry Bank) 1.x
  Board package: esp32 by Espressif 3.x  ·  Board: ESP32S3 Dev Module
  Tools: USB CDC On Boot Enabled · Flash 16MB · PSRAM OPI
         Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
*/

#include <Arduino.h>
#include <Wire.h>
#include <FFat.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>
#include <vector>
#include <algorithm>
#include <time.h>
#include <sys/time.h>

// ───────────── Pins (Waveshare docs) ─────────────
#define LCD_DC    4
#define LCD_CS    5
#define LCD_SCK   6
#define LCD_MOSI  7
#define LCD_RST   8
#define LCD_BL    15
#define I2C_SDA   11
#define I2C_SCL   10
#define TP_RST    13
#define TP_INT    14
#define BAT_ADC   1
#define SYS_OUT   40   // PWR button, LOW when pressed
#define SYS_EN    41   // keep HIGH to stay powered on battery
#define BUZZER    42

#define SCR_W 240
#define SCR_H 280
#define FB_BYTES (SCR_W * SCR_H * 2)

#define CST816_ADDR 0x15
#define RTC_ADDR    0x51

// ───────────── BLE protocol (must match the web app) ─────────────
#define SVC_UUID    "6e400001-c4a7-4d2e-9b3c-c4a2d7c0ffee"
#define RX_UUID     "6e400002-c4a7-4d2e-9b3c-c4a2d7c0ffee"
#define STATUS_UUID "6e400003-c4a7-4d2e-9b3c-c4a2d7c0ffee"
#define FW_VERSION  "3.0"
#define DEVICE_NAME "Larp Charm"

enum Cmd : uint8_t {
  CMD_MODE        = 0x01, // [mode]
  CMD_TEXT        = 0x02, // [style][fg565 hi lo][bg565 hi lo] text
  CMD_TIME        = 0x03, // [epoch u32][tz minutes i16]
  CMD_NOWPLAYING  = 0x04, // [r][g][b][playing][pos u32][dur u32] title 0x1F artist
  CMD_XFER_BEGIN  = 0x05, // [kind 0=photo 1=art 2=clip][size u32][id u32]
  CMD_XFER_DATA   = 0x06, // raw bytes
  CMD_XFER_END    = 0x07,
  CMD_BRIGHTNESS  = 0x08, // [0..255]
  CMD_PET         = 0x09, // [species][r][g][b] name
  CMD_CARD        = 0x0A, // [r][g][b] name 0x1F subtitle
  CMD_SETTINGS    = 0x0B, // [sleep s u16][swap swipe]
  CMD_PING        = 0x0C,
  CMD_BUZZ        = 0x0D, // [count]
  CMD_PET_FEED    = 0x0E,
  CMD_CLIP_LIST   = 0x0F,
  CMD_CLIP_PLAY   = 0x10, // [id u32]
  CMD_CLIP_DELETE = 0x11, // [id u32]
  CMD_LOOK        = 0x12, // [background][clock face]
  CMD_TIMER       = 0x13, // [seconds u16]  (0 = cancel)
  CMD_TOY         = 0x14, // [toy 0=8-ball 1=dice 2=coin][roll 0/1]
};

enum Mode : uint8_t { M_CLOCK = 0, M_TEXT, M_PHOTO, M_CLIP, M_MUSIC, M_PET, M_TOY, M_CARD, M_COUNT };
const char *MODE_NAMES[M_COUNT] = {"CLOCK", "TEXT", "PHOTO", "CLIPS", "MUSIC", "PET", "TOYS", "CARD"};

// ───────────── Display ─────────────
Arduino_DataBus *bus   = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX     *panel = new Arduino_ST7789(bus, LCD_RST, 0, true, SCR_W, SCR_H, 0, 20, 0, 20);
Arduino_Canvas  *gfx   = new Arduino_Canvas(SCR_W, SCR_H, panel);
JPEGDEC jpeg;

static inline uint16_t c565(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
static inline uint8_t r8(uint16_t c) { return ((c >> 11) & 0x1F) << 3; }
static inline uint8_t g8(uint16_t c) { return ((c >> 5) & 0x3F) << 2; }
static inline uint8_t b8(uint16_t c) { return (c & 0x1F) << 3; }

uint16_t hue565(uint8_t h) {
  uint8_t region = h / 43, rem = (h - region * 43) * 6;
  uint8_t q = 255 - rem, t = rem;
  switch (region) {
    case 0:  return c565(255, t, 0);
    case 1:  return c565(q, 255, 0);
    case 2:  return c565(0, 255, t);
    case 3:  return c565(0, q, 255);
    case 4:  return c565(t, 0, 255);
    default: return c565(255, 0, q);
  }
}
uint16_t mix565(uint16_t a, uint16_t b, uint8_t t) {
  return c565(r8(a) + ((int)(r8(b) - r8(a)) * t) / 255,
              g8(a) + ((int)(g8(b) - g8(a)) * t) / 255,
              b8(a) + ((int)(b8(b) - b8(a)) * t) / 255);
}

const uint16_t COL_BLACK  = 0x0000;
const uint16_t COL_WHITE  = 0xFFFF;
const uint16_t COL_GRAPE  = c565(124, 58, 237);
const uint16_t COL_GRAPE2 = c565(46, 16, 101);
const uint16_t COL_ICE    = c565(125, 211, 252);
const uint16_t COL_ICE2   = c565(12, 74, 110);
const uint16_t COL_PINK   = c565(244, 114, 182);
const uint16_t COL_LIME   = c565(52, 199, 89);
const uint16_t COL_AMBER  = c565(250, 204, 21);
const uint16_t COL_RED    = c565(239, 68, 68);
const uint16_t COL_GREY   = c565(90, 90, 110);

// ───────────── State ─────────────
Preferences prefs;

uint8_t  mode = M_CLOCK;
uint8_t  brightness = 200;
uint16_t sleepSec = 30;
bool     swapSwipe = false;
bool     use24h = true;
int16_t  tzMin = 330;

String   textMsg = "hello :)";
uint8_t  textStyle = 0;
uint16_t textFg = COL_WHITE, textBg = COL_GRAPE2;
std::vector<String> textLines;
uint8_t  textSize = 3;
int      marqueeX = SCR_W;

uint16_t *photoBuf = nullptr;
bool      photoLoaded = false;
#define ART_W 120
uint16_t *artBuf = nullptr;
bool      artLoaded = false;

String   npTitle = "", npArtist = "";
uint16_t npColor = COL_PINK;
bool     npPlaying = false;
uint32_t npPosMs = 0, npDurMs = 0, npStamp = 0;
int      npTitleX = 0;

uint8_t  petSpecies = 0;
uint16_t petColor = c565(255, 196, 120);
String   petName = "mochi";
int      petMood = 70;
uint32_t petHeartsUntil = 0, petLastDecay = 0;

String   cardName = "your name", cardSub = "tap for portfolio";
uint16_t cardAccent = COL_ICE;

// look
#define BG_COUNT 9
#define FACE_COUNT 4
uint8_t bgStyle = 0, faceStyle = 0;
const char *BG_NAMES[BG_COUNT] = {"aurora", "warp", "vapor", "matrix", "plasma", "lava", "snow", "checker", "photo"};

// options
bool    soundsOn = true, shakeWake = true, hourChime = false;
uint8_t tiltBits = 0;            // bit0 flip X, bit1 flip Y, bit2 swap axes

// motion sensor (QMI8658)
bool     imuOK = false, imuBE = false;
float    accX = 0, accY = 0, accZ = 1;      // g, mapped to the screen
float    tiltX = 0, tiltY = 0;              // smoothed
uint32_t lastImuRead = 0, shakeHitAt = 0, lastShakeAt = 0, moveTicks = 0;
uint8_t  shakeHits = 0;

// toys
uint8_t  toyKind = 0, toyResult = 0, toyResult2 = 0;
bool     toyHasResult = false;
uint32_t toyRollAt = 0;

// timer
uint32_t timerEndAt = 0, alarmUntil = 0, alarmNextBeep = 0;
int      lastChimeHour = -1;
uint32_t petDizzyUntil = 0;

// storage
bool fsOK = false;

// clips (MJPEG): header + frames [len u32][delay ms u16][jpeg]
#pragma pack(push, 1)
struct ClipHdr { char magic[4]; uint8_t ver; uint8_t flags; uint16_t w, h; uint32_t frames; char name[24]; };
#pragma pack(pop)
#define FRAME_MAX (160 * 1024)
File     clipFile;
ClipHdr  clipHdr;
uint32_t clipId = 0;
uint32_t clipNextAt = 0;
bool     clipHasFrame = false;
uint8_t *frameBuf = nullptr;
uint32_t chromeUntil = 0;

// transfers
uint8_t  xferKind = 0;
uint32_t xferSize = 0, xferGot = 0, xferAcked = 0, xferId = 0;
bool     xferActive = false;
volatile bool xferAbort = false;
File     xferFile;
#define XBUF_SZ 16384
uint8_t *xferBuf = nullptr;
uint32_t xferBufLen = 0;

// transitions
uint16_t *prevFb = nullptr, *mixFb = nullptr;

// runtime
volatile bool touchFlag = false;
volatile bool bleConnected = false;
bool     sleeping = false;
uint32_t lastInteraction = 0, lastFrame = 0, frameNo = 0;
float    batVolts = 4.0;
int      batPct = 80;
bool     charging = false;
uint32_t chargeSeenAt = 0;
float    vHist[12];
int      vHistN = 0;
uint32_t lastBatRead = 0, lastStatus = 0;
uint32_t toastUntil = 0;
String   toastMsg;
bool     timeValid = false;

NimBLECharacteristic *statusChar = nullptr;

struct Packet { uint16_t len; uint8_t data[514]; };
QueueHandle_t rxQueue;

// ───────────── Helpers ─────────────
void toast(const String &m, uint32_t ms = 1200) { toastMsg = m; toastUntil = millis() + ms; }

void touchActivity() {
  lastInteraction = millis();
  if (sleeping) { sleeping = false; analogWrite(LCD_BL, brightness); }
}

void setBacklight(int v) { brightness = constrain(v, 8, 255); if (!sleeping) analogWrite(LCD_BL, brightness); }

void beep(int n = 1) {
  for (int i = 0; i < n; i++) { tone(BUZZER, 2700, 80); delay(140); }
  noTone(BUZZER);
}

void sfx(uint8_t id) {
  if (!soundsOn) return;
  switch (id) {
    case 0: tone(BUZZER, 2200, 18); break;                                                      // tick
    case 1: tone(BUZZER, 1500, 22); tone(BUZZER, 2100, 28); break;                              // swipe
    case 2: tone(BUZZER, 1319, 70); tone(BUZZER, 1568, 70); tone(BUZZER, 2093, 110); break;     // hello
    case 3: tone(BUZZER, 900, 30);  tone(BUZZER, 1200, 30); tone(BUZZER, 1700, 40); break;      // roll
  }
}

void savePrefs() {
  prefs.putUChar("bg", bgStyle);      prefs.putUChar("face", faceStyle);
  prefs.putUChar("toy", toyKind);
  prefs.putUChar("mode", mode);       prefs.putUChar("bright", brightness);
  prefs.putUShort("sleep", sleepSec); prefs.putBool("swap", swapSwipe);
  prefs.putBool("h24", use24h);       prefs.putShort("tz", tzMin);
  prefs.putString("text", textMsg);   prefs.putUChar("tstyle", textStyle);
  prefs.putUShort("tfg", textFg);     prefs.putUShort("tbg", textBg);
  prefs.putUChar("petsp", petSpecies); prefs.putUShort("petcol", petColor);
  prefs.putString("petname", petName); prefs.putInt("petmood", petMood);
  prefs.putString("cname", cardName); prefs.putString("csub", cardSub);
  prefs.putUShort("cacc", cardAccent); prefs.putULong("clip", clipId);
}

void loadPrefs() {
  mode       = prefs.getUChar("mode", M_CLOCK) % M_COUNT;
  brightness = prefs.getUChar("bright", 200);
  sleepSec   = prefs.getUShort("sleep", 30);
  swapSwipe  = prefs.getBool("swap", false);
  use24h     = prefs.getBool("h24", true);
  tzMin      = prefs.getShort("tz", 330);
  textMsg    = prefs.getString("text", textMsg);
  textStyle  = prefs.getUChar("tstyle", 0);
  textFg     = prefs.getUShort("tfg", COL_WHITE);
  textBg     = prefs.getUShort("tbg", COL_GRAPE2);
  petSpecies = prefs.getUChar("petsp", 0);
  petColor   = prefs.getUShort("petcol", petColor);
  petName    = prefs.getString("petname", petName);
  petMood    = prefs.getInt("petmood", 70);
  cardName   = prefs.getString("cname", cardName);
  cardSub    = prefs.getString("csub", cardSub);
  cardAccent = prefs.getUShort("cacc", COL_ICE);
  clipId     = prefs.getULong("clip", 0);
  bgStyle    = prefs.getUChar("bg", 0) % BG_COUNT;
  faceStyle  = prefs.getUChar("face", 0) % FACE_COUNT;
  toyKind    = prefs.getUChar("toy", 0) % 3;
  uint8_t fl = prefs.getUChar("flags", 0x03);
  soundsOn = fl & 1; shakeWake = fl & 2; hourChime = fl & 4;
  tiltBits   = prefs.getUChar("tilt", 0) & 7;
}

String asciiOnly(const String &s) {
  String o; o.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (c == '\n' || (c >= 32 && c < 127)) o += (char)c;
    else if ((c & 0xC0) != 0x80) o += '?';
  }
  return o;
}

// ───────────── RTC (PCF85063) ─────────────
static uint8_t bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t dec2bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

bool rtcReadUTC(time_t &out) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)RTC_ADDR, (uint8_t)7) != 7) return false;
  uint8_t s = Wire.read(), mi = Wire.read(), h = Wire.read(), d = Wire.read();
  Wire.read();
  uint8_t mo = Wire.read(), y = Wire.read();
  if (s & 0x80) return false;
  struct tm t = {};
  t.tm_sec = bcd2dec(s & 0x7F); t.tm_min = bcd2dec(mi & 0x7F); t.tm_hour = bcd2dec(h & 0x3F);
  t.tm_mday = bcd2dec(d & 0x3F); t.tm_mon = bcd2dec(mo & 0x1F) - 1; t.tm_year = bcd2dec(y) + 100;
  out = mktime(&t);
  return out > 1700000000;
}

void rtcWriteUTC(time_t e) {
  struct tm t; gmtime_r(&e, &t);
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);
  Wire.write(dec2bcd(t.tm_sec));
  Wire.write(dec2bcd(t.tm_min));
  Wire.write(dec2bcd(t.tm_hour));
  Wire.write(dec2bcd(t.tm_mday));
  Wire.write(t.tm_wday);
  Wire.write(dec2bcd(t.tm_mon + 1));
  Wire.write(dec2bcd(t.tm_year % 100));
  Wire.endTransmission();
}

void setSystemTime(time_t e) {
  struct timeval tv = { e, 0 };
  settimeofday(&tv, nullptr);
  timeValid = true;
}

bool localNow(struct tm &t) {
  if (!timeValid) return false;
  time_t now; time(&now); now += tzMin * 60;
  gmtime_r(&now, &t);
  return true;
}

// ───────────── Motion sensor (QMI8658 accelerometer) ─────────────
#define IMU_ADDR 0x6B

void imuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(IMU_ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}

bool imuReadRaw(int16_t &x, int16_t &y, int16_t &z) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(0x35);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)IMU_ADDR, (uint8_t)6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  if (imuBE) { x = (int16_t)((b[0] << 8) | b[1]); y = (int16_t)((b[2] << 8) | b[3]); z = (int16_t)((b[4] << 8) | b[5]); }
  else       { x = (int16_t)((b[1] << 8) | b[0]); y = (int16_t)((b[3] << 8) | b[2]); z = (int16_t)((b[5] << 8) | b[4]); }
  return true;
}

void imuInit() {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(0x00);                                   // WHO_AM_I
  if (Wire.endTransmission(false) != 0) return;
  if (Wire.requestFrom((uint8_t)IMU_ADDR, (uint8_t)1) != 1) return;
  if (Wire.read() != 0x05) return;
  imuWrite(0x02, 0x40);     // CTRL1: register auto-increment
  imuWrite(0x03, 0x16);     // CTRL2: accel +-4 g, 125 Hz
  imuWrite(0x08, 0x01);     // CTRL7: accelerometer on, gyro off (saves power)
  delay(60);
  // work out the byte order: at rest the reading must be about 1 g
  for (int pass = 0; pass < 2; pass++) {
    imuBE = (pass == 1);
    int good = 0;
    for (int i = 0; i < 5; i++) {
      int16_t x, y, z;
      if (imuReadRaw(x, y, z)) {
        float m = sqrtf((float)x * x + (float)y * y + (float)z * z) / 8192.0f;
        if (m > 0.7f && m < 1.4f) good++;
      }
      delay(12);
    }
    if (good >= 4) { imuOK = true; return; }
  }
  imuBE = false;
}

// Reads the accelerometer. Returns true when the charm was shaken.
bool imuUpdate() {
  if (!imuOK) return false;
  uint32_t now = millis();
  if (now - lastImuRead < (sleeping ? 70u : 20u)) return false;
  lastImuRead = now;
  int16_t rx, ry, rz;
  if (!imuReadRaw(rx, ry, rz)) return false;
  float x = rx / 8192.0f, y = ry / 8192.0f, z = rz / 8192.0f;
  if (tiltBits & 4) { float t = x; x = y; y = t; }
  if (tiltBits & 1) x = -x;
  if (tiltBits & 2) y = -y;
  float dx = x - accX, dy = y - accY, dz = z - accZ;
  float d = sqrtf(dx * dx + dy * dy + dz * dz);
  accX = x; accY = y; accZ = z;
  tiltX = tiltX * 0.8f + x * 0.2f;
  tiltY = tiltY * 0.8f + y * 0.2f;
  if (d > 0.22f) moveTicks++;
  bool shook = false;
  if (d > 0.9f) {
    if (now - shakeHitAt > 700) shakeHits = 0;
    shakeHitAt = now;
    shakeHits++;
    if (shakeHits >= 3 && now - lastShakeAt > 900) { lastShakeAt = now; shakeHits = 0; shook = true; }
  }
  return shook;
}

// ───────────── Text layout ─────────────
void layoutText() {
  String msg = asciiOnly(textMsg);
  for (int s = 6; s >= 1; --s) {
    textLines.clear();
    int maxChars = 220 / (6 * s);
    String line;
    int i = 0, n = msg.length();
    while (i <= n) {
      int j = i;
      while (j < n && msg[j] != ' ' && msg[j] != '\n') j++;
      String word = msg.substring(i, j);
      while ((int)word.length() > maxChars) {
        if (line.length()) { textLines.push_back(line); line = ""; }
        textLines.push_back(word.substring(0, maxChars));
        word = word.substring(maxChars);
      }
      if (line.length() == 0) line = word;
      else if ((int)(line.length() + 1 + word.length()) <= maxChars) line += " " + word;
      else { textLines.push_back(line); line = word; }
      if (j < n && msg[j] == '\n') { textLines.push_back(line); line = ""; }
      i = j + 1;
    }
    if (line.length()) textLines.push_back(line);
    int h = textLines.size() * (8 * s + 4);
    if (h <= 200 || s == 1) { textSize = s; break; }
  }
  marqueeX = SCR_W;
}

// ───────────── Drawing primitives ─────────────
void textCentered(const String &s, int y, uint8_t size, uint16_t color) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  int w = s.length() * 6 * size;
  gfx->setCursor((SCR_W - w) / 2, y);
  gfx->print(s);
}

void verticalGradient(uint16_t top, uint16_t bottom) {
  for (int y = 0; y < SCR_H; y++) gfx->drawFastHLine(0, y, SCR_W, mix565(top, bottom, y * 255 / SCR_H));
}

void sparkle(int x, int y, int r, uint16_t c) {
  gfx->drawFastHLine(x - r, y, 2 * r + 1, c);
  gfx->drawFastVLine(x, y - r, 2 * r + 1, c);
  gfx->drawPixel(x - 1, y - 1, c); gfx->drawPixel(x + 1, y + 1, c);
  gfx->drawPixel(x + 1, y - 1, c); gfx->drawPixel(x - 1, y + 1, c);
}

void drawSparkles(uint16_t c) {
  static const uint8_t pts[][2] = {{24,52},{210,70},{40,230},{200,214},{120,40},{18,150},{224,150},{90,250}};
  for (int i = 0; i < 8; i++) {
    int phase = (frameNo + i * 9) % 40;
    int r = phase < 20 ? phase / 5 : (40 - phase) / 5;
    if (r > 0) sparkle(pts[i][0], pts[i][1], r, c);
  }
}

void drawBolt(int cx, int cy, uint16_t c) {
  gfx->fillTriangle(cx + 2, cy - 7, cx - 4, cy + 1, cx + 1, cy + 1, c);
  gfx->fillTriangle(cx - 1, cy - 1, cx + 4, cy - 1, cx - 2, cy + 7, c);
}

void drawStatusBar(uint16_t fg) {
  gfx->fillCircle(30, 14, 3, bleConnected ? COL_ICE : COL_GREY);
  int x = 190, y = 9;
  gfx->drawRoundRect(x, y, 22, 11, 2, fg);
  gfx->fillRect(x + 22, y + 3, 2, 5, fg);
  if (charging) {
    // animated fill sweeping from the current level to full
    int base = 18 * batPct / 100;
    int sweep = base + (int)((millis() / 60) % (19 - min(base, 18) + 1));
    gfx->fillRect(x + 2, y + 2, max(1, min(18, sweep)), 7, COL_LIME);
    drawBolt(x + 11, y + 5, COL_BLACK);
    drawBolt(x + 10, y + 5, COL_WHITE);
    gfx->setTextSize(1);
    gfx->setTextColor(fg);
    String p = String(batPct) + "%";
    gfx->setCursor(x - 4 - p.length() * 6, y + 2);
    gfx->print(p);
  } else {
    uint16_t bc = batPct > 40 ? COL_LIME : batPct > 15 ? COL_AMBER : COL_RED;
    gfx->fillRect(x + 2, y + 2, max(1, 18 * batPct / 100), 7, bc);
  }
}

void drawToast() {
  if (millis() > toastUntil) return;
  int w = toastMsg.length() * 12 + 24;
  int x = (SCR_W - w) / 2;
  gfx->fillRoundRect(x, 236, w, 28, 14, COL_BLACK);
  gfx->drawRoundRect(x, 236, w, 28, 14, COL_ICE);
  textCentered(toastMsg, 243, 2, COL_WHITE);
}

void drawTransferOverlay() {
  if (!xferActive || xferSize == 0) return;
  gfx->fillRoundRect(30, 116, 180, 52, 12, COL_BLACK);
  gfx->drawRoundRect(30, 116, 180, 52, 12, COL_ICE);
  int pct = (int)(100ULL * xferGot / xferSize);
  textCentered(String("receiving ") + pct + "%", 126, 2, COL_WHITE);
  gfx->fillRoundRect(45, 150, 150, 6, 3, COL_GREY);
  gfx->fillRoundRect(45, 150, max(6, 150 * pct / 100), 6, 3, COL_PINK);
}

// ───────────── Clips (MJPEG on FFat) ─────────────
int jpegDraw(JPEGDRAW *p) {
  gfx->draw16bitRGBBitmap(p->x, p->y, p->pPixels, p->iWidth, p->iHeight);
  return 1;
}

String clipPath(uint32_t id) { return String("/c/") + id + ".clp"; }

void closeClip() {
  if (clipFile) clipFile.close();
  clipHasFrame = false;
}

bool openClip(uint32_t id) {
  closeClip();
  if (!fsOK || id == 0) return false;
  File f = FFat.open(clipPath(id), "r");
  if (!f) return false;
  if (f.read((uint8_t *)&clipHdr, sizeof(clipHdr)) != sizeof(clipHdr) || memcmp(clipHdr.magic, "LCLP", 4) != 0 || clipHdr.frames == 0) {
    f.close();
    return false;
  }
  clipFile = f;
  clipId = id;
  clipNextAt = 0;
  prefs.putULong("clip", clipId);
  return true;
}

std::vector<uint32_t> listClipIds() {
  std::vector<uint32_t> ids;
  if (!fsOK) return ids;
  File dir = FFat.open("/c");
  if (!dir) return ids;
  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String n = f.name();
      int slash = n.lastIndexOf('/');
      if (slash >= 0) n = n.substring(slash + 1);
      if (n.endsWith(".clp")) {
        uint32_t id = strtoul(n.c_str(), nullptr, 10);
        if (id) ids.push_back(id);
      }
    }
    f.close();
    f = dir.openNextFile();
  }
  dir.close();
  std::sort(ids.begin(), ids.end());
  return ids;
}

void openNextClip(int dir) {
  std::vector<uint32_t> ids = listClipIds();
  if (ids.empty()) { closeClip(); clipId = 0; return; }
  size_t idx = 0;
  for (size_t i = 0; i < ids.size(); i++) if (ids[i] == clipId) { idx = i; break; }
  idx = (idx + ids.size() + dir) % ids.size();
  if (!openClip(ids[idx]) && ids.size() > 1) openClip(ids[(idx + 1) % ids.size()]);
}

// Decodes the next frame into the canvas. Returns true if a frame was drawn.
bool clipTick(bool force) {
  if (!clipFile || !frameBuf) return false;
  if (!force && millis() < clipNextAt) return false;
  for (int attempt = 0; attempt < 2; attempt++) {
    uint8_t hdr[6];
    if (clipFile.read(hdr, 6) != 6) { clipFile.seek(sizeof(ClipHdr)); continue; }   // loop
    uint32_t len = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    uint16_t dly = hdr[4] | (hdr[5] << 8);
    if (len == 0 || len > FRAME_MAX) { clipFile.seek(sizeof(ClipHdr)); continue; }
    if (clipFile.read(frameBuf, len) != len) { clipFile.seek(sizeof(ClipHdr)); continue; }
    if (jpeg.openRAM(frameBuf, len, jpegDraw)) {
      jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
      jpeg.decode(0, 0, 0);
      jpeg.close();
      clipHasFrame = true;
    }
    uint32_t now = millis();
    clipNextAt = (clipNextAt == 0 || now - clipNextAt > 250) ? now + dly : clipNextAt + dly;
    return true;
  }
  return false;
}

void drawClipScene(bool allowDecode) {
  if (clipFile) {
    if (allowDecode) clipTick(true);
    if (clipHasFrame) {
      if (millis() < chromeUntil) drawStatusBar(COL_WHITE);
      return;
    }
  }
  verticalGradient(COL_BLACK, COL_GRAPE2);
  drawStatusBar(COL_WHITE);
  gfx->drawRoundRect(70, 86, 100, 80, 12, COL_ICE);
  gfx->fillTriangle(108, 108, 108, 144, 138, 126, COL_PINK);
  textCentered("no clips yet", 188, 2, COL_WHITE);
  textCentered("send a GIF or video", 214, 1, COL_ICE);
  textCentered("from the app", 228, 1, COL_ICE);
}

// ───────────── Animated backgrounds ─────────────
static int8_t sinLut[256];
struct Star  { float x, y, z; };
struct Flake { float x, y, vx, vy; uint8_t r; };
struct Blob  { float x, y, vx, vy, r; };
Star  stars[70];
Flake flakes[70];
Blob  blobs[5];
float matrixY[40];
uint8_t matrixSpd[40];

void initFx() {
  for (int i = 0; i < 256; i++) sinLut[i] = (int8_t)(sinf(i * 6.2831853f / 256.0f) * 127.0f);
  for (int i = 0; i < 70; i++) {
    stars[i].x = (int)random(-1000, 1000) / 1000.0f;
    stars[i].y = (int)random(-1000, 1000) / 1000.0f;
    stars[i].z = (int)random(100, 1000) / 1000.0f;
    flakes[i].x = random(0, SCR_W - 4); flakes[i].y = random(0, SCR_H - 4);
    flakes[i].vx = 0; flakes[i].vy = 0; flakes[i].r = 1 + random(0, 3);
  }
  for (int i = 0; i < 5; i++) {
    blobs[i].x = random(12, 48); blobs[i].y = random(12, 58);
    blobs[i].vx = (int)random(-20, 21) / 100.0f; blobs[i].vy = (int)random(-20, 21) / 100.0f;
    blobs[i].r = 7 + i * 1.5f;
  }
  for (int i = 0; i < 40; i++) { matrixY[i] = -(float)random(0, 35); matrixSpd[i] = random(2, 7); }
}

// fillRect that is safe when partly off screen
void rectC(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > SCR_W) w = SCR_W - x;
  if (y + h > SCR_H) h = SCR_H - y;
  if (w > 0 && h > 0) gfx->fillRect(x, y, w, h, c);
}

void textAt(const String &s, int x, int y, uint8_t size, uint16_t color) {
  gfx->setTextSize(size); gfx->setTextColor(color); gfx->setCursor(x, y); gfx->print(s);
}

// centred text with a hard shadow so it reads on any background
void textPop(const String &s, int y, uint8_t size, uint16_t color) {
  int w = s.length() * 6 * size, x = (SCR_W - w) / 2, o = max(1, size / 2);
  textAt(s, x + o, y + o, size, COL_BLACK);
  textAt(s, x, y, size, color);
}

void bgWarp() {
  gfx->fillScreen(c565(4, 2, 18));
  float cx = 120 + tiltX * 40;
  for (int i = 0; i < 70; i++) {
    Star &s = stars[i];
    s.z -= 0.012f;
    if (s.z <= 0.05f) { s.x = (int)random(-1000, 1000) / 1000.0f; s.y = (int)random(-1000, 1000) / 1000.0f; s.z = 1.0f; }
    int px = (int)(cx + s.x / s.z * 120), py = (int)(140 + s.y / s.z * 140);
    if (px < 0 || px >= SCR_W - 2 || py < 0 || py >= SCR_H - 2) { s.z = 0; continue; }
    int b = (int)(255 * (1.0f - s.z));
    int sz = s.z < 0.3f ? 2 : 1;
    gfx->fillRect(px, py, sz, sz, c565(b, b, min(255, b + 60)));
  }
}

void bgVapor() {
  const int hz = 170;
  for (int y = 0; y < hz; y++) gfx->drawFastHLine(0, y, SCR_W, mix565(c565(20, 6, 60), c565(255, 90, 170), y * 255 / hz));
  for (int dy = -52; dy <= 52; dy++) {                       // striped sun
    int yy = 118 + dy;
    if (yy >= hz) break;
    if (dy > 4 && ((dy + (int)(frameNo / 3)) % 10) < 3) continue;
    int w = (int)sqrtf((float)(52 * 52 - dy * dy));
    if (w > 0) gfx->drawFastHLine(120 - w, yy, 2 * w, mix565(c565(255, 230, 90), c565(255, 80, 140), (dy + 52) * 255 / 104));
  }
  gfx->fillRect(0, hz, SCR_W, SCR_H - hz, c565(14, 4, 40));
  uint16_t gc = c565(90, 220, 255);
  float ph = (frameNo % 20) / 20.0f;
  for (int i = 0; i < 9; i++) {
    float t = (i + ph) / 9.0f;
    int y = hz + (int)(t * t * (SCR_H - hz));
    if (y < SCR_H) gfx->drawFastHLine(0, y, SCR_W, gc);
  }
  for (int i = -8; i <= 8; i++) {                            // floor lines, clipped to the screen
    float x0 = 120 + i * 14, x1 = 120 + i * 90, y1 = SCR_H - 1;
    if (x1 < 0)          { y1 = hz + (0 - x0) / (x1 - x0) * (SCR_H - 1 - hz); x1 = 0; }
    if (x1 > SCR_W - 1)  { y1 = hz + (SCR_W - 1 - x0) / (x1 - x0) * (SCR_H - 1 - hz); x1 = SCR_W - 1; }
    gfx->drawLine((int)x0, hz, (int)x1, (int)y1, gc);
  }
  gfx->drawFastHLine(0, hz, SCR_W, COL_PINK);
}

void bgMatrix() {
  gfx->fillScreen(COL_BLACK);
  gfx->setTextSize(1);
  for (int c = 0; c < 40; c++) {
    matrixY[c] += matrixSpd[c] * 0.12f;
    if (matrixY[c] > 49) { matrixY[c] = -(float)random(0, 20); matrixSpd[c] = random(2, 7); }
    int head = (int)matrixY[c];
    for (int k = 0; k < 12; k++) {
      int row = head - k;
      if (row < 0 || row >= 35) continue;
      int g = 220 - k * 18;
      uint16_t col = (k == 0) ? c565(200, 255, 220) : c565(0, g, g / 4);
      char ch = 33 + ((c * 31 + row * 17 + (int)(frameNo >> 4)) % 90);
      gfx->drawChar(c * 6, row * 8, ch, col, COL_BLACK);
    }
  }
}

void bgPlasma() {
  uint8_t t = frameNo * 3;
  for (int y = 0; y < 70; y++) {
    for (int x = 0; x < 60; x++) {
      int v = sinLut[(uint8_t)(x * 5 + t)] + sinLut[(uint8_t)(y * 4 - t)] + sinLut[(uint8_t)((x + y) * 3 + t / 2)] + sinLut[(uint8_t)(x * 2 - y * 3 + t)];
      gfx->fillRect(x * 4, y * 4, 4, 4, mix565(hue565((uint8_t)(v / 3 + t)), COL_GRAPE2, 110));
    }
  }
}

void bgLava() {
  for (int i = 0; i < 5; i++) {
    Blob &b = blobs[i];
    b.vx += tiltX * 0.006f;
    b.vy += tiltY * 0.006f + sinf(frameNo * 0.012f + i * 1.7f) * 0.012f;
    b.vx = constrain(b.vx * 0.99f, -0.6f, 0.6f);
    b.vy = constrain(b.vy * 0.99f, -0.6f, 0.6f);
    b.x += b.vx; b.y += b.vy;
    if (b.x < 6)  { b.x = 6;  b.vx = fabsf(b.vx); }
    if (b.x > 54) { b.x = 54; b.vx = -fabsf(b.vx); }
    if (b.y < 6)  { b.y = 6;  b.vy = fabsf(b.vy); }
    if (b.y > 64) { b.y = 64; b.vy = -fabsf(b.vy); }
  }
  const uint16_t cBg = c565(40, 8, 60), cEdge = c565(255, 60, 120), cMid = c565(255, 140, 60), cHot = c565(255, 225, 120);
  for (int y = 0; y < 70; y++) {
    for (int x = 0; x < 60; x++) {
      float s = 0;
      for (int i = 0; i < 5; i++) {
        float dx = x - blobs[i].x, dy = y - blobs[i].y;
        s += blobs[i].r * blobs[i].r / (dx * dx + dy * dy + 1.0f);
      }
      gfx->fillRect(x * 4, y * 4, 4, 4, s > 1.7f ? cHot : s > 1.15f ? cMid : s > 0.85f ? cEdge : cBg);
    }
  }
}

// Snow that always falls toward real-world "down", whichever way the charm hangs.
void bgSnow() {
  verticalGradient(c565(8, 20, 60), c565(40, 90, 160));
  float gxv = imuOK ? tiltX : 0, gyv = imuOK ? tiltY : 1;
  for (int i = 0; i < 70; i++) {
    Flake &f = flakes[i];
    f.vx += gxv * 0.06f + (accX - tiltX) * 1.2f + (int)random(-10, 11) * 0.008f;
    f.vy += gyv * 0.06f + (accY - tiltY) * 1.2f + (int)random(-10, 11) * 0.008f;
    f.vx *= 0.93f; f.vy *= 0.93f;
    f.x += f.vx * f.r; f.y += f.vy * f.r;
    if (f.x < 0) f.x += SCR_W - 4;
    if (f.x > SCR_W - 4) f.x -= SCR_W - 4;
    if (f.y < 0) f.y += SCR_H - 4;
    if (f.y > SCR_H - 4) f.y -= SCR_H - 4;
    rectC((int)f.x, (int)f.y, f.r, f.r, COL_WHITE);
  }
}

void bgChecker() {
  int off = (frameNo % 80) / 2;
  const uint16_t a = c565(255, 170, 215), b = c565(170, 225, 255);
  for (int ty = -1; ty < 8; ty++)
    for (int tx = -1; tx < 7; tx++)
      rectC(tx * 40 + off, ty * 40 + off, 40, 40, ((tx + ty) & 1) ? a : b);
}

void drawBackground() {
  switch (bgStyle) {
    case 1: bgWarp(); break;
    case 2: bgVapor(); break;
    case 3: bgMatrix(); break;
    case 4: bgPlasma(); break;
    case 5: bgLava(); break;
    case 6: bgSnow(); break;
    case 7: bgChecker(); break;
    case 8:
      if (photoLoaded && photoBuf) { gfx->draw16bitRGBBitmap(0, 0, photoBuf, SCR_W, SCR_H); break; }
      // no photo yet: fall through to aurora
    default:
      verticalGradient(COL_GRAPE2, COL_ICE2);
      drawSparkles(c565(200, 180, 255));
  }
}

// ───────────── Screens ─────────────
void clockHand(float ang, int len, uint16_t c, int thick) {
  int x1 = 120 + (int)(sinf(ang) * len), y1 = 140 - (int)(cosf(ang) * len);
  for (int o = -(thick / 2); o <= thick / 2; o++) {
    gfx->drawLine(120 + o, 140, x1 + o, y1, c);
    gfx->drawLine(120, 140 + o, x1, y1 + o, c);
  }
}

void drawClock() {
  drawBackground();
  drawStatusBar(COL_WHITE);

  struct tm t;
  if (!localNow(t)) {
    textPop("--:--", 100, 7, COL_WHITE);
    textPop("open the app", 180, 2, COL_ICE);
    textPop("to sync time", 202, 2, COL_ICE);
    return;
  }
  int h = t.tm_hour;
  if (!use24h) { h = h % 12; if (h == 0) h = 12; }
  char buf[24];
  static const char *DAYS[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
  static const char *MON[]  = {"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};

  if (faceStyle == 1) {                                   // analog
    gfx->drawCircle(120, 140, 98, COL_WHITE);
    gfx->drawCircle(120, 140, 97, COL_WHITE);
    for (int i = 0; i < 12; i++) {
      float a = i * 0.5235988f;
      int r0 = (i % 3 == 0) ? 82 : 88;
      gfx->drawLine(120 + (int)(sinf(a) * r0), 140 - (int)(cosf(a) * r0), 120 + (int)(sinf(a) * 95), 140 - (int)(cosf(a) * 95), COL_WHITE);
    }
    clockHand((t.tm_hour % 12 + t.tm_min / 60.0f) * 0.5235988f, 50, COL_BLACK, 7);
    clockHand((t.tm_hour % 12 + t.tm_min / 60.0f) * 0.5235988f, 50, COL_WHITE, 5);
    clockHand((t.tm_min + t.tm_sec / 60.0f) * 0.1047198f, 76, COL_BLACK, 5);
    clockHand((t.tm_min + t.tm_sec / 60.0f) * 0.1047198f, 76, COL_ICE, 3);
    clockHand(t.tm_sec * 0.1047198f, 86, COL_PINK, 1);
    gfx->fillCircle(120, 140, 5, COL_PINK);
    snprintf(buf, sizeof(buf), "%s %02d %s", DAYS[t.tm_wday], t.tm_mday, MON[t.tm_mon]);
    textPop(buf, 256, 1, COL_WHITE);
  } else if (faceStyle == 2) {                            // words
    static const char *HN[12] = {"twelve", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven"};
    int m5 = ((t.tm_min + 2) / 5) * 5, hh = t.tm_hour;
    bool to = m5 > 30;
    if (to) hh++;
    if (m5 == 60) m5 = 0;
    hh %= 12;
    int k = (to && m5) ? 60 - m5 : m5;
    const char *pre = k == 5 ? "five" : k == 10 ? "ten" : k == 15 ? "quarter" : k == 20 ? "twenty" : k == 25 ? "twenty five" : "half";
    if (k == 0) {
      textPop(HN[hh], 108, 4, COL_WHITE);
      textPop("o'clock", 152, 3, COL_ICE);
    } else {
      textPop(pre, 90, 3, COL_ICE);
      textPop(to ? "to" : "past", 126, 3, COL_PINK);
      textPop(HN[hh], 162, 4, COL_WHITE);
    }
  } else if (faceStyle == 3) {                            // bouncing time
    static float bx = 40, by = 60, bvx = 1.6f, bvy = 1.2f;
    static uint16_t bc = COL_WHITE;
    snprintf(buf, sizeof(buf), "%02d:%02d", h, t.tm_min);
    bx += bvx; by += bvy;
    int hit = 0;
    if (bx < 0)           { bx = 0;           bvx = fabsf(bvx);  hit++; }
    if (bx > SCR_W - 154) { bx = SCR_W - 154; bvx = -fabsf(bvx); hit++; }
    if (by < 26)          { by = 26;          bvy = fabsf(bvy);  hit++; }
    if (by > SCR_H - 44)  { by = SCR_H - 44;  bvy = -fabsf(bvy); hit++; }
    if (hit) { bc = hue565((uint8_t)random(0, 256)); if (hit >= 2) sfx(2); }
    gfx->setTextWrap(false);
    textAt(buf, (int)bx + 3, (int)by + 3, 5, COL_BLACK);
    textAt(buf, (int)bx, (int)by, 5, bc);
    gfx->setTextWrap(true);
  } else {                                                // big digital
    bool colon = (millis() / 500) % 2;
    snprintf(buf, sizeof(buf), "%02d%c%02d", h, colon ? ':' : ' ', t.tm_min);
    textPop(buf, 90, 7, COL_WHITE);
    snprintf(buf, sizeof(buf), "%s %02d %s", DAYS[t.tm_wday], t.tm_mday, MON[t.tm_mon]);
    textPop(buf, 162, 2, COL_ICE);
    if (!use24h) textPop(t.tm_hour < 12 ? "AM" : "PM", 186, 2, COL_PINK);
    int w = 160 * t.tm_sec / 59;
    gfx->drawRoundRect(40, 214, 160, 8, 4, COL_WHITE);
    gfx->fillRoundRect(40, 214, max(8, w), 8, 4, COL_PINK);
  }

  if (timerEndAt) {
    uint32_t left = (timerEndAt - millis()) / 1000;
    snprintf(buf, sizeof(buf), "timer %lu:%02lu", (unsigned long)(left / 60), (unsigned long)(left % 60));
    int w = strlen(buf) * 6 + 20, x = (SCR_W - w) / 2;
    gfx->fillRoundRect(x, 28, w, 18, 9, COL_BLACK);
    gfx->drawRoundRect(x, 28, w, 18, 9, COL_PINK);
    textAt(buf, x + 10, 33, 1, COL_WHITE);
  }

  if (charging) {
    String s = String("charging ") + batPct + "%";
    int w = s.length() * 6 + 30;
    int x = (SCR_W - w) / 2;
    gfx->fillRoundRect(x, 236, w, 18, 9, mix565(COL_BLACK, COL_LIME, 70));
    drawBolt(x + 11, 245, COL_LIME);
    gfx->setTextSize(1); gfx->setTextColor(COL_WHITE);
    gfx->setCursor(x + 22, 241); gfx->print(s);
  }
}

void drawText() {
  gfx->fillScreen(textBg);
  drawStatusBar(textFg);
  if (textStyle == 1) {
    String m = asciiOnly(textMsg); m.replace('\n', ' ');
    int sz = 6, w = m.length() * 6 * sz;
    gfx->setTextWrap(false);
    gfx->setTextSize(sz);
    gfx->setTextColor(textFg);
    gfx->setCursor(marqueeX, SCR_H / 2 - 24);
    gfx->print(m);
    gfx->setTextWrap(true);
    marqueeX -= 5;
    if (marqueeX < -w) marqueeX = SCR_W;
    for (int y = 70; y < 210; y += 6) gfx->drawFastHLine(0, y, SCR_W, mix565(textBg, textFg, 25));
    return;
  }
  int lineH = 8 * textSize + 4;
  int y = (SCR_H - (int)textLines.size() * lineH) / 2 + 4;
  for (size_t li = 0; li < textLines.size(); li++) {
    const String &ln = textLines[li];
    int x = (SCR_W - (int)ln.length() * 6 * textSize) / 2;
    gfx->setTextSize(textSize);
    if (textStyle == 2) {
      for (size_t c = 0; c < ln.length(); c++) {
        gfx->setTextColor(hue565((c * 18 + li * 30 + frameNo * 4) & 0xFF));
        gfx->setCursor(x + c * 6 * textSize, y);
        gfx->print(ln[c]);
      }
    } else {
      gfx->setTextColor(mix565(textBg, COL_BLACK, 160));
      gfx->setCursor(x + 2, y + 2); gfx->print(ln);
      gfx->setTextColor(textFg);
      gfx->setCursor(x, y); gfx->print(ln);
    }
    y += lineH;
  }
}

void drawPhoto() {
  if (photoLoaded && photoBuf) {
    gfx->draw16bitRGBBitmap(0, 0, photoBuf, SCR_W, SCR_H);
  } else {
    verticalGradient(COL_ICE2, COL_GRAPE2);
    drawStatusBar(COL_WHITE);
    gfx->drawRoundRect(60, 80, 120, 100, 12, COL_ICE);
    gfx->fillCircle(95, 110, 10, COL_PINK);
    gfx->fillTriangle(70, 170, 120, 120, 170, 170, COL_ICE);
    textCentered("send a photo", 200, 2, COL_WHITE);
    textCentered("from the app", 222, 2, COL_WHITE);
  }
}

uint32_t npPosition() {
  uint32_t p = npPosMs + (npPlaying ? millis() - npStamp : 0);
  return npDurMs ? min(p, npDurMs) : p;
}

void drawMusic() {
  uint16_t dark = mix565(npColor, COL_BLACK, 200);
  verticalGradient(dark, mix565(npColor, COL_BLACK, 120));
  float beat = npPlaying ? (sinf(frameNo * 0.35f) * 0.5f + 0.5f) : 0.2f;
  for (int i = 0; i < 3; i++) {
    int r = 78 + i * 8 + (int)(beat * 6);
    gfx->drawCircle(120, 102, r, mix565(dark, npColor, 200 - i * 60));
  }
  if (artLoaded && artBuf) {
    gfx->fillRoundRect(56, 38, ART_W + 8, ART_W + 8, 10, COL_WHITE);
    gfx->draw16bitRGBBitmap(60, 42, artBuf, ART_W, ART_W);
  } else {
    gfx->fillCircle(120, 102, 62, COL_BLACK);
    for (int r = 58; r > 24; r -= 6) gfx->drawCircle(120, 102, r, c565(40, 40, 48));
    gfx->fillCircle(120, 102, 20, npColor);
    float a = frameNo * (npPlaying ? 0.15f : 0.0f);
    gfx->drawLine(120, 102, 120 + cosf(a) * 56, 102 + sinf(a) * 56, c565(120, 120, 130));
  }
  drawStatusBar(COL_WHITE);

  String title  = asciiOnly(npTitle.length() ? npTitle : String("nothing playing"));
  String artist = asciiOnly(npArtist);
  int tw = title.length() * 12;
  gfx->setTextWrap(false);
  gfx->setTextSize(2);
  gfx->setTextColor(COL_WHITE);
  if (tw <= 210) { gfx->setCursor((SCR_W - tw) / 2, 178); npTitleX = 0; }
  else { gfx->setCursor(15 - npTitleX, 178); npTitleX = (npTitleX + 2) % (tw + 60); }
  gfx->print(title);
  gfx->setTextWrap(true);
  if (artist.length() > 34) artist = artist.substring(0, 33) + ".";
  textCentered(artist, 200, 1, mix565(npColor, COL_WHITE, 160));

  for (int i = 0; i < 16; i++) {
    float v = npPlaying ? (sinf(frameNo * 0.3f + i * 0.9f) * 0.5f + 0.5f) * (0.5f + 0.5f * sinf(i * 1.7f + frameNo * 0.11f)) : 0.08f;
    int h = 3 + (int)(fabsf(v) * 18);
    gfx->fillRect(24 + i * 12, 236 - h, 8, h, mix565(npColor, COL_WHITE, i * 8));
  }
  if (npDurMs) {
    int w = (int)(180.0f * npPosition() / npDurMs);
    gfx->fillRoundRect(30, 244, 180, 5, 2, mix565(dark, COL_WHITE, 60));
    gfx->fillRoundRect(30, 244, max(5, w), 5, 2, COL_WHITE);
  }
}

const char *CAT[12] = {
  "K..........K", "KK........KK", "KBK......KBK", "KBBKKKKKKBBK",
  "KBBBBBBBBBBK", "KBBKBBBBKBBK", "KBBKBBBBKBBK", "KBPBBKKBBPBK",
  "KBBBBBBBBBBK", ".KBBBBBBBBK.", "..KBBBBBBK..", "...KKKKKK..."};
const char *GHOST[12] = {
  "...KKKKKK...", "..KBBBBBBK..", ".KBBBBBBBBK.", "KBBBBBBBBBBK",
  "KBBKBBBBKBBK", "KBBKBBBBKBBK", "KBBBBBBBBBBK", "KBPBBBBBBPBK",
  "KBBBBKKBBBBK", "KBBBBBBBBBBK", "KBKBBKKBBKBK", "KK.KK..KK.KK"};
const char *FROG[12] = {
  ".KKK....KKK.", "KWWKK..KKWWK", "KWKWKKKKWKWK", "KBBBBBBBBBBK",
  "KBBBBBBBBBBK", "KBPBBBBBBPBK", "KBBKBBBBKBBK", "KBBBKKKKBBBK",
  "KBBBBBBBBBBK", ".KBBBBBBBBK.", "KBBK....KBBK", "KKK......KKK"};

void drawSprite(const char *const *spr, int ox, int oy, int scale, uint16_t body, bool blink) {
  int eyeRow = (spr == GHOST) ? 4 : (spr == FROG) ? 2 : 5;
  for (int y = 0; y < 12; y++) {
    for (int x = 0; x < 12; x++) {
      char ch = spr[y][x];
      if (ch == '.') continue;
      uint16_t c = ch == 'K' ? c565(30, 20, 50) : ch == 'W' ? COL_WHITE : ch == 'P' ? COL_PINK : body;
      if (blink && ch == 'K' && x > 0 && x < 11 && y == eyeRow) c = (spr == FROG) ? COL_WHITE : body;
      gfx->fillRect(ox + x * scale, oy + y * scale, scale, scale, c);
    }
  }
}

void drawPet() {
  uint16_t bg1 = mix565(petColor, COL_WHITE, 170), bg2 = mix565(petColor, COL_GRAPE, 120);
  verticalGradient(bg1, bg2);
  for (int y = 200; y < SCR_H; y += 16)
    for (int x = ((y / 16) % 2) * 16; x < SCR_W; x += 32) gfx->fillRect(x, y, 16, 16, mix565(bg2, COL_WHITE, 50));
  drawStatusBar(COL_GRAPE2);
  textCentered(asciiOnly(petName), 34, 3, COL_GRAPE2);

  const char *const *spr = petSpecies == 1 ? GHOST : petSpecies == 2 ? FROG : CAT;
  int bounce = (int)(fabsf(sinf(frameNo * (petMood > 40 ? 0.18f : 0.07f))) * (petMood > 40 ? 14 : 4));
  bool blink = (frameNo % 60) < 3;
  bool dizzy = millis() < petDizzyUntil;
  int slide = imuOK ? constrain((int)(tiltX * 28), -24, 24) : 0;      // leans the way you tilt it
  if (dizzy) slide += (int)(sinf(frameNo * 0.9f) * 6);
  gfx->fillEllipse(120 + slide, 198, 46 - bounce, 7, mix565(bg2, COL_BLACK, 90));
  drawSprite(spr, 60 + slide, 74 - bounce, 10, petColor, blink || dizzy);
  if (dizzy) {
    for (int i = 0; i < 3; i++) {
      float a = frameNo * 0.35f + i * 2.094f;
      sparkle(120 + slide + (int)(cosf(a) * 44), 66 - bounce + (int)(sinf(a) * 9), 4, COL_AMBER);
    }
  }

  if (millis() < petHeartsUntil) {
    for (int i = 0; i < 4; i++) {
      int hx = 40 + i * 52, hy = 80 - (int)((millis() / 20 + i * 17) % 50);
      gfx->fillCircle(hx - 3, hy, 4, COL_PINK); gfx->fillCircle(hx + 3, hy, 4, COL_PINK);
      gfx->fillTriangle(hx - 7, hy + 1, hx + 7, hy + 1, hx, hy + 9, COL_PINK);
    }
  }
  gfx->drawRoundRect(50, 228, 140, 12, 6, COL_GRAPE2);
  gfx->fillRoundRect(52, 230, max(8, 136 * petMood / 100), 8, 4, petMood > 50 ? COL_LIME : petMood > 20 ? COL_AMBER : COL_RED);
  textCentered(dizzy ? "dizzy!!" : petMood > 70 ? "happy!" : petMood > 35 ? "chillin" : "hungry...", 246, 1, COL_GRAPE2);
}

void drawCard() {
  verticalGradient(COL_BLACK, mix565(cardAccent, COL_BLACK, 170));
  for (int i = 0; i < 4; i++) gfx->fillRect(0, 40 + i * 7, SCR_W, 3, mix565(cardAccent, COL_WHITE, 60 + i * 40));
  drawStatusBar(COL_WHITE);
  drawSparkles(cardAccent);
  String name = asciiOnly(cardName), sub = asciiOnly(cardSub);
  textCentered(name, 92, name.length() <= 10 ? 3 : 2, COL_WHITE);
  if (sub.length() > 19) sub = sub.substring(0, 18) + ".";
  textCentered(sub, 128, 2, cardAccent);
  int phase = (frameNo / 4) % 4;
  for (int i = 0; i < 3; i++) {
    uint16_t c = (i < phase) ? cardAccent : mix565(cardAccent, COL_BLACK, 170);
    gfx->drawCircle(120, 205, 16 + i * 12, c);
    gfx->drawCircle(120, 205, 17 + i * 12, c);
  }
  gfx->fillCircle(120, 205, 9, COL_WHITE);
  textCentered("TAP PHONE HERE", 252, 1, COL_WHITE);
}

// ───────────── Toys: magic 8-ball, dice, coin ─────────────
const char *EIGHT[] = {"yes!", "no.", "maybe", "ask\nlater", "for\nsure", "nope", "do it", "bad\nidea", "100%", "lol no",
                       "go for\nit", "sleep\non it", "ask ur\nmom", "signs\nsay yes", "not\ntoday", "duh", "hmm...\nno",
                       "totally", "try\nagain", "vibes\nsay yes"};
#define EIGHT_N ((int)(sizeof(EIGHT) / sizeof(EIGHT[0])))

void textBlock(const char *s, int cy, uint8_t size, uint16_t color) {
  String str(s);
  int n = 1, len = str.length();
  for (int i = 0; i < len; i++) if (str[i] == '\n') n++;
  int lh = 8 * size + 4, y = cy - n * lh / 2 + 2, start = 0;
  for (int i = 0; i <= len; i++) {
    if (i == len || str[i] == '\n') { textCentered(str.substring(start, i), y, size, color); y += lh; start = i + 1; }
  }
}

void toyRoll() {
  touchActivity();
  toyRollAt = millis();
  toyHasResult = true;
  if (toyKind == 0)      toyResult = random(0, EIGHT_N);
  else if (toyKind == 1) { toyResult = 1 + random(0, 6); toyResult2 = 1 + random(0, 6); }
  else                   toyResult = random(0, 2);
  sfx(3);
}

void drawDie(int x, int y, int s, uint8_t v) {
  static const uint16_t P[7] = {0, 0b000010000, 0b100000001, 0b100010001, 0b101000101, 0b101010101, 0b101101101};
  gfx->fillRoundRect(x + 4, y + 4, s, s, 12, COL_BLACK);
  gfx->fillRoundRect(x, y, s, s, 12, COL_WHITE);
  if (v < 1 || v > 6) v = 1;
  for (int i = 0; i < 9; i++)
    if ((P[v] >> (8 - i)) & 1) gfx->fillCircle(x + s / 4 * (1 + i % 3), y + s / 4 * (1 + i / 3), s / 11, COL_GRAPE2);
}

void drawToy() {
  static const char *TN[3] = {"< 8-ball >", "< dice >", "< coin >"};
  uint32_t el = millis() - toyRollAt;
  bool rolling = toyRollAt && el < 1100;
  int jx = rolling ? (int)random(-5, 6) : 0, jy = rolling ? (int)random(-5, 6) : 0;

  if (toyKind == 0) {
    verticalGradient(c565(30, 10, 70), c565(8, 4, 24));
    gfx->fillCircle(120 + jx, 160 + jy, 94, COL_BLACK);
    gfx->drawCircle(120 + jx, 160 + jy, 94, c565(70, 70, 90));
    gfx->fillCircle(86 + jx, 112 + jy, 13, c565(60, 60, 80));
    gfx->fillCircle(120 + jx, 164 + jy, 60, c565(20, 40, 160));
    gfx->drawCircle(120 + jx, 164 + jy, 60, COL_ICE);
    if (rolling)           textBlock("...", 164, 3, COL_WHITE);
    else if (toyHasResult) textBlock(EIGHT[toyResult % EIGHT_N], 164, 2, COL_WHITE);
    else                   textBlock(imuOK ? "shake\nme" : "tap\nme", 164, 2, COL_WHITE);
  } else if (toyKind == 1) {
    verticalGradient(c565(10, 90, 60), c565(4, 40, 30));
    uint8_t a = rolling ? 1 + random(0, 6) : toyResult, b = rolling ? 1 + random(0, 6) : toyResult2;
    if (!toyHasResult) { a = 5; b = 2; }
    drawDie(26 + jx, 104 + jy, 88, a);
    drawDie(126 - jx, 104 - jy, 88, b);
    if (toyHasResult && !rolling) textPop(String("= ") + (toyResult + toyResult2), 216, 4, COL_WHITE);
    else if (!toyHasResult)       textPop(imuOK ? "shake to roll" : "tap to roll", 224, 2, COL_WHITE);
  } else {
    verticalGradient(c565(70, 30, 10), c565(24, 8, 40));
    const uint16_t gold = c565(255, 200, 60), dark = c565(190, 130, 20);
    if (rolling) {
      int rx = max(3, (int)(fabsf(sinf(el * 0.02f)) * 70));
      gfx->fillEllipse(120, 158 - (int)(sinf(el * 3.14159f / 1100.0f) * 50), rx, 70, gold);
    } else {
      gfx->fillCircle(120, 158, 72, dark);
      gfx->fillCircle(120, 158, 66, gold);
      gfx->drawCircle(120, 158, 58, dark);
      if (toyHasResult) {
        textAt(toyResult ? "T" : "H", 120 - 21, 158 - 28, 7, dark);
        textPop(toyResult ? "tails" : "heads", 244, 2, COL_WHITE);
      } else {
        textAt("?", 120 - 21, 158 - 28, 7, dark);
        textPop(imuOK ? "shake to flip" : "tap to flip", 244, 2, COL_WHITE);
      }
    }
  }
  drawStatusBar(COL_WHITE);
  textPop(TN[toyKind % 3], 30, 2, COL_WHITE);
}

void drawAlarm() {
  if (!alarmUntil) return;
  bool flash = (millis() / 300) % 2;
  gfx->fillRoundRect(16, 92, 208, 96, 18, flash ? COL_PINK : COL_BLACK);
  gfx->drawRoundRect(16, 92, 208, 96, 18, COL_WHITE);
  textCentered("time's up!", 116, 3, COL_WHITE);
  textCentered("tap to stop", 160, 1, COL_WHITE);
}

void drawScene() {
  switch (mode) {
    case M_CLOCK: drawClock(); break;
    case M_TEXT:  drawText();  break;
    case M_PHOTO: drawPhoto(); break;
    case M_CLIP:  drawClipScene(!xferActive); break;
    case M_MUSIC: drawMusic(); break;
    case M_PET:   drawPet();   break;
    case M_TOY:   drawToy();   break;
    case M_CARD:  drawCard();  break;
  }
}

void render() {
  drawScene();
  drawAlarm();
  drawTransferOverlay();
  drawToast();
  gfx->flush();
  frameNo++;
}

void setMode(int m, bool announce = true) {
  mode = (m + M_COUNT) % M_COUNT;
  prefs.putUChar("mode", mode);
  if (mode == M_CLIP) {
    if (!clipFile) { if (!openClip(clipId)) openNextClip(0); }
    clipNextAt = 0;
    chromeUntil = millis() + 1500;
  }
  if (announce) toast(MODE_NAMES[mode], 700);
}

// Slide the new screen in. dir > 0: content moves left (next), dir < 0: moves right.
void transitionTo(int newMode, int dir) {
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb || !prevFb || !mixFb || sleeping) { setMode(newMode); return; }
  sfx(1);
  memcpy(prevFb, fb, FB_BYTES);
  setMode(newMode, false);
  drawScene();
  const int steps = 7;
  for (int s = 1; s <= steps; s++) {
    float t = (float)s / steps;
    t = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);   // ease-out
    int off = (s == steps) ? SCR_W : (int)(SCR_W * t);
    for (int y = 0; y < SCR_H; y++) {
      uint16_t *dst = mixFb + y * SCR_W, *o = prevFb + y * SCR_W, *n = fb + y * SCR_W;
      if (dir > 0) {
        memcpy(dst, o + off, (SCR_W - off) * 2);
        memcpy(dst + SCR_W - off, n, off * 2);
      } else {
        memcpy(dst, n + SCR_W - off, off * 2);
        memcpy(dst + off, o, (SCR_W - off) * 2);
      }
    }
    panel->draw16bitRGBBitmap(0, 0, mixFb, SCR_W, SCR_H);
  }
  toast(MODE_NAMES[mode], 600);
  drawTransferOverlay();
  drawToast();
  gfx->flush();
  frameNo++;
  lastFrame = millis();
}

// ───────────── Status notifications ─────────────
void notifyText(const char *s) {
  if (!statusChar || !bleConnected) return;
  statusChar->setValue((const uint8_t *)s, strlen(s));
  statusChar->notify();
}

void sendStatus(const char *extra = nullptr) {
  char buf[220];
  unsigned long used = fsOK ? (unsigned long)FFat.usedBytes() : 0;
  unsigned long total = fsOK ? (unsigned long)FFat.totalBytes() : 0;
  snprintf(buf, sizeof(buf), "b=%d;v=%.2f;c=%d;m=%d;fw=%s;fs=%lu,%lu;clip=%lu;lk=%d,%d;imu=%d;toy=%d%s%s",
           batPct, batVolts, charging ? 1 : 0, mode, FW_VERSION, used, total, (unsigned long)clipId,
           bgStyle, faceStyle, imuOK ? 1 : 0, toyKind,
           extra ? ";" : "", extra ? extra : "");
  notifyText(buf);
}

void sendClipList() {
  std::vector<uint32_t> ids = listClipIds();
  for (uint32_t id : ids) {
    File f = FFat.open(clipPath(id), "r");
    if (!f) continue;
    ClipHdr h;
    if (f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && memcmp(h.magic, "LCLP", 4) == 0) {
      char nm[25]; memcpy(nm, h.name, 24); nm[24] = 0;
      char buf[96];
      snprintf(buf, sizeof(buf), "item=%lu,%lu,%lu,%s", (unsigned long)id, (unsigned long)h.frames, (unsigned long)f.size(), nm);
      notifyText(buf);
      delay(25);
    }
    f.close();
  }
  sendStatus("list=end");
}

// ───────────── Transfers ─────────────
void xferCleanup() {
  if (xferFile) xferFile.close();
  if (xferKind == 2 && fsOK) FFat.remove("/c/tmp.part");
  xferActive = false;
  xferBufLen = 0;
}

void xferFlush() {
  if (xferKind == 2 && xferFile && xferBufLen) { xferFile.write(xferBuf, xferBufLen); xferBufLen = 0; }
}

static uint32_t rdU32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

void xferBegin(const uint8_t *p, uint16_t len) {
  if (xferActive) xferCleanup();
  if (len < 5) { sendStatus("xfer=err;why=bad"); return; }
  xferKind = p[0];
  xferSize = rdU32(p + 1);
  xferId = len >= 9 ? rdU32(p + 5) : 0;
  xferGot = xferAcked = 0;
  xferBufLen = 0;
  bool ok = false;
  const char *why = "bad";
  if (xferKind == 0)      { ok = photoBuf && xferSize == FB_BYTES; if (ok) photoLoaded = false; }
  else if (xferKind == 1) { ok = artBuf && xferSize == ART_W * ART_W * 2; }
  else if (xferKind == 2) {
    if (!fsOK) why = "nofs";
    else if (xferId == 0 || xferSize <= sizeof(ClipHdr)) why = "bad";
    else if ((uint64_t)xferSize + 65536 > (uint64_t)FFat.freeBytes()) why = "space";
    else {
      FFat.remove("/c/tmp.part");
      xferFile = FFat.open("/c/tmp.part", "w");
      ok = (bool)xferFile && xferBuf;
      if (!ok) why = "open";
    }
  }
  if (!ok) { char b[32]; snprintf(b, sizeof(b), "xfer=err;why=%s", why); sendStatus(b); return; }
  xferActive = true;
  xferAbort = false;
}

void xferData(const uint8_t *p, uint16_t len) {
  if (!xferActive) return;
  uint32_t take = min<uint32_t>(len, xferSize - xferGot);
  if (xferKind == 0)      memcpy((uint8_t *)photoBuf + xferGot, p, take);
  else if (xferKind == 1) memcpy((uint8_t *)artBuf + xferGot, p, take);
  else {
    uint32_t off = 0;
    while (off < take) {
      uint32_t n = min<uint32_t>(take - off, XBUF_SZ - xferBufLen);
      memcpy(xferBuf + xferBufLen, p + off, n);
      xferBufLen += n; off += n;
      if (xferBufLen == XBUF_SZ) xferFlush();
    }
  }
  xferGot += take;
}

void xferEnd() {
  if (!xferActive || xferGot != xferSize) { xferCleanup(); sendStatus("xfer=err;why=short"); return; }
  if (xferKind == 0) {
    photoLoaded = true;
    if (fsOK) { File f = FFat.open("/photo.bin", "w"); if (f) { f.write((uint8_t *)photoBuf, FB_BYTES); f.close(); } }
    xferActive = false;
    setMode(M_PHOTO, false);
  } else if (xferKind == 1) {
    artLoaded = true;
    xferActive = false;
  } else {
    xferFlush();
    xferFile.close();
    // validate
    File f = FFat.open("/c/tmp.part", "r");
    ClipHdr h;
    bool ok = f && f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && memcmp(h.magic, "LCLP", 4) == 0 && h.frames > 0;
    if (f) f.close();
    if (!ok) { xferCleanup(); sendStatus("xfer=err;why=format"); return; }
    if (clipId == xferId) closeClip();
    FFat.remove(clipPath(xferId));
    FFat.rename("/c/tmp.part", clipPath(xferId));
    xferActive = false;
    openClip(xferId);
    setMode(M_CLIP, false);
  }
  char b[32]; snprintf(b, sizeof(b), "ack=%lu", (unsigned long)xferGot);
  notifyText(b);
  sendStatus("xfer=ok");
}

// ───────────── Command handling ─────────────
void handlePacket(const uint8_t *d, uint16_t n) {
  if (n == 0) return;
  if (d[0] != CMD_XFER_DATA) touchActivity();
  else lastInteraction = millis();
  const uint8_t *p = d + 1; uint16_t len = n - 1;
  switch (d[0]) {
    case CMD_MODE: if (len >= 1) setMode(p[0]); break;

    case CMD_TEXT:
      if (len >= 5) {
        textStyle = p[0] % 3;
        textFg = (p[1] << 8) | p[2];
        textBg = (p[3] << 8) | p[4];
        textMsg = String((const char *)(p + 5), len - 5);
        layoutText();
        prefs.putString("text", textMsg); prefs.putUChar("tstyle", textStyle);
        prefs.putUShort("tfg", textFg);   prefs.putUShort("tbg", textBg);
        setMode(M_TEXT, false);
      }
      break;

    case CMD_TIME:
      if (len >= 6) {
        time_t e = rdU32(p);
        tzMin = (int16_t)(p[4] | (p[5] << 8));
        setSystemTime(e);
        rtcWriteUTC(e);
        prefs.putShort("tz", tzMin);
      }
      break;

    case CMD_NOWPLAYING:
      if (len >= 12) {
        npColor = c565(p[0], p[1], p[2]);
        npPlaying = p[3];
        npPosMs = rdU32(p + 4);
        npDurMs = rdU32(p + 8);
        npStamp = millis();
        String s((const char *)(p + 12), len - 12);
        int sep = s.indexOf('\x1F');
        String newTitle = sep >= 0 ? s.substring(0, sep) : s;
        if (newTitle != npTitle) { artLoaded = false; npTitleX = 0; }
        npTitle = newTitle;
        npArtist = sep >= 0 ? s.substring(sep + 1) : "";
      }
      break;

    case CMD_XFER_BEGIN: xferBegin(p, len); break;
    case CMD_XFER_DATA:  xferData(p, len);  break;
    case CMD_XFER_END:   xferEnd();         break;

    case CMD_BRIGHTNESS:
      if (len >= 1) { setBacklight(p[0]); prefs.putUChar("bright", brightness); }
      break;

    case CMD_PET:
      if (len >= 4) {
        petSpecies = p[0] % 3;
        petColor = c565(p[1], p[2], p[3]);
        if (len > 4) petName = String((const char *)(p + 4), len - 4);
        prefs.putUChar("petsp", petSpecies); prefs.putUShort("petcol", petColor);
        prefs.putString("petname", petName);
        setMode(M_PET, false);
      }
      break;

    case CMD_PET_FEED:
      petMood = min(100, petMood + 25);
      petHeartsUntil = millis() + 2500;
      prefs.putInt("petmood", petMood);
      setMode(M_PET, false);
      break;

    case CMD_CARD:
      if (len >= 3) {
        cardAccent = c565(p[0], p[1], p[2]);
        String s((const char *)(p + 3), len - 3);
        int sep = s.indexOf('\x1F');
        cardName = sep >= 0 ? s.substring(0, sep) : s;
        cardSub  = sep >= 0 ? s.substring(sep + 1) : "";
        prefs.putString("cname", cardName); prefs.putString("csub", cardSub);
        prefs.putUShort("cacc", cardAccent);
        setMode(M_CARD, false);
      }
      break;

    case CMD_SETTINGS:
      if (len >= 3) {
        sleepSec = p[0] | (p[1] << 8);
        swapSwipe = p[2];
        prefs.putUShort("sleep", sleepSec); prefs.putBool("swap", swapSwipe);
        if (len >= 4) { soundsOn = p[3] & 1; shakeWake = p[3] & 2; hourChime = p[3] & 4; prefs.putUChar("flags", p[3] & 7); }
        if (len >= 5) { tiltBits = p[4] & 7; prefs.putUChar("tilt", tiltBits); }
      }
      break;

    case CMD_LOOK:
      if (len >= 2) {
        bgStyle = p[0] % BG_COUNT;
        faceStyle = p[1] % FACE_COUNT;
        prefs.putUChar("bg", bgStyle); prefs.putUChar("face", faceStyle);
        setMode(M_CLOCK, false);
      }
      break;

    case CMD_TIMER:
      if (len >= 2) {
        uint32_t sec = p[0] | (p[1] << 8);
        alarmUntil = 0;
        timerEndAt = sec ? (millis() + sec * 1000UL) : 0;
        if (timerEndAt == 0 && sec) timerEndAt = 1;
        toast(sec ? "timer set" : "timer off", 900);
      }
      break;

    case CMD_TOY:
      if (len >= 1) {
        if (p[0] % 3 != toyKind) { toyKind = p[0] % 3; toyHasResult = false; toyRollAt = 0; prefs.putUChar("toy", toyKind); }
        setMode(M_TOY, false);
        if (len >= 2 && p[1]) toyRoll();
      }
      break;

    case CMD_PING:      sendStatus(); break;
    case CMD_BUZZ:      beep(len >= 1 ? constrain((int)p[0], 1, 10) : 3); break;
    case CMD_CLIP_LIST: sendClipList(); break;

    case CMD_CLIP_PLAY:
      if (len >= 4 && openClip(rdU32(p))) setMode(M_CLIP, false);
      sendStatus();
      break;

    case CMD_CLIP_DELETE:
      if (len >= 4 && fsOK) {
        uint32_t id = rdU32(p);
        bool wasCurrent = (id == clipId);
        if (wasCurrent) closeClip();
        FFat.remove(clipPath(id));
        if (wasCurrent) { clipId = 0; openNextClip(0); }
      }
      sendClipList();
      break;
  }
}

// ───────────── BLE ─────────────
class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, NimBLEConnInfo &info) override {
    bleConnected = true;
    toast("connected", 1000);
  }
  void onDisconnect(NimBLEServer *s, NimBLEConnInfo &info, int reason) override {
    bleConnected = false;
    xferAbort = true;
    NimBLEDevice::startAdvertising();
  }
};

class RxCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
    static Packet pkt;
    NimBLEAttValue v = c->getValue();
    pkt.len = min<size_t>(v.length(), sizeof(pkt.data));
    memcpy(pkt.data, v.data(), pkt.len);
    xQueueSend(rxQueue, &pkt, pdMS_TO_TICKS(3000));
  }
};

void bleInit() {
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setMTU(517);
  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCB());
  NimBLEService *svc = server->createService(SVC_UUID);
  NimBLECharacteristic *rx = svc->createCharacteristic(RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new RxCB());
  statusChar = svc->createCharacteristic(STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  statusChar->setValue((const uint8_t *)"hello", 5);
  svc->start();
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);
  NimBLEAdvertisementData scan;
  scan.setName(DEVICE_NAME);
  adv->setScanResponseData(scan);
  adv->enableScanResponse(true);
  adv->start();
}

// ───────────── Touch: coordinate-based gestures ─────────────
void IRAM_ATTR onTouchInt() { touchFlag = true; }

void touchWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(CST816_ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}

void touchInit() {
  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);  delay(10);
  digitalWrite(TP_RST, HIGH); delay(60);
  touchWrite(0xFE, 0x01);   // DisAutoSleep: stay awake so every touch registers
  touchWrite(0xFA, 0x70);   // IrqCtl: interrupt on touch, change and motion
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), onTouchInt, FALLING);
}

bool readTouch(int16_t &x, int16_t &y, uint8_t &fingers) {
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)CST816_ADDR, (uint8_t)5) != 5) return false;
  fingers = Wire.read() & 0x0F;
  uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;
  return true;
}

struct { bool down = false; bool consumed = false; int16_t x0 = 0, y0 = 0, x = 0, y = 0; uint32_t t0 = 0; } tch;
uint32_t lastTouchPoll = 0, lastTapAt = 0;

#define SWIPE_PX      40   // distance that commits a swipe while the finger is still down
#define FLICK_PX      22   // shorter distance accepted on release if it was quick
#define TAP_PX        16
#define LONGPRESS_MS 650

void onSwipe(int dir) {        // dir +1 = next
  if (swapSwipe) dir = -dir;
  transitionTo(mode + dir, dir);
}

void onVSwipe(int up) {
  setBacklight(brightness + (up > 0 ? 40 : -40));
  prefs.putUChar("bright", brightness);
  toast(up > 0 ? "bright +" : "bright -", 500);
}

void onTap(bool dbl) {
  sfx(0);
  if (mode == M_TOY) {
    if (tch.y0 < 70) { toyKind = (toyKind + 1) % 3; toyHasResult = false; toyRollAt = 0; prefs.putUChar("toy", toyKind); }
    else toyRoll();
  }
  if (mode == M_TEXT)  { textStyle = (textStyle + 1) % 3; prefs.putUChar("tstyle", textStyle); marqueeX = SCR_W; }
  if (mode == M_CLOCK) { use24h = !use24h; prefs.putBool("h24", use24h); }
  if (mode == M_CLIP)  {
    openNextClip(1);
    if (clipFile) { char nm[25]; memcpy(nm, clipHdr.name, 24); nm[24] = 0; toast(asciiOnly(String(nm)).substring(0, 14), 900); }
    chromeUntil = millis() + 1500;
  }
  if (mode == M_PET) {
    petMood = min(100, petMood + (dbl ? 25 : 3));
    petHeartsUntil = millis() + (dbl ? 2500 : 1500);
    if (dbl) { prefs.putInt("petmood", petMood); toast("yum!", 700); }
  }
}

void onShake() {
  if (sleeping) {
    if (shakeWake) { touchActivity(); sfx(0); }
    return;
  }
  touchActivity();
  switch (mode) {
    case M_CLOCK:
      bgStyle = (bgStyle + 1) % BG_COUNT;
      if (bgStyle == 8 && !photoLoaded) bgStyle = 0;
      prefs.putUChar("bg", bgStyle);
      toast(BG_NAMES[bgStyle], 800);
      sfx(1);
      break;
    case M_PET:
      petDizzyUntil = millis() + 3000;
      toast("wheee!", 900);
      sfx(3);
      break;
    case M_TOY:
      toyRoll();
      break;
    case M_TEXT:
      textStyle = (textStyle + 1) % 3;
      prefs.putUChar("tstyle", textStyle);
      marqueeX = SCR_W;
      break;
    default: break;
  }
}

void handleTouch() {
  uint32_t now = millis();
  if (!touchFlag && now - lastTouchPoll < 12) return;
  if (sleeping && !touchFlag && now - lastTouchPoll < 60) return;
  lastTouchPoll = now;
  touchFlag = false;
  int16_t x, y; uint8_t fingers;
  if (!readTouch(x, y, fingers)) return;

  if (fingers > 0) {
    if (!tch.down) {
      tch.down = true; tch.consumed = false;
      tch.x0 = tch.x = x; tch.y0 = tch.y = y; tch.t0 = now;
      if (alarmUntil) { alarmUntil = 0; noTone(BUZZER); touchActivity(); tch.consumed = true; return; }
      if (sleeping) { touchActivity(); tch.consumed = true; return; }   // first touch only wakes
      touchActivity();
    }
    tch.x = x; tch.y = y;
    lastInteraction = now;
    if (tch.consumed) return;
    int dx = x - tch.x0, dy = y - tch.y0;
    if (abs(dx) >= SWIPE_PX && abs(dx) > abs(dy)) { tch.consumed = true; onSwipe(dx < 0 ? 1 : -1); return; }
    if (abs(dy) >= SWIPE_PX && abs(dy) > abs(dx)) { tch.consumed = true; onVSwipe(dy < 0 ? 1 : -1); return; }
    if (now - tch.t0 > LONGPRESS_MS && abs(dx) < TAP_PX && abs(dy) < TAP_PX) {
      tch.consumed = true;
      transitionTo(M_CARD, 1);
    }
  } else if (tch.down) {
    tch.down = false;
    if (tch.consumed) return;
    int dx = tch.x - tch.x0, dy = tch.y - tch.y0;
    uint32_t dt = now - tch.t0;
    if (abs(dx) < TAP_PX && abs(dy) < TAP_PX) {
      bool dbl = (now - lastTapAt) < 350;
      lastTapAt = dbl ? 0 : now;
      onTap(dbl);
    } else if (dt < 400 && abs(dx) >= FLICK_PX && abs(dx) > abs(dy)) {
      onSwipe(dx < 0 ? 1 : -1);
    } else if (dt < 400 && abs(dy) >= FLICK_PX && abs(dy) > abs(dx)) {
      onVSwipe(dy < 0 ? 1 : -1);
    }
  }
}

// ───────────── Power & battery ─────────────
void handleButton() {
  static bool armed = false;
  static uint32_t downAt = 0;
  static bool wasDown = false;
  bool down = digitalRead(SYS_OUT) == LOW;
  if (!armed) { if (!down) armed = true; return; }

  if (down && !wasDown) downAt = millis();
  if (down && millis() - downAt > 2000) {
    gfx->fillScreen(COL_BLACK);
    textCentered("bye!", 120, 4, COL_PINK);
    gfx->flush();
    tone(BUZZER, 1800, 120); delay(200); noTone(BUZZER);
    savePrefs();
    digitalWrite(SYS_EN, LOW);
    delay(1500);
    toast("on USB power", 1500);
    armed = false;
    wasDown = true;
    return;
  }
  if (!down && wasDown && millis() - downAt < 600) {
    if (alarmUntil) { alarmUntil = 0; noTone(BUZZER); touchActivity(); }
    else if (sleeping) touchActivity();
    else { touchActivity(); transitionTo(mode + 1, 1); }
  }
  wasDown = down;
}

int voltsToPct(float v) {
  const float V[] = {3.30, 3.60, 3.70, 3.75, 3.80, 3.90, 4.00, 4.10, 4.20};
  const int   P[] = {0,    8,    20,   35,   48,   65,   80,   92,   100};
  if (v >= V[8]) return 100;
  if (v <= V[0]) return 0;
  for (int i = 0; i < 8; i++)
    if (v < V[i + 1]) return P[i] + (int)((v - V[i]) / (V[i + 1] - V[i]) * (P[i + 1] - P[i]));
  return 100;
}

void updateBattery() {
  uint32_t now = millis();
  if (lastBatRead && now - lastBatRead < 5000) return;
  lastBatRead = now;
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(BAT_ADC);
  float v = (mv / 16) * 3.0f / 1000.0f;
  batVolts = (vHistN == 0) ? v : batVolts * 0.7f + v * 0.3f;

  // 60 s history to spot a rising voltage (charging from a wall adapter)
  float oldest = vHist[vHistN % 12];
  bool rising = vHistN >= 12 && (batVolts - oldest) > 0.012f;
  vHist[vHistN % 12] = batVolts;
  vHistN++;

  bool usb = Serial.isPlugged();          // connected to a computer
  if (usb || rising || batVolts > 4.23f) chargeSeenAt = now;
  bool wasCharging = charging;
  charging = chargeSeenAt && (now - chargeSeenAt < 90000);
  if (charging && !wasCharging) { touchActivity(); toast("charging", 1500); }

  // the charger lifts the voltage, so read a little lower while charging
  int pct = voltsToPct(charging ? batVolts - 0.08f : batVolts);
  batPct = constrain(pct, 0, 100);
}

void updatePet() {
  if (millis() - petLastDecay > 10UL * 60 * 1000) {
    petLastDecay = millis();
    if (petMood > 0) { petMood--; prefs.putInt("petmood", petMood); }
  }
  if (moveTicks > 400) {              // carrying it around counts as a walk
    moveTicks = 0;
    if (petMood < 100) petMood++;
  }
}

void serviceTimers() {
  uint32_t now = millis();
  if (timerEndAt && (int32_t)(now - timerEndAt) >= 0) {
    timerEndAt = 0;
    alarmUntil = now + 20000;
    alarmNextBeep = 0;
    touchActivity();
  }
  if (alarmUntil) {
    if (now > alarmUntil) alarmUntil = 0;
    else if (now >= alarmNextBeep) {
      alarmNextBeep = now + 900;
      tone(BUZZER, 2093, 110); tone(BUZZER, 2637, 110); tone(BUZZER, 3136, 160);
      lastInteraction = now;
    }
  }
  static uint32_t lastChk = 0;
  if (hourChime && now - lastChk > 1000) {
    lastChk = now;
    struct tm t;
    if (localNow(t) && t.tm_min == 0 && t.tm_hour != lastChimeHour) {
      lastChimeHour = t.tm_hour;
      tone(BUZZER, 2093, 90); tone(BUZZER, 2637, 140);
    }
  }
}

// ───────────── Setup / loop ─────────────
void setup() {
  pinMode(SYS_EN, OUTPUT);
  digitalWrite(SYS_EN, HIGH);
  pinMode(SYS_OUT, INPUT);
  pinMode(BUZZER, OUTPUT);

  Serial.begin(115200);
  setCpuFrequencyMhz(240);

  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  prefs.begin("charm", false);
  loadPrefs();

  if (!gfx->begin(80000000)) Serial.println("display init failed");
  gfx->fillScreen(COL_BLACK);
  gfx->flush();
  pinMode(LCD_BL, OUTPUT);
  analogWrite(LCD_BL, brightness);

  verticalGradient(COL_GRAPE, COL_ICE);
  textCentered("LARP", 100, 5, COL_WHITE);
  textCentered("CHARM", 146, 5, COL_GRAPE2);
  gfx->flush();

  touchInit();
  imuInit();
  initFx();
  sfx(2);

  photoBuf = (uint16_t *)ps_malloc(FB_BYTES);
  artBuf   = (uint16_t *)ps_malloc(ART_W * ART_W * 2);
  prevFb   = (uint16_t *)ps_malloc(FB_BYTES);
  mixFb    = (uint16_t *)ps_malloc(FB_BYTES);
  frameBuf = (uint8_t *)ps_malloc(FRAME_MAX);
  xferBuf  = (uint8_t *)malloc(XBUF_SZ);
  if (!xferBuf) xferBuf = (uint8_t *)ps_malloc(XBUF_SZ);

  fsOK = FFat.begin(true);
  if (!fsOK) Serial.println("FFat mount failed - check Partition Scheme (16M Flash 3MB APP/9.9MB FATFS)");
  if (fsOK) {
    if (!FFat.exists("/c")) FFat.mkdir("/c");
    FFat.remove("/c/tmp.part");
    File f = FFat.open("/photo.bin", "r");
    if (f && photoBuf && f.size() == FB_BYTES) { f.read((uint8_t *)photoBuf, FB_BYTES); photoLoaded = true; }
    if (f) f.close();
    if (!openClip(clipId)) openNextClip(0);
  }

  time_t e;
  if (rtcReadUTC(e)) setSystemTime(e);

  layoutText();
  rxQueue = xQueueCreate(32, sizeof(Packet));
  bleInit();
  updateBattery();

  delay(500);
  lastInteraction = millis();
  if (mode == M_CLIP) setMode(M_CLIP, false);
}

void loop() {
  static Packet pkt;
  bool gotData = false;
  int handled = 0;
  while (handled < 40 && xQueueReceive(rxQueue, &pkt, 0) == pdTRUE) {
    handlePacket(pkt.data, pkt.len);
    if (pkt.len && pkt.data[0] == CMD_XFER_DATA) gotData = true;
    handled++;
  }
  if (xferAbort) { xferAbort = false; if (xferActive) xferCleanup(); }
  if (gotData && xferActive && xferGot != xferAcked && uxQueueMessagesWaiting(rxQueue) == 0) {
    char b[24]; snprintf(b, sizeof(b), "ack=%lu", (unsigned long)xferGot);
    notifyText(b);
    xferAcked = xferGot;
  }

  handleTouch();
  handleButton();
  if (imuUpdate()) onShake();
  serviceTimers();
  updateBattery();
  updatePet();

  if (!sleeping && sleepSec > 0 && !xferActive && !alarmUntil && millis() - lastInteraction > sleepSec * 1000UL) {
    sleeping = true;
    analogWrite(LCD_BL, 0);
  }

  if (bleConnected && millis() - lastStatus > 15000) { lastStatus = millis(); sendStatus(); }

  if (sleeping) { delay(10); return; }

  if (mode == M_CLIP && clipFile && !xferActive && !alarmUntil) {
    // clips run at their own frame rate
    if (clipTick(false)) {
      if (millis() < chromeUntil) drawStatusBar(COL_WHITE);
      drawToast();
      gfx->flush();
      frameNo++;
    }
    return;
  }

  uint32_t interval = xferActive ? 150 : 33;
  if (millis() - lastFrame >= interval) {
    lastFrame = millis();
    render();
  }
}

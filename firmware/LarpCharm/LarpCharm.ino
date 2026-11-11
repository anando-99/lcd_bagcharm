/*
  LARP CHARM firmware v4.0  "LARP//OS"
  Board : Waveshare ESP32-S3-Touch-LCD-1.69 (240x280 ST7789V2, CST816T touch)
  Control: Bluetooth LE from the Larp Charm web app

  One fused screen, the DECK, shows everything live: time, the song that is playing,
  the pet, the motion sensor, your message. Tap a block to open it full screen.

  Touch:
    tap a block on the deck -> open it          swipe down -> close it, back to the deck
    swipe left / right      -> deck: colour theme · chrono: background · audio: next / previous song
                               vid: next clip · msg: style · rng: switch toy
    tap                     -> chrono: clock style (double tap 12/24h) · audio: left prev, middle play/pause, right next
                               pet: pat (double tap feeds) · rng: roll
    swipe up / down on deck -> brightness
    press and hold          -> ID card (NFC "tap here"), from anywhere
  Shake: deck / chrono = next background · pet = dizzy · rng = roll · asleep = wake
  PWR button: short = back to the deck (on the deck: screen off), hold 2 s = power off (battery only)

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
#define FW_VERSION  "4.0"
#define DEVICE_NAME "Larp Charm"

enum Cmd : uint8_t {
  CMD_MODE        = 0x01, // [mode]
  CMD_TEXT        = 0x02, // [style][fg565 hi lo][bg565 hi lo] text
  CMD_TIME        = 0x03, // [epoch u32][tz minutes i16]
  CMD_NOWPLAYING  = 0x04, // [r][g][b][playing][pos u32][dur u32] title 0x1F artist
  CMD_XFER_BEGIN  = 0x05, // [kind 0=photo 2=clip 3=album art JPEG 240x280][size u32][id u32]
  CMD_XFER_DATA   = 0x06, // raw bytes
  CMD_XFER_END    = 0x07,
  CMD_BRIGHTNESS  = 0x08, // [0..255]
  CMD_PET         = 0x09, // [species][r][g][b] name
  CMD_CARD        = 0x0A, // [r][g][b] name 0x1F subtitle
  CMD_SETTINGS    = 0x0B, // [sleep s u16][swap swipe][flags][tilt]
  CMD_PING        = 0x0C,
  CMD_BUZZ        = 0x0D, // [count]
  CMD_PET_FEED    = 0x0E,
  CMD_CLIP_LIST   = 0x0F,
  CMD_CLIP_PLAY   = 0x10, // [id u32]
  CMD_CLIP_DELETE = 0x11, // [id u32]
  CMD_LOOK        = 0x12, // [background][clock face]([theme])
  CMD_TIMER       = 0x13, // [seconds u16]  (0 = cancel)
  CMD_TOY         = 0x14, // [toy 0=8-ball 1=dice 2=coin][roll 0/1]
  CMD_THEME       = 0x15, // [theme 0..5]
};

enum Mode : uint8_t { M_CLOCK = 0, M_TEXT, M_PHOTO, M_CLIP, M_MUSIC, M_PET, M_TOY, M_CARD, M_DECK, M_COUNT };

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
const uint16_t COL_VOID   = c565(3, 5, 12);

// colour themes for the deck (swipe sideways on the deck, or pick one in the app)
#define THEME_COUNT 6
const uint16_t THEMES[THEME_COUNT] = { c565(0, 229, 255), c565(255, 43, 214), c565(255, 176, 0), c565(57, 255, 20), c565(255, 59, 48), c565(200, 220, 255) };
const char *THEME_NAMES[THEME_COUNT] = {"ice", "neon", "amber", "phosphor", "redline", "ghost"};
#define TEXT_STYLES 4

// ───────────── State ─────────────
Preferences prefs;

uint8_t  mode = M_DECK;
uint8_t  theme = 0;
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
uint16_t *artBuf = nullptr;      // album cover, full screen
bool      artLoaded = false;
#define ART_JPG_MAX (96 * 1024)
uint8_t  *artJpg = nullptr;

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
#define BG_COUNT 11
#define FACE_COUNT 4
uint8_t bgStyle = 0, faceStyle = 0;
const char *BG_NAMES[BG_COUNT] = {"aurora", "warp", "vapor", "matrix", "plasma", "lava", "snow", "checker", "photo", "net", "void"};

// options
bool    soundsOn = true, shakeWake = true, hourChime = false, fxOn = true;
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

// screen effects
uint16_t *prevFb = nullptr;
uint16_t *FB = nullptr;                               // the canvas frame, so effects can edit pixels directly
uint16_t  ACC = 0x07FF, ACC_DIM = 0x0410, ACC_LOW = 0x0208;   // live accent colour and two darker steps
float     accR = 0, accG = 229, accB = 255;
uint8_t   glitchFrames = 0;
uint32_t  wakeFxAt = 0, themePreviewUntil = 0, typeStart = 0;
uint32_t  mediaFlashUntil = 0;
uint8_t   mediaFlashKind = 0;

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
void toast(const String &m, uint32_t ms = 1200) { toastMsg = m; toastMsg.toUpperCase(); toastUntil = millis() + ms; }

void touchActivity() {
  lastInteraction = millis();
  if (sleeping) { sleeping = false; wakeFxAt = millis(); analogWrite(LCD_BL, brightness); }
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
  prefs.putUChar("toy", toyKind);     prefs.putUChar("theme", theme);
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
  textStyle  = prefs.getUChar("tstyle", 0) % TEXT_STYLES;
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
  theme      = prefs.getUChar("theme", 0) % THEME_COUNT;
  uint8_t fl = prefs.getUChar("flags", 0x0B);
  if (prefs.getUChar("uiv", 0) < 4) {          // first start after the 4.0 update: land on the deck
    prefs.putUChar("uiv", 4);
    mode = M_DECK; bgStyle = 9; fl |= 8;
    prefs.putUChar("mode", mode); prefs.putUChar("bg", bgStyle); prefs.putUChar("flags", fl);
  }
  soundsOn = fl & 1; shakeWake = fl & 2; hourChime = fl & 4; fxOn = fl & 8;
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
void notifyText(const char *s);

void textAt(const String &s, int x, int y, uint8_t size, uint16_t color) {
  gfx->setTextSize(size); gfx->setTextColor(color); gfx->setCursor(x, y); gfx->print(s);
}

void textCentered(const String &s, int y, uint8_t size, uint16_t color) {
  textAt(s, (SCR_W - (int)s.length() * 6 * size) / 2, y, size, color);
}

// centred text with a hard shadow so it reads on any background
void textPop(const String &s, int y, uint8_t size, uint16_t color) {
  int w = s.length() * 6 * size, x = (SCR_W - w) / 2, o = max(1, size / 2);
  textAt(s, x + o, y + o, size, COL_BLACK);
  textAt(s, x, y, size, color);
}

void verticalGradient(uint16_t top, uint16_t bottom) {
  for (int y = 0; y < SCR_H; y++) gfx->drawFastHLine(0, y, SCR_W, mix565(top, bottom, y * 255 / SCR_H));
}

// fillRect that is safe when partly off screen
void rectC(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > SCR_W) w = SCR_W - x;
  if (y + h > SCR_H) h = SCR_H - y;
  if (w > 0 && h > 0) gfx->fillRect(x, y, w, h, c);
}

// Darkens part of the frame in place. level 1 = half, 2 = quarter, 3 = three quarters.
void dimRect(int x, int y, int w, int h, uint8_t level) {
  if (!FB) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > SCR_W) w = SCR_W - x;
  if (y + h > SCR_H) h = SCR_H - y;
  if (w <= 0 || h <= 0) return;
  for (int j = 0; j < h; j++) {
    uint16_t *p = FB + (y + j) * SCR_W + x;
    if (level == 1)      for (int i = 0; i < w; i++) p[i] = (p[i] >> 1) & 0x7BEF;
    else if (level == 2) for (int i = 0; i < w; i++) p[i] = (p[i] >> 2) & 0x39E7;
    else                 for (int i = 0; i < w; i++) p[i] -= (p[i] >> 2) & 0x39E7;
  }
}

// Fades rows y0..y1-1 from brightness k0 to k1 (0..256). Used as a scrim over album art.
void shadeRows(int y0, int y1, int k0, int k1) {
  if (!FB) return;
  y0 = max(0, y0); y1 = min((int)SCR_H, y1);
  for (int y = y0; y < y1; y++) {
    int k = k0 + (k1 - k0) * (y - y0) / max(1, y1 - y0 - 1);
    uint16_t *p = FB + y * SCR_W;
    for (int x = 0; x < SCR_W; x++) {
      uint16_t v = p[x];
      p[x] = ((((v >> 11) & 31) * k >> 8) << 11) | ((((v >> 5) & 63) * k >> 8) << 5) | ((v & 31) * k >> 8);
    }
  }
}

// The accent colour follows the theme, and takes the album's colour while music plays.
void accentTarget(uint8_t &r, uint8_t &g, uint8_t &b) {
  uint16_t t = THEMES[theme % THEME_COUNT];
  r = r8(t); g = g8(t); b = b8(t);
  if (npPlaying && npTitle.length() && millis() > themePreviewUntil) {
    int nr = r8(npColor), ng = g8(npColor), nb = b8(npColor), mx = max(nr, max(ng, nb));
    if (mx >= 40) { r = nr * 255 / mx; g = ng * 255 / mx; b = nb * 255 / mx; }
  }
}
void setAccent() {
  ACC = c565((uint8_t)accR, (uint8_t)accG, (uint8_t)accB);
  ACC_DIM = mix565(ACC, COL_BLACK, 140);
  ACC_LOW = mix565(ACC, COL_BLACK, 208);
}
void snapAccent() {
  uint8_t r, g, b; accentTarget(r, g, b);
  accR = r; accG = g; accB = b;
  setAccent();
}
void updateAccent() {
  uint8_t r, g, b; accentTarget(r, g, b);
  accR += (r - accR) * 0.14f; accG += (g - accG) * 0.14f; accB += (b - accB) * 0.14f;
  setAccent();
}

void drawBolt(int cx, int cy, uint16_t c) {
  gfx->fillTriangle(cx + 2, cy - 7, cx - 4, cy + 1, cx + 1, cy + 1, c);
  gfx->fillTriangle(cx - 1, cy - 1, cx + 4, cy - 1, cx - 2, cy + 7, c);
}

// angular panel outline: top-left and bottom-right corners are cut off
void hudPanel(int x, int y, int w, int h, uint16_t c, uint8_t dim) {
  if (dim) dimRect(x, y, w, h, dim);
  const int k = 6;
  gfx->drawFastHLine(x + k, y, w - k, c);
  gfx->drawFastVLine(x, y + k, h - k, c);
  gfx->drawLine(x, y + k, x + k, y, c);
  gfx->drawFastHLine(x, y + h - 1, w - k, c);
  gfx->drawFastVLine(x + w - 1, y, h - k, c);
  gfx->drawLine(x + w - 1 - k, y + h - 1, x + w - 1, y + h - 1 - k, c);
}

void brackets(int x, int y, int w, int h, int k, uint16_t c) {
  gfx->drawFastHLine(x, y, k, c);                 gfx->drawFastVLine(x, y, k, c);
  gfx->drawFastHLine(x + w - k, y, k, c);         gfx->drawFastVLine(x + w - 1, y, k, c);
  gfx->drawFastHLine(x, y + h - 1, k, c);         gfx->drawFastVLine(x, y + h - k, k, c);
  gfx->drawFastHLine(x + w - k, y + h - 1, k, c); gfx->drawFastVLine(x + w - 1, y + h - k, k, c);
}

void tagLabel(const char *s, int x, int y) {
  gfx->fillRect(x, y, (int)strlen(s) * 6 + 5, 9, ACC);
  textAt(s, x + 3, y + 1, 1, COL_BLACK);
}

// Seven-segment digits with faint "off" segments, like an old LCD. d < 0 draws a dash.
const uint8_t SEG7[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
void segDigit(int x, int y, int w, int h, int t, int d, uint16_t on, uint16_t off) {
  uint8_t m = (d >= 0 && d <= 9) ? SEG7[d] : 0x40;
  int vl = (h - 3 * t) / 2 - 2, hl = w - 2 * t - 2;
  int yg = y + t + vl + 2, yd = yg + t + vl + 2;
  gfx->fillRect(x + t + 1, y, hl, t, (m & 0x01) ? on : off);
  gfx->fillRect(x + w - t, y + t + 1, t, vl, (m & 0x02) ? on : off);
  gfx->fillRect(x + w - t, yg + t + 1, t, vl, (m & 0x04) ? on : off);
  gfx->fillRect(x + t + 1, yd, hl, t, (m & 0x08) ? on : off);
  gfx->fillRect(x, yg + t + 1, t, vl, (m & 0x10) ? on : off);
  gfx->fillRect(x, y + t + 1, t, vl, (m & 0x20) ? on : off);
  gfx->fillRect(x + t + 1, yg, hl, t, (m & 0x40) ? on : off);
}

// HH:MM in seven-segment digits. Total width is 4*w + 5*t. hh < 0 draws dashes.
void segTime(int x, int y, int w, int h, int t, int hh, int mm, bool colon, uint16_t on, uint16_t off) {
  segDigit(x, y, w, h, t, hh < 0 ? -1 : hh / 10, on, off);
  segDigit(x + w + t, y, w, h, t, hh < 0 ? -1 : hh % 10, on, off);
  int cx = x + 2 * w + 2 * t;
  gfx->fillRect(cx, y + h / 3 - t / 2, t, t, colon ? on : off);
  gfx->fillRect(cx, y + 2 * h / 3 - t / 2, t, t, colon ? on : off);
  segDigit(cx + 2 * t, y, w, h, t, hh < 0 ? -1 : mm / 10, on, off);
  segDigit(cx + 3 * t + w, y, w, h, t, hh < 0 ? -1 : mm % 10, on, off);
}

void drawBatteryCells(int x, int y) {
  uint16_t col = charging ? COL_LIME : batPct > 40 ? ACC : batPct > 15 ? COL_AMBER : COL_RED;
  int lit = (batPct + 10) / 20;
  if (charging && lit < 5) lit += (int)((millis() / 220) % (6 - lit));
  for (int i = 0; i < 5; i++) {
    if (i < lit) gfx->fillRect(x + i * 6, y, 5, 8, col);
    else         gfx->drawRect(x + i * 6, y, 5, 8, ACC_DIM);
  }
  if (charging) drawBolt(x - 8, y + 4, COL_AMBER);
}

// The status line every screen shares: name, time or timer, link bars, battery.
void topBar(const char *label, bool showTime) {
  dimRect(0, 0, SCR_W, 23, 1);
  textAt(label, 34, 9, 1, ACC);
  char buf[16];
  if (timerEndAt) {
    unsigned long left = (timerEndAt - millis()) / 1000;
    if (left >= 6000) snprintf(buf, sizeof(buf), "T-%luh%02lu", left / 3600, (left / 60) % 60);
    else              snprintf(buf, sizeof(buf), "T-%02lu:%02lu", left / 60, left % 60);
    textAt(buf, 96, 9, 1, COL_AMBER);
  } else if (showTime) {
    struct tm t;
    if (localNow(t)) { snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min); textAt(buf, 105, 9, 1, COL_WHITE); }
  }
  for (int i = 0; i < 3; i++) gfx->fillRect(148 + i * 4, 15 - i * 2, 3, 2 + i * 2, bleConnected ? ACC : COL_GREY);
  drawBatteryCells(176, 8);
}

void drawToast() {
  if (millis() > toastUntil) return;
  int w = toastMsg.length() * 12 + 20, x = (SCR_W - w) / 2;
  gfx->fillRect(x, 230, w, 26, COL_BLACK);
  hudPanel(x, 230, w, 26, ACC, 0);
  textCentered(toastMsg, 236, 2, ACC);
}

void drawTransferOverlay() {
  if (!xferActive || xferSize == 0 || xferKind == 3) return;      // album art arrives quietly
  int pct = (int)(100ULL * xferGot / xferSize);
  gfx->fillRect(30, 112, 180, 56, COL_BLACK);
  hudPanel(30, 112, 180, 56, ACC, 0);
  textCentered(String("UPLINK ") + pct + "%", 122, 2, COL_WHITE);
  for (int i = 0; i < 16; i++) gfx->fillRect(41 + i * 10, 148, 8, 8, i * 100 < pct * 16 ? ACC : ACC_LOW);
}

// what a screen shows when it has nothing to show yet
void emptyState(const char *label, const char *big, const char *l1, const char *l2) {
  gfx->fillScreen(COL_VOID);
  for (int y = 40; y < 250; y += 20) for (int x = 20; x < SCR_W; x += 20) gfx->drawPixel(x, y, ACC_DIM);
  topBar(label, true);
  brackets(34, 84, 172, 100, 14, ACC);
  textCentered(big, 106, 2, ACC);
  textCentered(l1, 138, 1, COL_WHITE);
  textCentered(l2, 152, 1, COL_WHITE);
}

String marquee(const String &s, int chars, int speedDiv) {
  if ((int)s.length() <= chars) return s;
  String loop = s + "   ";
  int off = (frameNo / speedDiv) % loop.length();
  String d = loop + loop;
  return d.substring(off, off + chars);
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
      if (millis() < chromeUntil) topBar("//VID", true);
      return;
    }
  }
  emptyState("//VID", "NO CLIPS", "send a GIF or video", "from the app");
}

// ───────────── Animated backgrounds ─────────────
static int8_t sinLut[256];
struct Star  { float x, y, z; };
struct Flake { float x, y, vx, vy; uint8_t r; };
struct Blob  { float x, y, vx, vy, r; };
struct NetNode { float x, y, vx, vy; };
#define NODE_N 16
NetNode nodes[NODE_N];
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
  for (int i = 0; i < NODE_N; i++) {
    nodes[i].x = random(10, 230); nodes[i].y = random(10, 270);
    nodes[i].vx = (int)random(-40, 41) / 100.0f; nodes[i].vy = (int)random(-40, 41) / 100.0f;
  }
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

// drifting nodes that link up when they pass close to each other
void bgNet() {
  gfx->fillScreen(COL_VOID);
  for (int i = 0; i < NODE_N; i++) {
    NetNode &n = nodes[i];
    n.x += n.vx + tiltX * 0.4f; n.y += n.vy;
    if (n.x < 4)   { n.x = 4;   n.vx = fabsf(n.vx); }
    if (n.x > 235) { n.x = 235; n.vx = -fabsf(n.vx); }
    if (n.y < 4)   { n.y = 4;   n.vy = fabsf(n.vy); }
    if (n.y > 275) { n.y = 275; n.vy = -fabsf(n.vy); }
  }
  for (int i = 0; i < NODE_N; i++)
    for (int j = i + 1; j < NODE_N; j++) {
      float dx = nodes[i].x - nodes[j].x, dy = nodes[i].y - nodes[j].y, d2 = dx * dx + dy * dy;
      if (d2 < 6400.0f)
        gfx->drawLine((int)nodes[i].x, (int)nodes[i].y, (int)nodes[j].x, (int)nodes[j].y, mix565(ACC, COL_VOID, (uint8_t)(d2 * 255.0f / 6400.0f)));
    }
  for (int i = 0; i < NODE_N; i++) gfx->fillRect((int)nodes[i].x - 1, (int)nodes[i].y - 1, 3, 3, ACC);
}

// near-black with a dot grid and a slow scan beam. Easiest on the battery.
void bgVoid() {
  gfx->fillScreen(COL_BLACK);
  for (int y = 10; y < SCR_H; y += 20) for (int x = 10; x < SCR_W; x += 20) gfx->drawPixel(x, y, ACC_DIM);
  int sy = (int)((frameNo * 2) % (SCR_H + 60)) - 30;
  for (int k = 0; k < 24; k++) {
    int y = sy - k;
    if (y >= 0 && y < SCR_H) gfx->drawFastHLine(0, y, SCR_W, mix565(ACC, COL_BLACK, 160 + k * 4));
  }
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
    case 9: bgNet(); break;
    case 10: bgVoid(); break;
    case 8:
      if (photoLoaded && photoBuf) { gfx->draw16bitRGBBitmap(0, 0, photoBuf, SCR_W, SCR_H); break; }
      // no photo yet: fall through to aurora
    default:
      verticalGradient(COL_GRAPE2, COL_ICE2);
      drawSparkles(c565(200, 180, 255));
  }
}

// ───────────── Shared bits for the screens ─────────────
static const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *MON[]  = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

uint32_t npPosition() {
  uint32_t p = npPosMs + (npPlaying ? millis() - npStamp : 0);
  return npDurMs ? min(p, npDurMs) : p;
}

// perspective floor, clipped to the screen
void floorGrid(int hz, uint16_t gc) {
  for (int i = 0; i < 9; i++) {
    float t = i / 9.0f;
    int y = hz + (int)(t * t * (SCR_H - hz));
    if (y < SCR_H) gfx->drawFastHLine(0, y, SCR_W, gc);
  }
  for (int i = -8; i <= 8; i++) {
    float x0 = 120 + i * 14, x1 = 120 + i * 90, y1 = SCR_H - 1;
    if (x1 < 0)         { y1 = hz + (0 - x0) / (x1 - x0) * (SCR_H - 1 - hz); x1 = 0; }
    if (x1 > SCR_W - 1) { y1 = hz + (SCR_W - 1 - x0) / (x1 - x0) * (SCR_H - 1 - hz); x1 = SCR_W - 1; }
    gfx->drawLine((int)x0, hz, (int)x1, (int)y1, gc);
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

// ───────────── The deck: everything live on one screen ─────────────
// Tap a block to open it full screen. Order: chrono, audio, daemon, rng, msg, img, vid, id.
#define TILE_COUNT 8
const int16_t TILE_X[TILE_COUNT] = {8, 8, 160, 8, 86, 18, 88, 158};
const int16_t TILE_Y[TILE_COUNT] = {26, 104, 104, 174, 174, 238, 238, 238};
const int16_t TILE_W[TILE_COUNT] = {224, 146, 72, 72, 146, 64, 64, 64};
const int16_t TILE_H[TILE_COUNT] = {72, 64, 64, 58, 58, 22, 22, 22};
const uint8_t TILE_MODE[TILE_COUNT] = {M_CLOCK, M_MUSIC, M_PET, M_TOY, M_TEXT, M_PHOTO, M_CLIP, M_CARD};

int tileAt(int x, int y) {
  for (int i = 0; i < TILE_COUNT; i++) {
    int bottom = i >= 5 ? SCR_H : TILE_Y[i] + TILE_H[i] + 3;     // the dock reaches the bottom edge
    if (x >= TILE_X[i] - 3 && x < TILE_X[i] + TILE_W[i] + 3 && y >= TILE_Y[i] - 3 && y < bottom) return i;
  }
  return -1;
}

int tileOfMode(uint8_t m) {
  for (int i = 0; i < TILE_COUNT; i++) if (TILE_MODE[i] == m) return i;
  return -1;
}

void drawDeck() {
  drawBackground();
  dimRect(0, 0, SCR_W, SCR_H, 1);
  topBar("LARP//OS", false);
  char buf[24];
  struct tm t;
  bool haveTime = localNow(t);

  // chrono: sits straight on the background
  brackets(TILE_X[0], TILE_Y[0], TILE_W[0], TILE_H[0], 10, ACC);
  int hh = -1, mm = 0;
  if (haveTime) { hh = t.tm_hour; if (!use24h) { hh %= 12; if (hh == 0) hh = 12; } mm = t.tm_min; }
  segTime(16, 34, 30, 52, 6, hh, mm, !haveTime || (millis() / 500) % 2, ACC, ACC_LOW);
  if (haveTime) {
    segDigit(180, 34, 14, 25, 3, t.tm_sec / 10, COL_WHITE, ACC_LOW);
    segDigit(198, 34, 14, 25, 3, t.tm_sec % 10, COL_WHITE, ACC_LOW);
    snprintf(buf, sizeof(buf), "%s %02d", DAYS[t.tm_wday], t.tm_mday);
    textAt(buf, 180, 66, 1, COL_WHITE);
    textAt(MON[t.tm_mon], 180, 77, 1, ACC);
    for (int i = 0; i < 60; i++) gfx->fillRect(16 + i * 208 / 60, 91, 2, 3, i <= t.tm_sec ? ACC : ACC_LOW);
  } else {
    textAt("NO SYNC", 180, 40, 1, COL_AMBER);
    textAt("open", 180, 56, 1, COL_WHITE);
    textAt("the app", 180, 67, 1, COL_WHITE);
  }

  // audio
  int x = TILE_X[1], y = TILE_Y[1];
  hudPanel(x, y, TILE_W[1], TILE_H[1], ACC, 1);
  int ax = x + 8, ay = y + 10, tx = x + 62;
  if (artLoaded && artBuf && FB) {
    for (int j = 0; j < 48; j++) {
      const uint16_t *src = artBuf + (20 + j * 5) * SCR_W;
      uint16_t *dst = FB + (ay + j) * SCR_W + ax;
      for (int i = 0; i < 48; i++) dst[i] = src[i * 5];
    }
  } else {
    for (int j = 0; j < 48; j += 4)
      for (int i = 0; i < 48; i += 4) gfx->fillRect(ax + i, ay + j, 4, 4, random(0, 4) == 0 ? ACC_DIM : COL_BLACK);
  }
  gfx->drawRect(ax - 1, ay - 1, 50, 50, ACC_DIM);
  if (npTitle.length()) {
    textAt(marquee(asciiOnly(npTitle), 13, 8), tx, y + 10, 1, COL_WHITE);
    textAt(marquee(asciiOnly(npArtist), 13, 11), tx, y + 22, 1, ACC);
  } else {
    textAt("NO SIGNAL", tx, y + 10, 1, COL_WHITE);
    textAt("play a song", tx, y + 22, 1, ACC_DIM);
  }
  for (int i = 0; i < 13; i++) {
    float v = npPlaying ? fabsf(sinf(frameNo * 0.3f + i * 0.9f) * sinf(i * 1.7f + frameNo * 0.11f)) : 0.06f;
    int h = 2 + (int)(v * 14);
    gfx->fillRect(tx + i * 6, y + 50 - h, 4, h, ACC);
  }
  gfx->drawFastHLine(tx, y + 55, 78, ACC_LOW);
  if (npDurMs) gfx->fillRect(tx, y + 54, max(2, (int)(78.0f * npPosition() / npDurMs)), 3, COL_WHITE);
  tagLabel("AUDIO", x + 10, y - 4);

  // daemon (the pet)
  x = TILE_X[2]; y = TILE_Y[2];
  hudPanel(x, y, TILE_W[2], TILE_H[2], ACC, 1);
  const char *const *spr = petSpecies == 1 ? GHOST : petSpecies == 2 ? FROG : CAT;
  bool dizzy = millis() < petDizzyUntil;
  int bob = (int)(fabsf(sinf(frameNo * (petMood > 40 ? 0.18f : 0.07f))) * 4);
  int lean = imuOK ? constrain((int)(tiltX * 8), -8, 8) : 0;
  if (dizzy) lean += (int)(sinf(frameNo * 0.9f) * 3);
  drawSprite(spr, x + 18 + lean, y + 16 - bob, 3, petColor, (frameNo % 60) < 3 || dizzy);
  for (int i = 0; i < 8; i++) gfx->fillRect(x + 8 + i * 7, y + 55, 5, 4, i < (petMood + 6) / 13 ? (petMood > 35 ? ACC : COL_RED) : ACC_LOW);
  tagLabel("DAEMON", x + 10, y - 4);

  // rng: a gimbal that follows the motion sensor
  x = TILE_X[3]; y = TILE_Y[3];
  hudPanel(x, y, TILE_W[3], TILE_H[3], ACC, 1);
  int gx0 = x + 36, gy0 = y + 32;
  gfx->drawCircle(gx0, gy0, 19, ACC_DIM);
  gfx->drawFastHLine(gx0 - 23, gy0, 47, ACC_LOW);
  gfx->drawFastVLine(gx0, gy0 - 23, 47, ACC_LOW);
  int px = gx0, py = gy0;
  if (imuOK) { px += constrain((int)(tiltX * 18), -18, 18); py += constrain((int)(tiltY * 18), -18, 18); }
  else       { px += (int)(cosf(frameNo * 0.08f) * 12); py += (int)(sinf(frameNo * 0.08f) * 12); }
  gfx->fillRect(px - 2, py - 2, 5, 5, COL_WHITE);
  gfx->drawRect(px - 4, py - 4, 9, 9, ACC);
  tagLabel("RNG", x + 10, y - 4);

  // msg
  x = TILE_X[4]; y = TILE_Y[4];
  hudPanel(x, y, TILE_W[4], TILE_H[4], ACC, 1);
  String m = asciiOnly(textMsg);
  m.replace('\n', ' ');
  textAt(marquee(m, 11, 5), x + 8, y + 16, 2, COL_WHITE);
  textAt((millis() / 400) % 2 ? "> _" : ">", x + 8, y + 41, 1, ACC);
  tagLabel("MSG", x + 10, y - 4);

  // dock
  static const char *DOCK[3] = {"IMG", "VID", "ID"};
  for (int i = 0; i < 3; i++) {
    int k = 5 + i;
    hudPanel(TILE_X[k], TILE_Y[k], TILE_W[k], TILE_H[k], ACC, 2);
    textAt(DOCK[i], TILE_X[k] + (TILE_W[k] - (int)strlen(DOCK[i]) * 12) / 2, TILE_Y[k] + 4, 2, ACC);
  }
}

// ───────────── Full-screen modules ─────────────
void clockHand(float ang, int len, uint16_t c, int thick) {
  int x1 = 120 + (int)(sinf(ang) * len), y1 = 140 - (int)(cosf(ang) * len);
  for (int o = -(thick / 2); o <= thick / 2; o++) {
    gfx->drawLine(120 + o, 140, x1 + o, y1, c);
    gfx->drawLine(120, 140 + o, x1, y1 + o, c);
  }
}

void drawClock() {
  drawBackground();
  topBar("//CHRONO", false);

  struct tm t;
  if (!localNow(t)) {
    dimRect(8, 74, 224, 102, 1);
    brackets(8, 74, 224, 102, 12, ACC);
    segTime(16, 86, 42, 78, 8, -1, 0, true, ACC, ACC_LOW);
    textPop("NO TIME SYNC", 190, 2, COL_WHITE);
    textPop("open the app once", 214, 1, ACC);
    return;
  }
  int h = t.tm_hour;
  if (!use24h) { h = h % 12; if (h == 0) h = 12; }
  char buf[28];

  if (faceStyle == 1) {                                   // analog
    gfx->drawCircle(120, 140, 98, ACC);
    gfx->drawCircle(120, 140, 97, ACC);
    for (int i = 0; i < 12; i++) {
      float a = i * 0.5235988f;
      int r0 = (i % 3 == 0) ? 82 : 88;
      gfx->drawLine(120 + (int)(sinf(a) * r0), 140 - (int)(cosf(a) * r0), 120 + (int)(sinf(a) * 95), 140 - (int)(cosf(a) * 95), COL_WHITE);
    }
    clockHand((t.tm_hour % 12 + t.tm_min / 60.0f) * 0.5235988f, 50, COL_BLACK, 7);
    clockHand((t.tm_hour % 12 + t.tm_min / 60.0f) * 0.5235988f, 50, COL_WHITE, 5);
    clockHand((t.tm_min + t.tm_sec / 60.0f) * 0.1047198f, 76, COL_BLACK, 5);
    clockHand((t.tm_min + t.tm_sec / 60.0f) * 0.1047198f, 76, ACC, 3);
    clockHand(t.tm_sec * 0.1047198f, 86, COL_WHITE, 1);
    gfx->fillRect(116, 136, 9, 9, ACC);
    snprintf(buf, sizeof(buf), "%s %02d %s", DAYS[t.tm_wday], t.tm_mday, MON[t.tm_mon]);
    textPop(buf, 248, 1, COL_WHITE);
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
      textPop("o'clock", 152, 3, ACC);
    } else {
      textPop(pre, 90, 3, ACC);
      textPop(to ? "to" : "past", 126, 3, COL_WHITE);
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
    if (by > SCR_H - 60)  { by = SCR_H - 60;  bvy = -fabsf(bvy); hit++; }
    if (hit) { bc = hue565((uint8_t)random(0, 256)); if (hit >= 2) sfx(2); }
    gfx->setTextWrap(false);
    textAt(buf, (int)bx + 3, (int)by + 3, 5, COL_BLACK);
    textAt(buf, (int)bx, (int)by, 5, bc);
    gfx->setTextWrap(true);
  } else {                                                // seven-segment
    dimRect(8, 74, 224, 102, 1);
    brackets(8, 74, 224, 102, 12, ACC);
    segTime(16, 86, 42, 78, 8, h, t.tm_min, (millis() / 500) % 2, COL_WHITE, mix565(ACC, COL_BLACK, 215));
    if (use24h) snprintf(buf, sizeof(buf), "%s %02d %s", DAYS[t.tm_wday], t.tm_mday, MON[t.tm_mon]);
    else        snprintf(buf, sizeof(buf), "%s %02d %s %s", DAYS[t.tm_wday], t.tm_mday, MON[t.tm_mon], t.tm_hour < 12 ? "AM" : "PM");
    textPop(buf, 188, 2, ACC);
    for (int i = 0; i < 60; i++) gfx->fillRect(20 + i * 200 / 60, 214, 2, 6, i <= t.tm_sec ? COL_WHITE : ACC_DIM);
  }
  if (charging) textPop(String("CHARGING ") + batPct + "%", 236, 1, COL_LIME);
}

void drawText() {
  if (textStyle == 3) {                                   // terminal: types itself out
    gfx->fillScreen(COL_BLACK);
    topBar("//MSG", true);
    int shown = (int)((millis() - typeStart) / 45), count = 0;
    int lineH = 8 * textSize + 4;
    int y = (SCR_H - (int)textLines.size() * lineH) / 2 + 4;
    int cx = 10, cy = y;
    gfx->setTextSize(textSize);
    gfx->setTextColor(textFg);
    for (size_t li = 0; li < textLines.size() && count < shown; li++) {
      const String &ln = textLines[li];
      cx = 10; cy = y;
      for (size_t c = 0; c < ln.length() && count < shown; c++, count++) {
        gfx->setCursor(10 + c * 6 * textSize, y);
        gfx->print(ln[c]);
        cx = 10 + (c + 1) * 6 * textSize;
      }
      count++;
      y += lineH;
    }
    if ((millis() / 300) % 2) rectC(cx, cy, 5 * textSize, 7 * textSize, textFg);
    if (shown > count + 60) typeStart = millis();
    return;
  }
  gfx->fillScreen(textBg);
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
    if (millis() < chromeUntil) topBar("//IMG", true);
  } else {
    emptyState("//IMG", "NO IMAGE", "send a photo", "from the app");
  }
}

// Music: the album cover fills the whole screen, with the readout laid over it.
// Tap left / middle / right = previous / play-pause / next (sent to the phone).
void mediaCmd(uint8_t kind) {
  if (!bleConnected) { toast("no link", 800); return; }
  static const char *M[3] = {"media=toggle", "media=next", "media=prev"};
  if (kind == 0) { npPosMs = npPosition(); npStamp = millis(); npPlaying = !npPlaying; }
  mediaFlashKind = kind;
  mediaFlashUntil = millis() + 600;
  notifyText(M[kind % 3]);
}

void drawMusic() {
  if (artLoaded && artBuf && FB) {
    memcpy(FB, artBuf, FB_BYTES);
  } else {
    gfx->fillScreen(COL_VOID);
    float beat = npPlaying ? (sinf(frameNo * 0.35f) * 0.5f + 0.5f) : 0.2f;
    for (int i = 0; i < 6; i++) gfx->drawCircle(120, 108, 20 + i * 14 + (int)(beat * 6), mix565(ACC, COL_BLACK, 50 + i * 32));
    float a = frameNo * (npPlaying ? 0.08f : 0.0f);
    gfx->drawLine(120, 108, 120 + (int)(cosf(a) * 96), 108 + (int)(sinf(a) * 96), ACC);
    gfx->fillRect(116, 104, 9, 9, ACC);
  }
  shadeRows(140, SCR_H, 256, 22);
  topBar(npPlaying ? "//AUDIO" : "//PAUSED", true);

  String title  = asciiOnly(npTitle.length() ? npTitle : String("NO SIGNAL"));
  String artist = asciiOnly(npArtist.length() || npTitle.length() ? npArtist : String("play something on your phone"));
  int tw = title.length() * 12;
  gfx->setTextWrap(false);
  gfx->setTextSize(2);
  gfx->setTextColor(COL_WHITE);
  if (tw <= 212) { gfx->setCursor(14, 190); gfx->print(title); npTitleX = 0; }
  else {
    gfx->setCursor(14 - npTitleX, 190); gfx->print(title);
    gfx->setCursor(14 - npTitleX + tw + 60, 190); gfx->print(title);
    npTitleX = (npTitleX + 2) % (tw + 60);
  }
  gfx->setTextWrap(true);
  if (artist.length() > 35) artist = artist.substring(0, 34) + ".";
  textAt(artist, 14, 212, 1, ACC);

  uint32_t pos = npPosition();
  char buf[16];
  gfx->fillRect(14, 228, 212, 2, c565(70, 70, 80));
  if (npDurMs) {
    int w = max(2, (int)(212.0f * pos / npDurMs));
    gfx->fillRect(14, 227, w, 4, ACC);
    gfx->fillRect(12 + w, 224, 3, 10, COL_WHITE);
    snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)(npDurMs / 60000), (unsigned long)((npDurMs / 1000) % 60));
    textAt(buf, 226 - (int)strlen(buf) * 6, 240, 1, COL_WHITE);
  }
  snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)(pos / 60000), (unsigned long)((pos / 1000) % 60));
  textAt(buf, 14, 240, 1, COL_WHITE);
  for (int i = 0; i < 20; i++) {
    float v = npPlaying ? fabsf(sinf(frameNo * 0.3f + i * 0.9f) * sinf(i * 1.7f + frameNo * 0.11f)) : 0.05f;
    int h = 2 + (int)(v * 16);
    gfx->fillRect(60 + i * 6, 258 - h, 4, h, mix565(ACC, COL_WHITE, i * 6));
  }

  if (millis() < mediaFlashUntil) {
    const int cx = 120, cy = 104;
    dimRect(84, 68, 72, 72, 2);
    brackets(84, 68, 72, 72, 10, ACC);
    if (mediaFlashKind == 0) {
      if (npPlaying) gfx->fillTriangle(cx - 12, cy - 18, cx - 12, cy + 18, cx + 18, cy, COL_WHITE);
      else { gfx->fillRect(cx - 14, cy - 18, 10, 36, COL_WHITE); gfx->fillRect(cx + 4, cy - 18, 10, 36, COL_WHITE); }
    } else {
      int d = mediaFlashKind == 1 ? 1 : -1;
      gfx->fillTriangle(cx - 16 * d, cy - 16, cx - 16 * d, cy + 16, cx + 2 * d, cy, COL_WHITE);
      gfx->fillTriangle(cx - 2 * d, cy - 16, cx - 2 * d, cy + 16, cx + 16 * d, cy, COL_WHITE);
    }
  }
}

void drawPet() {
  verticalGradient(COL_VOID, mix565(petColor, COL_BLACK, 205));
  floorGrid(204, ACC_LOW);
  topBar("//DAEMON", true);
  textCentered(asciiOnly(petName), 34, 3, COL_WHITE);

  const char *const *spr = petSpecies == 1 ? GHOST : petSpecies == 2 ? FROG : CAT;
  int bounce = (int)(fabsf(sinf(frameNo * (petMood > 40 ? 0.18f : 0.07f))) * (petMood > 40 ? 14 : 4));
  bool blink = (frameNo % 60) < 3;
  bool dizzy = millis() < petDizzyUntil;
  int slide = imuOK ? constrain((int)(tiltX * 28), -24, 24) : 0;      // leans the way you tilt it
  if (dizzy) slide += (int)(sinf(frameNo * 0.9f) * 6);
  gfx->fillEllipse(120 + slide, 200, 46 - bounce, 6, ACC_LOW);
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
  uint16_t mc = petMood > 50 ? ACC : petMood > 20 ? COL_AMBER : COL_RED;
  for (int i = 0; i < 10; i++) gfx->fillRect(51 + i * 14, 226, 11, 8, i < (petMood + 5) / 10 ? mc : ACC_LOW);
  textCentered(dizzy ? "STATE: DIZZY" : petMood > 70 ? "STATE: HAPPY" : petMood > 35 ? "STATE: IDLE" : "STATE: HUNGRY", 242, 1, COL_WHITE);
}

void drawCard() {
  const uint16_t a = cardAccent;
  gfx->fillScreen(COL_VOID);
  topBar("//ID", true);
  hudPanel(12, 32, 216, 112, a, 0);
  gfx->fillRect(12, 38, 4, 100, a);
  textAt("OPERATOR", 26, 42, 1, mix565(a, COL_BLACK, 90));
  String name = asciiOnly(cardName), sub = asciiOnly(cardSub);
  textAt(name, 26, 58, name.length() <= 10 ? 3 : name.length() <= 16 ? 2 : 1, COL_WHITE);
  textAt(sub, 26, 90, sub.length() <= 16 ? 2 : 1, a);
  uint32_t hsh = 2166136261u;                                   // barcode drawn from the name
  for (size_t i = 0; i < name.length(); i++) hsh = (hsh ^ (uint8_t)name[i]) * 16777619u;
  for (int x = 26, i = 0; x < 214; i++) {
    hsh = hsh * 1664525u + 1013904223u;
    int w = 1 + ((hsh >> 28) & 3);
    if (!(i & 1)) rectC(x, 116, min(w, 214 - x), 18, a);
    x += w;
  }
  int phase = (frameNo / 4) % 4;
  for (int i = 0; i < 3; i++) {
    uint16_t c = (i < phase) ? a : mix565(a, COL_BLACK, 190);
    gfx->drawCircle(120, 198, 14 + i * 11, c);
    gfx->drawCircle(120, 198, 15 + i * 11, c);
  }
  gfx->fillRect(115, 193, 11, 11, COL_WHITE);
  textCentered(">> TAP PHONE HERE <<", 244, 1, COL_WHITE);
}

// ───────────── RNG: oracle (8-ball), dice, coin ─────────────
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
  gfx->drawRect(x + 5, y + 5, s, s, ACC);
  gfx->fillRect(x, y, s, s, COL_WHITE);
  if (v < 1 || v > 6) v = 1;
  for (int i = 0; i < 9; i++)
    if ((P[v] >> (8 - i)) & 1) gfx->fillRect(x + s / 4 * (1 + i % 3) - s / 11, y + s / 4 * (1 + i / 3) - s / 11, 2 * (s / 11), 2 * (s / 11), COL_BLACK);
}

void drawToy() {
  static const char *TN[3] = {"< ORACLE >", "< DICE >", "< COIN >"};
  uint32_t el = millis() - toyRollAt;
  bool rolling = toyRollAt && el < 1100;
  int jx = rolling ? (int)random(-5, 6) : 0, jy = rolling ? (int)random(-5, 6) : 0;

  gfx->fillScreen(COL_VOID);
  for (int y = 50; y < SCR_H; y += 20) for (int x = 10; x < SCR_W; x += 20) gfx->drawPixel(x, y, ACC_DIM);

  if (toyKind == 0) {
    gfx->fillCircle(120 + jx, 160 + jy, 92, COL_BLACK);
    gfx->drawCircle(120 + jx, 160 + jy, 92, ACC);
    gfx->drawCircle(120 + jx, 160 + jy, 86, ACC_LOW);
    gfx->fillCircle(120 + jx, 164 + jy, 60, mix565(ACC, COL_BLACK, 200));
    gfx->drawCircle(120 + jx, 164 + jy, 60, ACC);
    if (rolling)           textBlock("...", 164, 3, COL_WHITE);
    else if (toyHasResult) textBlock(EIGHT[toyResult % EIGHT_N], 164, 2, COL_WHITE);
    else                   textBlock(imuOK ? "shake\nme" : "tap\nme", 164, 2, COL_WHITE);
  } else if (toyKind == 1) {
    uint8_t a = rolling ? 1 + random(0, 6) : toyResult, b = rolling ? 1 + random(0, 6) : toyResult2;
    if (!toyHasResult) { a = 5; b = 2; }
    drawDie(26 + jx, 100 + jy, 88, a);
    drawDie(126 - jx, 100 - jy, 88, b);
    if (toyHasResult && !rolling) textPop(String("= ") + (toyResult + toyResult2), 208, 4, ACC);
    else if (!toyHasResult)       textPop(imuOK ? "shake to roll" : "tap to roll", 218, 2, COL_WHITE);
  } else {
    const uint16_t gold = c565(255, 200, 60), dark = c565(190, 130, 20);
    if (rolling) {
      int rx = max(3, (int)(fabsf(sinf(el * 0.02f)) * 70));
      gfx->fillEllipse(120, 150 - (int)(sinf(el * 3.14159f / 1100.0f) * 44), rx, 70, gold);
    } else {
      gfx->fillCircle(120, 150, 72, dark);
      gfx->fillCircle(120, 150, 66, gold);
      gfx->drawCircle(120, 150, 58, dark);
      if (toyHasResult) {
        textAt(toyResult ? "T" : "H", 120 - 21, 150 - 28, 7, dark);
        textPop(toyResult ? "TAILS" : "HEADS", 234, 2, ACC);
      } else {
        textAt("?", 120 - 21, 150 - 28, 7, dark);
        textPop(imuOK ? "shake to flip" : "tap to flip", 234, 2, COL_WHITE);
      }
    }
  }
  topBar("//RNG", true);
  textPop(TN[toyKind % 3], 30, 2, ACC);
}

void drawAlarm() {
  if (!alarmUntil) return;
  bool flash = (millis() / 300) % 2;
  gfx->fillRect(16, 92, 208, 96, flash ? ACC : COL_BLACK);
  hudPanel(16, 92, 208, 96, COL_WHITE, 0);
  textCentered("TIME'S UP", 116, 3, flash ? COL_BLACK : ACC);
  textCentered("tap to stop", 160, 1, flash ? COL_BLACK : COL_WHITE);
}

// a line across the bottom of the other screens while a song is playing
void drawTicker() {
  if (!npPlaying || !npTitle.length()) return;
  dimRect(44, 260, 152, 13, 2);
  gfx->drawChar(48, 263, 0x0E, ACC, ACC);
  String s = asciiOnly(npTitle);
  if (npArtist.length()) s += String(" - ") + asciiOnly(npArtist);
  textAt(marquee(s, 22, 7), 58, 263, 1, COL_WHITE);
}

// ───────────── Screen effects ─────────────
void scanlines() {
  if (!FB) return;
  for (int y = 1; y < SCR_H; y += 2) {
    uint16_t *p = FB + y * SCR_W;
    for (int x = 0; x < SCR_W; x++) p[x] -= (p[x] >> 2) & 0x39E7;
  }
}

// tears a few bands of the frame sideways
void glitchFx(int bands) {
  if (!FB) return;
  static uint16_t rowTmp[SCR_W];
  for (int b = 0; b < bands; b++) {
    int y = random(0, SCR_H - 16), h = random(2, 14), off = random(-28, 29);
    if (off == 0) continue;
    for (int j = 0; j < h; j++) {
      uint16_t *row = FB + (y + j) * SCR_W;
      memcpy(rowTmp, row, SCR_W * 2);
      for (int x = 0; x < SCR_W; x++) row[x] = rowTmp[(x - off + SCR_W) % SCR_W];
    }
  }
  gfx->drawFastHLine(0, random(0, SCR_H), SCR_W, ACC);
}

// old-TV switch-on when the screen wakes
void wakeFx() {
  if (!wakeFxAt) return;
  uint32_t el = millis() - wakeFxAt;
  if (el > 260) { wakeFxAt = 0; return; }
  float t = el / 260.0f;
  int half = 2 + (int)(t * t * 138);
  gfx->fillRect(0, 0, SCR_W, 140 - half, COL_BLACK);
  gfx->fillRect(0, 140 + half, SCR_W, 140 - half, COL_BLACK);
  gfx->drawFastHLine(0, 140 - half, SCR_W, COL_WHITE);
  gfx->drawFastHLine(0, 140 + half - 1, SCR_W, COL_WHITE);
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
    default:      drawDeck();  break;
  }
}

void render() {
  updateAccent();
  drawScene();
  if (mode == M_CLOCK || mode == M_PET || mode == M_TOY || mode == M_CARD) drawTicker();
  if (fxOn && mode != M_PHOTO && mode != M_CLIP && mode != M_MUSIC) scanlines();
  if (glitchFrames) { glitchFx(5); glitchFrames--; }
  else if (fxOn && mode == M_DECK && random(0, 220) == 0) glitchFrames = 2;
  drawAlarm();
  drawTransferOverlay();
  drawToast();
  wakeFx();
  gfx->flush();
  frameNo++;
}

void setMode(int m, bool announce = true) {
  (void)announce;
  mode = ((m % M_COUNT) + M_COUNT) % M_COUNT;
  prefs.putUChar("mode", mode);
  if (mode == M_CLIP) {
    if (!clipFile) { if (!openClip(clipId)) openNextClip(0); }
    clipNextAt = 0;
  }
  if (mode == M_CLIP || mode == M_PHOTO) chromeUntil = millis() + 1500;
  if (mode == M_TEXT) typeStart = millis();
  glitchFrames = 3;
}

// A frame grows out of the tapped block until it fills the screen (or shrinks back into it).
void zoomAnim(int ti, bool opening) {
  if (!FB || !prevFb || sleeping || ti < 0) return;
  memcpy(prevFb, FB, FB_BYTES);
  const int steps = 6;
  for (int s = 1; s <= steps; s++) {
    float t = (float)s / steps;
    t = 1.0f - (1.0f - t) * (1.0f - t);
    if (!opening) t = 1.0f - t;
    int x = TILE_X[ti] - (int)(TILE_X[ti] * t), y = TILE_Y[ti] - (int)(TILE_Y[ti] * t);
    int w = TILE_W[ti] + (int)((SCR_W - TILE_W[ti]) * t), h = TILE_H[ti] + (int)((SCR_H - TILE_H[ti]) * t);
    memcpy(FB, prevFb, FB_BYTES);
    gfx->fillRect(x, y, w, h, COL_BLACK);
    gfx->drawRect(x, y, w, h, ACC);
    gfx->drawRect(x + 1, y + 1, w - 2, h - 2, ACC);
    gfx->flush();
  }
}

void openModule(int m) {
  if (mode == M_DECK) { sfx(1); zoomAnim(tileOfMode(m), true); }
  setMode(m);
  lastFrame = 0;
}

void closeModule() {
  if (mode == M_DECK) return;
  int ti = tileOfMode(mode);
  sfx(1);
  setMode(M_DECK);
  updateAccent();
  drawScene();
  zoomAnim(ti, false);
  glitchFrames = 1;
  lastFrame = 0;
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
  snprintf(buf, sizeof(buf), "b=%d;v=%.2f;c=%d;m=%d;fw=%s;fs=%lu,%lu;clip=%lu;lk=%d,%d;imu=%d;toy=%d;th=%d%s%s",
           batPct, batVolts, charging ? 1 : 0, mode, FW_VERSION, used, total, (unsigned long)clipId,
           bgStyle, faceStyle, imuOK ? 1 : 0, toyKind, theme,
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

// decodes the album-cover JPEG straight into its own full-screen buffer
int jpegToArt(JPEGDRAW *p) {
  if (p->x < 0 || p->x >= SCR_W) return 1;
  int w = min((int)p->iWidth, SCR_W - (int)p->x);
  for (int row = 0; row < p->iHeight; row++) {
    int y = p->y + row;
    if (y < 0 || y >= SCR_H) continue;
    memcpy(artBuf + y * SCR_W + p->x, p->pPixels + row * p->iWidth, w * 2);
  }
  return 1;
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
  else if (xferKind == 3) { ok = artBuf && artJpg && xferSize > 100 && xferSize <= ART_JPG_MAX; }
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
  else if (xferKind == 3) memcpy(artJpg + xferGot, p, take);
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
  } else if (xferKind == 3) {
    xferActive = false;
    bool ok = false;
    if (jpeg.openRAM(artJpg, xferSize, jpegToArt)) {
      jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
      ok = jpeg.decode(0, 0, 0);
      jpeg.close();
    }
    artLoaded = ok;
    if (!ok) { sendStatus("xfer=err;why=format"); return; }
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
  // background traffic (now-playing updates, album art, time sync) must not keep the screen awake
  bool quiet = d[0] == CMD_XFER_DATA || d[0] == CMD_NOWPLAYING || d[0] == CMD_PING || d[0] == CMD_TIME ||
               (d[0] == CMD_XFER_BEGIN && n >= 2 && d[1] == 3) || (d[0] == CMD_XFER_END && xferKind == 3);
  if (!quiet) touchActivity();
  else if (d[0] == CMD_XFER_DATA && xferKind != 3) lastInteraction = millis();
  const uint8_t *p = d + 1; uint16_t len = n - 1;
  switch (d[0]) {
    case CMD_MODE: if (len >= 1) setMode(p[0]); break;

    case CMD_TEXT:
      if (len >= 5) {
        textStyle = p[0] % TEXT_STYLES;
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
        if (len >= 4) { soundsOn = p[3] & 1; shakeWake = p[3] & 2; hourChime = p[3] & 4; fxOn = p[3] & 8; prefs.putUChar("flags", p[3] & 15); }
        if (len >= 5) { tiltBits = p[4] & 7; prefs.putUChar("tilt", tiltBits); }
      }
      break;

    case CMD_LOOK:
      if (len >= 2) {
        bgStyle = p[0] % BG_COUNT;
        faceStyle = p[1] % FACE_COUNT;
        prefs.putUChar("bg", bgStyle); prefs.putUChar("face", faceStyle);
        if (len >= 3) { theme = p[2] % THEME_COUNT; prefs.putUChar("theme", theme); }
        if (mode == M_DECK) glitchFrames = 2; else setMode(M_CLOCK, false);     // the deck shows the background too
      }
      break;

    case CMD_THEME:
      if (len >= 1) {
        theme = p[0] % THEME_COUNT;
        prefs.putUChar("theme", theme);
        themePreviewUntil = millis() + 2500;
        glitchFrames = 2;
        toast(THEME_NAMES[theme], 900);
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
    toast("link up", 1000);
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

void cycleBg(int dir) {
  bgStyle = (bgStyle + BG_COUNT + dir) % BG_COUNT;
  if (bgStyle == 8 && !photoLoaded) bgStyle = (bgStyle + BG_COUNT + dir) % BG_COUNT;
  prefs.putUChar("bg", bgStyle);
  toast(BG_NAMES[bgStyle], 800);
  glitchFrames = 2;
  sfx(1);
}

void showClipName() {
  if (clipFile) { char nm[25]; memcpy(nm, clipHdr.name, 24); nm[24] = 0; toast(asciiOnly(String(nm)).substring(0, 14), 900); }
  chromeUntil = millis() + 1500;
}

// Sideways swipes act on whatever is open. dir +1 = finger moved left.
void onSwipe(int dir) {
  if (swapSwipe) dir = -dir;
  switch (mode) {
    case M_DECK:
      theme = (theme + THEME_COUNT + dir) % THEME_COUNT;
      prefs.putUChar("theme", theme);
      themePreviewUntil = millis() + 2500;
      toast(THEME_NAMES[theme], 900);
      glitchFrames = 2;
      sfx(1);
      break;
    case M_CLOCK: cycleBg(dir); break;
    case M_TEXT:
      textStyle = (textStyle + TEXT_STYLES + dir) % TEXT_STYLES;
      prefs.putUChar("tstyle", textStyle);
      marqueeX = SCR_W; typeStart = millis();
      sfx(1);
      break;
    case M_CLIP:  openNextClip(dir); showClipName(); break;
    case M_MUSIC: mediaCmd(dir > 0 ? 1 : 2); break;
    case M_TOY:
      toyKind = (toyKind + 3 + dir) % 3; toyHasResult = false; toyRollAt = 0;
      prefs.putUChar("toy", toyKind);
      sfx(1);
      break;
    case M_PET:
      petMood = min(100, petMood + 3);
      petHeartsUntil = millis() + 1500;
      break;
    default: break;
  }
}

// Swipe down closes whatever is open. On the deck, up and down change brightness.
void onVSwipe(int up) {
  if (up < 0 && mode != M_DECK) { closeModule(); return; }
  setBacklight(brightness + (up > 0 ? 40 : -40));
  prefs.putUChar("bright", brightness);
  toast(up > 0 ? "bright +" : "bright -", 500);
}

void onTap(bool dbl) {
  sfx(0);
  switch (mode) {
    case M_DECK: {
      int ti = tileAt(tch.x0, tch.y0);
      if (ti >= 0) openModule(TILE_MODE[ti]);
      break;
    }
    case M_CLOCK:
      if (dbl) {              // second tap of a double tap: undo the style change, flip 12/24 h
        faceStyle = (faceStyle + FACE_COUNT - 1) % FACE_COUNT;
        use24h = !use24h; prefs.putBool("h24", use24h);
        toast(use24h ? "24h" : "12h", 700);
      } else faceStyle = (faceStyle + 1) % FACE_COUNT;
      prefs.putUChar("face", faceStyle);
      break;
    case M_TEXT:
      textStyle = (textStyle + 1) % TEXT_STYLES;
      prefs.putUChar("tstyle", textStyle);
      marqueeX = SCR_W; typeStart = millis();
      break;
    case M_PHOTO: chromeUntil = millis() + 1500; break;
    case M_CLIP:  openNextClip(1); showClipName(); break;
    case M_MUSIC: mediaCmd(tch.x0 < 80 ? 2 : tch.x0 > 160 ? 1 : 0); break;
    case M_PET:
      petMood = min(100, petMood + (dbl ? 25 : 3));
      petHeartsUntil = millis() + (dbl ? 2500 : 1500);
      if (dbl) { prefs.putInt("petmood", petMood); toast("yum!", 700); }
      break;
    case M_TOY:
      if (tch.y0 < 70) { toyKind = (toyKind + 1) % 3; toyHasResult = false; toyRollAt = 0; prefs.putUChar("toy", toyKind); }
      else toyRoll();
      break;
    default: break;
  }
}

void onShake() {
  if (sleeping) {
    if (shakeWake) { touchActivity(); sfx(0); }
    return;
  }
  touchActivity();
  switch (mode) {
    case M_DECK:
    case M_CLOCK: cycleBg(1); break;
    case M_PET:
      petDizzyUntil = millis() + 3000;
      toast("wheee!", 900);
      sfx(3);
      break;
    case M_TOY: toyRoll(); break;
    case M_TEXT:
      textStyle = (textStyle + 1) % TEXT_STYLES;
      prefs.putUChar("tstyle", textStyle);
      marqueeX = SCR_W; typeStart = millis();
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
      tch.consumed = true;                       // press and hold: the ID card, from anywhere
      if (mode == M_CARD) closeModule(); else openModule(M_CARD);
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
    textCentered("SHUTDOWN", 124, 3, ACC);
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
    else if (mode != M_DECK) { touchActivity(); closeModule(); }
    else { sleeping = true; analogWrite(LCD_BL, 0); }             // on the deck: screen off
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
int bootY = 54;
void bootLine(const char *k, const char *v, bool ok) {
  textAt(String("> ") + k, 24, bootY, 1, ACC);
  textAt(v, 216 - (int)strlen(v) * 6, bootY, 1, ok ? COL_WHITE : COL_AMBER);
  bootY += 14;
  gfx->flush();
  delay(60);
}

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

  // boot log: each line is a real check
  FB = gfx->getFramebuffer();
  snapAccent();
  textAt("LARP//OS", 24, 22, 2, ACC);
  textAt("v" FW_VERSION, 130, 29, 1, COL_WHITE);
  gfx->drawFastHLine(24, 42, 192, ACC_DIM);
  bootLine("display", "ST7789 240x280", true);

  touchInit();
  bootLine("touch", "CST816", true);
  imuInit();
  bootLine("motion", imuOK ? "QMI8658" : "no reply", imuOK);
  initFx();

  photoBuf = (uint16_t *)ps_malloc(FB_BYTES);
  artBuf   = (uint16_t *)ps_malloc(FB_BYTES);
  artJpg   = (uint8_t *)ps_malloc(ART_JPG_MAX);
  prevFb   = (uint16_t *)ps_malloc(FB_BYTES);
  frameBuf = (uint8_t *)ps_malloc(FRAME_MAX);
  bootLine("memory", (photoBuf && artBuf && artJpg && prevFb && frameBuf) ? "PSRAM ok" : "low", photoBuf && artBuf && artJpg && prevFb && frameBuf);
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
  bootLine("storage", fsOK ? "FFat ok" : "not mounted", fsOK);

  time_t e;
  if (rtcReadUTC(e)) setSystemTime(e);
  bootLine("clock", timeValid ? "RTC ok" : "needs sync", timeValid);

  layoutText();
  rxQueue = xQueueCreate(32, sizeof(Packet));
  bleInit();
  bootLine("uplink", "BLE ready", true);
  updateBattery();
  char pw[16];
  snprintf(pw, sizeof(pw), "%d%% %.2fV", batPct, batVolts);
  bootLine("power", pw, batPct > 15);

  textAt("JACKING IN", 24, bootY + 12, 2, COL_WHITE);
  gfx->flush();
  sfx(2);
  delay(450);
  lastInteraction = millis();
  setMode(mode, false);
  glitchFrames = 5;
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
      if (millis() < chromeUntil) topBar("//VID", true);
      drawToast();
      gfx->flush();
      frameNo++;
    }
    return;
  }

  uint32_t interval = (xferActive && xferKind != 3) ? 150 : 33;
  if (millis() - lastFrame >= interval) {
    lastFrame = millis();
    render();
  }
}

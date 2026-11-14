/*
  LARP CHARM firmware v5.0
  Board : Waveshare ESP32-S3-Touch-LCD-1.69 (240x280 ST7789V2, CST816T touch)
  Control: Bluetooth LE from the Larp Charm web app

  Two looks, switch by holding a finger on the home screen (or from the app):
    DECK  the cyberdeck: one fused screen, tap a block to open it
    AERO  glossy Frutiger Aero / Y2K: an orbit of orbs; drag sideways or tilt the charm to spin it,
          tap the front orb to open it
  Swipe down (or the side button) closes whatever is open.

  Blocks: clock, music, eyes, jukebox, pet, dice, message, photo, clips, ID card, torch,
          and a charging screen that appears when you plug it in.

  Touch:
    swipe left / right  -> deck home: colour theme · clock: backdrop (aero: style) · music: next / previous
                           eyes: expression · jukebox: skip (aero: pick a song) · clips: next · message: style
    tap                 -> clock: style (double tap 12/24h) · music: left prev, middle play/pause, right next
                           eyes: react (double tap: love) · jukebox: queue a song · pet: pat · dice: roll
    swipe up / down on home -> brightness
    press and hold      -> home: switch look · anywhere else: ID card
  Shake: home / clock = next backdrop · pet = dizzy · eyes = dizzy · dice = roll · jukebox = shuffle · asleep = wake
  PWR button: short = back home (on home: screen off), hold 2 s = power off (battery only)

  Files in this sketch: LarpCharm.ino, aafonts.h (Nunito, SIL Open Font Licence), songs.h (public-domain melodies)
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
#include "aafonts.h"
#include "songs.h"

// shape of one eye (kept up here so the IDE's generated prototypes can see it)
struct EyeShape { float w, h, rad, lidT, slope, lidB; };
struct OrbSprite { uint16_t *px; uint8_t *a; };
#include "questart.h"   // Quest art (its types must be visible to the IDE's generated prototypes)
#include "roninart.h"   // Ronin art

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
#define FW_VERSION  "6.0"
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
  CMD_SKIN        = 0x16, // [look 0=deck 1=aero]([aero style 0..2])
  CMD_EYES        = 0x17, // [expression 0..11, 255 = automatic]
  CMD_TUNE        = 0x18, // [op 0=stop 1=play now 2=add to queue 3=skip][song ids...]
  CMD_TORCH       = 0x19, // [on 0/1][colour 0..3]
  CMD_SONG_LIST   = 0x1A, // -> "song=id,notes,name" ... "songs=end"
  CMD_SONG_DELETE = 0x1B, // [song id 100..107]
};

enum Mode : uint8_t { M_CLOCK = 0, M_TEXT, M_PHOTO, M_CLIP, M_MUSIC, M_PET, M_TOY, M_CARD, M_DECK, M_EYES, M_JUKE, M_TORCH, M_CHARGE, M_COUNT };

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
const uint16_t AERO_ACC_C[3] = { c565(0, 132, 255), c565(0, 168, 214), c565(232, 64, 168) };

// colour themes for the deck (swipe sideways on the deck, or pick one in the app)
#define THEME_COUNT 6
const uint16_t THEMES[THEME_COUNT] = { c565(0, 229, 255), c565(255, 43, 214), c565(255, 176, 0), c565(57, 255, 20), c565(255, 59, 48), c565(200, 220, 255) };
const char *THEME_NAMES[THEME_COUNT] = {"ice", "neon", "amber", "phosphor", "redline", "ghost"};
#define TEXT_STYLES 4

// ───────────── State ─────────────
Preferences prefs;

uint8_t  mode = M_DECK;
uint8_t  theme = 0;
#define SKIN_QUEST 2
#define SKIN_RONIN 3
#define SKIN_COUNT 4
#define PIXEL_SKIN (skin >= SKIN_QUEST)
uint8_t  skin = 0, aeroVar = 0;                       // look: 0 deck, 1 aero
bool     eyesIdle = false, tiltSpin = true, idleEyesOn = false;
uint8_t  idleReturn = 8;
uint32_t stepsToday = 0, lastStepAt = 0, stepsSaved = 0;
int      stepDay = -1;
uint8_t  stepRun = 0;
bool     stepArmed = true;
float    stepLP = 1.0f;
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
float    vHist[12], vSlow[6], vFast = 4.0f;
int      vSlowN = 0;
uint32_t lastSlowAt = 0;
bool     wallPower = false;
int      vHistN = 0;
uint32_t lastBatRead = 0, lastStatus = 0;
uint32_t toastUntil = 0;
String   toastMsg;
bool     timeValid = false;

NimBLECharacteristic *statusChar = nullptr;

struct Packet { uint16_t len; uint8_t data[514]; };
QueueHandle_t rxQueue;

// ───────────── Helpers ─────────────
void idleEyesEnd();
void stepFeed(float x, float y, float z, uint32_t now);
extern bool jbPlaying;
void toast(const String &m, uint32_t ms = 1200) { toastMsg = m; toastUntil = millis() + ms; }

void touchActivity() {
  lastInteraction = millis();
  if (idleEyesOn) idleEyesEnd();
  if (sleeping) { sleeping = false; wakeFxAt = skin == 0 ? millis() : 0; analogWrite(LCD_BL, mode == 11 ? 255 : brightness); }
}

void setBacklight(int v) { brightness = constrain(v, 8, 255); if (!sleeping && mode != 11) analogWrite(LCD_BL, brightness); }

void beep(int n = 1) {
  for (int i = 0; i < n; i++) { tone(BUZZER, 2700, 80); delay(140); }
  noTone(BUZZER);
}

void sfx(uint8_t id) {
  if (!soundsOn || jbPlaying) return;
  switch (id) {
    case 0: tone(BUZZER, 2200, 18); break;                                                      // tick
    case 1: tone(BUZZER, 1500, 22); tone(BUZZER, 2100, 28); break;                              // swipe
    case 2: tone(BUZZER, 1319, 70); tone(BUZZER, 1568, 70); tone(BUZZER, 2093, 110); break;     // hello
    case 3: tone(BUZZER, 900, 30);  tone(BUZZER, 1200, 30); tone(BUZZER, 1700, 40); break;      // roll
    case 4: tone(BUZZER, 1800, 25); tone(BUZZER, 2600, 35); break;                              // chirp
  }
}

void savePrefs() {
  prefs.putUChar("bg", bgStyle);      prefs.putUChar("face", faceStyle);
  prefs.putUChar("toy", toyKind);     prefs.putUChar("theme", theme);
  prefs.putULong("steps", stepsToday);
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
  mode       = prefs.getUChar("mode", M_DECK) % M_COUNT;
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
  uint8_t fl = prefs.getUChar("flags", 0x2B);
  if (prefs.getUChar("uiv", 0) == 4) { prefs.putUChar("uiv", 5); fl |= 0x20; prefs.putUChar("flags", fl); }
  if (prefs.getUChar("uiv", 0) < 4) {          // first start after the 4.0 update: land on the deck
    prefs.putUChar("uiv", 5);
    mode = M_DECK; bgStyle = 9; fl |= 8;
    prefs.putUChar("mode", mode); prefs.putUChar("bg", bgStyle); prefs.putUChar("flags", fl);
  }
  soundsOn = fl & 1; shakeWake = fl & 2; hourChime = fl & 4; fxOn = fl & 8; eyesIdle = fl & 16; tiltSpin = fl & 32;
  skin       = prefs.getUChar("skin", 0) % SKIN_COUNT;
  aeroVar    = prefs.getUChar("avar", 0) % 3;
  stepsToday = stepsSaved = prefs.getULong("steps", 0);
  stepDay    = prefs.getShort("sday", -1);
  if (mode == 11 || mode == 12) mode = M_DECK;
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
  stepFeed(x, y, z, now);
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
  uint16_t t = skin == SKIN_RONIN ? c565(230, 70, 56) : skin == SKIN_QUEST ? c565(250, 210, 90) : skin ? AERO_ACC_C[aeroVar % 3] : THEMES[theme % THEME_COUNT];
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

// The battery level as a number, nothing else. A bolt sits in front of it while charging.
void drawBatteryCells(int x, int y) {
  (void)x;
  char b[6];
  snprintf(b, sizeof(b), "%d%%", batPct);
  uint16_t col = charging ? COL_LIME : batPct > 30 ? ACC : batPct > 15 ? COL_AMBER : COL_RED;
  int w = strlen(b) * 6, rx = 214;
  textAt(b, rx - w, y + 1, 1, col);
  if (charging) drawBolt(rx - w - 7, y + 4, COL_AMBER);
}

// The status line every screen shares: name, time or timer, link bars, battery.
void aeroTopBar(bool showTime);
void questTopBar(bool showTime);
void topBar(const char *label, bool showTime) {
  if (PIXEL_SKIN) { questTopBar(showTime); return; }
  if (skin) { aeroTopBar(showTime); return; }
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
  } else if (mode == M_DECK && timeValid) {
    if (stepsToday < 10000) snprintf(buf, sizeof(buf), "%lu STP", (unsigned long)stepsToday);
    else snprintf(buf, sizeof(buf), "%.1fK STP", stepsToday / 1000.0f);
    textAt(buf, 88, 9, 1, COL_WHITE);
  }
  for (int i = 0; i < 3; i++) gfx->fillRect(148 + i * 4, 15 - i * 2, 3, 2 + i * 2, bleConnected ? ACC : COL_GREY);
  drawBatteryCells(176, 8);
}

void aeroToast();
void questToast();
void drawToast() {
  if (millis() > toastUntil) return;
  if (PIXEL_SKIN) { questToast(); return; }
  if (skin) { aeroToast(); return; }
  String m = toastMsg; m.toUpperCase();
  int w = m.length() * 12 + 20, x = (SCR_W - w) / 2;
  gfx->fillRect(x, 230, w, 26, COL_BLACK);
  hudPanel(x, 230, w, 26, ACC, 0);
  textCentered(m, 236, 2, ACC);
}

void aeroOverlayPanel(const char *big, const char *small, float frac);
void questOverlay(const char *big, const char *small, float frac);
void drawTransferOverlay() {
  if (!xferActive || xferSize == 0 || xferKind == 3) return;      // album art arrives quietly
  int pct = (int)(100ULL * xferGot / xferSize);
  if (PIXEL_SKIN) { questOverlay("Receiving", nullptr, pct / 100.0f); return; }
  if (skin) { aeroOverlayPanel("Receiving", nullptr, pct / 100.0f); return; }
  gfx->fillRect(30, 112, 180, 56, COL_BLACK);
  hudPanel(30, 112, 180, 56, ACC, 0);
  textCentered(String("UPLINK ") + pct + "%", 122, 2, COL_WHITE);
  for (int i = 0; i < 16; i++) gfx->fillRect(41 + i * 10, 148, 8, 8, i * 100 < pct * 16 ? ACC : ACC_LOW);
}

// what a screen shows when it has nothing to show yet
void aeroBackdrop(const uint16_t *base = nullptr);
void questEmpty(const char *big, const char *l1, const char *l2);
void emptyState(const char *label, const char *big, const char *l1, const char *l2) {
  if (PIXEL_SKIN) { questEmpty(big, l1, l2); return; }
  if (skin) {
    aeroBackdrop();
    topBar(label, true);
    String b = big; b.toLowerCase(); b.setCharAt(0, toupper(b[0]));
    aeroOverlayPanel(b.c_str(), (String(l1) + " " + l2).c_str(), -1);
    return;
  }
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

// ═════════════ v5: anti-aliased drawing (used by the Aero look and the eyes) ═════════════
static inline uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a) {
  if (a >= 252) return fg;
  if (a < 8) return bg;
  uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81F;
  uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81F;
  uint32_t r = ((((f - b) * (a >> 3)) >> 5) + b) & 0x07E0F81F;
  return (uint16_t)(r | (r >> 16));
}
static inline void blendPx(int x, int y, uint16_t c, uint8_t a) {
  if (FB && (unsigned)x < SCR_W && (unsigned)y < SCR_H) { uint16_t *p = FB + y * SCR_W + x; *p = blend565(*p, c, a); }
}
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline uint16_t rgbf(float r, float g, float b) {
  return c565((uint8_t)constrain((int)r, 0, 255), (uint8_t)constrain((int)g, 0, 255), (uint8_t)constrain((int)b, 0, 255));
}
static inline float scr(float c, float a) { return c + (255.0f - c) * a; }       // "screen": lighten toward white

// signed distance to a rounded rectangle centred at the origin
static inline float sdRound(float px, float py, float hw, float hh, float r) {
  float qx = fabsf(px) - (hw - r), qy = fabsf(py) - (hh - r);
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r;
}

const uint16_t AERO_INK = c565(8, 38, 78);

const AAGlyph *aaGlyph(const AAFont &f, char ch) {
  if ((uint8_t)ch < f.first || (uint8_t)ch > f.last) {
    if ('?' < f.first || '?' > f.last) return nullptr;
    ch = '?';
  }
  return &f.g[(uint8_t)ch - f.first];
}
int aaWidth(const AAFont &f, const char *s) {
  int w = 0;
  for (; *s; s++) { const AAGlyph *g = aaGlyph(f, *s); if (g) w += g->adv; }
  return w;
}
// y is the baseline
void aaText(const AAFont &f, const char *s, int x, int y, uint16_t col, uint8_t alpha = 255) {
  if (!FB) return;
  for (; *s; s++) {
    const AAGlyph *g = aaGlyph(f, *s);
    if (!g) continue;
    const uint8_t *bmp = f.bmp + g->off;
    int rb = (g->w + 1) / 2;
    for (int j = 0; j < g->h; j++) {
      int yy = y + g->dy + j;
      if ((unsigned)yy >= SCR_H) continue;
      uint16_t *row = FB + yy * SCR_W;
      for (int i = 0; i < g->w; i++) {
        int xx = x + g->dx + i;
        if ((unsigned)xx >= SCR_W) continue;
        uint8_t v = bmp[j * rb + (i >> 1)];
        v = (i & 1) ? (v & 15) : (v >> 4);
        if (v) row[xx] = blend565(row[xx], col, (uint8_t)((v * 17 * alpha) / 255));
      }
    }
    x += g->adv;
  }
}
void aaTextS(const AAFont &f, const char *s, int x, int y, uint16_t col) {     // soft drop shadow
  aaText(f, s, x + 1, y + 2, c565(0, 30, 70), 120);
  aaText(f, s, x, y, col);
}
void aaTextC(const AAFont &f, const char *s, int cx, int y, uint16_t col, bool shadow = false) {
  int x = cx - aaWidth(f, s) / 2;
  if (shadow) aaTextS(f, s, x, y, col); else aaText(f, s, x, y, col);
}
// largest of the three text fonts that fits
const AAFont &aaFit(const char *s, int maxW) {
  if (aaWidth(F_L, s) <= maxW) return F_L;
  if (aaWidth(F_M, s) <= maxW) return F_M;
  return F_S;
}
// cuts a string so it fits, adding ".."
String aaClip(const AAFont &f, const String &s, int maxW) {
  if (aaWidth(f, s.c_str()) <= maxW) return s;
  String o = s;
  while (o.length() > 1 && aaWidth(f, (o + "..").c_str()) > maxW) o = o.substring(0, o.length() - 1);
  return o + "..";
}

// Frosted glass panel: soft white fill, glossy upper half, bright inner rim, faint dark outline.
void glassPanel(int x, int y, int w, int h, int r, float a0 = 0.42f, float a1 = 0.16f) {
  if (!FB) return;
  float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
  float rr = fminf((float)r, fminf(hw, hh));
  const uint16_t edge = c565(0, 40, 90);
  for (int j = max(0, y - 1); j < min((int)SCR_H, y + h + 1); j++) {
    float py = j + 0.5f - cy, t = (j - y) / (float)h;
    float fill = a0 + (a1 - a0) * t + (t < 0.48f ? 0.16f : 0.0f);
    uint16_t *row = FB + j * SCR_W;
    for (int i = max(0, x - 1); i < min((int)SCR_W, x + w + 1); i++) {
      float sd = sdRound(i + 0.5f - cx, py, hw, hh, rr);
      float ol = clamp01(1.0f - fabsf(sd - 0.2f)) * 0.30f;
      float a = clamp01(0.5f - sd);
      if (ol <= 0 && a <= 0) continue;
      if (ol > 0) row[i] = blend565(row[i], edge, (uint8_t)(ol * 255));
      if (a > 0) {
        float rim = clamp01(1.0f - fabsf(sd + 1.3f)) * (0.9f - 0.55f * t);
        row[i] = blend565(row[i], COL_WHITE, (uint8_t)(clamp01(fill * a + rim) * 255));
      }
    }
  }
}

// Glossy sphere, the Aero "orb": shaded body, glowing bottom, bright specular band on top.
void glossOrb(int cx, int cy, int r, uint16_t base, float fade = 1.0f) {
  if (!FB || r < 2) return;
  float br = r8(base), bg = g8(base), bb = b8(base), rr = r;
  for (int y = -r - 1; y <= r; y++) {
    int yy = cy + y;
    if ((unsigned)yy >= SCR_H) continue;
    float ny = (y + 0.5f) / rr;
    uint16_t *row = FB + yy * SCR_W;
    for (int x = -r - 1; x <= r; x++) {
      int xx = cx + x;
      if ((unsigned)xx >= SCR_W) continue;
      float nx = (x + 0.5f) / rr, d2 = nx * nx + ny * ny;
      if (d2 > 1.1f) continue;
      float d = sqrtf(d2), a = clamp01((1.0f - d) * rr + 0.5f);
      if (a <= 0) continue;
      float k = 0.58f + 0.42f * (ny + 1) * 0.5f;
      float cr = br * k, cg = bg * k, cb = bb * k;
      float glow = clamp01((ny - 0.15f) * 1.4f) * clamp01(1.15f - d) * 0.75f;
      cr = scr(cr, glow * 0.6f); cg = scr(cg, glow * 0.6f); cb = scr(cb, glow * 0.6f);
      float rim = d2 * d2 * 0.35f;
      cr *= 1 - rim; cg *= 1 - rim; cb *= 1 - rim;
      float ex = nx / 0.74f, ey = (ny + 0.47f) / 0.40f, e2 = ex * ex + ey * ey;
      if (e2 < 1) {
        float s = clamp01((1 - e2) * 4.0f) * (0.92f - 0.62f * clamp01((ny + 0.87f) / 0.8f));
        cr = scr(cr, s); cg = scr(cg, s); cb = scr(cb, s);
      }
      row[xx] = blend565(row[xx], rgbf(cr, cg, cb), (uint8_t)(a * fade * 255));
    }
  }
}

// Soap bubble: thin ring, faint fill, little highlight.
void drawBubble(float cx, float cy, float r) {
  if (!FB) return;
  for (int y = (int)(cy - r - 2); y <= (int)(cy + r + 2); y++) {
    if ((unsigned)y >= SCR_H) continue;
    for (int x = (int)(cx - r - 2); x <= (int)(cx + r + 2); x++) {
      if ((unsigned)x >= SCR_W) continue;
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
      if (d > r + 1.5f) continue;
      float ring = clamp01(1.0f - fabsf(d - (r - 0.8f)) / 1.2f) * (0.55f + 0.3f * (dy / r));
      float fill = d < r ? 0.07f : 0;
      float hx = (dx + r * 0.38f) / (r * 0.3f), hy = (dy + r * 0.42f) / (r * 0.22f), h2 = hx * hx + hy * hy;
      float hl = h2 < 1 ? (1 - h2) * 0.9f : 0;
      float a = clamp01(ring + fill + hl);
      if (a > 0.02f) FB[y * SCR_W + x] = blend565(FB[y * SCR_W + x], COL_WHITE, (uint8_t)(a * 255));
    }
  }
}

// ═════════════ v5: the Aero look ═════════════
#define SKIN_DECK 0
#define SKIN_AERO 1
#define AERO_VARS 3
const char *AERO_NAMES[AERO_VARS] = {"Meadow", "Ocean", "Bubblegum"};
const uint16_t AERO_ACC[AERO_VARS] = { c565(0, 132, 255), c565(0, 168, 214), c565(232, 64, 168) };
const char *AERO_TITLE[M_COUNT] = {"Clock", "Message", "Photo", "Clips", "Music", "Pet", "Dice", "ID card", "Larp Charm", "Eyes", "Jukebox", "Torch", "Power"};
uint16_t *aeroBg = nullptr;
int8_t    aeroBuilt = -1;

// The still part of the Aero backdrop is worked out once per style and kept in PSRAM.
void buildAeroBg() {
  if (!aeroBg || aeroBuilt == (int8_t)aeroVar) return;
  for (int y = 0; y < SCR_H; y++) {
    for (int x = 0; x < SCR_W; x++) {
      float r, g, b;
      if (aeroVar == 1) {                                         // ocean
        float t = y / 279.0f;
        if (t < 0.5f) { float k = t / 0.5f; r = 150 - 130 * k; g = 232 - 82 * k; b = 255 - 35 * k; }
        else          { float k = (t - 0.5f) / 0.5f; r = 20 - 18 * k; g = 150 - 98 * k; b = 220 - 100 * k; }
        float u = x + y * 0.42f;
        float ray = powf(sinf(u * 0.045f) * 0.5f + 0.5f, 10) * (1 - t) * 0.38f + powf(sinf(u * 0.081f + 1.3f) * 0.5f + 0.5f, 14) * (1 - t) * 0.22f;
        r = scr(r, ray); g = scr(g, ray); b = scr(b, ray);
        float sand = 262 + 5 * sinf(x * 0.035f + 0.7f) + 3 * sinf(x * 0.09f);
        float sa = clamp01(y - sand + 0.5f);
        if (sa > 0) { float k = clamp01((y - sand) / 18); float sr = 240 - 40 * k, sg = 222 - 46 * k, sb = 170 - 50 * k;
          r += (sr - r) * sa; g += (sg - g) * sa; b += (sb - b) * sa; }
      } else if (aeroVar == 2) {                                  // bubblegum Y2K
        float t = y / 279.0f;
        if (t < 0.5f) { float k = t / 0.5f; r = 255 - 50 * k; g = 208 - 46 * k; b = 238 + 17 * k; }
        else          { float k = (t - 0.5f) / 0.5f; r = 205 - 105 * k; g = 162 - 96 * k; b = 255 - 55 * k; }
        for (int i = 0; i < 3; i++) {
          static const float BOKEH_X[3] = {48, 196, 120}, BOKEH_Y[3] = {70, 150, 238}, BOKEH_R[3] = {40, 56, 34};
          float dx = x - BOKEH_X[i], dy = y - BOKEH_Y[i], d = sqrtf(dx * dx + dy * dy);
          float a = clamp01((BOKEH_R[i] - d) / 3) * 0.16f + clamp01(1 - fabsf(d - BOKEH_R[i]) / 1.5f) * 0.28f;
          r = scr(r, a); g = scr(g, a); b = scr(b, a);
        }
        float yc = 136 + 24 * sinf(x * 0.02f + 0.4f), dd = (y - yc) / 8;
        float rib = expf(-dd * dd) * 0.45f + expf(-dd * dd / 6) * 0.12f;
        r = scr(r, rib); g = scr(g, rib); b = scr(b, rib);
      } else {                                                    // meadow
        float t = y / 210.0f;
        if (t < 0.55f) { float k = t / 0.55f; r = 14 + 72 * k; g = 92 + 86 * k; b = 204 + 46 * k; }
        else           { float k = clamp01((t - 0.55f) / 0.45f); r = 86 + 128 * k; g = 178 + 64 * k; b = 250 + 5 * k; }
        float sdx = x - 198, sdy = y - 30, sd = sqrtf(sdx * sdx + sdy * sdy);
        float sun = expf(-sd / 34) * 0.85f + expf(-sd / 8) * 0.5f;
        r = scr(r, sun); g = scr(g, sun * 0.97f); b = scr(b, sun * 0.85f);
        float yc = 126 + 26 * sinf(x * 0.018f + 0.6f), d1 = (y - yc) / 7;
        float yc2 = 150 + 18 * sinf(x * 0.026f + 2.2f), d2 = (y - yc2) / 5;
        float rib = expf(-d1 * d1) * 0.42f + expf(-d1 * d1 / 7) * 0.12f + expf(-d2 * d2) * 0.26f;
        r = scr(r, rib); g = scr(g, rib); b = scr(b, rib);
        float hb = 206 + 10 * sinf(x * 0.021f + 0.4f) + 4 * sinf(x * 0.07f);
        float ha = clamp01(y - hb + 0.5f);
        if (ha > 0) { float k = clamp01((y - hb) / 40); float hr = 128 - 70 * k, hg = 210 - 60 * k, hbb = 74 - 30 * k;
          float gl = expf(-(y - hb) / 2.5f) * 0.5f; hr = scr(hr, gl); hg = scr(hg, gl); hbb = scr(hbb, gl);
          r += (hr - r) * ha; g += (hg - g) * ha; b += (hbb - b) * ha; }
        float hf = 238 + 8 * sinf(x * 0.03f + 2.0f);
        float fa = clamp01(y - hf + 0.5f);
        if (fa > 0) { float k = clamp01((y - hf) / 36); float hr = 96 - 66 * k, hg = 192 - 82 * k, hbb = 56 - 22 * k;
          float gl = expf(-(y - hf) / 2.5f) * 0.55f; hr = scr(hr, gl); hg = scr(hg, gl); hbb = scr(hbb, gl);
          r += (hr - r) * fa; g += (hg - g) * fa; b += (hbb - b) * fa; }
      }
      aeroBg[y * SCR_W + x] = rgbf(r, g, b);
    }
  }
  aeroBuilt = aeroVar;
}

struct AeroBubble { float x, y, r, v, ph; };
AeroBubble abub[12];
void initAeroBubbles() {
  for (int i = 0; i < 12; i++) {
    abub[i].x = random(10, 230); abub[i].y = random(20, 300);
    abub[i].r = 3 + random(0, 9); abub[i].v = 0.25f + random(0, 60) / 100.0f; abub[i].ph = random(0, 628) / 100.0f;
  }
}
void aeroBackdrop(const uint16_t *base) {
  buildAeroBg();
  if (!base) base = aeroBg;
  if (base && FB) memcpy(FB, base, FB_BYTES); else gfx->fillScreen(c565(80, 170, 240));
  int n = aeroVar == 1 ? 12 : 7;
  for (int i = 0; i < n; i++) {
    AeroBubble &b = abub[i];
    b.y -= b.v * (aeroVar == 1 ? 1.4f : 1.0f);
    b.x += sinf(frameNo * 0.03f + b.ph) * 0.35f + tiltX * 0.3f;
    if (b.y < -b.r - 4) { b.y = SCR_H + b.r + random(0, 60); b.x = random(10, 230); }
    drawBubble(b.x, b.y, b.r);
  }
  if (aeroVar == 2)
    for (int i = 0; i < 6; i++) {
      int ph = (frameNo + i * 13) % 50, rr = ph < 25 ? ph / 6 : (50 - ph) / 6;
      static const uint8_t P[6][2] = {{30, 40}, {212, 60}, {64, 200}, {186, 222}, {120, 30}, {20, 140}};
      if (rr > 0) sparkle(P[i][0], P[i][1], rr, COL_WHITE);
    }
}

void aeroTopBar(bool showTime) {
  const char *title = AERO_TITLE[mode < M_COUNT ? mode : M_DECK];
  int tw = aaWidth(F_S, title);
  glassPanel(26, 5, tw + 20, 20, 10, 0.62f, 0.36f);
  aaText(F_S, title, 36, 20, AERO_INK);
  char b[8];
  snprintf(b, sizeof(b), "%d%%", batPct);
  int bw = aaWidth(F_S, b) + (charging ? 28 : 20), bx = 214 - bw;
  glassPanel(bx, 5, bw, 20, 10, 0.62f, 0.36f);
  int tx = bx + 10;
  if (charging) { drawBolt(bx + 12, 15, c565(20, 150, 40)); tx += 8; }
  aaText(F_S, b, tx, 20, (!charging && batPct < 15) ? c565(200, 20, 30) : AERO_INK);
  glossOrb(bx - 9, 15, 4, bleConnected ? c565(40, 200, 60) : c565(150, 150, 160));
  char c[12] = "";
  struct tm t;
  if (timerEndAt) { unsigned long left = (timerEndAt - millis()) / 1000; snprintf(c, sizeof(c), "%lu:%02lu", left / 60, left % 60); }
  else if (showTime && localNow(t)) snprintf(c, sizeof(c), "%d:%02d", t.tm_hour, t.tm_min);
  if (c[0]) {
    int cw = aaWidth(F_S, c), mid = (26 + tw + 20 + bx - 14) / 2;
    if (mid - cw / 2 - 6 > 26 + tw + 22) aaTextS(F_S, c, mid - cw / 2, 20, timerEndAt ? c565(255, 236, 140) : COL_WHITE);
  }
}

void aeroToast() {
  int w = aaWidth(F_M, toastMsg.c_str()) + 32, x = (SCR_W - w) / 2;
  glassPanel(x, 226, w, 30, 15, 0.82f, 0.6f);
  aaTextC(F_M, toastMsg.c_str(), 120, 247, AERO_INK);
}

void aeroProgress(int x, int y, int w, int h, float f, uint16_t col) {
  glassPanel(x, y, w, h, h / 2, 0.30f, 0.12f);
  int fw = (int)((w - 4) * clamp01(f));
  if (fw > h - 4) {
    for (int j = 0; j < h - 4; j++) {
      float t = j / (float)(h - 4);
      uint16_t c = rgbf(scr(r8(col), 0.45f * (1 - t)), scr(g8(col), 0.45f * (1 - t)), scr(b8(col), 0.45f * (1 - t)));
      int inset = (j < 2 || j > h - 7) ? 2 : 0;
      gfx->drawFastHLine(x + 2 + inset, y + 2 + j, fw - 2 * inset, c);
    }
  }
}

void aeroOverlayPanel(const char *big, const char *small, float frac) {
  glassPanel(26, 100, 188, 80, 20, 0.85f, 0.62f);
  aaTextC(F_M, big, 120, 132, AERO_INK);
  if (frac >= 0) aeroProgress(44, 146, 152, 14, frac, ACC);
  else if (small) aaTextC(F_S, small, 120, 158, AERO_INK);
}

const char *DAYS_T[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *MON_T[]  = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

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
#define TILE_COUNT 10
const int16_t TILE_X[TILE_COUNT] = {8, 8, 160, 8, 86, 9, 54, 99, 144, 189};
const int16_t TILE_Y[TILE_COUNT] = {26, 104, 104, 174, 174, 238, 238, 238, 238, 238};
const int16_t TILE_W[TILE_COUNT] = {224, 146, 72, 72, 146, 42, 42, 42, 42, 42};
const int16_t TILE_H[TILE_COUNT] = {72, 64, 64, 58, 58, 22, 22, 22, 22, 22};
const uint8_t TILE_MODE[TILE_COUNT] = {M_CLOCK, M_MUSIC, M_PET, M_TOY, M_TEXT, M_PHOTO, M_CLIP, M_CARD, M_EYES, M_JUKE};

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
  static const char *DOCK[5] = {"IMG", "VID", "ID", "EYE", "JBX"};
  for (int i = 0; i < 5; i++) {
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

void drawClockAero();
void drawClockQuest();
void drawClock() {
  if (PIXEL_SKIN) { drawClockQuest(); return; }
  if (skin) { drawClockAero(); return; }
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

void drawMusicAero();
void drawMusicQuest();
void drawMusic() {
  if (PIXEL_SKIN) { drawMusicQuest(); return; }
  if (skin) { drawMusicAero(); return; }
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

void drawPetQuest();
void drawPet() {
  if (PIXEL_SKIN) { drawPetQuest(); return; }
  if (skin) {
    aeroBackdrop();
    topBar("//DAEMON", true);
    aaTextC(F_L, asciiOnly(petName).c_str(), 120, 62, COL_WHITE, true);
  } else {
    verticalGradient(COL_VOID, mix565(petColor, COL_BLACK, 205));
    floorGrid(204, ACC_LOW);
    topBar("//DAEMON", true);
    textCentered(asciiOnly(petName), 34, 3, COL_WHITE);
  }

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
  if (skin) {
    aeroProgress(50, 212, 140, 14, petMood / 100.0f, petMood > 50 ? c565(60, 200, 70) : mc);
    aaTextC(F_S, dizzy ? "Dizzy!" : petMood > 70 ? "Happy" : petMood > 35 ? "Just chilling" : "Hungry", 120, 248, COL_WHITE, true);
    return;
  }
  for (int i = 0; i < 10; i++) gfx->fillRect(51 + i * 14, 226, 11, 8, i < (petMood + 5) / 10 ? mc : ACC_LOW);
  textCentered(dizzy ? "STATE: DIZZY" : petMood > 70 ? "STATE: HAPPY" : petMood > 35 ? "STATE: IDLE" : "STATE: HUNGRY", 242, 1, COL_WHITE);
}

void drawCardAero();
void drawCardQuest();
void drawCard() {
  if (PIXEL_SKIN) { drawCardQuest(); return; }
  if (skin) { drawCardAero(); return; }
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

void drawToyQuest();
void drawToy() {
  if (PIXEL_SKIN) { drawToyQuest(); return; }
  static const char *TN[3] = {"< ORACLE >", "< DICE >", "< COIN >"};
  uint32_t el = millis() - toyRollAt;
  bool rolling = toyRollAt && el < 1100;
  int jx = rolling ? (int)random(-5, 6) : 0, jy = rolling ? (int)random(-5, 6) : 0;

  if (skin) aeroBackdrop();
  else {
    gfx->fillScreen(COL_VOID);
    for (int y = 50; y < SCR_H; y += 20) for (int x = 10; x < SCR_W; x += 20) gfx->drawPixel(x, y, ACC_DIM);
  }

  if (toyKind == 0 && skin) {
    glossOrb(120 + jx, 160 + jy, 90, c565(40, 34, 90));
    glossOrb(120 + jx, 168 + jy, 50, mix565(ACC, COL_BLACK, 110), 0.95f);
    const char *ans = rolling ? "..." : toyHasResult ? EIGHT[toyResult % EIGHT_N] : (imuOK ? "shake\nme" : "tap\nme");
    char ln[2][16] = {"", ""};
    const char *nl = strchr(ans, '\n');
    if (nl) { snprintf(ln[0], sizeof(ln[0]), "%.*s", (int)(nl - ans), ans); snprintf(ln[1], sizeof(ln[1]), "%s", nl + 1); }
    else snprintf(ln[0], sizeof(ln[0]), "%s", ans);
    if (ln[1][0]) { aaTextC(F_M, ln[0], 120 + jx, 166 + jy, COL_WHITE, true); aaTextC(F_M, ln[1], 120 + jx, 188 + jy, COL_WHITE, true); }
    else aaTextC(F_M, ln[0], 120 + jx, 176 + jy, COL_WHITE, true);
  } else if (toyKind == 0) {
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
  if (skin) {
    static const char *TA[3] = {"Oracle", "Dice", "Coin"};
    int w = aaWidth(F_M, TA[toyKind % 3]) + 30;
    glassPanel(120 - w / 2, 32, w, 26, 13, 0.6f, 0.35f);
    aaTextC(F_M, TA[toyKind % 3], 120, 51, AERO_INK);
  } else textPop(TN[toyKind % 3], 30, 2, ACC);
}

void drawAlarmQuest();
void drawAlarm() {
  if (!alarmUntil) return;
  if (PIXEL_SKIN) { drawAlarmQuest(); return; }
  if (skin) {
    glassPanel(20, 92, 200, 96, 26, (millis() / 300) % 2 ? 0.9f : 0.6f, 0.45f);
    aaTextC(F_L, "Time's up!", 120, 136, AERO_INK);
    aaTextC(F_S, "Tap to stop", 120, 164, AERO_INK);
    return;
  }
  bool flash = (millis() / 300) % 2;
  gfx->fillRect(16, 92, 208, 96, flash ? ACC : COL_BLACK);
  hudPanel(16, 92, 208, 96, COL_WHITE, 0);
  textCentered("TIME'S UP", 116, 3, flash ? COL_BLACK : ACC);
  textCentered("tap to stop", 160, 1, flash ? COL_BLACK : COL_WHITE);
}

// a line across the bottom of the other screens while a song is playing
void drawTicker() {
  if (!npPlaying || !npTitle.length()) return;
  if (PIXEL_SKIN) return;
  if (skin) {
    if (mode != M_CLOCK) return;
    String s = asciiOnly(npTitle);
    if (npArtist.length()) s += String(" - ") + asciiOnly(npArtist);
    s = aaClip(F_S, s, 150);
    int w = aaWidth(F_S, s.c_str()) + 30;
    glassPanel(120 - w / 2, 250, w, 22, 11, 0.6f, 0.35f);
    glossOrb(120 - w / 2 + 12, 261, 4, ACC);
    aaText(F_S, s.c_str(), 120 - w / 2 + 20, 266, AERO_INK);
    return;
  }
  dimRect(44, 260, 152, 13, 2);
  gfx->drawChar(48, 263, 0x0E, ACC, ACC);
  String s = asciiOnly(npTitle);
  if (npArtist.length()) s += String(" - ") + asciiOnly(npArtist);
  textAt(marquee(s, 22, 7), 58, 263, 1, COL_WHITE);
}

// ═════════════ v5: Aero home, an orbit of glossy orbs ═════════════
// Drag sideways (or tilt the charm) to spin it. Tap an orb to bring it to the front, tap the front one to open it.
#define ORB_N 11
const uint8_t ORB_MODE[ORB_N] = {M_CLOCK, M_MUSIC, M_EYES, M_JUKE, M_PET, M_TOY, M_TEXT, M_PHOTO, M_CLIP, M_CARD, M_TORCH};
const char *ORB_NAME[ORB_N] = {"Clock", "Music", "Eyes", "Jukebox", "Pet", "Dice", "Message", "Photo", "Clips", "ID card", "Torch"};
const uint16_t ORB_COL[ORB_N] = { c565(0, 140, 255), c565(255, 70, 160), c565(20, 90, 235), c565(140, 70, 255), c565(255, 140, 30),
                                  c565(40, 190, 70), c565(0, 185, 190), c565(255, 196, 0), c565(235, 55, 60), c565(80, 90, 220), c565(170, 190, 210) };
#define ORB_STEP  (6.2831853f / ORB_N)
#define ORBIT_CX  120
#define ORBIT_CY  184
#define ORBIT_RX  90
#define ORBIT_RY  26
float    orbitA = 0, orbitV = 0, orbitGoal = 0, orbitFlick = 0;
bool     orbitDrag = false, orbitGoalOn = false;
int      orbitLastX = 0, orbFront = 0;
uint32_t lastTouchUp = 0, orbitLastT = 0, orbitFrameT = 0;

// Each orb is drawn once per size into PSRAM at start-up, then just copied each frame.
#define ORB_RMIN 9
#define ORB_RMAX 28
OrbSprite orbSpr[ORB_N][ORB_RMAX - ORB_RMIN + 1];
bool      orbSprReady = false;
uint16_t *homeBase = nullptr;                   // backdrop + clock pane + track, rebuilt when the style changes
int8_t    homeBaseVar = -1;

void renderOrbSprite(OrbSprite &sp, int r, uint16_t base) {
  float br = r8(base), bg = g8(base), bb = b8(base), rr = r;
  int sz = 2 * r + 2;
  for (int y = -r - 1; y <= r; y++) {
    float ny = (y + 0.5f) / rr;
    for (int x = -r - 1; x <= r; x++) {
      int idx = (y + r + 1) * sz + (x + r + 1);
      float nx = (x + 0.5f) / rr, d2 = nx * nx + ny * ny, d = sqrtf(d2);
      float a = clamp01((1.0f - d) * rr + 0.5f);
      sp.a[idx] = (uint8_t)(a * 255);
      if (a <= 0) { sp.px[idx] = 0; continue; }
      float k = 0.58f + 0.42f * (ny + 1) * 0.5f;
      float cr = br * k, cg = bg * k, cb = bb * k;
      float glow = clamp01((ny - 0.15f) * 1.4f) * clamp01(1.15f - d) * 0.75f;
      cr = scr(cr, glow * 0.6f); cg = scr(cg, glow * 0.6f); cb = scr(cb, glow * 0.6f);
      float rim = d2 * d2 * 0.35f;
      cr *= 1 - rim; cg *= 1 - rim; cb *= 1 - rim;
      float ex = nx / 0.74f, ey = (ny + 0.47f) / 0.40f, e2 = ex * ex + ey * ey;
      if (e2 < 1) {
        float s = clamp01((1 - e2) * 4.0f) * (0.92f - 0.62f * clamp01((ny + 0.87f) / 0.8f));
        cr = scr(cr, s); cg = scr(cg, s); cb = scr(cb, s);
      }
      sp.px[idx] = rgbf(cr, cg, cb);
    }
  }
}
void buildOrbSprites() {
  if (orbSprReady) return;
  size_t total = 0;
  for (int r = ORB_RMIN; r <= ORB_RMAX; r++) total += (size_t)(2 * r + 2) * (2 * r + 2) * 3;
  uint8_t *mem = (uint8_t *)ps_malloc(total * ORB_N);
  if (!mem) return;
  for (int i = 0; i < ORB_N; i++)
    for (int r = ORB_RMIN; r <= ORB_RMAX; r++) {
      size_t n = (size_t)(2 * r + 2) * (2 * r + 2);
      OrbSprite &sp = orbSpr[i][r - ORB_RMIN];
      sp.px = (uint16_t *)mem; mem += n * 2;
      sp.a = mem; mem += n;
      renderOrbSprite(sp, r, ORB_COL[i]);
    }
  orbSprReady = true;
}
void blitOrb(int i, int cx, int cy, int r, float fade) {
  if (!orbSprReady || !FB) { glossOrb(cx, cy, r, ORB_COL[i], fade); return; }
  r = constrain(r, ORB_RMIN, ORB_RMAX);
  const OrbSprite &sp = orbSpr[i][r - ORB_RMIN];
  int sz = 2 * r + 2, x0 = cx - r - 1, y0 = cy - r - 1;
  uint16_t f = (uint16_t)(fade * 256);
  for (int j = 0; j < sz; j++) {
    int y = y0 + j;
    if ((unsigned)y >= SCR_H) continue;
    uint16_t *row = FB + y * SCR_W;
    const uint16_t *sp16 = sp.px + j * sz;
    const uint8_t *sa = sp.a + j * sz;
    for (int k = 0; k < sz; k++) {
      int x = x0 + k;
      if ((unsigned)x >= SCR_W) continue;
      uint8_t a = (uint8_t)((sa[k] * f) >> 8);
      if (a >= 8) row[x] = blend565(row[x], sp16[k], a);
    }
  }
}
void buildHomeBase() {
  buildAeroBg();
  if (!homeBase || !aeroBg || homeBaseVar == (int8_t)aeroVar) return;
  memcpy(homeBase, aeroBg, FB_BYTES);
  uint16_t *screen = FB;
  FB = homeBase;
  glassPanel(16, 32, 208, 92, 22);
  for (int k = 0; k < 180; k++) {
    float a = k * 6.2831853f / 180;
    blendPx(ORBIT_CX + (int)(sinf(a) * ORBIT_RX), ORBIT_CY + (int)(cosf(a) * ORBIT_RY), COL_WHITE, cosf(a) > 0 ? 150 : 80);
  }
  FB = screen;
  homeBaseVar = aeroVar;
}
bool orbitMoving() {
  return orbitDrag || orbitGoalOn || fabsf(orbitV) > 0.002f || fabsf(orbitA - roundf(orbitA / ORB_STEP) * ORB_STEP) > 0.002f;
}
int16_t  orbX[ORB_N], orbY[ORB_N], orbR[ORB_N];
uint8_t  orbOrder[ORB_N];

int orbOfMode(uint8_t m) {
  for (int i = 0; i < ORB_N; i++) if (ORB_MODE[i] == m) return i;
  return -1;
}
void orbitFocus(int i, bool instant) {
  if (i < 0) return;
  float target = -i * ORB_STEP;
  target += roundf((orbitA - target) / 6.2831853f) * 6.2831853f;
  orbitV = 0;
  if (instant) { orbitA = target; orbitGoalOn = false; orbFront = i; }
  else { orbitGoal = target; orbitGoalOn = true; }
}

void orbGlyph(int i, int x, int y, int r) {
  const uint16_t W = COL_WHITE, D = mix565(ORB_COL[i], COL_BLACK, 120);
  int s = max(1, r * 10 / 26);                // ~10 at the front
  switch (ORB_MODE[i]) {
    case M_CLOCK:
      gfx->drawCircle(x, y, s, W); gfx->drawCircle(x, y, s - 1, W);
      gfx->drawLine(x, y, x, y - s * 7 / 10, W); gfx->drawLine(x, y, x + s / 2, y, W);
      break;
    case M_MUSIC:
      gfx->fillCircle(x - s / 3, y + s * 6 / 10, s * 4 / 10, W);
      gfx->fillRect(x - s / 3 + s * 3 / 10, y - s * 8 / 10, max(2, s / 5), s * 14 / 10, W);
      gfx->fillTriangle(x + s / 5, y - s * 8 / 10, x + s * 8 / 10, y - s * 4 / 10, x + s / 5, y - s * 3 / 10, W);
      break;
    case M_EYES:
      gfx->fillRoundRect(x - s * 8 / 10, y - s * 6 / 10, s * 6 / 10, s * 12 / 10, max(1, s / 4), W);
      gfx->fillRoundRect(x + s * 2 / 10, y - s * 6 / 10, s * 6 / 10, s * 12 / 10, max(1, s / 4), W);
      break;
    case M_JUKE:
      gfx->fillCircle(x - s * 6 / 10, y + s * 6 / 10, s * 3 / 10, W); gfx->fillCircle(x + s * 4 / 10, y + s * 4 / 10, s * 3 / 10, W);
      gfx->fillRect(x - s * 4 / 10, y - s * 6 / 10, max(2, s / 6), s * 12 / 10, W);
      gfx->fillRect(x + s * 6 / 10, y - s * 8 / 10, max(2, s / 6), s * 12 / 10, W);
      gfx->fillTriangle(x - s * 4 / 10, y - s * 6 / 10, x + s * 7 / 10, y - s * 9 / 10, x + s * 7 / 10, y - s * 6 / 10, W);
      gfx->fillTriangle(x - s * 4 / 10, y - s * 6 / 10, x - s * 4 / 10, y - s * 3 / 10, x + s * 7 / 10, y - s * 6 / 10, W);
      break;
    case M_PET:
      gfx->fillCircle(x, y + s / 6, s * 7 / 10, W);
      gfx->fillTriangle(x - s * 7 / 10, y - s / 6, x - s * 6 / 10, y - s, x - s / 6, y - s / 2, W);
      gfx->fillTriangle(x + s * 7 / 10, y - s / 6, x + s * 6 / 10, y - s, x + s / 6, y - s / 2, W);
      gfx->fillCircle(x - s / 3, y + s / 8, max(1, s / 8), D); gfx->fillCircle(x + s / 3, y + s / 8, max(1, s / 8), D);
      break;
    case M_TOY:
      gfx->fillRoundRect(x - s * 8 / 10, y - s * 8 / 10, s * 16 / 10, s * 16 / 10, max(1, s / 4), W);
      gfx->fillCircle(x - s * 4 / 10, y - s * 4 / 10, max(1, s / 7), D); gfx->fillCircle(x, y, max(1, s / 7), D);
      gfx->fillCircle(x + s * 4 / 10, y + s * 4 / 10, max(1, s / 7), D);
      break;
    case M_TEXT:
      gfx->fillRoundRect(x - s, y - s * 7 / 10, s * 2, s * 12 / 10, max(1, s / 4), W);
      gfx->fillTriangle(x - s / 2, y + s / 2, x - s / 2, y + s, x, y + s / 2, W);
      gfx->drawFastHLine(x - s * 6 / 10, y - s / 4, s * 12 / 10, D); gfx->drawFastHLine(x - s * 6 / 10, y + s / 8, s * 8 / 10, D);
      break;
    case M_PHOTO:
      gfx->drawRoundRect(x - s, y - s * 7 / 10, s * 2, s * 15 / 10, max(1, s / 5), W);
      gfx->fillTriangle(x - s * 8 / 10, y + s * 6 / 10, x - s / 5, y - s / 10, x + s * 4 / 10, y + s * 6 / 10, W);
      gfx->fillCircle(x + s * 5 / 10, y - s * 3 / 10, max(1, s / 6), W);
      break;
    case M_CLIP:
      gfx->fillTriangle(x - s * 5 / 10, y - s * 8 / 10, x - s * 5 / 10, y + s * 8 / 10, x + s * 9 / 10, y, W);
      break;
    case M_CARD:
      gfx->fillRoundRect(x - s, y - s * 7 / 10, s * 2, s * 14 / 10, max(1, s / 5), W);
      gfx->fillCircle(x - s / 2, y - s / 8, s / 4, D);
      gfx->drawFastHLine(x, y - s / 4, s * 7 / 10, D); gfx->drawFastHLine(x, y + s / 8, s / 2, D);
      break;
    case M_TORCH:
      gfx->fillCircle(x, y - s / 5, s * 6 / 10, W);
      gfx->fillRect(x - s * 3 / 10, y + s * 3 / 10, s * 6 / 10, s * 5 / 10, W);
      gfx->drawFastHLine(x - s * 3 / 10, y + s * 6 / 10, s * 6 / 10, D);
      break;
  }
}

void drawAeroHome() {
  // physics in units of a 30 fps frame, so the spin feels the same however fast frames come
  uint32_t nowT = millis();
  float dt = orbitFrameT ? constrain((nowT - orbitFrameT) / 33.3f, 0.2f, 3.0f) : 1.0f;
  orbitFrameT = nowT;
  if (!orbitDrag) {
    if (orbitGoalOn) {
      orbitA += (orbitGoal - orbitA) * (1 - powf(0.74f, dt));
      if (fabsf(orbitGoal - orbitA) < 0.003f) { orbitA = orbitGoal; orbitGoalOn = false; }
    } else {
      if (tiltSpin && imuOK && nowT - lastTouchUp > 800 && fabsf(tiltX) > 0.35f)
        orbitV += (tiltX > 0 ? tiltX - 0.35f : tiltX + 0.35f) * 0.012f * dt;
      orbitA += orbitV * dt;
      orbitV *= powf(0.92f, dt);
      if (fabsf(orbitV) < 0.014f) {                     // settle into the nearest slot with a soft spring
        float target = roundf(orbitA / ORB_STEP) * ORB_STEP;
        orbitV += (target - orbitA) * 0.09f * dt;
        orbitV *= powf(0.72f, dt);
        if (fabsf(target - orbitA) < 0.0015f && fabsf(orbitV) < 0.001f) { orbitA = target; orbitV = 0; }
      }
    }
  }
  int f = ((int)lroundf(-orbitA / ORB_STEP) % ORB_N + ORB_N) % ORB_N;
  if (f != orbFront) { orbFront = f; sfx(0); }

  buildOrbSprites();
  buildHomeBase();
  aeroBackdrop(homeBaseVar == (int8_t)aeroVar ? homeBase : nullptr);
  aeroTopBar(false);

  // clock in a glass pane (the pane itself is part of homeBase)
  if (homeBaseVar != (int8_t)aeroVar) glassPanel(16, 32, 208, 92, 22);
  struct tm t;
  char buf[32];
  if (localNow(t)) {
    int h = t.tm_hour;
    if (!use24h) { h %= 12; if (h == 0) h = 12; }
    snprintf(buf, sizeof(buf), "%d:%02d", h, t.tm_min);
    aaTextC(F_XL, buf, 120, 94, COL_WHITE, true);
    snprintf(buf, sizeof(buf), "%s %d %s", DAYS_T[t.tm_wday], t.tm_mday, MON_T[t.tm_mon]);
    aaText(F_S, buf, 32, 115, AERO_INK);
  } else {
    aaTextC(F_XL, "--:--", 120, 94, COL_WHITE, true);
    aaText(F_S, "Open the app to set the time", 32, 115, AERO_INK);
  }
  if (timeValid) {
    snprintf(buf, sizeof(buf), "%lu steps", (unsigned long)stepsToday);
    aaText(F_S, buf, 208 - aaWidth(F_S, buf), 115, AERO_INK);
  }


  float depth[ORB_N];
  for (int i = 0; i < ORB_N; i++) {
    float th = orbitA + i * ORB_STEP, c = cosf(th);
    depth[i] = (c + 1) * 0.5f;
    orbX[i] = ORBIT_CX + (int)lroundf(sinf(th) * ORBIT_RX);
    orbY[i] = ORBIT_CY + (int)lroundf(c * ORBIT_RY);
    orbR[i] = 9 + (int)lroundf(19 * powf(depth[i], 1.6f));
    orbOrder[i] = i;
  }
  for (int i = 1; i < ORB_N; i++)
    for (int j = i; j > 0 && depth[orbOrder[j]] < depth[orbOrder[j - 1]]; j--) { uint8_t tmp = orbOrder[j]; orbOrder[j] = orbOrder[j - 1]; orbOrder[j - 1] = tmp; }
  for (int k = 0; k < ORB_N; k++) {
    int i = orbOrder[k], r = orbR[i];
    if (depth[i] > 0.45f)                                         // soft shadow on the "floor"
      for (int y = 0; y < max(2, r / 4); y++)
        for (int x = -r; x <= r; x++) {
          float e = (x / (float)r) * (x / (float)r) + (y / (r / 4.0f + 0.5f)) * (y / (r / 4.0f + 0.5f));
          if (e < 1) blendPx(orbX[i] + x, orbY[i] + r + 1 + y, c565(0, 30, 60), (uint8_t)((1 - e) * 70 * depth[i]));
        }
    blitOrb(i, orbX[i], orbY[i], r, 0.5f + 0.5f * depth[i]);
    if (r >= 13) orbGlyph(i, orbX[i], orbY[i] - r / 12, r);
  }

  const char *nm = ORB_NAME[orbFront];
  int w = aaWidth(F_M, nm) + 34;
  glassPanel(120 - w / 2, 246, w, 28, 14, 0.7f, 0.42f);
  aaTextC(F_M, nm, 120, 266, AERO_INK);
  int ph = (frameNo / 3) % 12, nudge = ph < 6 ? ph / 2 : (12 - ph) / 2;
  aaTextS(F_M, "<", 120 - w / 2 - 16 - nudge, 266, COL_WHITE);
  aaTextS(F_M, ">", 120 + w / 2 + 6 + nudge, 266, COL_WHITE);
}

// ═════════════ v5: Aero versions of the clock, music and card ═════════════
void drawClockAero() {
  aeroBackdrop();
  aeroTopBar(false);
  struct tm t;
  char buf[32];
  bool ok = localNow(t);
  int h = ok ? t.tm_hour : 0;
  if (!use24h) { h %= 12; if (h == 0) h = 12; }
  if (faceStyle == 1 && ok) {                                     // a glass orb with hands
    glossOrb(120, 150, 96, mix565(ACC, COL_WHITE, 90));
    for (int i = 0; i < 12; i++) {
      float a = i * 0.5235988f;
      int r0 = (i % 3 == 0) ? 74 : 80;
      for (int o = 0; o < 2; o++) gfx->drawLine(120 + (int)(sinf(a) * r0) + o, 150 - (int)(cosf(a) * r0), 120 + (int)(sinf(a) * 86) + o, 150 - (int)(cosf(a) * 86), COL_WHITE);
    }
    float ha = (t.tm_hour % 12 + t.tm_min / 60.0f) * 0.5235988f, ma = (t.tm_min + t.tm_sec / 60.0f) * 0.1047198f, sa = t.tm_sec * 0.1047198f;
    for (int o = -2; o <= 2; o++) gfx->drawLine(120 + o, 150, 120 + (int)(sinf(ha) * 46) + o, 150 - (int)(cosf(ha) * 46), AERO_INK);
    for (int o = -1; o <= 1; o++) gfx->drawLine(120 + o, 150, 120 + (int)(sinf(ma) * 68) + o, 150 - (int)(cosf(ma) * 68), AERO_INK);
    gfx->drawLine(120, 150, 120 + (int)(sinf(sa) * 76), 150 - (int)(cosf(sa) * 76), c565(230, 40, 90));
    glossOrb(120, 150, 7, c565(230, 40, 90));
    return;
  }
  if (faceStyle == 2 && ok) {                                     // in words
    static const char *HN[12] = {"twelve", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven"};
    int m5 = ((t.tm_min + 2) / 5) * 5, hh = t.tm_hour;
    bool to = m5 > 30;
    if (to) hh++;
    if (m5 == 60) m5 = 0;
    hh %= 12;
    int k = (to && m5) ? 60 - m5 : m5;
    const char *pre = k == 5 ? "five" : k == 10 ? "ten" : k == 15 ? "quarter" : k == 20 ? "twenty" : k == 25 ? "twenty five" : "half";
    glassPanel(16, 70, 208, 136, 24);
    if (k == 0) { aaTextC(F_L, HN[hh], 120, 132, COL_WHITE, true); aaTextC(F_L, "o'clock", 120, 168, AERO_INK); }
    else { aaTextC(F_L, pre, 120, 116, AERO_INK); aaTextC(F_M, to ? "to" : "past", 120, 144, COL_WHITE, true); aaTextC(F_L, HN[hh], 120, 182, COL_WHITE, true); }
    return;
  }
  if (faceStyle == 3 && ok) {                                     // a bubble that bounces around
    static float bx = 80, by = 110, vx = 1.3f, vy = 1.0f;
    bx += vx; by += vy;
    if (bx < 62 || bx > 178) { vx = -vx; bx = constrain(bx, 62.0f, 178.0f); }
    if (by < 88 || by > 220) { vy = -vy; by = constrain(by, 88.0f, 220.0f); }
    glossOrb((int)bx, (int)by, 56, ACC);
    snprintf(buf, sizeof(buf), "%d:%02d", h, t.tm_min);
    aaTextC(F_L, buf, (int)bx, (int)by + 10, COL_WHITE, true);
    return;
  }
  glassPanel(14, 66, 212, 136, 26);
  if (ok) snprintf(buf, sizeof(buf), "%d:%02d", h, t.tm_min); else strcpy(buf, "--:--");
  aaTextC(F_XL, buf, 120, 134, COL_WHITE, true);
  if (ok) {
    snprintf(buf, sizeof(buf), "%s %d %s%s", DAYS_T[t.tm_wday], t.tm_mday, MON_T[t.tm_mon], use24h ? "" : (t.tm_hour < 12 ? "  am" : "  pm"));
    aaTextC(F_M, buf, 120, 164, AERO_INK);
    aeroProgress(40, 176, 160, 12, t.tm_sec / 59.0f, ACC);
  } else aaTextC(F_S, "Open the app to set the time", 120, 168, AERO_INK);
  if (charging) {
    snprintf(buf, sizeof(buf), "Charging %d%%", batPct);
    int w = aaWidth(F_S, buf) + 24;
    glassPanel(120 - w / 2, 216, w, 22, 11, 0.6f, 0.4f);
    aaTextC(F_S, buf, 120, 232, c565(20, 120, 40));
  }
}

void drawMusicAero() {
  if (artLoaded && artBuf && FB) memcpy(FB, artBuf, FB_BYTES);
  else {
    aeroBackdrop();
    float beat = npPlaying ? sinf(frameNo * 0.3f) * 3 : 0;
    glossOrb(120, 106, 56 + (int)beat, ACC);
    int s = 22;
    gfx->fillCircle(120 - s / 3, 106 + s * 6 / 10, s * 4 / 10, COL_WHITE);
    gfx->fillRect(120 - s / 3 + s * 3 / 10, 106 - s * 8 / 10, 4, s * 14 / 10, COL_WHITE);
    gfx->fillTriangle(120 + s / 5, 106 - s * 8 / 10, 120 + s * 8 / 10, 106 - s * 4 / 10, 120 + s / 5, 106 - s * 3 / 10, COL_WHITE);
  }
  aeroTopBar(true);
  // frosted panel along the bottom
  if (FB) for (int y = 168; y < SCR_H; y++) {
    uint16_t *row = FB + y * SCR_W;
    uint8_t a = (uint8_t)(150 + (y - 168) * 0.6f);
    for (int x = 0; x < SCR_W; x++) row[x] = blend565(row[x], c565(236, 246, 255), a);
  }
  gfx->drawFastHLine(0, 168, SCR_W, COL_WHITE);
  String title = asciiOnly(npTitle.length() ? npTitle : String("Nothing playing"));
  String artist = asciiOnly(npArtist.length() || npTitle.length() ? npArtist : String("Play something on your phone"));
  aaText(F_M, aaClip(F_M, title, 204).c_str(), 18, 192, AERO_INK);
  aaText(F_S, aaClip(F_S, artist, 204).c_str(), 18, 210, mix565(ACC, COL_BLACK, 60));
  uint32_t pos = npPosition();
  aeroProgress(18, 216, 204, 10, npDurMs ? (float)pos / npDurMs : 0, ACC);
  char b[12];
  snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(pos / 60000), (unsigned long)((pos / 1000) % 60));
  aaText(F_S, b, 22, 242, AERO_INK);
  if (npDurMs) { snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(npDurMs / 60000), (unsigned long)((npDurMs / 1000) % 60)); aaText(F_S, b, 218 - aaWidth(F_S, b), 242, AERO_INK); }
  bool flash = millis() < mediaFlashUntil;
  glossOrb(84, 254, 13, flash && mediaFlashKind == 2 ? ACC : c565(120, 140, 160));
  glossOrb(120, 254, 17, ACC);
  glossOrb(156, 254, 13, flash && mediaFlashKind == 1 ? ACC : c565(120, 140, 160));
  gfx->fillTriangle(88, 248, 88, 260, 79, 254, COL_WHITE); gfx->fillRect(78, 248, 2, 13, COL_WHITE);
  gfx->fillTriangle(152, 248, 152, 260, 161, 254, COL_WHITE); gfx->fillRect(161, 248, 2, 13, COL_WHITE);
  if (npPlaying) { gfx->fillRect(113, 246, 5, 16, COL_WHITE); gfx->fillRect(122, 246, 5, 16, COL_WHITE); }
  else gfx->fillTriangle(115, 245, 115, 263, 129, 254, COL_WHITE);
}

void drawCardAero() {
  aeroBackdrop();
  aeroTopBar(true);
  glassPanel(14, 38, 212, 120, 20, 0.6f, 0.34f);
  for (int y = 40; y < 156; y++) gfx->drawFastHLine(16, y, 5, mix565(cardAccent, COL_WHITE, (y - 40) * 2));
  String name = asciiOnly(cardName), sub = asciiOnly(cardSub);
  const AAFont &nf = aaFit(name.c_str(), 186);
  aaText(nf, name.c_str(), 30, 82, AERO_INK);
  aaText(F_M, aaClip(F_M, sub, 186).c_str(), 30, 108, mix565(cardAccent, COL_BLACK, 70));
  uint32_t hsh = 2166136261u;
  for (size_t i = 0; i < name.length(); i++) hsh = (hsh ^ (uint8_t)name[i]) * 16777619u;
  for (int x = 30, i = 0; x < 210; i++) {
    hsh = hsh * 1664525u + 1013904223u;
    int w = 1 + ((hsh >> 28) & 3);
    if (!(i & 1)) rectC(x, 122, min(w, 210 - x), 22, AERO_INK);
    x += w;
  }
  float p = (frameNo % 40) / 40.0f;
  for (int i = 0; i < 3; i++) {
    float rr = 16 + (i + p) * 14;
    int a = (int)(200 * (1 - (i + p) / 3));
    for (int k = 0; k < 120; k++) { float an = k * 6.2831853f / 120; blendPx(120 + (int)(cosf(an) * rr), 206 + (int)(sinf(an) * rr), COL_WHITE, a); }
  }
  glossOrb(120, 206, 14, cardAccent);
  aaTextC(F_S, "Tap your phone here", 120, 266, COL_WHITE, true);
}

float eyeBounce = 0;
// ═════════════ v5: buzzer jukebox ═════════════
// Built-in songs are ids 0..SONG_COUNT-1. Songs you add from the app are stored on the charm as ids 100..107.
#define MY_SONGS   8
#define MY_SONG_MAX 1500
#define MY_CAT     CAT_COUNT
char     mySongName[MY_SONGS][25];
uint16_t mySongN[MY_SONGS];
uint8_t  mySongBpm[MY_SONGS];
bool     mySongHas[MY_SONGS];
uint8_t *jbNotesBuf = nullptr;                 // the custom song being played, loaded from flash
const uint8_t *jbNotes = nullptr;
uint16_t jbCount = 0;
uint8_t  jbBpm = 120, jbCat = 0;
int16_t  jbSelId = 0;

#pragma pack(push, 1)
struct SongHdr { char magic[4]; uint8_t ver; uint8_t bpm; uint16_t n; char name[24]; };
#pragma pack(pop)

String mySongPath(int slot) { return String("/s/") + slot + ".sng"; }

void scanMySongs() {
  for (int i = 0; i < MY_SONGS; i++) {
    mySongHas[i] = false;
    if (!fsOK) continue;
    File f = FFat.open(mySongPath(i), "r");
    SongHdr h;
    if (f && f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && memcmp(h.magic, "LSNG", 4) == 0 && h.n > 0 && h.n <= MY_SONG_MAX) {
      mySongHas[i] = true; mySongN[i] = h.n; mySongBpm[i] = max((uint8_t)30, h.bpm);
      memcpy(mySongName[i], h.name, 24); mySongName[i][24] = 0;
    }
    if (f) f.close();
  }
}
int mySongCount() { int n = 0; for (int i = 0; i < MY_SONGS; i++) n += mySongHas[i]; return n; }

bool songExists(int id) { return (id >= 0 && id < SONG_COUNT) || (id >= 100 && id < 100 + MY_SONGS && mySongHas[id - 100]); }
const char *songName(int id) {
  if (id >= 0 && id < SONG_COUNT) return SONGS[id].name;
  if (id >= 100 && id < 100 + MY_SONGS && mySongHas[id - 100]) return mySongName[id - 100];
  return "?";
}
int songCat(int id) { return id >= 100 ? MY_CAT : SONGS[id].cat; }
int catCount() { return mySongCount() ? CAT_COUNT + 1 : CAT_COUNT; }
const char *catName(int c) { return c >= CAT_COUNT ? "My songs" : SONG_CATS[c]; }
int songsInCat(int c, int16_t *out, int maxN) {
  int n = 0;
  if (c >= CAT_COUNT) { for (int i = 0; i < MY_SONGS && n < maxN; i++) if (mySongHas[i]) out[n++] = 100 + i; }
  else for (int i = 0; i < SONG_COUNT && n < maxN; i++) if (SONGS[i].cat == c) out[n++] = i;
  return n;
}
// every song in category order, for flicking through them one by one
int allSongs(int16_t *out, int maxN) {
  int n = 0;
  for (int c = 0; c < catCount(); c++) n += songsInCat(c, out + n, maxN - n);
  return n;
}
int16_t stepSong(int16_t id, int dir) {
  int16_t all[48];
  int n = allSongs(all, 48), k = 0;
  for (int i = 0; i < n; i++) if (all[i] == id) { k = i; break; }
  return all[(k + n + dir) % n];
}

uint8_t  jbQueue[16];
uint8_t  jbQLen = 0, jbLastMidi = 0;
int16_t  jbSong = -1;
uint16_t jbNote = 0;
uint32_t jbNoteEnd = 0, jbNoteOff = 0;
bool     jbPlaying = false, jbToneOn = false;
uint8_t  jbRoll[20];
uint32_t jbSelUntil = 0;
bool     jbDirty = false;

static inline unsigned midiHz(uint8_t m) { return (unsigned)(440.0f * powf(2.0f, (m - 69) / 12.0f) + 0.5f); }

bool jbLoad(int id) {
  if (id >= 0 && id < SONG_COUNT) { jbNotes = SONGS[id].notes; jbCount = SONGS[id].n; jbBpm = SONGS[id].bpm; return true; }
  int slot = id - 100;
  if (slot < 0 || slot >= MY_SONGS || !mySongHas[slot] || !fsOK || !jbNotesBuf) return false;
  File f = FFat.open(mySongPath(slot), "r");
  SongHdr h;
  bool ok = f && f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.n <= MY_SONG_MAX && f.read(jbNotesBuf, h.n * 2) == h.n * 2;
  if (f) f.close();
  if (!ok) return false;
  jbNotes = jbNotesBuf; jbCount = h.n; jbBpm = max((uint8_t)30, h.bpm);
  return true;
}
void jbStartSong(int s) {
  jbDirty = true;
  if (!songExists(s) || !jbLoad(s)) { jbPlaying = false; jbSong = -1; return; }
  jbSong = s; jbNote = 0; jbPlaying = true;
  jbNoteEnd = millis() + 250;
  memset(jbRoll, 0, sizeof(jbRoll));
}
void jbStop() {
  jbDirty = true;
  jbPlaying = false; jbSong = -1; jbQLen = 0;
  if (jbToneOn) { noTone(BUZZER); jbToneOn = false; }
}
void jbEnqueue(int s) {
  if (!songExists(s)) return;
  jbDirty = true;
  if (!jbPlaying) jbStartSong(s);
  else if (jbQLen < sizeof(jbQueue)) jbQueue[jbQLen++] = s;
}
void jbSkip() {
  if (jbToneOn) { noTone(BUZZER); jbToneOn = false; }
  if (!jbQLen) { jbStop(); return; }
  int s = jbQueue[0];
  memmove(jbQueue, jbQueue + 1, --jbQLen);
  jbStartSong(s);
}
void jbService() {
  if (!jbPlaying) return;
  uint32_t now = millis();
  if (jbToneOn && (int32_t)(now - jbNoteOff) >= 0) { noTone(BUZZER); jbToneOn = false; }
  if ((int32_t)(now - jbNoteEnd) < 0) return;
  if (jbNote >= jbCount) { jbSkip(); return; }
  uint8_t m = jbNotes[jbNote * 2], d = jbNotes[jbNote * 2 + 1];
  jbNote++;
  uint32_t dur = (uint32_t)max((uint8_t)1, d) * 15000UL / jbBpm;
  jbNoteEnd = (now - jbNoteEnd > 60) ? now + dur : jbNoteEnd + dur;
  memmove(jbRoll, jbRoll + 1, sizeof(jbRoll) - 1);
  jbRoll[sizeof(jbRoll) - 1] = m;
  if (m) { tone(BUZZER, midiHz(m)); jbToneOn = true; jbNoteOff = now + dur * 85 / 100; jbLastMidi = m; eyeBounce = 6; }
}
float jbProgress() { return (jbSong < 0 || !jbCount) ? 0 : (float)jbNote / jbCount; }

// ═════════════ v5: the eyes ═════════════
// Two glowing blue eyes. They look around, blink, follow tilt, and change expression.
enum Expr : uint8_t { E_NEUTRAL, E_HAPPY, E_LAUGH, E_SAD, E_ANGRY, E_SURPRISED, E_SLEEPY, E_BORED, E_SUSPICIOUS, E_WINK, E_LOVE, E_DIZZY, EXPR_COUNT };
const char *EXPR_NAMES[EXPR_COUNT] = {"neutral", "happy", "laugh", "sad", "angry", "surprised", "sleepy", "bored", "suspicious", "wink", "love", "dizzy"};
EyeShape eyeL = {62, 74, 18, 0, 0, 0}, eyeR = {62, 74, 18, 0, 0, 0}, eyeLT = eyeL, eyeRT = eyeR;
float    lookX = 0, lookY = 0, lookTX = 0, lookTY = 0, tiltBaseX = 0, tiltBaseY = 0;
uint8_t  exprSet = E_NEUTRAL, exprShown = 255, idleExpr = E_NEUTRAL;
bool     exprAuto = true;
uint32_t exprUntil = 0, nextSaccade = 0, nextBlink = 0, blinkAt = 0, nextIdleExpr = 0, idleExprUntil = 0;

void exprTargets(uint8_t e) {
  const EyeShape n = {62, 74, 18, 0, 0, 0};
  eyeLT = eyeRT = n;
  switch (e) {
    case E_HAPPY:      eyeLT.lidB = eyeRT.lidB = 0.5f; break;
    case E_LAUGH:      eyeLT.lidB = eyeRT.lidB = 0.62f; eyeLT.h = eyeRT.h = 66; break;
    case E_SAD:        eyeLT.lidT = eyeRT.lidT = 0.3f; eyeLT.slope = eyeRT.slope = 0.42f; eyeLT.h = eyeRT.h = 66; break;
    case E_ANGRY:      eyeLT.lidT = eyeRT.lidT = 0.36f; eyeLT.slope = eyeRT.slope = -0.55f; eyeLT.h = eyeRT.h = 64; break;
    case E_SURPRISED:  eyeLT.w = eyeRT.w = 70; eyeLT.h = eyeRT.h = 86; eyeLT.rad = eyeRT.rad = 33; break;
    case E_SLEEPY:     eyeLT.lidT = eyeRT.lidT = 0.62f; eyeLT.slope = eyeRT.slope = 0.12f; break;
    case E_BORED:      eyeLT.lidT = eyeRT.lidT = 0.46f; eyeLT.lidB = eyeRT.lidB = 0.1f; break;
    case E_SUSPICIOUS: eyeLT.lidT = 0.52f; eyeRT.lidT = 0.16f; eyeRT.h = 70; break;
    case E_WINK:       eyeLT.lidB = 0.5f; eyeRT.h = 9; eyeRT.rad = 4; break;
    case E_DIZZY:      eyeLT.w = eyeRT.w = 50; eyeLT.h = eyeRT.h = 50; eyeLT.rad = eyeRT.rad = 25; break;
    default: break;
  }
}

void setExpr(uint8_t e, uint32_t ms) {
  exprSet = e % EXPR_COUNT;
  exprAuto = false;
  exprUntil = millis() + ms;
}

static inline void lerpShape(EyeShape &a, const EyeShape &b, float k) {
  a.w += (b.w - a.w) * k; a.h += (b.h - a.h) * k; a.rad += (b.rad - a.rad) * k;
  a.lidT += (b.lidT - a.lidT) * k; a.slope += (b.slope - a.slope) * k; a.lidB += (b.lidB - a.lidB) * k;
}

void eyesThink() {
  uint32_t now = millis();
  if (!exprAuto && now > exprUntil) exprAuto = true;
  uint8_t want = exprSet;
  if (exprAuto) {
    if (jbPlaying) want = (jbNote / 8) % 2 ? E_HAPPY : E_LAUGH;
    else if (charging) want = E_HAPPY;
    else if (batPct < 15) want = E_SLEEPY;
    else if (now - lastInteraction > 20000) want = E_SLEEPY;
    else {
      if (now > nextIdleExpr) {
        static const uint8_t IDLE[] = {E_NEUTRAL, E_HAPPY, E_SUSPICIOUS, E_BORED, E_SURPRISED, E_WINK, E_HAPPY, E_NEUTRAL};
        idleExpr = IDLE[random(0, sizeof(IDLE))];
        idleExprUntil = now + random(1400, 3200);
        nextIdleExpr = now + random(5000, 11000);
      }
      want = now < idleExprUntil ? idleExpr : E_NEUTRAL;
    }
  }
  if (want != exprShown) { exprShown = want; exprTargets(want); }

  if (now > nextSaccade) {
    if (random(0, 3) == 0) { lookTX = 0; lookTY = 0; }
    else { lookTX = random(-26, 27); lookTY = random(-16, 17); }
    nextSaccade = now + random(900, 3200);
  }
  if (imuOK) {
    tiltBaseX += (tiltX - tiltBaseX) * 0.004f; tiltBaseY += (tiltY - tiltBaseY) * 0.004f;
    float dx = tiltX - tiltBaseX, dy = tiltY - tiltBaseY;
    if (fabsf(dx) > 0.12f || fabsf(dy) > 0.12f) {
      lookTX = constrain(dx * 75, -30.0f, 30.0f); lookTY = constrain(dy * 55, -20.0f, 20.0f);
      nextSaccade = now + 900;
    }
  }
  if (exprShown == E_SLEEPY) { lookTY = 10; }
  lookX += (lookTX - lookX) * 0.32f;
  lookY += (lookTY - lookY) * 0.32f;
  lerpShape(eyeL, eyeLT, 0.28f);
  lerpShape(eyeR, eyeRT, 0.28f);
  eyeBounce *= 0.8f;
  if (now > nextBlink) {
    blinkAt = now;
    nextBlink = now + (random(0, 5) == 0 ? 280 : random(2200, 6000));
    if (exprShown == E_SLEEPY) nextBlink += 2500;
  }
}

// One eye: a rounded block with lids, a soft blue glow and a highlight.
void drawEye(float cx, float cy, const EyeShape &e, bool left) {
  if (!FB) return;
  float hw = e.w * 0.5f, hh = fmaxf(1.5f, e.h * 0.5f), r = fminf(e.rad, fminf(hw, hh));
  float vis = (1 - e.lidT * 0.9f) * (1 - e.lidB * 0.6f) * fminf(1.0f, e.h / 60.0f);
  int x0 = max(0, (int)(cx - hw - 16)), x1 = min(SCR_W - 1, (int)(cx + hw + 16));
  int y0 = max(0, (int)(cy - hh - 16)), y1 = min(SCR_H - 1, (int)(cy + hh + 16));
  const uint16_t glowC = c565(0, 96, 255);
  for (int y = y0; y <= y1; y++) {
    float py = y + 0.5f - cy, t = clamp01((py + hh) / (2 * hh));
    uint16_t core = rgbf(170 - 150 * t, 236 - 110 * t, 255);
    uint16_t *row = FB + y * SCR_W;
    for (int x = x0; x <= x1; x++) {
      float px = x + 0.5f - cx;
      float sd = sdRound(px, py, hw, hh, r);
      if (sd > 16) continue;
      float a = 0.5f - sd;
      if (a > 0) {
        float xn = px / hw, u = left ? -xn : xn;
        if (e.lidT > 0.002f || fabsf(e.slope) > 0.002f) a = fminf(a, py - (-1 + 2 * e.lidT + e.slope * u) * hh + 0.5f);
        if (e.lidB > 0.002f) a = fminf(a, hh - e.lidB * 2 * hh * (1 - 0.85f * xn * xn) - py + 0.5f);
      }
      uint16_t c = row[x];
      if (sd > 0) c = blend565(c, glowC, (uint8_t)(clamp01(0.5f * vis / (1 + sd * sd * 0.05f)) * 255));
      if (a > 0) {
        uint16_t cc = core;
        float hx = (px + hw * 0.36f) / (hw * 0.3f), hy = (py + hh * 0.5f) / (hh * 0.2f), h2 = hx * hx + hy * hy;
        if (h2 < 1) cc = blend565(cc, COL_WHITE, (uint8_t)((1 - h2) * 150));
        c = blend565(c, cc, (uint8_t)(clamp01(a) * 255));
      }
      row[x] = c;
    }
  }
}

void drawHeart(float cx, float cy, float s, uint16_t col) {
  if (!FB) return;
  for (int y = (int)(cy - s * 1.3f); y <= (int)(cy + s * 1.3f); y++) {
    if ((unsigned)y >= SCR_H) continue;
    for (int x = (int)(cx - s * 1.3f); x <= (int)(cx + s * 1.3f); x++) {
      if ((unsigned)x >= SCR_W) continue;
      int in = 0;
      for (int k = 0; k < 4; k++) {
        float X = (x + 0.25f + 0.5f * (k & 1) - cx) / s, Y = -(y + 0.25f + 0.5f * (k >> 1) - cy) / s + 0.3f;
        float q = X * X + Y * Y - 1;
        if (q * q * q - X * X * Y * Y * Y <= 0) in++;
      }
      if (in) FB[y * SCR_W + x] = blend565(FB[y * SCR_W + x], col, in * 63);
    }
  }
}

void drawEyes() {
  eyesThink();
  if (skin != SKIN_DECK) verticalGradient(c565(0, 14, 40), COL_BLACK);
  else {
    gfx->fillScreen(COL_BLACK);
    for (int y = 10; y < SCR_H; y += 20) for (int x = 10; x < SCR_W; x += 20) gfx->drawPixel(x, y, c565(0, 26, 52));
  }
  uint32_t now = millis();
  float t = now / 1000.0f;
  uint32_t el = now - blinkAt;
  float bf = el < 90 ? 1 - el / 90.0f * 0.92f : el < 180 ? 0.08f + (el - 90) / 90.0f * 0.92f : 1;
  float cy = 140 + lookY - eyeBounce;
  if (exprShown == E_LAUGH) cy += sinf(t * 22) * 3;
  if (npPlaying && !jbPlaying) cy += sinf(t * 7.5f) * 3;
  if (exprShown == E_LOVE) {
    float s = 30 * (1 + 0.1f * sinf(t * 9));
    drawHeart(70 + lookX, cy, s, c565(255, 80, 160));
    drawHeart(170 + lookX, cy, s, c565(255, 80, 160));
  } else {
    EyeShape L = eyeL, R = eyeR;
    L.h *= bf; if (R.h > 12) R.h *= bf;
    L.w *= 1 - lookX * 0.004f; L.h *= 1 - lookX * 0.003f;
    R.w *= 1 + lookX * 0.004f; R.h *= 1 + lookX * 0.003f;
    float dxl = 0, dyl = 0, dxr = 0, dyr = 0;
    if (exprShown == E_DIZZY) { dxl = cosf(t * 8) * 12; dyl = sinf(t * 8) * 12; dxr = cosf(-t * 8 + 1) * 12; dyr = sinf(-t * 8 + 1) * 12; }
    drawEye(70 + lookX + dxl, cy + dyl, L, true);
    drawEye(170 + lookX + dxr, cy + dyr, R, false);
    if (exprShown == E_DIZZY)
      for (int i = 0; i < 3; i++) { float a = t * 4 + i * 2.094f; sparkle(120 + (int)(cosf(a) * 70), 70 + (int)(sinf(a) * 10), 4, COL_AMBER); }
  }
  if (exprShown == E_SLEEPY) {
    for (int i = 0; i < 3; i++) {
      float p = fmodf(t * 0.5f + i / 3.0f, 1.0f);
      int zx = 190 + (int)(p * 26), zy = 92 - (int)(p * 50);
      if (skin != SKIN_DECK) aaText(F_M, "z", zx, zy, c565(120, 200, 255), (uint8_t)(255 * (1 - p)));
      else textAt("z", zx, zy, 1 + i % 2, mix565(c565(0, 140, 255), COL_BLACK, (uint8_t)(p * 255)));
    }
  }
  if (millis() < chromeUntil) topBar("//EYES", true);
}


void noteName(uint8_t m, char *out) {
  static const char *N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  if (!m) { strcpy(out, "--"); return; }
  sprintf(out, "%s%d", N[m % 12], m / 12 - 1);
}

#define JB_ROW_Y 50
#define JB_ROW_H 17
#define JB_ROWS  8
// Deck jukebox: a category bar, the songs in it, and a player strip. Tap a song to queue it.
void drawJukeDeck() {
  gfx->fillScreen(COL_VOID);
  topBar("//JUKEBOX", true);
  int16_t ids[JB_ROWS];
  if (jbCat >= catCount()) jbCat = 0;
  int n = songsInCat(jbCat, ids, JB_ROWS);
  hudPanel(10, 27, 220, 19, ACC, 1);
  String cn = catName(jbCat); cn.toUpperCase();
  textCentered(cn, 33, 1, COL_WHITE);
  textAt("<", 18, 33, 1, ACC); textAt(">", 216, 33, 1, ACC);
  for (int i = 0; i < n; i++) {
    int id = ids[i], y = JB_ROW_Y + i * JB_ROW_H;
    bool cur = jbPlaying && jbSong == id;
    int qpos = -1;
    for (int k = 0; k < jbQLen; k++) if (jbQueue[k] == id) { qpos = k; break; }
    if (cur) gfx->fillRect(14, y, 212, JB_ROW_H - 2, mix565(ACC, COL_BLACK, 170));
    String nm = songName(id); nm.toUpperCase();
    char b[40];
    snprintf(b, sizeof(b), "%02d %s", i + 1, nm.substring(0, 28).c_str());
    textAt(b, 20, y + 4, 1, cur ? COL_WHITE : (qpos >= 0 ? ACC : mix565(COL_WHITE, COL_BLACK, 70)));
    if (cur) textAt(">", 212, y + 4, 1, ACC);
    else if (qpos >= 0) { snprintf(b, sizeof(b), "Q%d", qpos + 1); textAt(b, 208, y + 4, 1, ACC); }
  }
  const int py = 192;
  hudPanel(10, py, 220, 58, ACC, 1);
  char nn[6];
  noteName(jbPlaying ? jbLastMidi : 0, nn);
  textAt(nn, 20, py + 10, 3, jbToneOn ? COL_WHITE : ACC_DIM);
  for (int i = 0; i < 20; i++) {
    uint8_t m = jbRoll[i];
    if (!m) continue;
    int h = constrain((m - 60) * 2, 2, 30);
    gfx->fillRect(100 + i * 6, py + 36 - h, 4, h, i == 19 && jbToneOn ? COL_WHITE : ACC);
  }
  gfx->fillRect(20, py + 42, 200, 2, ACC_LOW);
  gfx->fillRect(20, py + 42, (int)(200 * jbProgress()), 2, COL_WHITE);
  if (jbPlaying) {
    textAt(String("+") + (int)jbQLen + " QUEUED", 20, py + 48, 1, COL_WHITE);
    textAt("STOP", 132, py + 48, 1, ACC); textAt("SKIP >", 184, py + 48, 1, ACC);
  } else textAt("TAP A SONG TO PLAY OR QUEUE", 20, py + 48, 1, COL_WHITE);
}

// spinning disc made of rainbow light
void drawDisc(int cx, int cy, int r, float ang) {
  if (!FB) return;
  for (int y = -r; y <= r; y++) {
    uint16_t *row = FB + (cy + y) * SCR_W;
    for (int x = -r; x <= r; x++) {
      float d = sqrtf((float)(x * x + y * y));
      if (d > r + 0.5f || d < 6) continue;
      float a = clamp01(r + 0.5f - d) * clamp01(d - 6);
      float an = atan2f((float)y, (float)x) + ang;
      float hue = sinf(an * 2) * 0.5f + 0.5f;
      float groove = (((int)d) % 3 == 0) ? 0.88f : 1.0f;
      float base = (200 + 40 * sinf(an * 2 + 1.2f)) * groove;
      float cr = base + (hue - 0.5f) * 60, cg = base + 40 * sinf(an * 2 + 2.4f) * 0.6f, cb = base + 50 * (1 - hue);
      if (d < 22) { cr = r8(ACC); cg = g8(ACC); cb = b8(ACC); }
      row[cx + x] = blend565(row[cx + x], rgbf(cr, cg, cb), (uint8_t)(a * 255));
    }
  }
  glossOrb(cx, cy, 20, ACC, 0.9f);
  gfx->fillCircle(cx, cy, 5, c565(30, 40, 60));
}

void drawJukeAero() {
  aeroBackdrop();
  aeroTopBar(true);
  static float spin = 0;
  if (jbPlaying) spin += 0.09f;
  drawDisc(120, 90, 54, spin);
  int show = (jbPlaying && millis() > jbSelUntil) ? jbSong : jbSelId;
  if (!songExists(show)) show = jbSelId = 0;
  glassPanel(16, 152, 208, 42, 18, 0.72f, 0.45f);
  aaTextC(F_S, catName(songCat(show)), 120, 168, mix565(ACC, COL_BLACK, 80));
  aaTextC(F_M, aaClip(F_M, songName(show), 168).c_str(), 120, 188, AERO_INK);
  aaTextS(F_M, "<", 22, 182, ACC); aaText(F_M, ">", 208, 182, ACC);
  if (jbPlaying) aeroProgress(30, 197, 180, 9, jbProgress(), ACC);
  glassPanel(20, 210, 200, 56, 18, 0.5f, 0.28f);
  if (jbQLen) {
    aaText(F_S, "Up next", 34, 227, mix565(ACC, COL_BLACK, 80));
    for (int k = 0; k < min(2, (int)jbQLen); k++) aaText(F_S, aaClip(F_S, songName(jbQueue[k]), 170).c_str(), 34, 244 + k * 15, AERO_INK);
  } else {
    aaTextC(F_S, jbPlaying ? "Tap here to queue the" : "Tap the disc to play,", 120, 233, AERO_INK);
    aaTextC(F_S, jbPlaying ? "song shown above" : "or here to queue it", 120, 251, AERO_INK);
  }
}

void drawJukeQuest();
void drawJuke() { if (PIXEL_SKIN) drawJukeQuest(); else if (skin == SKIN_AERO) drawJukeAero(); else drawJukeDeck(); }

// ═════════════ v5: charging screen ═════════════
uint8_t chargeReturn = M_DECK;

void drawChargeDeck() {
  gfx->fillScreen(COL_VOID);
  for (int y = 40; y < 250; y += 20) for (int x = 20; x < SCR_W; x += 20) gfx->drawPixel(x, y, ACC_LOW);
  topBar("//POWER", true);
  uint16_t col = charging ? COL_LIME : ACC;
  int p = batPct, nd = p >= 100 ? 3 : p >= 10 ? 2 : 1, dw = 34, dt = 7, gap = 8;
  int x = (SCR_W - (nd * dw + (nd - 1) * gap + 30)) / 2;
  char s[4]; snprintf(s, sizeof(s), "%d", p);
  for (int i = 0; i < nd; i++) segDigit(x + i * (dw + gap), 48, dw, 64, dt, s[i] - '0', col, mix565(col, COL_BLACK, 220));
  textAt("%", x + nd * (dw + gap), 88, 3, col);
  hudPanel(22, 132, 186, 52, col, 1);
  gfx->fillRect(208, 148, 8, 20, col);
  int lit = (p + 9) / 10;
  for (int i = 0; i < 10; i++) {
    bool on = i < lit - 1 || (i == lit - 1 && (!charging || (millis() / 400) % 2));
    gfx->fillRect(30 + i * 17, 140, 14, 36, on ? col : mix565(col, COL_BLACK, 215));
  }
  if (charging) {
    int py = (millis() / 30) % 40;
    for (int k = 0; k < 4; k++) gfx->fillRect(118, 196 + (py + k * 10) % 40, 4, 4, mix565(col, COL_BLACK, k * 50));
  }
  char b[32];
  snprintf(b, sizeof(b), "%s  %.2fV", charging ? (p >= 99 ? "FULL" : "CHARGING") : "ON BATTERY", batVolts);
  textCentered(b, 244, 1, COL_WHITE);
}

void drawChargeAero() {
  aeroBackdrop();
  aeroTopBar(true);
  char b[32];
  snprintf(b, sizeof(b), "%d%%", batPct);
  aaTextC(F_XL, b, 120, 100, COL_WHITE, true);
  const int bx = 26, by = 124, bw = 178, bh = 70;
  glassPanel(bx, by, bw, bh, 24, 0.34f, 0.14f);
  glassPanel(bx + bw + 2, by + 22, 12, 26, 6, 0.55f, 0.3f);
  float lvl = clamp01(batPct / 100.0f), t = millis() / 1000.0f;
  for (int y = by + 6; y < by + bh - 6; y++) {
    uint16_t *row = FB + y * SCR_W;
    float k = (y - by - 6) / (float)(bh - 12);
    float wave = charging ? sinf(y * 0.18f + t * 5) * 3 : 0;
    float edge = bx + 6 + (bw - 12) * lvl + wave;
    uint16_t c = rgbf(scr(60, 0.5f * (1 - k)), scr(200 - 50 * k, 0.4f * (1 - k)), scr(70, 0.4f * (1 - k)));
    if (batPct < 15 && !charging) c = rgbf(240, 80 - 30 * k, 80 - 30 * k);
    for (int x = bx + 6; x < bx + bw - 6; x++) {
      float a = clamp01(edge - x + 0.5f);
      float sd = sdRound(x + 0.5f - (bx + bw / 2.0f), y + 0.5f - (by + bh / 2.0f), bw / 2.0f - 6, bh / 2.0f - 6, 18);
      a = fminf(a, clamp01(0.5f - sd));
      if (a > 0) row[x] = blend565(row[x], c, (uint8_t)(a * 230));
    }
  }
  if (charging)
    for (int i = 0; i < 5; i++) {
      float p = fmodf(t * 0.6f + i * 0.21f, 1.0f);
      float x = bx + 20 + fmodf(i * 37.0f, (bw - 40) * lvl + 1);
      drawBubble(x, by + bh - 10 - p * (bh - 20), 2 + (i % 3));
    }
  glassPanel(bx, by, bw, bh / 2, 22, 0.18f, 0.0f);
  snprintf(b, sizeof(b), "%s", charging ? (batPct >= 99 ? "Fully charged" : "Charging") : "On battery");
  aaTextC(F_L, b, 120, 232, COL_WHITE, true);
  snprintf(b, sizeof(b), "%.2f volts", batVolts);
  aaTextC(F_S, b, 120, 254, AERO_INK);
}

void drawChargeQuest();
void drawCharge() { if (PIXEL_SKIN) drawChargeQuest(); else if (skin == SKIN_AERO) drawChargeAero(); else drawChargeDeck(); }

// ═════════════ v5: torch ═════════════
const uint16_t TORCH_COL[4] = { COL_WHITE, c565(255, 206, 150), c565(255, 0, 0), c565(120, 230, 255) };
const char *TORCH_NAMES[4] = {"white", "warm", "red", "ice"};
uint8_t torchCol = 0;
void drawTorch() {
  gfx->fillScreen(TORCH_COL[torchCol & 3]);
  if (millis() < chromeUntil) {
    if (skin == SKIN_AERO) aaTextC(F_S, "Tap for another colour", 120, 256, c565(90, 90, 90));
    else textCentered("TAP: COLOUR  SWIPE DOWN: OFF", 252, 1, c565(90, 90, 90));
  }
}

// ═════════════ v5: step counter ═════════════
void stepFeed(float x, float y, float z, uint32_t now) {
  float m = sqrtf(x * x + y * y + z * z);
  stepLP = stepLP * 0.7f + m * 0.3f;
  if (!stepArmed && stepLP < 1.03f) stepArmed = true;
  if (stepArmed && stepLP > 1.17f && now - lastStepAt > 260) {
    stepArmed = false;
    if (now - lastStepAt > 2000) stepRun = 0;
    lastStepAt = now;
    if (stepRun < 255) stepRun++;
    if (stepRun == 5) stepsToday += 5;                  // ignore single bumps: count once a rhythm starts
    else if (stepRun > 5) stepsToday++;
  }
}
void stepDayCheck() {
  struct tm t;
  if (!localNow(t)) return;
  if (stepDay != t.tm_yday) {
    if (stepDay >= 0) stepsToday = 0;
    stepDay = t.tm_yday;
    prefs.putShort("sday", stepDay);
    prefs.putULong("steps", stepsToday);
  }
  if (stepsToday - stepsSaved >= 200) { stepsSaved = stepsToday; prefs.putULong("steps", stepsToday); petMood = min(100, petMood + 1); }
}


// ═════════════ v6: the Quest look, a little pixel-art JRPG village ═════════════
// Everything here is drawn at half resolution: one art pixel is a 2x2 block on the screen.
// The village loops: walk left or right to visit a landmark, tap it to go in.
// Light follows the real time of day (dawn, day, golden hour, dusk, night).
void openModule(int m);

const uint8_t QLOC_MODE[QW_NLOC] = {M_CLOCK, M_MUSIC, M_JUKE, M_EYES, M_PET, M_TOY, M_TEXT, M_PHOTO, M_CLIP, M_CARD, M_TORCH};
const char *const QLOC_SUB[QW_NLOC] = {"Time and date", "Now playing", "Buzzer songs", "Robot eyes", "Your pet", "Oracle, dice, coin",
                                       "Your message", "Your photo", "Your clips", "Status and NFC", "A bright light"};
const int8_t QHERO_SX[QW_NLOC] = {36, 36, 64, 74, 42, 36, 46, 34, 32, 34, 36};   // where the hero waits, on screen
const int8_t QDOOR[QW_NLOC][2] = {{64, 92}, {65, 93}, {78, 94}, {64, 80}, {72, 93}, {64, 88}, {41, 74}, {59, 84}, {61, 76}, {63, 86}, {68, 97}};  // the enter arrow

uint8_t  *qWorld = nullptr, *qFarI = nullptr, *qHillI = nullptr, *qFgI = nullptr;
uint16_t *qFar = nullptr, *qHill = nullptr, *qFg = nullptr;
uint8_t  *qHillA = nullptr, *qFgA = nullptr, *qTmp = nullptr;
#define Q_TMP_BYTES (240 * RF_H * 2 * 4)            // big enough for either look's largest layer
uint16_t  qLut[256], qLutS[256];
uint8_t   qRgb[256][3];
uint16_t  qSky[140];
uint8_t   qHor[3];
float     qNight = 0, qDayK = 1, qHourNow = 12, qHourForce = -1;
int       qBakeKey = -1;
bool      qReady = false;

uint16_t qIconLut[256]; bool qIconOk = false;
const uint16_t *qIcons() {
  if (!qIconOk) { for (int i = 0; i < QPAL_N; i++) qIconLut[i] = c565(QPAL[i][0], QPAL[i][1], QPAL[i][2]); qIconOk = true; }
  return qIconLut;
}
int qLocOfMode(uint8_t m) { for (int i = 0; i < QW_NLOC; i++) if (QLOC_MODE[i] == m) return i; return -1; }

void qRle(const uint8_t *src, uint8_t *dst, int n) {
  int o = 0, i = 0;
  while (o < n) {
    uint8_t b = src[i++];
    if (b < 0x80) { uint8_t v = src[i++]; int c = min(b + 1, n - o); memset(dst + o, v, c); o += c; }
    else { int c = b - 0x7F; while (c-- && o < n) dst[o++] = src[i++]; }
  }
}

bool questInit() {
  if (qReady) return true;
  if (!qWorld) qWorld = (uint8_t *)ps_malloc(QW_W * QW_H);
  if (!qFarI)  qFarI  = (uint8_t *)ps_malloc(QF_W * QF_H);
  if (!qHillI) qHillI = (uint8_t *)ps_malloc(QH_W * QH_H);
  if (!qFgI)   qFgI   = (uint8_t *)ps_malloc(QG_W * QG_H);
  if (!qFar)   qFar   = (uint16_t *)ps_malloc(QF_W * 2 * QF_H * 2 * 2);
  if (!qHill)  qHill  = (uint16_t *)ps_malloc(QH_W * 2 * QH_H * 2 * 2);
  if (!qHillA) qHillA = (uint8_t *)ps_malloc(QH_W * 2 * QH_H * 2);
  if (!qFg)    qFg    = (uint16_t *)ps_malloc(QG_W * 2 * QG_H * 2 * 2);
  if (!qFgA)   qFgA   = (uint8_t *)ps_malloc(QG_W * 2 * QG_H * 2);
  if (!qTmp)   qTmp   = (uint8_t *)ps_malloc(Q_TMP_BYTES);
  if (!qWorld || !qFarI || !qHillI || !qFgI || !qFar || !qHill || !qHillA || !qFg || !qFgA || !qTmp) return false;
  qRle(QW_RLE, qWorld, QW_W * QW_H);
  qRle(QF_RLE, qFarI, QF_W * QF_H);
  qRle(QH_RLE, qHillI, QH_W * QH_H);
  qRle(QG_RLE, qFgI, QG_W * QG_H);
  qReady = true;
  qBakeKey = -1;
  return true;
}

// ───── light ─────
struct QKey { float h; uint8_t top[3], hor[3]; float l[3], night; };
const QKey QKEYS[] = {
  {0.0f,  {8, 12, 40},   {36, 46, 100},  {0.32f, 0.38f, 0.64f}, 1.0f},
  {4.6f,  {8, 12, 40},   {36, 46, 100},  {0.32f, 0.38f, 0.64f}, 1.0f},
  {5.7f,  {44, 42, 112}, {246, 150, 130}, {0.74f, 0.60f, 0.72f}, 0.6f},
  {7.0f,  {76, 146, 234}, {255, 214, 176}, {1.0f, 0.93f, 0.86f}, 0.05f},
  {9.0f,  {64, 136, 234}, {178, 222, 255}, {1.0f, 1.0f, 1.0f}, 0.0f},
  {16.0f, {64, 136, 234}, {178, 222, 255}, {1.0f, 1.0f, 1.0f}, 0.0f},
  {17.4f, {82, 112, 204}, {255, 200, 128}, {1.05f, 0.9f, 0.74f}, 0.1f},
  {18.7f, {46, 40, 112},  {248, 128, 100}, {0.78f, 0.58f, 0.66f}, 0.55f},
  {20.0f, {14, 18, 56},   {52, 58, 122},  {0.40f, 0.44f, 0.70f}, 0.95f},
  {24.0f, {8, 12, 40},    {36, 46, 100},  {0.32f, 0.38f, 0.64f}, 1.0f},
};
const char *qPeriod() {
  float h = qHourNow;
  return h < 5 ? "Night" : h < 7.5f ? "Dawn" : h < 12 ? "Morning" : h < 17 ? "Afternoon" : h < 19.5f ? "Evening" : "Night";
}
static inline float qLerp(float a, float b, float t) { return a + (b - a) * t; }

void qBake();
void qLightUpdate(bool force) {
  static uint32_t lastChk = 0;
  if (!force && millis() - lastChk < 4000 && qBakeKey >= 0) return;
  lastChk = millis();
  float h = 12;
  struct tm t;
  if (qHourForce >= 0) h = qHourForce;
  else if (localNow(t)) h = t.tm_hour + t.tm_min / 60.0f;
  qHourNow = h;
  int key = (int)(h * 6);                              // a new look every 10 minutes
  if (!force && key == qBakeKey) return;
  qBakeKey = key;
  int k = 0;
  while (k < 8 && QKEYS[k + 1].h <= h) k++;
  const QKey &a = QKEYS[k], &b = QKEYS[k + 1];
  float f = (h - a.h) / max(0.01f, b.h - a.h);
  float top[3], hor[3], l[3];
  for (int i = 0; i < 3; i++) { top[i] = qLerp(a.top[i], b.top[i], f); hor[i] = qLerp(a.hor[i], b.hor[i], f); l[i] = qLerp(a.l[i], b.l[i], f); qHor[i] = (uint8_t)hor[i]; }
  qNight = qLerp(a.night, b.night, f);
  qDayK = clamp01(1 - qNight * 1.6f);
  for (int i = 0; i < QPAL_N; i++) {
    float r = QPAL[i][0], g = QPAL[i][1], bb = QPAL[i][2]; uint8_t kind = QPAL[i][3];
    float o[3];
    if (kind == 2) { o[0] = r; o[1] = g; o[2] = bb; }
    else {
      o[0] = r * l[0]; o[1] = g * l[1]; o[2] = bb * l[2];
      if (kind == 1) { o[0] = qLerp(o[0], 255, qNight); o[1] = qLerp(o[1], 196, qNight); o[2] = qLerp(o[2], 104, qNight); }
      if (kind == 3) for (int c = 0; c < 3; c++) o[c] = qLerp(o[c], hor[c], 0.22f);
    }
    for (int c = 0; c < 3; c++) qRgb[i][c] = (uint8_t)constrain((int)o[c], 0, 255);
    qLut[i] = c565(qRgb[i][0], qRgb[i][1], qRgb[i][2]);
    float s = 0.42f;                                    // characters keep a little more light so you can see them
    qLutS[i] = kind == 2 ? qLut[i] : c565((uint8_t)constrain((int)(r * qLerp(l[0], 1, s)), 0, 255), (uint8_t)constrain((int)(g * qLerp(l[1], 1, s)), 0, 255), (uint8_t)constrain((int)(bb * qLerp(l[2], 1, s)), 0, 255));
  }
  for (int y = 0; y < 140; y++) {
    float tt = clamp01(y / 100.0f); tt = tt * tt * (3 - 2 * tt);
    qSky[y] = c565((uint8_t)qLerp(top[0], hor[0], tt), (uint8_t)qLerp(top[1], hor[1], tt), (uint8_t)qLerp(top[2], hor[2], tt));
  }
  if (qReady) qBake();
}

// box blur of an RGBA tile (w x h, premultiplied), wraps sideways
static void qBlur(uint8_t *px, int w, int h, int r, uint8_t *line) {
  int n = 2 * r + 1;
  for (int y = 0; y < h; y++) {
    uint8_t *row = px + y * w * 4;
    memcpy(line, row, w * 4);
    for (int c = 0; c < 4; c++) {
      int s = 0;
      for (int k = -r; k <= r; k++) s += line[((k + w) % w) * 4 + c];
      for (int x = 0; x < w; x++) {
        row[x * 4 + c] = s / n;
        s += line[((x + r + 1) % w) * 4 + c] - line[((x - r + w) % w) * 4 + c];
      }
    }
  }
  for (int x = 0; x < w; x++) {
    for (int y = 0; y < h; y++) memcpy(line + y * 4, px + (y * w + x) * 4, 4);
    for (int c = 0; c < 4; c++) {
      int s = 0;
      for (int k = -r; k <= r; k++) s += line[constrain(k, 0, h - 1) * 4 + c];
      for (int y = 0; y < h; y++) {
        px[(y * w + x) * 4 + c] = s / n;
        s += line[min(y + r + 1, h - 1) * 4 + c] - line[max(y - r, 0) * 4 + c];
      }
    }
  }
}

// Renders an indexed layer at full size into RGBA, blurs it, and keeps it as RGB565 (+ alpha).
static void qBakeLayer(const uint8_t *idx, int lw, int lh, int ly0, bool skyFill, int radius, uint16_t *out, uint8_t *alpha, const uint8_t (*rgb)[3] = qRgb) {
  int w = lw * 2, h = lh * 2;
  static uint8_t line[1024 * 4];
  for (int y = 0; y < h; y++) {
    uint16_t sky = qSky[min(139, ly0 + y / 2)];
    uint8_t sr = r8(sky), sg = g8(sky), sb = b8(sky);
    for (int x = 0; x < w; x++) {
      uint8_t i = idx[(y / 2) * lw + x / 2];
      uint8_t *p = qTmp + (y * w + x) * 4;
      if (i) { p[0] = rgb[i][0]; p[1] = rgb[i][1]; p[2] = rgb[i][2]; p[3] = 255; }
      else if (skyFill) { p[0] = sr; p[1] = sg; p[2] = sb; p[3] = 255; }
      else { p[0] = p[1] = p[2] = p[3] = 0; }
    }
  }
  if (radius) qBlur(qTmp, w, h, radius, line);
  for (int i = 0; i < w * h; i++) {
    uint8_t *p = qTmp + i * 4;
    if (alpha) {
      alpha[i] = p[3];
      if (p[3]) out[i] = c565(min(255, p[0] * 255 / p[3]), min(255, p[1] * 255 / p[3]), min(255, p[2] * 255 / p[3]));
    } else out[i] = c565(p[0], p[1], p[2]);
  }
}

void qBake() {
  qBakeLayer(qFarI, QF_W, QF_H, QF_Y0, true, 3, qFar, nullptr);
  qBakeLayer(qHillI, QH_W, QH_H, QH_Y0, false, 1, qHill, qHillA);
  qBakeLayer(qFgI, QG_W, QG_H, QG_Y0, false, 3, qFg, qFgA);
}

// ───── pixel helpers ─────
static inline uint16_t qAdd(uint16_t c, uint16_t g, uint8_t a) {
  int r = ((c >> 11) & 31) + ((((g >> 11) & 31) * a) >> 8), gg = ((c >> 5) & 63) + ((((g >> 5) & 63) * a) >> 8), b = (c & 31) + (((g & 31) * a) >> 8);
  return (min(r, 31) << 11) | (min(gg, 63) << 5) | min(b, 31);
}
static inline uint16_t qMul(uint16_t c, int k) {   // k: 0..256
  return (((((c >> 11) & 31) * k) >> 8) << 11) | (((((c >> 5) & 63) * k) >> 8) << 5) | (((c & 31) * k) >> 8);
}
static inline uint16_t qMul32(uint16_t c, uint32_t k) {   // k: 0..32, two multiplies
  uint32_t rb = c & 0xF81F, g = c & 0x07E0;
  return (((rb * k) >> 5) & 0xF81F) | (((g * k) >> 5) & 0x07E0);
}
static inline void qPx(int x, int y, uint16_t c) {           // one art pixel at real (x, y)
  if ((unsigned)x < SCR_W - 1 && (unsigned)y < SCR_H - 1) { uint16_t *p = FB + y * SCR_W + x; p[0] = c; p[1] = c; p[SCR_W] = c; p[SCR_W + 1] = c; }
  else { for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) if ((unsigned)(x + i) < SCR_W && (unsigned)(y + j) < SCR_H) FB[(y + j) * SCR_W + x + i] = c; }
}
static inline void qPxA(int x, int y, uint16_t c, uint8_t a) {
  for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) if ((unsigned)(x + i) < SCR_W && (unsigned)(y + j) < SCR_H) { uint16_t *p = FB + (y + j) * SCR_W + x + i; *p = blend565(*p, c, a); }
}
void qRect(int x, int y, int w, int h, uint16_t c) { gfx->fillRect(x, y, w, h, c); }
void qRectA(int x, int y, int w, int h, uint16_t c, uint8_t a) {
  int x0 = max(0, x), y0 = max(0, y), x1 = min((int)SCR_W, x + w), y1 = min((int)SCR_H, y + h);
  for (int yy = y0; yy < y1; yy++) { uint16_t *p = FB + yy * SCR_W; for (int xx = x0; xx < x1; xx++) p[xx] = blend565(p[xx], c, a); }
}

// an indexed sprite, scale = real px per art px
void qSprite(const uint8_t *spr, int w, int h, int x, int y, bool flip, const uint16_t *lut, int sc = 2, uint8_t alpha = 255) {
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    uint8_t v = spr[j * w + (flip ? w - 1 - i : i)];
    if (!v) continue;
    if (sc == 2 && alpha == 255) qPx(x + i * 2, y + j * 2, lut[v]);
    else if (alpha == 255) qRect(x + i * sc, y + j * sc, sc, sc, lut[v]);
    else qRectA(x + i * sc, y + j * sc, sc, sc, lut[v], alpha);
  }
}

// soft additive light
void qGlow(int cx, int cy, int r, uint16_t col, int strength) {
  if (strength <= 0) return;
  int r2 = r * r, inv = (1 << 24) / r2;
  int y0 = max(0, cy - r), y1 = min((int)SCR_H - 1, cy + r), x0 = max(0, cx - r), x1 = min((int)SCR_W - 1, cx + r);
  for (int y = y0; y <= y1; y++) {
    uint16_t *row = FB + y * SCR_W; int dy = y - cy;
    for (int x = x0; x <= x1; x++) {
      int dx = x - cx, d2 = dx * dx + dy * dy;
      if (d2 >= r2) continue;
      int t = 256 - ((d2 * inv) >> 16);
      int a = (t * t >> 8) * strength >> 8;
      if (a > 0) row[x] = qAdd(row[x], col, min(a, 255));
    }
  }
}

// ───── pixel text ─────
int qTextW(const QFont &f, const char *s, int sc = 2) {
  int w = 0;
  for (; *s; s++) { int c = (uint8_t)*s; if (c < 32 || c > 126 || (&f == &QF_BIG && c > 58)) c = ' '; w += f.g[c - 32].adv; }
  return w * sc;
}
void qText(const QFont &f, const char *s, int x, int y, uint16_t col, int sc = 2, int shadow = -1) {
  int base = y + f.asc * sc;
  for (; *s; s++) {
    int c = (uint8_t)*s; if (c < 32 || c > 126 || (&f == &QF_BIG && c > 58)) c = ' ';
    const QGlyph &g = f.g[c - 32];
    int bpr = (g.w + 7) / 8;
    for (int r = 0; r < g.h; r++) for (int i = 0; i < g.w; i++) {
      if (!(f.d[g.off + r * bpr + i / 8] & (0x80 >> (i & 7)))) continue;
      int px = x + (g.xo + i) * sc, py = base + (g.yo + r) * sc;
      if (shadow >= 0) qRect(px + sc / 2 + 1, py + sc / 2 + 1, sc, sc, (uint16_t)shadow);
      qRect(px, py, sc, sc, col);
    }
    x += g.adv * sc;
  }
}
void qTextC(const QFont &f, const char *s, int cx, int y, uint16_t col, int sc = 2, int shadow = -1) {
  qText(f, s, cx - qTextW(f, s, sc) / 2, y, col, sc, shadow);
}
String qClip(const QFont &f, String s, int maxW) {
  if (qTextW(f, s.c_str()) <= maxW) return s;
  while (s.length() > 1 && qTextW(f, (s + "..").c_str()) > maxW) s = s.substring(0, s.length() - 1);
  return s + "..";
}
#define Q_INK   qLutS[QC_INK]
#define Q_WHITE c565(250, 248, 240)
#define Q_GOLD  c565(250, 210, 90)
#define Q_DIM   (skin == SKIN_RONIN ? c565(206, 176, 160) : c565(170, 186, 230))
#define Q_SHADOW c565(16, 14, 40)

// The classic JRPG window: a blue gradient, a white frame, gold corners.
void qWindow(int x, int y, int w, int h, uint8_t alpha = 236) {
  x &= ~1; y &= ~1; w &= ~1; h &= ~1;
  bool rn = skin == SKIN_RONIN;
  const uint16_t top = rn ? c565(52, 18, 24) : c565(56, 80, 196), bot = rn ? c565(10, 6, 14) : c565(18, 26, 96);
  for (int j = 4; j < h - 4; j++) {
    uint16_t c = mix565(top, bot, (uint8_t)(j * 255 / h));
    int inset = (j < 6 || j >= h - 6) ? 6 : 4;
    qRectA(x + inset, y + j, w - 2 * inset, 1, c, alpha);
  }
  const uint16_t ink = c565(20, 16, 40), lite = rn ? c565(228, 184, 96) : c565(250, 250, 255), mid = rn ? c565(150, 100, 40) : c565(176, 186, 214);
  qRect(x + 4, y, w - 8, 2, ink); qRect(x + 4, y + h - 2, w - 8, 2, ink);
  qRect(x, y + 4, 2, h - 8, ink); qRect(x + w - 2, y + 4, 2, h - 8, ink);
  qRect(x + 2, y + 2, 2, 2, ink); qRect(x + w - 4, y + 2, 2, 2, ink); qRect(x + 2, y + h - 4, 2, 2, ink); qRect(x + w - 4, y + h - 4, 2, 2, ink);
  qRect(x + 4, y + 2, w - 8, 2, lite); qRect(x + 2, y + 4, 2, h - 8, lite);
  qRect(x + 4, y + h - 4, w - 8, 2, mid); qRect(x + w - 4, y + 4, 2, h - 8, mid);
  const uint16_t g1 = rn ? c565(240, 70, 56) : c565(250, 210, 90), g0 = rn ? c565(120, 20, 26) : c565(160, 100, 30);
  int cx[4] = {x + 2, x + w - 8, x + 2, x + w - 8}, cy[4] = {y + 2, y + 2, y + h - 8, y + h - 8};
  for (int k = 0; k < 4; k++) {
    qRect(cx[k] + 2, cy[k], 2, 6, g0); qRect(cx[k], cy[k] + 2, 6, 2, g0); qRect(cx[k] + 2, cy[k] + 2, 2, 2, g1);
  }
}
// a small gold title plate that sits on a window's top edge
void qPlate(const char *s, int cx, int y) {
  int w = qTextW(QF_BODY, s) + 16; int x = (cx - w / 2) & ~1;
  qRect(x + 2, y, w - 4, 18, c565(60, 34, 14)); qRect(x, y + 2, w, 14, c565(60, 34, 14));
  bool rn = skin == SKIN_RONIN;
  qRect(x + 2, y + 2, w - 4, 14, rn ? c565(166, 30, 32) : c565(214, 152, 44)); qRect(x + 2, y + 2, w - 4, 2, rn ? c565(222, 76, 60) : c565(255, 230, 140)); qRect(x + 2, y + 14, w - 4, 2, rn ? c565(96, 14, 22) : c565(150, 92, 26));
  qText(QF_BODY, s, x + 8, y + 3, rn ? c565(255, 226, 160) : c565(60, 26, 10));
}
void qBar(int x, int y, int w, float f, uint16_t c0, uint16_t c1) {
  qRect(x, y, w, 10, c565(20, 16, 40)); qRect(x + 2, y + 2, w - 4, 6, c565(44, 40, 78));
  int fw = (int)((w - 4) * clamp01(f)) & ~1;
  if (fw > 0) { qRect(x + 2, y + 2, fw, 2, c1); qRect(x + 2, y + 4, fw, 4, c0); }
}
void qArrow(int x, int y, int dir, uint16_t c) {          // a little pixel triangle, dir: 0 down, 1 right, -1 left
  for (int k = 0; k < 4; k++) {
    if (dir == 0) qRect(x - 6 + k * 2, y + k * 2, 12 - k * 4, 2, c);
    else if (dir == 2) qRect(x - 6 + k * 2, y - k * 2, 12 - k * 4, 2, c);
    else if (dir > 0) qRect(x + k * 2, y - 6 + k * 2, 2, 12 - k * 4, c);
    else qRect(x - k * 2, y - 6 + k * 2, 2, 12 - k * 4, c);
  }
}

// ───── the scene ─────
float qCam = 0, qHeroX = 36, qWalkFrom = 0, qCamFrom = 0;
int   qLoc = 0, qShown = 0;
int8_t qFace = 1;
uint32_t qWalkAt = 0, qWalkMs = 0, qArriveAt = 0, qJumpAt = 0, qEnterAt = 0;
uint8_t qMosaicIn = 0;
struct QMote { float x, y, vx, vy, ph; };
QMote qMote[16];
struct QCoin { float x, y, vx, vy; uint32_t t; };
QCoin qCoin[8];

static inline int qMod(int a, int m) { a %= m; return a < 0 ? a + m : a; }

void qBlitSky() {
  for (int y = 0; y < QF_Y0 * 2; y++) { uint16_t c = qSky[y / 2]; uint16_t *p = FB + y * SCR_W; for (int x = 0; x < SCR_W; x++) p[x] = c; }
}
void qStars() {
  if (qNight < 0.3f) return;
  uint32_t s = 1234567;
  for (int i = 0; i < 46; i++) {
    s = s * 1103515245u + 12345u; int x = (s >> 8) % 120; s = s * 1103515245u + 12345u; int y = (s >> 8) % 44;
    int tw = (frameNo / 3 + i * 7) % 40;
    uint16_t c = tw < 4 ? c565(255, 255, 255) : c565(200, 210, 255);
    uint8_t a = (uint8_t)(255 * clamp01((qNight - 0.3f) * 2.5f) * (tw < 4 ? 1 : 0.65f));
    if (i % 9 == 0) { qPxA(x * 2 - 2, y * 2, c, a / 2); qPxA(x * 2 + 2, y * 2, c, a / 2); qPxA(x * 2, y * 2 - 2, c, a / 2); qPxA(x * 2, y * 2 + 2, c, a / 2); }
    qPxA(x * 2, y * 2, c, a);
  }
}
void qSunMoon() {
  float h = qHourNow;
  bool day = h >= 6 && h < 18.8f;
  float f = day ? (h - 6) / 12.8f : (h >= 18.8f ? (h - 18.8f) : (h + 5.2f)) / 11.2f;
  int x = 24 + (int)(f * 192), y = 70 - (int)(sinf(f * 3.14159f) * 46);
  if (day) {
    qGlow(x, y, 36, c565(255, 220, 140), 130);
    qGlow(x, y, 15, c565(255, 250, 220), 255);
    gfx->fillCircle(x, y, 7, c565(255, 250, 226));
  } else {
    qGlow(x, y, 34, c565(140, 170, 255), 70);
    gfx->fillCircle(x, y, 8, c565(236, 236, 214));
    gfx->fillCircle(x + 4, y - 3, 7, qSky[min(139, y / 2)]);
    qRect(x - 5, y + 1, 2, 2, c565(200, 200, 180));
  }
}
void qClouds(float cam) {
  float t = millis() / 1000.0f;
  for (int i = 0; i < 4; i++) {
    const uint8_t *spr = i % 2 ? QCLOUD1 : QCLOUD0; int w = i % 2 ? QCLOUD1_W : QCLOUD0_W, h = i % 2 ? QCLOUD1_H : QCLOUD0_H;
    float span = 120 + w;
    float lx = fmodf(i * 61.0f - cam * 0.12f + t * (1.2f + i * 0.4f), span); if (lx < 0) lx += span;
    int x = (int)((lx - w) * 2), y = 18 + i * 16 + (i == 3 ? -30 : 0);
    qSprite(spr, w, h, x, y, false, qLut, 2, (uint8_t)(200 - qNight * 110));
  }
}
void qSkyLife() {
  uint32_t ms = millis();
  if (qNight < 0.5f) {                                   // a few birds cross now and then
    uint32_t cyc = ms % 26000;
    if (cyc < 9000) {
      float p = cyc / 9000.0f;
      for (int b = 0; b < 4; b++) {
        int x = (int)(-20 + p * 290) - b * 14 + (b % 2) * 4, y = 40 + b * 6 + (int)(sinf(p * 9 + b) * 4);
        bool up = ((ms / 140) + b) % 2;
        uint16_t c = qLut[QC_INK];
        qRect(x & ~1, y & ~1, 2, 2, c);
        qRect((x - 4) & ~1, (y - (up ? 2 : -2)) & ~1, 4, 2, c); qRect((x + 2) & ~1, (y - (up ? 2 : -2)) & ~1, 4, 2, c);
      }
    }
  } else {                                               // a shooting star
    uint32_t cyc = ms % 17000;
    if (cyc < 700) {
      float p = cyc / 700.0f;
      int x = 40 + (int)(p * 150), y = 16 + (int)(p * 50);
      for (int k = 0; k < 14; k++) qPxA(x - k * 4, y - k * 2 + (k & 1), c565(255, 255, 230), (uint8_t)(255 - k * 18));
    }
  }
}
void qBlitFar(float cam) {
  int off = qMod((int)(cam * 0.25f * 2), QF_W * 2);
  for (int y = 0; y < QF_H * 2; y++) {
    uint16_t *dst = FB + (QF_Y0 * 2 + y) * SCR_W, *src = qFar + y * QF_W * 2;
    int n1 = min((int)SCR_W, QF_W * 2 - off);
    memcpy(dst, src + off, n1 * 2);
    if (n1 < SCR_W) memcpy(dst + n1, src, (SCR_W - n1) * 2);
  }
}
void qBlitAlpha(const uint16_t *col, const uint8_t *al, int tw, int th, int y0, int off) {
  for (int y = 0; y < th; y++) {
    int yy = y0 + y; if (yy < 0 || yy >= SCR_H) continue;
    uint16_t *dst = FB + yy * SCR_W; const uint16_t *s = col + y * tw; const uint8_t *a = al + y * tw;
    int sx = qMod(off, tw);
    for (int x = 0; x < SCR_W; x++, sx++) {
      if (sx >= tw) sx = 0;
      uint8_t v = a[sx];
      if (!v) continue;
      dst[x] = v > 247 ? s[sx] : blend565(dst[x], s[sx], v);
    }
  }
}
void qBlitWorld(float cam) {
  int camR = (int)floorf(cam * 2);
  int par = camR & 1, lx0 = qMod(camR >> 1, QW_W);
  for (int ly = 0; ly < QW_H; ly++) {
    uint16_t *r0 = FB + (QW_Y0 + ly) * 2 * SCR_W, *r1 = r0 + SCR_W;
    const uint8_t *src = qWorld + ly * QW_W;
    int lx = lx0;
    for (int x = 0; x < SCR_W; x++) {
      uint8_t v = src[lx];
      if (v) { uint16_t c = qLut[v]; r0[x] = c; r1[x] = c; }
      if ((x + par) & 1) { if (++lx >= QW_W) lx = 0; }
    }
  }
}
// screen position (real px) of a world point; returns false when well off screen
bool qOnScreen(float cam, int wx, int wy, int &sx, int &sy, int margin = 40) {
  float d = wx - cam;
  d = fmodf(d, (float)QW_W); if (d < -QW_W / 2) d += QW_W; if (d > QW_W / 2) d -= QW_W;
  sx = (int)floorf(d * 2); sy = wy * 2;
  return sx > -margin && sx < SCR_W + margin;
}

void rSamurai(int sx, int sy, int frame, bool flip);
void qHero(int sx, int sy, int frame, bool flip, bool blink) {
  const uint8_t *f = QHERO + frame * QHERO_W * QHERO_H;
  qRectA(sx + 6, sy + QHERO_H * 2 - 4, 20, 4, c565(10, 10, 30), 90);    // shadow
  for (int j = 0; j < QHERO_H; j++) for (int i = 0; i < QHERO_W; i++) {
    uint8_t v = f[j * QHERO_W + (flip ? QHERO_W - 1 - i : i)];
    if (!v) continue;
    if (blink && (j == 10 || j == 11) && v == QC_INK) {
      int ii = flip ? QHERO_W - 1 - i : i;
      if (ii == 8 || ii == 11) v = j == 11 ? QC_INK : QC_SK;
    }
    qPx(sx + i * 2, sy + j * 2, qLutS[v]);
  }
}

// things that move in the village
void qDynamic(float cam, bool withPet) {
  int sx, sy; uint32_t ms = millis(); float t = ms / 1000.0f;
  // clock hands on the tower
  if (qOnScreen(cam, QD_CLOCK[0], QD_CLOCK[1], sx, sy)) {
    struct tm tm; float hh = 10, mm = 10;
    if (localNow(tm)) { hh = tm.tm_hour % 12 + tm.tm_min / 60.0f; mm = tm.tm_min; }
    float ah = hh * 0.5236f, am = mm * 0.10472f;
    uint16_t ink = qLut[QC_INK];
    for (int k = 1; k <= 4; k++) qPx(sx + (int)roundf(sinf(ah) * k) * 2, sy - (int)roundf(cosf(ah) * k) * 2, ink);
    for (int k = 1; k <= 6; k++) qPx(sx + (int)roundf(sinf(am) * k) * 2, sy - (int)roundf(cosf(am) * k) * 2, qLut[QC_R]);
    qPx(sx, sy, ink);
    if (qNight > 0.4f) qGlow(sx, sy, 26, c565(255, 220, 140), (int)(qNight * 70));
  }
  // chimney smoke
  if (qOnScreen(cam, QD_SMOKE[0], QD_SMOKE[1], sx, sy)) {
    for (int i = 0; i < 6; i++) {
      float p = fmodf(t * 0.5f + i / 6.0f, 1.0f);
      int x = sx + (int)(sinf(p * 6 + i) * 4 + p * 18), y = sy - (int)(p * 46);
      int r = 2 + (int)(p * 4);
      uint16_t c = mix565(qLut[QC_CL2], qLut[QC_CL3], (uint8_t)(p * 200));
      for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++)
        if (i * i + j * j <= r * r) qPxA((x & ~1) + i * 2, (y & ~1) + j * 2, c, (uint8_t)(170 * (1 - p)));
    }
  }
  // campfire
  if (qOnScreen(cam, QD_FIRE[0], QD_FIRE[1], sx, sy)) {
    qGlow(sx, sy - 6, 60, c565(255, 150, 60), 60 + (int)(qNight * 120) + (int)(sinf(t * 13) * 14));
    for (int i = 0; i < 7; i++) {
      float p = fmodf(t * 1.6f + i * 0.37f, 1.0f);
      int x = sx - 6 + (i * 5) % 12 + (int)(sinf(t * 9 + i) * 2), y = sy - 2 - (int)(p * 22);
      uint16_t c = p < 0.3f ? qLut[QC_F3] : p < 0.55f ? qLut[QC_F2] : p < 0.8f ? qLut[QC_F1] : qLut[QC_F0];
      int s = p < 0.5f ? 4 : 2;
      qRect(x & ~1, y & ~1, s, s, c);
    }
    qRect(sx - 4, sy - 4, 8, 4, qLut[QC_F2]); qRect(sx - 2, sy - 8, 4, 4, qLut[QC_F3]);
  }
  // the minstrel and notes
  if (qOnScreen(cam, QD_BARD[0], QD_BARD[1], sx, sy)) {
    int bob = ((frameNo / 8) % 2) * 2;
    qSprite(QBARD, QBARD_W, QBARD_H, sx - QBARD_W, sy - QBARD_H * 2 + 8 + bob, false, qLutS);
    bool playing = jbPlaying || (frameNo / 90) % 3 == 0;
    if (playing) for (int i = 0; i < 3; i++) {
      float p = fmodf(t * 0.6f + i / 3.0f, 1.0f);
      qSprite(QNOTE, QNOTE_W, QNOTE_H, sx + 4 + (int)(sinf(p * 7 + i) * 6) + i * 6, sy - 40 - (int)(p * 40), false, qLutS, 2, (uint8_t)(255 * (1 - p)));
    }
  }
  // the owl looks around and blinks
  if (qOnScreen(cam, QD_OWL[0], QD_OWL[1], sx, sy)) {
    int look = (int)(sinf(t * 0.7f) * 1.6f);
    bool blink = (frameNo % 90) < 4;
    for (int e = 0; e < 2; e++) {
      int ex = sx - 8 + e * 8;
      if (blink) qRect(ex - 2, sy + 4, 6, 2, qLut[QC_T1]);
      else { qRect(ex + look * 2, sy + 2, 2, 4, qLutS[QC_INK]); }
    }
    if (qNight > 0.4f) { qGlow(sx - 6, sy + 4, 8, c565(255, 230, 120), (int)(qNight * 90)); qGlow(sx + 2, sy + 4, 8, c565(255, 230, 120), (int)(qNight * 90)); }
  }
  // the pet in its yard
  if (withPet && qOnScreen(cam, QD_PET[0], QD_PET[1], sx, sy)) {
    const char *const *spr = petSpecies == 1 ? GHOST : petSpecies == 2 ? FROG : CAT;
    int hop = (int)(fabsf(sinf(t * 3)) * 4) & ~1;
    qRectA(sx - 10, sy - 2, 22, 4, c565(10, 10, 30), 80);
    drawSprite(spr, sx - 12, sy - 24 - hop, 2, mix565(petColor, COL_BLACK, (uint8_t)(qNight * 120)), (frameNo % 70) < 3);
  }
  // the crystal ball shimmers
  if (qOnScreen(cam, QD_CRYSTAL[0], QD_CRYSTAL[1], sx, sy)) {
    qGlow(sx, sy, 22, c565(120, 220, 255), 70 + (int)(sinf(t * 2.3f) * 30) + (int)(qNight * 80));
    int a = (frameNo / 6) % 8; qPx(sx - 4 + a, sy - 2 + (a % 3) * 2, c565(255, 255, 255));
  }
  // theatre marquee: chasing bulbs and the word SHOW
  if (qOnScreen(cam, QD_MARQUEE[0], QD_MARQUEE[1], sx, sy, 140)) {
    int w = QD_MARQUEE[2] * 2, h = QD_MARQUEE[3] * 2;
    for (int i = 0; i < w; i += 6) {
      bool on = ((i / 6 + frameNo / 4) % 3) == 0;
      uint16_t c = on ? c565(255, 240, 160) : c565(150, 90, 40);
      qRect(sx + i, sy, 2, 2, c); qRect(sx + w - 2 - i, sy + h - 2, 2, 2, c);
    }
    qTextC(QF_BODY, "TONIGHT", sx + w / 2, sy + 2, c565(255, 226, 120));
    if (qNight > 0.3f) qGlow(sx + w / 2, sy + h / 2, 50, c565(255, 180, 90), (int)(qNight * 60));
  }
  // the guild's flag waves
  if (qOnScreen(cam, QD_FLAG[0], QD_FLAG[1], sx, sy)) {
    qRect(sx, sy - 12, 2, 16, qLut[QC_M0]);
    for (int i = 0; i < 9; i++) {
      int dy = (int)(sinf(t * 6 - i * 0.8f) * 1.5f) * 2;
      for (int j = 0; j < 5; j++) qPx(sx + 2 + i * 2, sy - 12 + j * 2 + dy, j == 2 ? qLut[QC_Y2] : qLut[i < 2 ? QC_RD : QC_R]);
    }
  }
  // the lighthouse lamp sweeps its beam
  if (qOnScreen(cam, QD_LAMP[0], QD_LAMP[1], sx, sy, 200)) {
    float a = t * 1.4f, c = cosf(a);
    int len = (int)(fabsf(c) * 110) + 10;
    for (int k = 6; k < len; k += 2) {
      int bx = sx + (c > 0 ? k : -k), spread = 2 + k / 10;
      uint8_t al = (uint8_t)((40 + qNight * 80) * (1 - (float)k / len));
      for (int j = -spread; j <= spread; j += 2) if (bx >= 0 && bx < SCR_W && sy + j >= 0) { uint16_t *p = FB + (sy + j) * SCR_W + bx; *p = qAdd(*p, c565(255, 240, 180), al); p[1] = qAdd(p[1], c565(255, 240, 180), al); }
    }
    qGlow(sx, sy, 30, c565(255, 230, 150), 120 + (int)(qNight * 100));
  }
  // street lamps glow at night
  if (qNight > 0.15f)
    for (int i = 0; i < QLAMP_N; i++)
      if (qOnScreen(cam, QLAMP[i][0], QLAMP[i][1], sx, sy)) qGlow(sx, sy, 34, c565(255, 200, 110), (int)(qNight * 150));
}

void qMotes(bool fire) {
  float t = millis() / 1000.0f;
  for (int i = 0; i < 16; i++) {
    QMote &m = qMote[i];
    if (m.ph == 0) { m.x = random(0, 240); m.y = random(60, 250); m.vx = random(-30, 30) / 100.0f; m.vy = -random(5, 30) / 100.0f; m.ph = random(1, 628) / 100.0f; }
    m.x += m.vx + sinf(t + m.ph) * 0.3f; m.y += m.vy + cosf(t * 0.7f + m.ph) * 0.2f;
    if (m.y < 40 || m.x < -4 || m.x > 244) { m.x = random(0, 240); m.y = random(150, 250); }
    if (qNight > 0.5f || fire) {                                  // fireflies
      float b = 0.5f + 0.5f * sinf(t * 3 + m.ph * 3);
      if (i < 10) { qGlow((int)m.x, (int)m.y, 7, c565(200, 255, 120), (int)(200 * b)); qRect((int)m.x & ~1, (int)m.y & ~1, 2, 2, c565(240, 255, 190)); }
    } else if (i < 9) {                                           // dust in the sunlight
      qRectA((int)m.x & ~1, (int)m.y & ~1, 2, 2, c565(255, 250, 220), (uint8_t)(110 + 80 * sinf(t * 2 + m.ph)));
    }
  }
}
void qRays() {
  if (qDayK < 0.2f || qHourNow < 6.5f || qHourNow > 18.5f) return;
  float t = millis() / 1000.0f;
  uint8_t tri[32];
  for (int r = 0; r < 3; r++) {
    int w = 12 + r * 5;
    for (int d = 0; d < w; d++) tri[d] = (uint8_t)(255 * (w - d) / w);
    float x0 = -40 + r * 70 + sinf(t * 0.2f + r) * 6;
    int a0 = (int)((22 + 8 * sinf(t * 0.5f + r * 2)) * qDayK);
    for (int y = 0; y < 210; y += 2) {
      int fa = a0 * (210 - y) / 210;
      if (fa <= 0) break;
      int cx = (int)(x0 + y * 0.55f);
      uint16_t *r0 = FB + y * SCR_W, *r1 = r0 + SCR_W;
      for (int x = max(0, cx - w + 1); x < min((int)SCR_W, cx + w); x++) {
        int a = (fa * tri[abs(x - cx)]) >> 8;
        if (a > 0) { r0[x] = qAdd(r0[x], c565(255, 244, 210), a); r1[x] = qAdd(r1[x], c565(255, 244, 210), a); }
      }
    }
  }
}
uint8_t qVx[SCR_W], qVy[SCR_H]; bool qVinit = false;
void qVignette() {
  if (!qVinit) {
    for (int x = 0; x < SCR_W; x++) { float d = fabsf(x - 119.5f) / 120; qVx[x] = (uint8_t)(255 * (1 - powf(clamp01((d - 0.62f) / 0.38f), 2) * 0.42f)); }
    for (int y = 0; y < SCR_H; y++) { float d = fabsf(y - 139.5f) / 140; qVy[y] = (uint8_t)(255 * (1 - powf(clamp01((d - 0.6f) / 0.4f), 2) * 0.45f)); }
    qVinit = true;
  }
  for (int y = 0; y < SCR_H; y++) {
    uint16_t *row = FB + y * SCR_W; int ky = qVy[y] + 1;
    if (ky > 255) { for (int x = 0; x < 46; x++) { row[x] = qMul32(row[x], (qVx[x] + 1) >> 3); row[SCR_W - 1 - x] = qMul32(row[SCR_W - 1 - x], (qVx[SCR_W - 1 - x] + 1) >> 3); } }
    else for (int x = 0; x < SCR_W; x++) row[x] = qMul32(row[x], (ky * (qVx[x] + 1)) >> 11);
  }
}
// out-of-focus bottom edge (tilt-shift): more blur passes the closer to the bottom
static inline uint16_t qAvg(uint16_t a, uint16_t b) { return (a & b) + (((a ^ b) & 0xF7DE) >> 1); }
void qDof() {
  for (int y = 248; y < SCR_H; y++) {
    uint16_t *row = FB + y * SCR_W, *up = row - SCR_W;
    int passes = 1 + (y - 248) / 11;
    for (int p = 0; p < passes; p++) {
      uint16_t prev = row[0];
      for (int x = 0; x < SCR_W - 1; x++) { uint16_t cur = row[x]; row[x] = qAvg(qAvg(prev, row[x + 1]), cur); prev = cur; }
    }
    if (y > 252) for (int x = 0; x < SCR_W; x++) row[x] = qAvg(qAvg(row[x], up[x]), row[x]);
  }
}

// The whole scene at a camera position. hero: draw the hero (home only).
void qScene(float cam, bool hero, bool pet = true) {
  qLightUpdate(false);
  qBlitSky();
  qStars();
  qSunMoon();
  qSkyLife();
  qBlitFar(cam);
  qClouds(cam);
  qBlitAlpha(qHill, qHillA, QH_W * 2, QH_H * 2, QH_Y0 * 2, (int)(cam * 0.5f * 2));
  qBlitWorld(cam);
  qDynamic(cam, pet);
  if (hero) {
    uint32_t ms = millis();
    int frame = 0; bool walking = qWalkAt && ms - qWalkAt < qWalkMs;
    int lift = 0;
    if (walking) frame = 2 + (int)(fabsf(qHeroX) / 5) % 4;
    else frame = (ms / 600) % 2;
    if (qJumpAt && ms - qJumpAt < 420) { float p = (ms - qJumpAt) / 420.0f; lift = (int)(sinf(p * 3.14159f) * 26) & ~1; frame = 6; }
    if (qEnterAt) { float p = min(1.0f, (ms - qEnterAt) / 360.0f); lift = (int)(sinf(p * 3.14159f) * 20) & ~1; frame = 6; }
    int sx = (int)floorf((qHeroX - cam) * 2), sy = (QW_FEET - QHERO_H) * 2 - lift;
    qHero(sx, sy, frame, qFace < 0, !walking && (frameNo % 110) < 4);
    for (int i = 0; i < 8; i++) {                               // coins from a jump
      QCoin &c = qCoin[i];
      if (!c.t || ms - c.t > 900) continue;
      c.x += c.vx; c.y += c.vy; c.vy += 0.5f;
      bool edge = (frameNo / 3 + i) % 2;
      qSprite(edge ? QCOIN1 : QCOIN0, QCOIN0_W, QCOIN0_H, (int)c.x, (int)c.y, false, qLutS);
    }
  }
  qBlitAlpha(qFg, qFgA, QG_W * 2, QG_H * 2, QG_Y0 * 2, (int)(cam * 1.35f * 2));
  qRays();
  qMotes(false);
  qDof();
  qVignette();
}

// top corners: time on the left, battery on the right
void qHud() {
  char b[16];
  struct tm t;
  if (timerEndAt) {
    unsigned long left = (timerEndAt - millis()) / 1000;
    snprintf(b, sizeof(b), "%lu:%02lu", left / 60, left % 60);
  } else if (localNow(t)) {
    int h = t.tm_hour; if (!use24h) { h %= 12; if (!h) h = 12; }
    snprintf(b, sizeof(b), "%d:%02d", h, t.tm_min);
  } else snprintf(b, sizeof(b), "--:--");
  int w = qTextW(QF_BODY, b) + 34;
  qWindow(22, 8, w, 26, 210);
  bool night = qNight > 0.5f || skin == SKIN_RONIN;
  qSprite(night ? QMOON : QSUN, 8, 8, 30, 13, false, qIcons());
  qText(QF_BODY, b, 48, 13, timerEndAt ? Q_GOLD : Q_WHITE, 2, Q_SHADOW);
  snprintf(b, sizeof(b), "%d%%", batPct);
  int bw = qTextW(QF_BODY, b) + 36 + (bleConnected ? 14 : 0);
  int bx = (218 - bw) & ~1;
  qWindow(bx, 8, bw, 26, 210);
  uint16_t col = charging ? c565(150, 255, 120) : batPct > 20 ? Q_WHITE : c565(255, 110, 100);
  qSprite(QHEART, 8, 8, bx + 8, 13, false, qIcons());
  qText(QF_BODY, b, bx + 26, 13, col, 2, Q_SHADOW);
  if (bleConnected) qSprite(QGEM, 8, 8, bx + bw - 22, 13, false, qIcons());
}

void qMosaic(int block, int darken) {
  for (int by = 0; by < SCR_H; by += block) for (int bx = 0; bx < SCR_W; bx += block) {
    uint16_t c = FB[min(SCR_H - 1, by + block / 2) * SCR_W + min(SCR_W - 1, bx + block / 2)];
    if (darken) c = qMul(c, 256 - darken);
    int w = min(block, SCR_W - bx), h = min(block, SCR_H - by);
    for (int y = 0; y < h; y++) { uint16_t *p = FB + (by + y) * SCR_W + bx; for (int x = 0; x < w; x++) p[x] = c; }
  }
}
// The JRPG transition: the picture breaks into bigger and bigger blocks and fades out.
void questTransitionOut() {
  if (!FB || !prevFb || sleeping) return;
  memcpy(prevFb, FB, FB_BYTES);
  static const uint8_t B[6] = {4, 6, 10, 14, 20, 28};
  for (int s = 0; s < 6; s++) {
    memcpy(FB, prevFb, FB_BYTES);
    qMosaic(B[s], s * 40);
    gfx->flush();
  }
  qMosaicIn = 5;
}
void questPost() {                     // runs after a frame is drawn: the transition back in
  if (!qMosaicIn) return;
  static const uint8_t B[6] = {0, 4, 8, 12, 18, 24};
  qMosaic(B[qMosaicIn], qMosaicIn * 34);
  qMosaicIn--;
}

// ───── home: the village ─────
void questGo(int dir) {
  uint32_t ms = millis();
  qWalkFrom = qHeroX; qCamFrom = qCam;
  qLoc += dir;
  qFace = dir > 0 ? 1 : -1;
  qWalkAt = ms;
  float dist = fabsf(qLoc * (float)QW_SPACING + QHERO_SX[qMod(qLoc, QW_NLOC)] - qWalkFrom);
  qWalkMs = (uint32_t)(380 + dist * 6.5f);
  qArriveAt = 0;
  qEnterAt = 0;
  sfx(0);
}
void questPlace(int loc) {                // stand at a landmark without walking
  qLoc = loc; qShown = qMod(loc, QW_NLOC);
  qCam = loc * (float)QW_SPACING; qHeroX = qCam + QHERO_SX[qShown];
  qWalkAt = 0; qArriveAt = millis(); qFace = 1;
}
void questJump() {
  qJumpAt = millis();
  int sx = (int)((qHeroX - qCam) * 2) + 10, sy = (QW_FEET - QHERO_H) * 2;
  for (int i = 0; i < 8; i++) { QCoin &c = qCoin[i]; c.x = sx; c.y = sy; c.vx = random(-30, 31) / 10.0f; c.vy = -random(50, 90) / 10.0f; c.t = millis(); }
  sfx(3);
}
void questEnter() { if (!qEnterAt) { qEnterAt = millis(); sfx(4); } }

void drawQuestHome() {
  if (!questInit()) { gfx->fillScreen(c565(20, 30, 90)); qTextC(QF_BODY, "Not enough memory", 120, 130, Q_WHITE); return; }
  uint32_t ms = millis();
  if (qWalkAt) {
    float p = min(1.0f, (ms - qWalkAt) / (float)qWalkMs);
    float e = p < 0.5f ? 2 * p * p : 1 - powf(-2 * p + 2, 2) / 2;
    float tx = qLoc * (float)QW_SPACING + QHERO_SX[qMod(qLoc, QW_NLOC)], tc = qLoc * (float)QW_SPACING;
    qHeroX = qWalkFrom + (tx - qWalkFrom) * e;
    qCam = qCamFrom + (tc - qCamFrom) * e;
    if (p >= 1) { qWalkAt = 0; qArriveAt = ms; qShown = qMod(qLoc, QW_NLOC); qFace = 1; }
  }
  if (qEnterAt && ms - qEnterAt > 380) { qEnterAt = 0; openModule(QLOC_MODE[qShown]); return; }
  qScene(qCam, true);
  qHud();
  if (qWalkAt) return;
  // where you are: a window slides up from the bottom
  float s = min(1.0f, (ms - qArriveAt) / 220.0f); s = 1 - (1 - s) * (1 - s);
  int y = (284 - (int)(s * 60)) & ~1;
  const char *name = QLOC_NAME[qShown];
  int w = max(qTextW(QF_TITLE, name), qTextW(QF_BODY, QLOC_SUB[qShown])) + 36;
  w = max(w, 132);
  qWindow(120 - w / 2, y, w, 56);
  qTextC(QF_TITLE, name, 120, y + 3, Q_WHITE, 2, Q_SHADOW);
  // little arrows: walk on either side, enter in the middle
  int bob = ((ms / 300) % 2) * 2;
  qArrow(14 - bob, 140, -1, Q_WHITE); qArrow(226 + bob, 140, 1, Q_WHITE);
  int dx = (int)((qLoc * (float)QW_SPACING + QDOOR[qShown][0] - qCam) * 2);
  if (!qEnterAt && s >= 1) { qArrow(dx + 2, QDOOR[qShown][1] * 2 - 14 + bob + 2, 0, Q_SHADOW); qArrow(dx, QDOOR[qShown][1] * 2 - 14 + bob, 0, Q_GOLD); }
  qTextC(QF_BODY, QLOC_SUB[qShown], 120, y + 30, Q_DIM);
}

// ───── the screens behind each door ─────
float qCamOf(uint8_t m) { int l = qLocOfMode(m); return l < 0 ? 0 : l * (float)QW_SPACING; }
void rBackdrop(uint8_t m);
void qBackdrop(uint8_t m, int dim = 0) {
  if (skin == SKIN_RONIN) rBackdrop(m);
  else {
    if (!questInit()) { gfx->fillScreen(c565(20, 30, 90)); return; }
    qScene(qCamOf(m), false, m != M_PET);
  }
  if (dim) { uint32_t k = (256 - dim) >> 3; for (int i = 0; i < SCR_W * SCR_H; i++) FB[i] = qMul32(FB[i], k); }
}

void questTopBar(bool showTime) { (void)showTime; qHud(); }

void questToast() {
  String m = toastMsg;
  int w = qTextW(QF_BODY, m.c_str()) + 32; w = max(w, 80);
  qWindow(120 - w / 2, 214, w, 30);
  qTextC(QF_BODY, m.c_str(), 120, 220, Q_WHITE, 2, Q_SHADOW);
}

void questOverlay(const char *big, const char *small, float frac) {
  qWindow(24, 100, 192, frac >= 0 ? 80 : 86);
  qTextC(QF_TITLE, big, 120, 108, Q_WHITE, 2, Q_SHADOW);
  if (frac >= 0) {
    qBar(44, 150, 152, frac, c565(90, 220, 255), c565(200, 250, 255));
    char b[8]; snprintf(b, sizeof(b), "%d%%", (int)(frac * 100));
    qTextC(QF_BODY, b, 120, 162, Q_DIM);
  } else if (small) {
    String s = qClip(QF_BODY, String(small), 176);
    qTextC(QF_BODY, s.c_str(), 120, 148, Q_DIM);
  }
}

void questEmpty(const char *big, const char *l1, const char *l2) {
  qBackdrop(mode, 60);
  qHud();
  qWindow(20, 168, 200, 86);
  String b = big; b.toLowerCase(); b.setCharAt(0, toupper(b[0]));
  qText(QF_TITLE, b.c_str(), 36, 176, Q_GOLD, 2, Q_SHADOW);
  qText(QF_BODY, l1, 36, 208, Q_WHITE, 2, Q_SHADOW);
  qText(QF_BODY, l2, 36, 226, Q_WHITE, 2, Q_SHADOW);
  if ((millis() / 400) % 2) qArrow(204, 240, 0, Q_WHITE);
}

void drawClockQuest() {
  qBackdrop(M_CLOCK, 40);
  qHud();
  struct tm t;
  qWindow(18, 74, 204, 132);
  qPlate("TIME", 120, 66);
  if (!localNow(t)) {
    qTextC(QF_BIG, "--:--", 120, 92, Q_WHITE, 2, Q_SHADOW);
    qTextC(QF_BODY, "Open the app once to sync", 120, 170, Q_DIM);
    return;
  }
  int h = t.tm_hour; if (!use24h) { h %= 12; if (!h) h = 12; }
  char b[24];
  snprintf(b, sizeof(b), "%d:%02d", h, t.tm_min);
  int w = qTextW(QF_BIG, b);
  bool colon = (millis() / 500) % 2;
  int x = 120 - w / 2;
  for (const char *p = b; *p; p++) {
    char one[2] = {*p, 0};
    if (*p != ':' || colon) qText(QF_BIG, one, x, 82, Q_WHITE, 2, Q_SHADOW);
    x += QF_BIG.g[*p - 32].adv * 2;
  }
  if (!use24h) qText(QF_BODY, t.tm_hour < 12 ? "AM" : "PM", 120 + w / 2 + 4, 128, Q_GOLD, 2, Q_SHADOW);
  static const char *DN[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char *MN[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
  snprintf(b, sizeof(b), "%s %d %s", DN[t.tm_wday], t.tm_mday, MN[t.tm_mon]);
  qTextC(QF_BODY, b, 120, 152, Q_WHITE, 2, Q_SHADOW);
  // seconds as a row of pips, then the part of the day
  for (int i = 0; i < 30; i++) qRect(36 + i * 6, 176, 4, 4, i * 2 <= t.tm_sec ? Q_GOLD : c565(40, 50, 120));
  static const char *ZOD[12] = {"Rat", "Ox", "Tiger", "Rabbit", "Dragon", "Snake", "Horse", "Goat", "Monkey", "Rooster", "Dog", "Boar"};
  if (skin == SKIN_RONIN) snprintf(b, sizeof(b), "Hour of the %s", ZOD[((t.tm_hour + 1) / 2) % 12]);
  else snprintf(b, sizeof(b), "%s  -  day %d", qPeriod(), t.tm_yday + 1);
  qTextC(QF_BODY, b, 120, 186, Q_DIM);
  if (npPlaying && npTitle.length()) {
    String s = qClip(QF_BODY, asciiOnly(npTitle), 150);
    int sw = qTextW(QF_BODY, s.c_str()) + 40;
    qWindow(120 - sw / 2, 222, sw, 28, 210);
    qSprite(QNOTE, QNOTE_W, QNOTE_H, 120 - sw / 2 + 12, 228, false, qIcons());
    qText(QF_BODY, s.c_str(), 120 - sw / 2 + 28, 228, Q_WHITE, 2, Q_SHADOW);
  }
}

void drawMusicQuest() {
  if (artLoaded && artBuf && FB) {
    for (int y = 0; y < SCR_H; y += 4) for (int x = 0; x < SCR_W; x += 4) {   // the cover as chunky pixels
      uint16_t c = artBuf[(y + 1) * SCR_W + x + 1];
      for (int j = 0; j < 4; j++) { uint16_t *p = FB + (y + j) * SCR_W + x; p[0] = p[1] = p[2] = p[3] = c; }
    }
    shadeRows(150, SCR_H, 256, 60);
  } else qBackdrop(M_MUSIC, 40);
  qHud();
  qWindow(12, 178, 216, 88);
  qPlate(npPlaying ? "NOW PLAYING" : "PAUSED", 120, 170);
  String title = asciiOnly(npTitle.length() ? npTitle : String("Nothing playing"));
  String artist = asciiOnly(npArtist.length() || npTitle.length() ? npArtist : String("Play a song on your phone"));
  int tw = qTextW(QF_TITLE, title.c_str());
  if (tw <= 196) qTextC(QF_TITLE, title.c_str(), 120, 188, Q_WHITE, 2, Q_SHADOW);
  else {
    static int off = 0; off = (off + 2) % (tw + 60);
    String two = title + "     " + title;
    // draw clipped by hand: only glyphs inside the window
    int x = 24 - off;
    for (const char *p = two.c_str(); *p && x < 216; p++) {
      int c = (uint8_t)*p; if (c < 32 || c > 126) c = '?';
      int adv = QF_TITLE.g[c - 32].adv * 2;
      if (x >= 22 && x + adv <= 218) { char one[2] = {(char)c, 0}; qText(QF_TITLE, one, x, 188, Q_WHITE, 2, Q_SHADOW); }
      x += adv;
    }
  }
  artist = qClip(QF_BODY, artist, 190);
  qTextC(QF_BODY, artist.c_str(), 120, 216, Q_GOLD);
  uint32_t pos = npPosition();
  qBar(26, 236, 188, npDurMs ? (float)pos / npDurMs : 0, c565(250, 196, 70), c565(255, 240, 170));
  char b[16];
  snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(pos / 60000), (unsigned long)((pos / 1000) % 60));
  qText(QF_BODY, b, 26, 248, Q_DIM);
  if (npDurMs) { snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(npDurMs / 60000), (unsigned long)((npDurMs / 1000) % 60)); qText(QF_BODY, b, 214 - qTextW(QF_BODY, b), 248, Q_DIM); }
  if (millis() < mediaFlashUntil) {
    qWindow(84, 70, 72, 64);
    const int cx = 120, cy = 102;
    if (mediaFlashKind == 0) {
      if (npPlaying) for (int k = 0; k < 9; k++) qRect(cx - 10 + k * 2, cy - 16 + k * 2, 2, 32 - k * 4, Q_WHITE);
      else { qRect(cx - 12, cy - 14, 8, 28, Q_WHITE); qRect(cx + 4, cy - 14, 8, 28, Q_WHITE); }
    } else { qArrow(cx + (mediaFlashKind == 1 ? -8 : 8), cy, mediaFlashKind == 1 ? 1 : -1, Q_WHITE); qArrow(cx + (mediaFlashKind == 1 ? 4 : -4), cy, mediaFlashKind == 1 ? 1 : -1, Q_WHITE); }
  }
}

// Jukebox: a JRPG menu. Top: category, rows: songs, bottom: play / stop / skip.
#define QJ_ROW_Y 112
#define QJ_ROW_H 24
#define QJ_ROWS 4
int qjTop = 0;
void drawJukeQuest() {
  qBackdrop(M_JUKE, 30);
  qHud();
  qWindow(14, 50, 212, 46);
  const char *cat = catName(jbCat);
  qTextC(QF_TITLE, cat, 120, 54, Q_WHITE, 2, Q_SHADOW);
  int bob = ((millis() / 300) % 2) * 2;
  qArrow(30 - bob, 73, -1, Q_GOLD); qArrow(210 + bob, 73, 1, Q_GOLD);
  int16_t ids[24];
  int n = songsInCat(jbCat, ids, 24);
  int sel = 0;
  for (int i = 0; i < n; i++) if (ids[i] == (jbPlaying ? jbSong : jbSelId)) sel = i;
  if (sel < qjTop) qjTop = sel;
  if (sel >= qjTop + QJ_ROWS) qjTop = sel - QJ_ROWS + 1;
  if (qjTop > max(0, n - QJ_ROWS)) qjTop = max(0, n - QJ_ROWS);
  qWindow(14, 100, 212, QJ_ROWS * QJ_ROW_H + 18, 225);
  for (int r = 0; r < QJ_ROWS && qjTop + r < n; r++) {
    int i = qjTop + r, id = ids[i], y = QJ_ROW_Y + r * QJ_ROW_H;
    bool cur = jbPlaying && id == jbSong;
    String nm = qClip(QF_BODY, String(songName(id)), 150);
    if (i == sel) qSprite(QHAND, 9, 7, 24 + ((millis() / 250) % 2) * 2, y + 4, false, qIcons());
    qText(QF_BODY, nm.c_str(), 46, y + 2, cur ? Q_GOLD : Q_WHITE, 2, Q_SHADOW);
    if (cur) for (int k = 0; k < 3; k++) { int hh = 4 + (int)(fabsf(sinf(millis() / 120.0f + k)) * 8) & ~1; qRect(196 + k * 6, y + 16 - hh, 4, hh, Q_GOLD); }
  }
  if (n > QJ_ROWS) {                                      // scroll marks
    if (qjTop > 0) qArrow(214, 112, 2, Q_DIM);
    if (qjTop + QJ_ROWS < n) qArrow(214, 200, 0, Q_DIM);
  }
  qWindow(14, 222, 212, 40);
  if (jbPlaying) {
    qBar(26, 232, 120, jbProgress(), c565(120, 230, 160), c565(210, 255, 220));
    qText(QF_BODY, "Stop", 154, 228, Q_WHITE, 2, Q_SHADOW);
    qText(QF_BODY, "Skip", 190, 228, Q_GOLD, 2, Q_SHADOW);
    char b[16]; snprintf(b, sizeof(b), "%d queued", jbQLen);
    qText(QF_BODY, b, 26, 244, Q_DIM);
  } else {
    qTextC(QF_BODY, "Tap a song to play it", 120, 232, Q_DIM);
  }
}
void questJukeTap(int x, int y) {
  int16_t ids[24];
  int n = songsInCat(jbCat, ids, 24);
  if (y < 98) { jbCat = (jbCat + catCount() + (x < 120 ? -1 : 1)) % catCount(); qjTop = 0; sfx(1); return; }
  if (y >= QJ_ROW_Y - 4 && y < QJ_ROW_Y + QJ_ROWS * QJ_ROW_H) {
    int i = qjTop + (y - QJ_ROW_Y + 4) / QJ_ROW_H;
    if (x > 200 && n > QJ_ROWS) { qjTop = constrain(qjTop + (y < 150 ? -1 : 1), 0, n - QJ_ROWS); return; }
    if (i < n) { bool was = jbPlaying; jbSelId = ids[i]; jbEnqueue(ids[i]); toast(was ? "Added to the queue" : "Playing", 800); }
    return;
  }
  if (y >= 218) {
    if (jbPlaying) { if (x > 180) jbSkip(); else jbStop(); }
    else jbStartSong(jbSelId);
  }
}

void drawPetQuest() {
  qBackdrop(M_PET, 0);
  qHud();
  const char *const *spr = petSpecies == 1 ? GHOST : petSpecies == 2 ? FROG : CAT;
  float t = millis() / 1000.0f;
  bool dizzy = millis() < petDizzyUntil;
  int hop = (int)(fabsf(sinf(t * (petMood > 40 ? 3.2f : 1.4f))) * (petMood > 40 ? 14 : 4)) & ~1;
  int slide = imuOK ? constrain((int)(tiltX * 28), -24, 24) & ~1 : 0;
  if (dizzy) slide += (int)(sinf(t * 25) * 6) & ~1;
  qRectA(120 + slide - 26, 236, 52, 6, c565(10, 10, 30), 100);
  drawSprite(spr, 96 + slide, 190 - hop, 4, mix565(petColor, COL_BLACK, (uint8_t)(qNight * 90)), (frameNo % 60) < 3 || dizzy);
  if (dizzy) for (int i = 0; i < 3; i++) { float a = t * 8 + i * 2.09f; qSprite(QCOIN0, 5, 5, 116 + slide + (int)(cosf(a) * 30), 182 - hop + (int)(sinf(a) * 6), false, qIcons()); }
  if (millis() < petHeartsUntil)
    for (int i = 0; i < 4; i++) {
      int hx = 50 + i * 44, hy = 176 - (int)((millis() / 20 + i * 17) % 70);
      qSprite(QHEART, 8, 8, hx, hy, false, qIcons());
    }
  String nm = asciiOnly(petName);
  int w = qTextW(QF_TITLE, nm.c_str()) + 40;
  qWindow(120 - w / 2, 40, w, 34);
  qTextC(QF_TITLE, nm.c_str(), 120, 42, Q_WHITE, 2, Q_SHADOW);
  qWindow(30, 244, 180, 30, 220);
  int hearts = (petMood + 19) / 20;
  for (int i = 0; i < 5; i++) {
    if (i < hearts) qSprite(QHEART, 8, 8, 44 + i * 20, 251, false, qIcons());
    else { qRect(46 + i * 20, 253, 12, 10, c565(40, 40, 80)); }
  }
  qText(QF_BODY, dizzy ? "Dizzy!" : petMood > 70 ? "Happy" : petMood > 35 ? "Chilling" : "Hungry", 146, 250, Q_WHITE, 2, Q_SHADOW);
}

void drawToyQuest() {
  qBackdrop(M_TOY, 60);
  qHud();
  static const char *TN0[3] = {"Oracle", "Dice", "Coin"}, *TN1[3] = {"Omikuji", "Dice", "Coin"};
  const char *const *TN = skin == SKIN_RONIN ? TN1 : TN0;
  uint32_t el = millis() - toyRollAt;
  bool rolling = toyRollAt && el < 1100;
  int jx = rolling ? ((int)random(-3, 4)) * 2 : 0, jy = rolling ? ((int)random(-3, 4)) * 2 : 0;
  qWindow(52, 40, 136, 34);
  qTextC(QF_TITLE, TN[toyKind % 3], 120, 42, Q_WHITE, 2, Q_SHADOW);
  int bob = ((millis() / 300) % 2) * 2;
  qArrow(72 - bob, 57, -1, Q_GOLD); qArrow(168 + bob, 57, 1, Q_GOLD);
  if (toyKind == 0) {
    int cx = 120 + jx, cy = 136 + jy;
    if (skin == SKIN_RONIN) {                                  // a wooden omikuji box with a fortune stick
      qGlow(cx, cy, 70, c565(255, 120, 60), 80 + (rolling ? 80 : 0));
      int sy = rolling ? cy - 40 : toyHasResult ? cy - 66 : cy - 46;
      qRect(cx - 3, sy, 6, 40, c565(40, 24, 20)); qRect(cx - 2, sy + 1, 4, 38, c565(224, 200, 150)); qRect(cx - 2, sy + 1, 4, 6, c565(200, 40, 36));
      qRect(cx - 26, cy - 34, 52, 74, c565(20, 12, 16)); qRect(cx - 24, cy - 32, 48, 70, c565(150, 98, 60)); qRect(cx - 24, cy - 32, 8, 70, c565(190, 136, 86)); qRect(cx + 16, cy - 32, 8, 70, c565(110, 66, 42));
      qRect(cx - 16, cy - 18, 32, 40, c565(200, 40, 36)); qRect(cx - 12, cy - 14, 24, 32, c565(150, 22, 28));
      qTextC(QF_TITLE, "?", cx, cy - 14, c565(255, 220, 150), 3);
    } else {
    qGlow(cx, cy, 70, c565(120, 200, 255), 120 + (rolling ? 80 : 0));
    gfx->fillCircle(cx, cy, 40, c565(30, 20, 46));
    gfx->fillCircle(cx, cy, 37, c565(40, 120, 220));
    gfx->fillCircle(cx - 4, cy - 4, 30, c565(90, 190, 250));
    gfx->fillCircle(cx - 12, cy - 14, 8, c565(220, 250, 255));
    qRect(cx - 30, cy + 34, 60, 8, c565(110, 60, 140)); qRect(cx - 24, cy + 42, 48, 6, c565(70, 36, 100));
    }
    qWindow(20, 200, 200, 58);
    const char *ans = rolling ? (skin == SKIN_RONIN ? "Shaking the box..." : "The crystal is thinking...") : toyHasResult ? EIGHT[toyResult % EIGHT_N] : (imuOK ? "Shake me for an answer" : "Tap for an answer");
    String a = ans; a.replace('\n', ' ');
    if (toyHasResult && !rolling) { a.setCharAt(0, toupper(a[0])); qTextC(QF_TITLE, a.c_str(), 120, 210, Q_GOLD, 2, Q_SHADOW); }
    else qTextC(QF_BODY, a.c_str(), 120, 220, Q_WHITE, 2, Q_SHADOW);
  } else if (toyKind == 1) {
    uint8_t a = rolling ? 1 + random(0, 6) : toyResult, b = rolling ? 1 + random(0, 6) : toyResult2;
    if (!toyHasResult) { a = 5; b = 2; }
    for (int d = 0; d < 2; d++) {
      int x = (d ? 128 - jx : 40 + jx) & ~1, y = (96 + (d ? -jy : jy)) & ~1, s = 72;
      qRect(x + 4, y + 6, s, s, c565(20, 14, 40));
      qRect(x, y, s, s, c565(30, 20, 46)); qRect(x + 2, y + 2, s - 4, s - 4, c565(250, 248, 240)); qRect(x + 2, y + s - 8, s - 4, 6, c565(200, 200, 214)); qRect(x + s - 8, y + 2, 6, s - 4, c565(200, 200, 214));
      static const uint16_t PIPS[7] = {0, 0b000010000, 0b100000001, 0b100010001, 0b101000101, 0b101010101, 0b101101101};
      uint8_t v = d ? b : a; if (v < 1 || v > 6) v = 1;
      for (int i = 0; i < 9; i++) if ((PIPS[v] >> (8 - i)) & 1) qRect(x + 10 + (i % 3) * 20, y + 10 + (i / 3) * 20, 12, 12, v == 1 ? c565(206, 48, 62) : c565(30, 20, 46));
    }
    qWindow(40, 200, 160, 52);
    char b2[16];
    if (toyHasResult && !rolling) { snprintf(b2, sizeof(b2), "Total %d", toyResult + toyResult2); qTextC(QF_TITLE, b2, 120, 208, Q_GOLD, 2, Q_SHADOW); }
    else qTextC(QF_BODY, rolling ? "Rolling..." : imuOK ? "Shake to roll" : "Tap to roll", 120, 216, Q_WHITE, 2, Q_SHADOW);
  } else {
    int cy = 140;
    if (rolling) {
      int rx = max(4, (int)(fabsf(sinf(el * 0.02f)) * 46)) & ~1;
      cy -= (int)(sinf(el * 3.14159f / 1100.0f) * 50) & ~1;
      gfx->fillEllipse(120, cy, rx, 46, c565(250, 210, 90));
    } else {
      qGlow(120, cy, 70, c565(255, 210, 90), 80);
      gfx->fillCircle(120, cy, 48, c565(142, 90, 30)); gfx->fillCircle(120, cy, 44, c565(210, 150, 42)); gfx->fillCircle(118, cy - 2, 38, c565(250, 210, 90));
      qTextC(QF_TITLE, toyHasResult ? (toyResult ? "T" : "H") : "?", 120, cy - 30, c565(142, 90, 30), 4);
    }
    qWindow(40, 210, 160, 44);
    qTextC(toyHasResult && !rolling ? QF_TITLE : QF_BODY, rolling ? "Flipping..." : toyHasResult ? (toyResult ? "Tails" : "Heads") : (imuOK ? "Shake to flip" : "Tap to flip"), 120, toyHasResult && !rolling ? 214 : 222, toyHasResult && !rolling ? Q_GOLD : Q_WHITE, 2, Q_SHADOW);
  }
}

// ID card: a character status screen
void drawCardQuest() {
  qBackdrop(M_CARD, 70);
  qHud();
  qWindow(14, 44, 212, 222);
  qPlate("STATUS", 120, 36);
  qRect(26, 58, 52, 64, c565(20, 16, 40));
  if (skin == SKIN_RONIN) { qRect(28, 60, 48, 60, c565(90, 20, 26)); qRect(28, 104, 48, 16, c565(30, 22, 34)); gfx->fillCircle(64, 76, 10, c565(200, 50, 44)); rSamurai(36, 64, (millis() / 600) % 2, false); }
  else { qRect(28, 60, 48, 60, c565(70, 110, 190)); qRect(28, 104, 48, 16, c565(60, 140, 70)); qHero(36, 64, (millis() / 600) % 2, false, (frameNo % 110) < 4); }
  String name = asciiOnly(cardName), sub = asciiOnly(cardSub);
  name = qClip(QF_TITLE, name, 130);
  qText(QF_TITLE, name.c_str(), 88, 56, Q_WHITE, 2, Q_SHADOW);
  sub = qClip(QF_BODY, sub, 130);
  qText(QF_BODY, sub.c_str(), 88, 84, Q_GOLD, 2, Q_SHADOW);
  char b[24];
  int lv = 1 + min(98, (int)(stepsToday / 1000));
  snprintf(b, sizeof(b), "LV %d", lv);
  qText(QF_BODY, b, 88, 102, Q_WHITE, 2, Q_SHADOW);
  // HP = battery, EXP = steps toward the next level
  qText(QF_BODY, "HP", 28, 132, Q_GOLD, 2, Q_SHADOW);
  qBar(56, 134, 112, batPct / 100.0f, batPct > 30 ? c565(110, 220, 110) : c565(240, 90, 80), c565(220, 255, 210));
  snprintf(b, sizeof(b), "%d/100", batPct); qText(QF_BODY, b, 174, 132, Q_WHITE, 2, Q_SHADOW);
  qText(QF_BODY, "EXP", 28, 152, Q_GOLD, 2, Q_SHADOW);
  qBar(56, 154, 112, (stepsToday % 1000) / 1000.0f, c565(110, 180, 255), c565(220, 240, 255));
  snprintf(b, sizeof(b), "%lu", (unsigned long)stepsToday); qText(QF_BODY, b, 174, 152, Q_WHITE, 2, Q_SHADOW);
  qText(QF_BODY, "LINK", 28, 172, Q_GOLD, 2, Q_SHADOW);
  qText(QF_BODY, bleConnected ? "Connected" : "Offline", 70, 172, bleConnected ? c565(150, 240, 255) : Q_DIM, 2, Q_SHADOW);
  // the NFC spot: rings pulse out of a crest
  int phase = (frameNo / 4) % 4;
  for (int i = 0; i < 3; i++) gfx->drawCircle(120, 220, 10 + i * 8, i < phase ? Q_GOLD : c565(60, 70, 140));
  qSprite(QGEM, 8, 8, 112, 212, false, qIcons());
  qTextC(QF_BODY, "Tap a phone here", 120, 244, Q_WHITE, 2, Q_SHADOW);
}

// Charging: the hero naps by the campfire
void rCharge();
void drawChargeQuest() {
  if (skin == SKIN_RONIN) { rCharge(); return; }
  if (!questInit()) { gfx->fillScreen(c565(20, 30, 90)); return; }
  float cam = qCamOf(M_JUKE);
  qScene(cam, false, true);
  int sx, sy;
  if (qOnScreen(cam, QD_FIRE[0], QD_FIRE[1], sx, sy)) {
    qSprite(QSLEEP, QSLEEP_W, QSLEEP_H, sx + 18, sy - QSLEEP_H * 2 + 12, false, qLutS);
    for (int i = 0; i < 3; i++) {
      float p = fmodf(millis() / 1600.0f + i / 3.0f, 1.0f);
      qText(QF_TITLE, "z", sx + 30 + (int)(p * 24) + i * 2, sy - 34 - (int)(p * 40), Q_WHITE, 2, Q_SHADOW);
    }
  }
  qHud();
  qWindow(20, 52, 200, 96);
  qPlate(charging ? (batPct >= 99 ? "FULLY RESTED" : "RESTING") : "ON THE ROAD", 120, 44);
  char b[16]; snprintf(b, sizeof(b), "%d%%", batPct);
  qTextC(QF_BIG, b, 120, 58, charging ? c565(170, 255, 140) : Q_WHITE, 2, Q_SHADOW);
  float f = batPct / 100.0f;
  qBar(36, 114, 168, f, charging ? c565(110, 220, 110) : c565(250, 196, 70), c565(220, 255, 210));
  if (charging) { int sp = (int)((millis() / 12) % 168) & ~1; if (sp < (int)(164 * f)) qRect(38 + sp, 116, 4, 6, c565(255, 255, 255)); }
  snprintf(b, sizeof(b), "%.2fV", batVolts);
  qTextC(QF_BODY, b, 120, 126, Q_DIM);
}

void drawAlarmQuest() {
  bool flash = (millis() / 300) % 2;
  if (flash) qRectA(0, 0, SCR_W, SCR_H, c565(255, 255, 255), 60);
  qWindow(28, 84, 184, 112);
  qTextC(QF_BIG, "!", 120, 86, Q_GOLD, 2, Q_SHADOW);
  qTextC(QF_TITLE, "Time's up!", 120, 140, Q_WHITE, 2, Q_SHADOW);
  qTextC(QF_BODY, "Tap to stop", 120, 168, Q_DIM);
}

// title screen while it boots
void questBoot() {
  questInit();
  qLightUpdate(true);
  questPlace(qMod(qLoc, QW_NLOC));
  qScene(qCam, false);
  qRectA(0, 0, SCR_W, SCR_H, c565(10, 10, 40), 70);
  qTextC(QF_TITLE, "Larp Charm", 120, 96, Q_GOLD, 3, Q_SHADOW);
  qTextC(QF_BODY, "A pocket-sized quest", 120, 150, Q_WHITE, 2, Q_SHADOW);
  qTextC(QF_BODY, "Press start", 120, 220, Q_WHITE, 2, Q_SHADOW);
}

// ═════════════ v6: the Ronin look, a samurai climbing his burning mountain village ═════════════
// Same half-resolution pixel engine as Quest, but the world scrolls up and down: swipe up to climb the
// stairs to the next landing, swipe down to go back. Every landing holds one landmark; tap the middle to go in.
uint8_t  *rWorld = nullptr, *rFarI = nullptr, *rMidI = nullptr, *rFgI = nullptr;
uint16_t *rFar = nullptr, *rMid = nullptr, *rFg = nullptr;
uint8_t  *rFarA = nullptr, *rMidA = nullptr, *rFgA = nullptr;
uint16_t  rLut[256], rSky[SCR_H];
uint8_t   rRgb[256][3];
bool      rReady = false;
int       rLoc = 0, rShown = 0;
float     rFeet = 0, rFrom = 0;
uint32_t  rMoveAt = 0, rMoveMs = 0, rArriveAt = 0, rEnterAt = 0, rSlashAt = 0;
int8_t    rDir = 1;
struct RMote { float x, y, vx, vy, ph; };
RMote rEmber[28], rAsh[14];

static inline int rGY(int i) { return RW_GY0 - i * RW_S; }
static inline float rCamOfLoc(int i) { return rGY(i) - 112.0f; }

bool roninInit() {
  if (rReady) return true;
  if (!rWorld) rWorld = (uint8_t *)ps_malloc(RW_W * RW_H);
  if (!rFarI)  rFarI  = (uint8_t *)ps_malloc(RW_W * RF_H);
  if (!rMidI)  rMidI  = (uint8_t *)ps_malloc(RW_W * RM_H);
  if (!rFgI)   rFgI   = (uint8_t *)ps_malloc(RW_W * RG_H);
  if (!rFar)   rFar   = (uint16_t *)ps_malloc(240 * RF_H * 2 * 2);
  if (!rFarA)  rFarA  = (uint8_t *)ps_malloc(240 * RF_H * 2);
  if (!rMid)   rMid   = (uint16_t *)ps_malloc(240 * RM_H * 2 * 2);
  if (!rMidA)  rMidA  = (uint8_t *)ps_malloc(240 * RM_H * 2);
  if (!rFg)    rFg    = (uint16_t *)ps_malloc(240 * RG_H * 2 * 2);
  if (!rFgA)   rFgA   = (uint8_t *)ps_malloc(240 * RG_H * 2);
  if (!qTmp)   qTmp   = (uint8_t *)ps_malloc(Q_TMP_BYTES);
  if (!rWorld || !rFarI || !rMidI || !rFgI || !rFar || !rFarA || !rMid || !rMidA || !rFg || !rFgA || !qTmp) return false;
  qRle(RW_RLE, rWorld, RW_W * RW_H);
  qRle(RF_RLE, rFarI, RW_W * RF_H);
  qRle(RM_RLE, rMidI, RW_W * RM_H);
  qRle(RG_RLE, rFgI, RW_W * RG_H);
  for (int i = 0; i < RPAL_N; i++) { rRgb[i][0] = RPAL[i][0]; rRgb[i][1] = RPAL[i][1]; rRgb[i][2] = RPAL[i][2]; rLut[i] = c565(rRgb[i][0], rRgb[i][1], rRgb[i][2]); }
  qBakeLayer(rFarI, RW_W, RF_H, 0, false, 2, rFar, rFarA, rRgb);
  qBakeLayer(rMidI, RW_W, RM_H, 0, false, 1, rMid, rMidA, rRgb);
  qBakeLayer(rFgI, RW_W, RG_H, 0, false, 3, rFg, rFgA, rRgb);
  for (int y = 0; y < SCR_H; y++) {                    // night sky, glowing red low down from the fires
    float t = y / (float)(SCR_H - 1);
    float r = t < 0.55f ? qLerp(4, 22, t / 0.55f) : qLerp(22, 96, (t - 0.55f) / 0.45f);
    float g = t < 0.55f ? qLerp(3, 8, t / 0.55f) : qLerp(8, 22, (t - 0.55f) / 0.45f);
    float b = t < 0.55f ? qLerp(12, 26, t / 0.55f) : qLerp(26, 24, (t - 0.55f) / 0.45f);
    rSky[y] = c565((uint8_t)r, (uint8_t)g, (uint8_t)b);
  }
  rReady = true;
  return true;
}

// the light flickers a little with the fires
void rFlicker() {
  float t = millis() / 1000.0f;
  float f = 0.93f + 0.04f * sinf(t * 7.3f) + 0.03f * sinf(t * 13.1f + 1);
  for (int i = 0; i < RPAL_N; i++) {
    if (RPAL[i][3] == 2) continue;
    rLut[i] = c565((uint8_t)min(255, (int)(rRgb[i][0] * f)), (uint8_t)min(255, (int)(rRgb[i][1] * f)), (uint8_t)min(255, (int)(rRgb[i][2] * f)));
  }
}

void rBlitRows(const uint16_t *col, const uint8_t *al, int th, int off, bool wrap) {
  for (int y = 0; y < SCR_H; y++) {
    int fy = y + off;
    if (wrap) { fy %= th; if (fy < 0) fy += th; }
    else if (fy < 0 || fy >= th) continue;
    uint16_t *dst = FB + y * SCR_W; const uint16_t *s = col + fy * SCR_W; const uint8_t *a = al + fy * SCR_W;
    for (int x = 0; x < SCR_W; x++) { uint8_t v = a[x]; if (!v) continue; dst[x] = v > 247 ? s[x] : blend565(dst[x], s[x], v); }
  }
}
void rBlitWorld(int camR) {
  for (int y = 0; y < SCR_H; y++) {
    int wy = (camR + y) >> 1;
    if (wy < 0 || wy >= RW_H) continue;
    const uint8_t *src = rWorld + wy * RW_W; uint16_t *dst = FB + y * SCR_W;
    for (int lx = 0; lx < RW_W; lx++) { uint8_t v = src[lx]; if (v) { uint16_t c = rLut[v]; dst[lx * 2] = c; dst[lx * 2 + 1] = c; } }
  }
}

void rMoon() {
  const int mx = 182, my = 58;
  qGlow(mx, my, 60, c565(200, 40, 30), 90);
  qGlow(mx, my, 30, c565(255, 90, 60), 120);
  for (int y = -17; y <= 17; y++) for (int x = -17; x <= 17; x++) {
    int d2 = x * x + y * y; if (d2 > 17 * 17) continue;
    float l = clamp01(0.75f - (x + y) / 40.0f);
    uint16_t c = mix565(c565(130, 20, 22), c565(255, 116, 86), (uint8_t)(l * 255));
    if ((x + 30) * 7 % 11 == 0 && (y + 30) % 5 == 0) c = qMul32(c, 26);
    FB[(my + y) * SCR_W + mx + x] = c;
  }
  float t = millis() / 1000.0f;                       // smoke drifting across it
  for (int k = 0; k < 2; k++) {
    int cy = my - 8 + k * 14, cx = (int)fmodf(t * (8 + k * 5) + k * 90, 300) - 30;
    for (int y = -3; y <= 3; y++) qRectA(cx - 40 + abs(y) * 4, cy + y, 80 - abs(y) * 8, 1, c565(14, 8, 16), 120);
  }
}

void rFireArrows() {
  uint32_t ms = millis();
  for (int k = 0; k < 2; k++) {
    uint32_t cyc = (ms + k * 4700) % 9500;
    if (cyc > 1600) continue;
    float p = cyc / 1600.0f;
    int x0 = k ? 250 : -10, dir = k ? -1 : 1;
    for (int s = 0; s < 10; s++) {
      float q = p - s * 0.012f; if (q < 0) break;
      int x = x0 + dir * (int)(q * 260), y = 110 - (int)(sinf(q * 3.14159f) * 70) + k * 30;
      uint16_t c = s < 2 ? c565(255, 240, 170) : s < 5 ? c565(255, 170, 60) : c565(220, 70, 20);
      qPxA(x & ~1, y & ~1, c, (uint8_t)(255 - s * 22));
      if (s == 0) qGlow(x, y, 10, c565(255, 140, 50), 160);
    }
  }
}

void rFlames(int sx, int sy, int size, float t, int seed) {
  qGlow(sx, sy - size, size * 5, c565(255, 110, 40), 110 + (int)(sinf(t * 11 + seed) * 25));
  for (int i = 0; i < 6; i++) {
    float p = fmodf(t * 1.8f + i * 0.31f + seed * 0.17f, 1.0f);
    int x = sx - size + (i * 7 + seed) % (2 * size + 1) + (int)(sinf(t * 9 + i + seed) * 2);
    int y = sy - (int)(p * size * 5);
    uint16_t c = p < 0.25f ? rLut[RC_F3] : p < 0.5f ? rLut[RC_F2] : p < 0.75f ? rLut[RC_F1] : rLut[RC_F0];
    int s = p < 0.5f ? 4 : 2;
    qRect(x & ~1, y & ~1, s, s, c);
  }
  for (int i = 0; i < 4; i++) {                        // dark smoke rising off it
    float p = fmodf(t * 0.35f + i * 0.25f + seed * 0.11f, 1.0f);
    int x = sx + (int)(sinf(p * 5 + seed) * 6 + p * 20), y = sy - size * 4 - (int)(p * 90);
    int r = 3 + (int)(p * 6);
    for (int j = -r; j <= r; j += 2) for (int i2 = -r; i2 <= r; i2 += 2)
      if (i2 * i2 + j * j <= r * r) qPxA((x + i2) & ~1, (y + j) & ~1, c565(30, 22, 30), (uint8_t)(140 * (1 - p)));
  }
}

void rSamurai(int sx, int sy, int frame, bool flip) {
  const uint16_t *lut = rReady ? rLut : qIcons();
  const uint8_t *f = RSAM + frame * RSAM_W * RSAM_H;
  for (int j = 0; j < RSAM_H; j++) for (int i = 0; i < RSAM_W; i++) {
    uint8_t v = f[j * RSAM_W + (flip ? RSAM_W - 1 - i : i)];
    if (v) qPx(sx + i * 2, sy + j * 2, rReady ? lut[v] : c565(RPAL[v][0], RPAL[v][1], RPAL[v][2]));
  }
}
void rFlag(int sx, int sy) {                           // the banner on his back, fluttering
  float t = millis() / 1000.0f;
  qRect(sx + 4, sy, 2, 30, rLut[RC_K]);
  for (int j = 0; j < RFLAG_H; j++) for (int i = 0; i < RFLAG_W; i++) {
    uint8_t v = RFLAG[j * RFLAG_W + i];
    if (v) qPx(sx + 6 + i * 2, sy + j * 2 + ((int)(sinf(t * 7 - i * 0.9f) * 1.5f)) * 2, rLut[v]);
  }
}

void rMotes(int camR) {
  float t = millis() / 1000.0f;
  for (int i = 0; i < 28; i++) {
    RMote &m = rEmber[i];
    if (m.ph == 0 || m.y < -6) {
      m.x = random(0, 240); m.y = m.ph == 0 ? random(0, 280) : random(250, 300);
      if (RD_FIRES_N && random(0, 3) == 0) { int f = random(0, RD_FIRES_N); m.x = RD_FIRES[f][0] * 2 + random(-10, 11); m.y = RD_FIRES[f][1] * 2 - camR; }
      m.vx = random(-30, 31) / 100.0f; m.vy = -random(40, 140) / 100.0f; m.ph = random(1, 628) / 100.0f;
    }
    m.x += m.vx + sinf(t * 2 + m.ph) * 0.5f; m.y += m.vy;
    float b = 0.6f + 0.4f * sinf(t * 9 + m.ph * 4);
    qGlow((int)m.x, (int)m.y, 5, c565(255, 120, 40), (int)(220 * b));
    qRect((int)m.x & ~1, (int)m.y & ~1, 2, 2, i % 3 ? c565(255, 190, 90) : c565(255, 240, 190));
  }
  for (int i = 0; i < 14; i++) {
    RMote &a = rAsh[i];
    if (a.ph == 0 || a.y > 284) { a.x = random(0, 240); a.y = a.ph == 0 ? random(0, 280) : -4; a.vx = random(-20, 21) / 100.0f; a.vy = random(20, 60) / 100.0f; a.ph = random(1, 628) / 100.0f; }
    a.x += a.vx + sinf(t + a.ph) * 0.4f; a.y += a.vy;
    qRectA((int)a.x & ~1, (int)a.y & ~1, 2, 2, c565(120, 110, 120), 140);
  }
}

void rDynamic(int camR, bool pet) {
  float t = millis() / 1000.0f;
  for (int i = 0; i < RD_FIRES_N; i++) {
    int sx = RD_FIRES[i][0] * 2, sy = RD_FIRES[i][1] * 2 - camR;
    if (sy > -60 && sy < SCR_H + 120) rFlames(sx, sy, 5, t, i * 7);
  }
  for (int i = 0; i < RD_LANTERNS_N; i++) {
    int sx = RD_LANTERNS[i][0] * 2, sy = RD_LANTERNS[i][1] * 2 - camR;
    if (sy > -30 && sy < SCR_H + 30) qGlow(sx, sy, 22, c565(255, 150, 70), 110 + (int)(sinf(t * 6 + i) * 20));
  }
  for (int i = 0; i < RD_FOXEYES_N; i++) {
    int sx = RD_FOXEYES[i][0] * 2, sy = RD_FOXEYES[i][1] * 2 - camR;
    if (sy < -10 || sy > SCR_H + 10) continue;
    bool blink = (frameNo + i * 37) % 120 < 4;
    if (!blink) { qRect(sx, sy, 2, 2, c565(255, 200, 80)); qGlow(sx + 1, sy + 1, 10, c565(255, 120, 30), 200); }
  }
  { int sx = RD_BEACON[0] * 2, sy = RD_BEACON[1] * 2 - camR; if (sy > -40 && sy < SCR_H + 160) rFlames(sx + 2, sy + 6, 9, t, 3); }
  { int sx = RD_DRUM[0] * 2, sy = RD_DRUM[1] * 2 - camR;
    if (jbPlaying && sy > -40 && sy < SCR_H + 40) { int ph = (millis() / 120) % 6; gfx->drawCircle(sx, sy, 24 + ph * 4, c565(255, 120, 60)); qGlow(sx, sy, 34, c565(255, 90, 40), 80); } }
  if (pet) {
    int sx = RD_PET[0] * 2, sy = RD_PET[1] * 2 - camR;
    if (sy > -10 && sy < SCR_H + 30) {
      const char *const *spr = petSpecies == 1 ? GHOST : petSpecies == 2 ? FROG : CAT;
      int hop = (int)(fabsf(sinf(t * 3)) * 4) & ~1;
      drawSprite(spr, sx - 12, sy - 24 - hop, 2, mix565(petColor, COL_BLACK, 110), (frameNo % 70) < 3);
    }
  }
}

// The whole scene with the camera at world row camY. sam: draw the samurai.
void rScene(float camY, bool sam, bool pet = true) {
  if (!roninInit()) { gfx->fillScreen(c565(10, 6, 14)); return; }
  rFlicker();
  int camR = (int)floorf(camY * 2);
  for (int y = 0; y < SCR_H; y++) { uint16_t c = rSky[y], *p = FB + y * SCR_W; for (int x = 0; x < SCR_W; x++) p[x] = c; }
  rMoon();
  float camMax = rCamOfLoc(0);
  int farOff = (int)floorf(((camY - camMax) * 0.12f + RF_H - 140) * 2);
  rBlitRows(rFar, rFarA, RF_H * 2, farOff, false);
  { int sy = RD_CASTLE[1] * 2 - farOff; qGlow(RD_CASTLE[0] * 2, sy, 40, c565(255, 90, 30), 90 + (int)(sinf(millis() / 160.0f) * 25)); }
  rFireArrows();
  rBlitRows(rMid, rMidA, RM_H * 2, (int)floorf(camY * 0.45f * 2), true);
  rBlitWorld(camR);
  rDynamic(camR, pet);
  if (sam) {
    uint32_t ms = millis();
    bool moving = rMoveAt && ms - rMoveAt < rMoveMs;
    int sx = 52 * 2, sy = (int)floorf((rFeet - 25) * 2) - camR;
    if (rEnterAt) {                                    // walking off toward the landmark
      float p = min(1.0f, (ms - rEnterAt) / 380.0f);
      bool left = rShown % 2 == 0;
      sx += (left ? -1 : 1) * (int)(p * 40) & ~1;
      rFlag(sx + (left ? 14 : -6), sy - 4);
      rSamurai(sx, sy, 2 + (int)(p * 8) % 4, left);
    } else if (rSlashAt && ms - rSlashAt < 520) {      // the sword comes out
      bool left = rShown % 2 == 0;
      float p = (ms - rSlashAt) / 520.0f;
      const uint8_t *f = RSLASH;
      for (int j = 0; j < RSLASH_H; j++) for (int i = 0; i < RSLASH_W; i++) {
        uint8_t v = f[j * RSLASH_W + (left ? RSLASH_W - 1 - i : i)];
        if (v) qPx(sx - (left ? 24 : 0) + i * 2, sy + j * 2, rLut[v]);
      }
      int cx = sx + (left ? -10 : 42), cy = sy + 30;             // a white arc where the blade went
      for (int a = 0; a < 18; a++) {
        float ang = -1.2f + a * 0.14f, r = 26 + (a % 3);
        if (a / 18.0f > p * 1.6f) break;
        int x = cx + (left ? -1 : 1) * (int)(cosf(ang) * r), y = cy + (int)(sinf(ang) * r);
        qPxA(x & ~1, y & ~1, c565(255, 255, 255), (uint8_t)(255 * (1 - p)));
      }
      if (p < 0.3f) qGlow(cx, cy, 40, c565(255, 240, 220), (int)(200 * (1 - p / 0.3f)));
    } else if (moving) {
      bool up = rDir > 0;
      int frame = (up ? 6 : 8) + ((int)(fabsf(rFeet) / 7) % 2);
      if (up) { rSamurai(sx, sy, frame, false); rFlag(sx + 9, sy - 10); }
      else { rFlag(sx + 9, sy - 10); rSamurai(sx, sy, frame, false); }
    } else {
      bool left = rShown % 2 == 0;
      int frame = (ms / 700) % 2;
      rFlag(sx + (left ? 14 : -6), sy - 4);
      rSamurai(sx, sy, frame, left);
    }
  }
  rMotes(camR);
  rBlitRows(rFg, rFgA, RG_H * 2, (int)floorf(camY * 1.3f * 2), true);
  float fl = 0.8f + 0.2f * sinf(millis() / 90.0f);                  // the glow of fires below
  for (int y = 200; y < SCR_H; y += 2) {
    int a = (int)((y - 200) * 0.45f * fl);
    uint16_t *r0 = FB + y * SCR_W, *r1 = r0 + SCR_W;
    for (int x = 0; x < SCR_W; x++) { r0[x] = qAdd(r0[x], c565(255, 70, 30), a); r1[x] = qAdd(r1[x], c565(255, 70, 30), a); }
  }
  qDof();
  qVignette();
}

void rBackdrop(uint8_t m) {
  int l = qLocOfMode(m);
  rScene(rCamOfLoc(l < 0 ? 0 : l), false, m != M_PET);
}

void roninPlace(int loc) {
  rLoc = rShown = constrain(loc, 0, RW_N - 1);
  rFeet = rGY(rLoc); rMoveAt = 0; rArriveAt = millis(); rEnterAt = 0;
}
void roninGo(int dir) {
  int to = constrain(rLoc + dir, 0, RW_N - 1);
  if (to == rLoc) { toast(dir > 0 ? "The summit" : "The foot of the mountain", 900); return; }
  rFrom = rFeet; rLoc = to; rDir = dir; rMoveAt = millis();
  rMoveMs = (uint32_t)(260 + fabsf(rGY(rLoc) - rFrom) * 7.5f);
  rArriveAt = 0; rEnterAt = 0;
  sfx(0);
}
void roninEnter() { if (!rEnterAt) { rEnterAt = millis(); sfx(4); } }
void roninSlash() { rSlashAt = millis(); sfx(3); }

void drawRoninHome() {
  if (!roninInit()) { gfx->fillScreen(c565(10, 6, 14)); qTextC(QF_BODY, "Not enough memory", 120, 130, Q_WHITE); return; }
  uint32_t ms = millis();
  if (rMoveAt) {
    float p = min(1.0f, (ms - rMoveAt) / (float)rMoveMs);
    float e = p < 0.5f ? 2 * p * p : 1 - powf(-2 * p + 2, 2) / 2;
    rFeet = rFrom + (rGY(rLoc) - rFrom) * e;
    if (p >= 1) { rMoveAt = 0; rArriveAt = ms; rShown = rLoc; }
  }
  if (rEnterAt && ms - rEnterAt > 400) { rEnterAt = 0; openModule(QLOC_MODE[rShown]); return; }
  rScene(rFeet - 112, true);
  qHud();
  if (rMoveAt) return;
  float s = min(1.0f, (ms - rArriveAt) / 220.0f); s = 1 - (1 - s) * (1 - s);
  int y = (284 - (int)(s * 60)) & ~1;
  const char *name = RLOC_NAME[rShown];
  int w = max(max(qTextW(QF_TITLE, name), qTextW(QF_BODY, QLOC_SUB[rShown])) + 36, 132);
  qWindow(120 - w / 2, y, w, 56);
  qTextC(QF_TITLE, name, 120, y + 3, Q_WHITE, 2, Q_SHADOW);
  qTextC(QF_BODY, QLOC_SUB[rShown], 120, y + 30, Q_DIM);
  int bob = ((ms / 300) % 2) * 2;
  if (rShown < RW_N - 1) qArrow(120, 44 - bob, 2, Q_WHITE);
  if (rShown > 0 && s >= 1) qArrow(120, 212 + bob, 0, Q_WHITE);
  bool left = rShown % 2 == 0;
  if (!rEnterAt && s >= 1) qArrow(left ? 80 - bob : 160 + bob, 196, left ? -1 : 1, Q_GOLD);
}

// charging: he kneels and rests on the landing below the dojo
void rCharge() {
  if (!roninInit()) { gfx->fillScreen(c565(10, 6, 14)); return; }
  float cam = rCamOfLoc(9);
  rScene(cam, false, true);
  int camR = (int)floorf(cam * 2);
  int sx = 102, sy = (rGY(9) - RSEATED_H + 1) * 2 - camR;
  for (int j = 0; j < RSEATED_H; j++) for (int i = 0; i < RSEATED_W; i++) { uint8_t v = RSEATED[j * RSEATED_W + i]; if (v) qPx(sx + i * 2, sy + j * 2, rLut[v]); }
  for (int i = 0; i < 3; i++) {
    float p = fmodf(millis() / 1800.0f + i / 3.0f, 1.0f);
    qPxA(sx + 20 + (int)(sinf(p * 6 + i) * 8) & ~1, sy - 6 - (int)(p * 40) & ~1, c565(200, 190, 210), (uint8_t)(160 * (1 - p)));
  }
  qHud();
  qWindow(20, 52, 200, 96);
  qPlate(charging ? (batPct >= 99 ? "FULLY RESTED" : "RESTING") : "ON THE ROAD", 120, 44);
  char b[16]; snprintf(b, sizeof(b), "%d%%", batPct);
  qTextC(QF_BIG, b, 120, 58, charging ? c565(255, 200, 120) : Q_WHITE, 2, Q_SHADOW);
  float f = batPct / 100.0f;
  qBar(36, 114, 168, f, charging ? c565(230, 80, 60) : c565(250, 196, 70), c565(255, 200, 160));
  if (charging) { int sp = (int)((millis() / 12) % 168) & ~1; if (sp < (int)(164 * f)) qRect(38 + sp, 116, 4, 6, c565(255, 255, 255)); }
  snprintf(b, sizeof(b), "%.2fV", batVolts);
  qTextC(QF_BODY, b, 120, 126, Q_DIM);
}

void roninBoot() {
  roninInit();
  roninPlace(rLoc);
  rScene(rCamOfLoc(rLoc), false);
  for (int i = 0; i < SCR_W * SCR_H; i++) FB[i] = qMul32(FB[i], 18);
  qTextC(QF_TITLE, "Larp Charm", 120, 96, c565(240, 70, 56), 3, Q_SHADOW);
  qTextC(QF_BODY, "The long climb", 120, 150, c565(230, 200, 170), 2, Q_SHADOW);
  qTextC(QF_BODY, "Press start", 120, 220, Q_WHITE, 2, Q_SHADOW);
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
    case M_EYES:  drawEyes();  break;
    case M_JUKE:  drawJuke();  break;
    case M_TORCH: drawTorch(); break;
    case M_CHARGE: drawCharge(); break;
    default:      if (skin == SKIN_RONIN) drawRoninHome(); else if (skin == SKIN_QUEST) drawQuestHome(); else if (skin) drawAeroHome(); else drawDeck(); break;
  }
}

void render() {
  updateAccent();
  drawScene();
  if (PIXEL_SKIN) questPost();
  if (mode == M_CLOCK || mode == M_PET || mode == M_TOY || mode == M_CARD) drawTicker();
  bool fx = !skin && fxOn;
  if (fx && mode != M_PHOTO && mode != M_CLIP && mode != M_MUSIC && mode != M_TORCH) scanlines();
  if (glitchFrames && !skin) { glitchFx(5); glitchFrames--; }
  else if (fx && mode == M_DECK && random(0, 220) == 0) glitchFrames = 2;
  drawAlarm();
  drawTransferOverlay();
  drawToast();
  wakeFx();
  gfx->flush();
  frameNo++;
}

void setMode(int m, bool announce = true) {
  (void)announce;
  uint8_t old = mode;
  mode = ((m % M_COUNT) + M_COUNT) % M_COUNT;
  if (mode != M_CHARGE && mode != M_TORCH && !idleEyesOn) prefs.putUChar("mode", mode);
  if (old == M_TORCH && mode != M_TORCH && !sleeping) analogWrite(LCD_BL, brightness);
  if (mode == M_TORCH && old != M_TORCH) { analogWrite(LCD_BL, 255); chromeUntil = millis() + 2500; }
  if (mode == M_EYES && old != M_EYES) { chromeUntil = millis() + 1500; tiltBaseX = tiltX; tiltBaseY = tiltY; nextIdleExpr = millis() + 4000; exprShown = 255; }
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

// Aero: a bubble grows out of the orb until it fills the screen (or shrinks back into it).
void aeroZoom(int oi, bool opening) {
  if (!FB || !prevFb || sleeping) return;
  int sx = oi >= 0 ? orbX[oi] : 120, sy = oi >= 0 ? orbY[oi] : 140, sr = oi >= 0 ? orbR[oi] : 20;
  memcpy(prevFb, FB, FB_BYTES);
  uint16_t col = oi >= 0 ? ORB_COL[oi] : ACC;
  const int steps = 7;
  for (int s = 1; s <= steps; s++) {
    float t = (float)s / steps;
    t = 1.0f - (1.0f - t) * (1.0f - t);
    if (!opening) t = 1.0f - t;
    int r = sr + (int)((200 - sr) * t), cx = sx + (int)((120 - sx) * t), cy = sy + (int)((140 - sy) * t);
    memcpy(FB, prevFb, FB_BYTES);
    gfx->fillCircle(cx, cy, r, col);
    gfx->drawCircle(cx, cy, r, COL_WHITE);
    gfx->drawCircle(cx, cy, r - 1, COL_WHITE);
    gfx->flush();
  }
}

void openModule(int m) {
  if (mode == M_DECK) { sfx(1); if (PIXEL_SKIN) questTransitionOut(); else if (skin) aeroZoom(orbOfMode(m), true); else zoomAnim(tileOfMode(m), true); }
  setMode(m);
  lastFrame = 0;
}

void closeModule() {
  if (mode == M_DECK) return;
  if (mode == M_CHARGE) { setMode(chargeReturn == M_CHARGE ? M_DECK : chargeReturn); lastFrame = 0; return; }
  int ti = tileOfMode(mode), oi = orbOfMode(mode);
  sfx(1);
  if (PIXEL_SKIN) {
    int ql = qLocOfMode(mode);
    questTransitionOut();
    if (ql >= 0) { if (skin == SKIN_RONIN) roninPlace(ql); else questPlace(qLoc - qMod(qLoc, QW_NLOC) + ql); }
    setMode(M_DECK);
    lastFrame = 0;
    return;
  }
  if (skin == SKIN_AERO && oi >= 0) orbitFocus(oi, true);
  setMode(M_DECK);
  updateAccent();
  drawScene();
  if (skin) aeroZoom(oi, false); else zoomAnim(ti, false);
  glitchFrames = 1;
  lastFrame = 0;
}

// The idle eyes (an option): when the sleep timer runs out, the eyes take over and doze off first.
void idleEyesEnd() {
  if (!idleEyesOn) return;
  idleEyesOn = false;
  exprAuto = true;
  setMode(idleReturn);
  if (!sleeping) analogWrite(LCD_BL, mode == M_TORCH ? 255 : brightness);
  lastFrame = 0;
}

// ───────────── Status notifications ─────────────
void notifyText(const char *s) {
  if (!statusChar || !bleConnected) return;
  statusChar->setValue((const uint8_t *)s, strlen(s));
  statusChar->notify();
}

void sendStatus(const char *extra = nullptr) {
  char buf[300];
  unsigned long used = fsOK ? (unsigned long)FFat.usedBytes() : 0;
  unsigned long total = fsOK ? (unsigned long)FFat.totalBytes() : 0;
  snprintf(buf, sizeof(buf), "b=%d;v=%.2f;c=%d;m=%d;fw=%s;fs=%lu,%lu;clip=%lu;lk=%d,%d;imu=%d;toy=%d;th=%d;sk=%d;av=%d;st=%lu;eye=%d;jb=%d;q=%d%s%s",
           batPct, batVolts, charging ? 1 : 0, mode, FW_VERSION, used, total, (unsigned long)clipId,
           bgStyle, faceStyle, imuOK ? 1 : 0, toyKind, theme, skin, aeroVar, (unsigned long)stepsToday,
           exprAuto ? 255 : exprSet, jbPlaying ? jbSong : -1, jbQLen,
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
  if (xferKind == 4 && fsOK) FFat.remove("/s/tmp.part");
  xferActive = false;
  xferBufLen = 0;
}

void xferFlush() {
  if ((xferKind == 2 || xferKind == 4) && xferFile && xferBufLen) { xferFile.write(xferBuf, xferBufLen); xferBufLen = 0; }
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
  else if (xferKind == 4) {                        // a song: written to /s/<slot>.sng
    int slot = -1;
    for (int i = 0; i < MY_SONGS; i++) if (!mySongHas[i]) { slot = i; break; }
    if (!fsOK) why = "nofs";
    else if (slot < 0) why = "full";
    else if (xferSize <= sizeof(SongHdr) || xferSize > sizeof(SongHdr) + MY_SONG_MAX * 2) why = "bad";
    else { xferId = slot; FFat.remove("/s/tmp.part"); xferFile = FFat.open("/s/tmp.part", "w"); ok = (bool)xferFile && xferBuf; if (!ok) why = "open"; }
  }
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
  else if (xferKind == 4) { memcpy(xferBuf + xferBufLen, p, take); xferBufLen += take; if (xferBufLen > XBUF_SZ - 520) xferFlush(); }
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
  } else if (xferKind == 4) {
    xferFlush();
    xferFile.close();
    File f = FFat.open("/s/tmp.part", "r");
    SongHdr h;
    bool ok = f && f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && memcmp(h.magic, "LSNG", 4) == 0 && h.n > 0 && h.n <= MY_SONG_MAX && f.size() >= sizeof(h) + h.n * 2;
    if (f) f.close();
    if (!ok) { xferCleanup(); sendStatus("xfer=err;why=format"); return; }
    FFat.remove(mySongPath(xferId));
    FFat.rename("/s/tmp.part", mySongPath(xferId));
    xferActive = false;
    scanMySongs();
    jbCat = MY_CAT; jbSelId = 100 + xferId;
    toast(skin ? "Song added" : "song added", 1200);
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
void setSkin(uint8_t s);
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
        if (len >= 4) { soundsOn = p[3] & 1; shakeWake = p[3] & 2; hourChime = p[3] & 4; fxOn = p[3] & 8; eyesIdle = p[3] & 16; tiltSpin = p[3] & 32; prefs.putUChar("flags", p[3] & 63); }
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

    case CMD_SKIN:
      if (len >= 1) {
        if (len >= 2 && p[1] % AERO_VARS != aeroVar) { aeroVar = p[1] % AERO_VARS; prefs.putUChar("avar", aeroVar); if ((p[0] % SKIN_COUNT) == skin) toast(AERO_NAMES[aeroVar], 900); }
        if ((p[0] % SKIN_COUNT) != skin) setSkin(p[0] % SKIN_COUNT);
        sendStatus();
      }
      break;

    case CMD_EYES:
      if (len >= 1) {
        if (p[0] >= EXPR_COUNT) exprAuto = true; else setExpr(p[0], 10000);
        if (mode != M_EYES) openModule(M_EYES);
      }
      break;

    case CMD_TUNE:
      if (len >= 1) {
        if (p[0] == 0) jbStop();
        else if (p[0] == 3) jbSkip();
        else {
          if (p[0] == 1) jbStop();
          for (int i = 1; i < len; i++) jbEnqueue(p[i]);
          if (p[0] == 1 && mode != M_JUKE) openModule(M_JUKE);
        }
        sendStatus();
      }
      break;

    case CMD_TORCH:
      if (len >= 1) {
        if (len >= 2) torchCol = p[1] & 3;
        if (p[0] && mode != M_TORCH) openModule(M_TORCH);
        else if (!p[0] && mode == M_TORCH) closeModule();
      }
      break;

    case CMD_SONG_LIST:
      for (int i = 0; i < MY_SONGS; i++) if (mySongHas[i]) {
        char b[64]; snprintf(b, sizeof(b), "song=%d,%u,%s", 100 + i, mySongN[i], mySongName[i]); notifyText(b); delay(20);
      }
      sendStatus("songs=end");
      break;

    case CMD_SONG_DELETE:
      if (len >= 1 && p[0] >= 100 && p[0] < 100 + MY_SONGS && fsOK) {
        if (jbSong == p[0]) jbStop();
        FFat.remove(mySongPath(p[0] - 100));
        scanMySongs();
        if (jbCat >= catCount()) jbCat = 0;
        if (!songExists(jbSelId)) jbSelId = 0;
      }
      sendStatus("songs=changed");
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

void cycleAero(int dir) {
  aeroVar = (aeroVar + AERO_VARS + dir) % AERO_VARS;
  prefs.putUChar("avar", aeroVar);
  toast(AERO_NAMES[aeroVar], 900);
  sfx(1);
}

void setSkin(uint8_t s) {
  skin = s % SKIN_COUNT;
  if (skin == SKIN_QUEST && questInit()) { qLightUpdate(true); int l = qLocOfMode(mode); questPlace(l >= 0 ? l : qMod(qLoc, QW_NLOC)); }
  if (skin == SKIN_RONIN && roninInit()) { int l = qLocOfMode(mode); roninPlace(l >= 0 ? l : rLoc); }
  prefs.putUChar("skin", skin);
  if (skin == SKIN_AERO) { orbitFocus(max(0, orbOfMode(mode)), true); buildOrbSprites(); buildHomeBase(); }
  glitchFrames = skin == SKIN_DECK ? 5 : 0;
  snapAccent();
  toast(skin == SKIN_RONIN ? "Ronin" : skin == SKIN_QUEST ? "Quest" : skin == SKIN_AERO ? "Aero" : "Deck", 900);
  sfx(2);
  lastFrame = 0;
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
      if (skin == SKIN_QUEST) { questGo(dir); break; }
      if (skin == SKIN_RONIN) break;
      if (skin == SKIN_DECK) {
        theme = (theme + THEME_COUNT + dir) % THEME_COUNT;
        prefs.putUChar("theme", theme);
        themePreviewUntil = millis() + 2500;
        toast(THEME_NAMES[theme], 900);
        glitchFrames = 2;
        sfx(1);
      }
      break;
    case M_CLOCK: if (skin == SKIN_AERO) cycleAero(dir); else cycleBg(dir); break;
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
    case M_EYES: {
      uint8_t e = (exprShown + EXPR_COUNT + dir) % EXPR_COUNT;
      setExpr(e, 8000);
      toast(EXPR_NAMES[e], 800);
      sfx(4);
      break;
    }
    case M_JUKE:
      if (skin == SKIN_AERO) { jbSelId = stepSong((jbPlaying && millis() > jbSelUntil) ? jbSong : jbSelId, dir); jbSelUntil = millis() + 4000; sfx(0); }
      else { jbCat = (jbCat + catCount() + dir) % catCount(); sfx(1); }
      break;
    case M_TORCH: torchCol = (torchCol + 4 + dir) % 4; break;
    default: break;
  }
}

// Swipe down closes whatever is open. On the home screen, up and down change brightness.
void roninGo(int dir);
void onVSwipe(int up) {
  if (mode == M_DECK && skin == SKIN_RONIN) { roninGo(up > 0 ? 1 : -1); return; }
  if (up < 0 && mode != M_DECK) { closeModule(); return; }
  if (mode == M_TORCH) return;
  setBacklight(brightness + (up > 0 ? 40 : -40));
  prefs.putUChar("bright", brightness);
  toast(up > 0 ? "bright +" : "bright -", 500);
}

void onTap(bool dbl) {
  sfx(0);
  switch (mode) {
    case M_DECK:
      if (skin == SKIN_RONIN) {
        if (rMoveAt) roninGo(tch.y0 < 140 ? 1 : -1);
        else if (tch.y0 < 92) roninGo(1);
        else if (tch.y0 > 196) roninGo(-1);
        else roninEnter();
        break;
      }
      if (skin == SKIN_QUEST) {
        if (qWalkAt) questGo(tch.x0 < 120 ? -1 : 1);
        else if (tch.x0 < 66) questGo(-1);
        else if (tch.x0 > 174) questGo(1);
        else questEnter();
        break;
      }
      if (skin == SKIN_DECK) {
        int ti = tileAt(tch.x0, tch.y0);
        if (ti >= 0) openModule(TILE_MODE[ti]);
      } else {
        for (int k = ORB_N - 1; k >= 0; k--) {
          int i = orbOrder[k], dx = tch.x0 - orbX[i], dy = tch.y0 - orbY[i], rr = orbR[i] + 5;
          if (dx * dx + dy * dy <= rr * rr) {
            if (i == orbFront && !orbitGoalOn) openModule(ORB_MODE[i]); else orbitFocus(i, false);
            return;
          }
        }
        if (tch.y0 < 128) openModule(M_CLOCK);
        else if (tch.y0 > 240) openModule(ORB_MODE[orbFront]);
      }
      break;
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
    case M_EYES:
      if (dbl) setExpr(E_LOVE, 3500);
      else {
        static const uint8_t R[5] = {E_HAPPY, E_LAUGH, E_SURPRISED, E_HAPPY, E_WINK};
        setExpr(R[random(0, 5)], 2500);
        blinkAt = millis();
      }
      sfx(4);
      break;
    case M_JUKE:
      if (PIXEL_SKIN) { questJukeTap(tch.x0, tch.y0); break; }
      if (skin == SKIN_AERO) {
        int show = (jbPlaying && millis() > jbSelUntil) ? jbSong : jbSelId;
        if (tch.y0 < 148) { if (jbPlaying) jbStop(); else jbStartSong(jbSelId); }
        else if (tch.y0 < 200) { if (tch.x0 < 60) { jbSelId = stepSong(show, -1); jbSelUntil = millis() + 4000; } else if (tch.x0 > 180) { jbSelId = stepSong(show, 1); jbSelUntil = millis() + 4000; } }
        else { bool was = jbPlaying; jbEnqueue(jbSelId); toast(was ? "Added to the queue" : "Playing", 800); }
      } else {
        int16_t ids[JB_ROWS];
        int n = songsInCat(jbCat, ids, JB_ROWS), row = (tch.y0 - JB_ROW_Y) / JB_ROW_H;
        if (tch.y0 < JB_ROW_Y - 2) { jbCat = (jbCat + catCount() + (tch.x0 < 120 ? -1 : 1)) % catCount(); }
        else if (tch.y0 < 190 && row >= 0 && row < n) { bool was = jbPlaying; jbSelId = ids[row]; jbEnqueue(ids[row]); toast(was ? "queued" : "playing", 700); }
        else if (tch.y0 >= 190 && jbPlaying) { if (tch.x0 > 170) jbSkip(); else jbStop(); }
        else if (tch.y0 >= 190) jbStartSong(jbSelId);
      }
      break;
    case M_TORCH: torchCol = (torchCol + 1) % 4; chromeUntil = 0; break;
    case M_CHARGE: closeModule(); break;
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
      if (skin == SKIN_QUEST) { questJump(); break; }
      if (skin == SKIN_RONIN) { roninSlash(); break; }
      // fall through
    case M_CLOCK: if (skin == SKIN_AERO) cycleAero(1); else cycleBg(1); break;
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
    case M_EYES: setExpr(E_DIZZY, 3000); sfx(3); break;
    case M_JUKE: { int16_t all[48]; int n = allSongs(all, 48); jbSelId = all[random(0, n)]; jbStop(); jbStartSong(jbSelId); toast(skin == SKIN_AERO ? "Shuffle" : "shuffle", 800); break; }
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
      if (idleEyesOn) { touchActivity(); tch.consumed = true; return; } // and wakes you out of the idle eyes
      touchActivity();
    }
    tch.x = x; tch.y = y;
    lastInteraction = now;
    int dx = x - tch.x0, dy = y - tch.y0;
    // Aero home: sideways drags spin the orbit, live under the finger
    if (mode == M_DECK && skin == SKIN_AERO && !tch.consumed && !orbitDrag && abs(dx) > 8 && abs(dx) > abs(dy)) {
      orbitDrag = true; orbitGoalOn = false; tch.consumed = true; orbitLastX = tch.x0; orbitLastT = now; orbitFlick = 0; orbitV = 0;
    }
    if (orbitDrag) {
      float d = (x - orbitLastX) * 0.012f;
      orbitA += d;
      uint32_t ms = now - orbitLastT;
      if (x != orbitLastX && ms) { orbitFlick = orbitFlick * 0.4f + 0.6f * (d * 33.3f / ms); orbitLastT = now; }
      orbitLastX = x;
      return;
    }
    if (tch.consumed) return;
    if (abs(dx) >= SWIPE_PX && abs(dx) > abs(dy)) { tch.consumed = true; onSwipe(dx < 0 ? 1 : -1); return; }
    if (abs(dy) >= SWIPE_PX && abs(dy) > abs(dx)) { tch.consumed = true; onVSwipe(dy < 0 ? 1 : -1); return; }
    if (now - tch.t0 > LONGPRESS_MS && abs(dx) < TAP_PX && abs(dy) < TAP_PX) {
      tch.consumed = true;
      if (mode == M_DECK) setSkin((skin + 1) % SKIN_COUNT);                     // press and hold the home screen: switch look
      else if (mode == M_CARD) closeModule();
      else openModule(M_CARD);                                    // anywhere else: the ID card
    }
  } else if (tch.down) {
    tch.down = false;
    lastTouchUp = now;
    if (orbitDrag) {                           // let go: keep spinning with the finger's speed, unless it stopped first
      orbitDrag = false;
      orbitV = (now - orbitLastT > 90) ? 0 : constrain(orbitFlick, -0.45f, 0.45f);
      return;
    }
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
  if (lastBatRead && now - lastBatRead < 1000) return;
  lastBatRead = now;
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(BAT_ADC);
  float v = (mv / 16) * 3.0f / 1000.0f;
  vFast = (vHistN == 0) ? v : vFast * 0.5f + v * 0.5f;
  batVolts = (vHistN == 0) ? v : batVolts * 0.85f + v * 0.15f;

  // The board has no "charger connected" pin. USB power shows up two ways:
  //  - a computer: the USB port reports a host straight away
  //  - a plain charger: the battery voltage jumps up ~0.1 V within a few seconds of plugging in, and drops the same way when unplugged
  // Anything else is just the battery, so nothing slow or guessed counts as charging any more.
  float lo = vFast, hi = vFast;
  for (int i = 0; i < min(vHistN, 6); i++) { lo = min(lo, vHist[i]); hi = max(hi, vHist[i]); }
  if (vHistN >= 3 && !wallPower && vFast - lo > 0.07f) { wallPower = true; batVolts = vFast; }
  else if (wallPower && hi - vFast > 0.07f) { wallPower = false; batVolts = vFast; }
  vHist[vHistN % 6] = vFast;
  vHistN++;
  if (now - lastSlowAt >= 10000) {                 // a charger never lets the voltage sag for a whole minute
    lastSlowAt = now;
    vSlow[vSlowN % 6] = vFast; vSlowN++;
    if (wallPower && vSlowN >= 6 && vSlow[(vSlowN - 6) % 6] - vFast > 0.03f) wallPower = false;
  }

  bool usb = Serial.isPlugged();
  bool wasCharging = charging;
  charging = usb || wallPower;

  // the charger lifts the voltage, so read a little lower while charging
  int pct = voltsToPct(charging ? batVolts - 0.10f : batVolts);
  batPct = constrain(pct, 0, 100);

  // plugged in: show the charging screen. Unplugged: go back to what was open.
  if (charging && !wasCharging) {
    touchActivity();
    if (mode != M_CHARGE && !xferActive) { chargeReturn = mode == M_TORCH ? M_DECK : mode; setMode(M_CHARGE); }
  }
  if (!charging && wasCharging) {
    if (mode == M_CHARGE) setMode(chargeReturn);
    toast(skin ? "Unplugged" : "unplugged", 1200);
  }
  static bool lowWarned = false, fullWarned = false;
  if (!charging && batPct <= 15 && !lowWarned && vHistN > 3) { lowWarned = true; toast(skin ? "Battery low" : "battery low", 2500); }
  if (batPct >= 20) lowWarned = false;
  if (charging && batPct >= 100 && !fullWarned) { fullWarned = true; toast(skin ? "Fully charged" : "fully charged", 2500); }
  if (!charging) fullWarned = false;
}

void updatePet() {
  static uint32_t lastStepChk = 0;
  if (millis() - lastStepChk > 5000) { lastStepChk = millis(); stepDayCheck(); }
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
    jbStop();
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
  if (hourChime && !jbPlaying && now - lastChk > 1000) {
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
  if (skin) return;
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
  if (PIXEL_SKIN) {
    gfx->fillScreen(skin == SKIN_RONIN ? c565(10, 6, 14) : c565(14, 18, 56));
    gfx->flush();
  } else if (skin) {
    verticalGradient(c565(30, 120, 230), c565(190, 236, 255));
    aaTextC(F_L, "Larp Charm", 120, 150, COL_WHITE, true);
    gfx->flush();
  } else {
    textAt("LARP//OS", 24, 22, 2, ACC);
    textAt("v" FW_VERSION, 130, 29, 1, COL_WHITE);
    gfx->drawFastHLine(24, 42, 192, ACC_DIM);
  }
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
  aeroBg   = (uint16_t *)ps_malloc(FB_BYTES);
  homeBase = (uint16_t *)ps_malloc(FB_BYTES);
  jbNotesBuf = (uint8_t *)ps_malloc(MY_SONG_MAX * 2);
  initAeroBubbles();
  bootLine("memory", (photoBuf && artBuf && artJpg && prevFb && frameBuf) ? "PSRAM ok" : "low", photoBuf && artBuf && artJpg && prevFb && frameBuf);
  xferBuf  = (uint8_t *)malloc(XBUF_SZ);
  if (!xferBuf) xferBuf = (uint8_t *)ps_malloc(XBUF_SZ);

  fsOK = FFat.begin(true);
  if (!fsOK) Serial.println("FFat mount failed - check Partition Scheme (16M Flash 3MB APP/9.9MB FATFS)");
  if (fsOK) {
    if (!FFat.exists("/c")) FFat.mkdir("/c");
    if (!FFat.exists("/s")) FFat.mkdir("/s");
    FFat.remove("/s/tmp.part");
    FFat.remove("/c/tmp.part");
    File f = FFat.open("/photo.bin", "r");
    if (f && photoBuf && f.size() == FB_BYTES) { f.read((uint8_t *)photoBuf, FB_BYTES); photoLoaded = true; }
    if (f) f.close();
    if (!openClip(clipId)) openNextClip(0);
  }
  scanMySongs();
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

  if (skin == SKIN_RONIN) roninBoot();
  else if (skin == SKIN_QUEST) questBoot();
  else if (skin) {
    buildOrbSprites();
    buildHomeBase();
    aeroBackdrop();
    glossOrb(120, 104, 44, ACC);
    aaTextC(F_L, "Larp Charm", 120, 190, COL_WHITE, true);
    aaTextC(F_S, "Hello again", 120, 214, AERO_INK);
  } else textAt("JACKING IN", 24, bootY + 12, 2, COL_WHITE);
  gfx->flush();
  sfx(2);
  delay(skin ? 700 : 450);
  lastInteraction = millis();
  if (charging) { chargeReturn = mode; setMode(M_CHARGE, false); }
  else setMode(mode, false);
  if (skin == SKIN_AERO) orbitFocus(max(0, orbOfMode(chargeReturn)), true);
  if (skin == SKIN_QUEST) { int l = qLocOfMode(mode); if (l >= 0) questPlace(l); }
  if (skin == SKIN_RONIN) { int l = qLocOfMode(mode); if (l >= 0) roninPlace(l); }
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

  jbService();
  uint32_t idleLimit = mode == M_TORCH ? 300000UL : sleepSec * 1000UL;   // the torch stays on for 5 minutes
  if (!sleeping && sleepSec > 0 && !xferActive && !alarmUntil && millis() - lastInteraction > idleLimit) {
    if (eyesIdle && !idleEyesOn && mode != M_TORCH && mode != M_EYES && mode != M_CHARGE) {
      idleReturn = mode;
      setMode(M_EYES);
      idleEyesOn = true;
      setExpr(E_SLEEPY, 600000);
      chromeUntil = 0;
      analogWrite(LCD_BL, max(10, brightness / 3));
      lastInteraction = millis();
    } else {
      if (idleEyesOn) { idleEyesOn = false; exprAuto = true; setMode(idleReturn); }
      if (mode == M_TORCH) setMode(M_DECK);
      sleeping = true;
      analogWrite(LCD_BL, 0);
    }
  }

  if (bleConnected && (millis() - lastStatus > 15000 || jbDirty)) { lastStatus = millis(); jbDirty = false; sendStatus(); }

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
  if (mode == M_DECK && skin == SKIN_AERO && orbitMoving()) interval = 15;      // spin as smoothly as the screen allows
  if (millis() - lastFrame >= interval) {
    lastFrame = millis();
    render();
  }
}

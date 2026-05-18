#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFi.h>
#include <SpotifyEsp32.h>
#include <SPI.h>
#include "secrets.h"

// ===== TFT PINOUT =====
#define TFT_CS   6
#define TFT_RST  7
#define TFT_DC   8
#define TFT_SCLK 3
#define TFT_MOSI 10

// ===== BUTTONS =====
#define BTN_PREV  4
#define BTN_PAUSE 0
#define BTN_NEXT  1

#define LED_PIN   2

// ===== POLLING INTERVAL =====
#define SPOTIFY_POLL_MS 5000

// ===== COLOURS =====
#define C_BG        ST77XX_BLACK
#define C_ACCENT    0x07FF   // cyan
#define C_ARTIST    0xFFFF   // white
#define C_TRACK     0xFD20   // orange-yellow
#define C_DIM       0x4208   // dark grey
#define C_GREEN     0x07E0
#define C_BAR_FG    0x07FF
#define C_BAR_BG    0x2104

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

const char* SSID          = SECRET_SSID;
const char* PASSWORD      = SECRET_PASS;
const char* CLIENT_ID     = SECRET_ID;
const char* CLIENT_SECRET = SECRET_APIPASS;
const char* REFRESH_TOKEN = SECRET_TOKEN;

Spotify sp(CLIENT_ID, CLIENT_SECRET, REFRESH_TOKEN);

// ===== state =====
String  lastArtist    = "";
String  lastTrackname = "";
bool    lastPlaying   = false;
unsigned long lastPollMs = 0;

// ===== debounce =====
const unsigned long debounceDelay = 180;

struct Button {
  uint8_t       pin;
  bool          lastReading;
  bool          stableState;
  unsigned long lastChange;
};

Button btnPrev  = {BTN_PREV,  HIGH, HIGH, 0};
Button btnPause = {BTN_PAUSE, HIGH, HIGH, 0};
Button btnNext  = {BTN_NEXT,  HIGH, HIGH, 0};

bool updateButton(Button &b) {
  bool reading = digitalRead(b.pin);
  if (reading != b.lastReading) {
    b.lastChange  = millis();
    b.lastReading = reading;
  }
  if ((millis() - b.lastChange) > debounceDelay) {
    if (reading != b.stableState) {
      b.stableState = reading;
      if (b.stableState == LOW) return true;   // falling edge
    }
  }
  return false;
}

bool isValidSpotifyStr(const String &s) {
  return s.length() > 0 && s != "null" && s != "Something went wrong";
}

// ===== UI helpers =====

// Truncate a String to fit within `maxW` pixels at the current text size
String truncate(const String &s, int maxW, uint8_t textSize) {
  int charW = 6 * textSize;
  int maxChars = maxW / charW;
  if ((int)s.length() <= maxChars) return s;
  return s.substring(0, maxChars - 1) + "\x7E"; // trailing ~
}

void drawDivider(int y) {
  tft.drawFastHLine(4, y, tft.width() - 8, C_DIM);
}

// Draw a "now playing" icon (three animated bars) — static version
void drawNowPlayingIcon(int x, int y, bool playing) {
  uint16_t col = playing ? C_ACCENT : C_DIM;
  // Three bars of different heights
  int heights[3] = {8, 5, 10};
  for (int i = 0; i < 3; i++) {
    int bx = x + i * 5;
    int bh = playing ? heights[i] : 3;
    tft.fillRect(bx, y + (10 - bh), 3, bh, col);
  }
}

// Draw a play or pause icon
void drawPlayPauseIcon(int x, int y, bool playing) {
  tft.fillRect(x, y, 20, 14, C_BG);   // clear area
  if (playing) {
    // Pause: two rectangles
    tft.fillRect(x,     y, 5, 14, C_ACCENT);
    tft.fillRect(x + 9, y, 5, 14, C_ACCENT);
  } else {
    // Play: triangle (approximated with filled rows)
    for (int row = 0; row < 14; row++) {
      int half  = row / 2;
      int wid   = (row < 7) ? (row + 1) : (13 - row + 1);
      int start = (14 - wid) / 2;
      tft.drawFastHLine(x + row / 2, y + row, wid, C_ACCENT);
    }
    // Cleaner filled triangle via column fills
    tft.fillRect(x, y, 20, 14, C_BG);
    for (int col = 0; col < 14; col++) {
      int top = col / 2;
      int bot = 14 - top;
      tft.drawFastVLine(x + col, y + top, bot - top, C_ACCENT);
    }
  }
}

// Full screen redraw (called on boot and when track changes)
void drawUI(const String &artist, const String &track, bool playing) {
  tft.fillScreen(C_BG);

  // ── Header bar 
  tft.fillRect(0, 0, tft.width(), 16, 0x0320);   // very dark teal
  tft.setCursor(6, 4);
  tft.setTextColor(C_ACCENT);
  tft.setTextSize(1);
  tft.print("\x0E ");     // music note glyph (font char 14)
  tft.print("NOW PLAYING");

  // ── Divider
  drawDivider(17);

  // ── Artist name
  tft.setCursor(6, 22);
  tft.setTextColor(C_ARTIST);
  tft.setTextSize(1);
  tft.print("ARTIST");

  tft.setCursor(6, 33);
  tft.setTextColor(C_ARTIST);
  tft.setTextSize(1);
  tft.print(truncate(artist, tft.width() - 12, 1));

  drawDivider(45);

  // ── Track name
  tft.setCursor(6, 50);
  tft.setTextColor(C_DIM);
  tft.setTextSize(1);
  tft.print("TRACK");

  tft.setCursor(6, 61);
  tft.setTextColor(C_TRACK);
  tft.setTextSize(1);
  tft.print(truncate(track, tft.width() - 12, 1));

  drawDivider(75);

  // ── Play/pause indicator
  drawPlayPauseIcon(6, 82, playing);

  // ── Animated bars indicator 
  drawNowPlayingIcon(tft.width() - 26, 82, playing);

  // ── Button hint footer 
  tft.fillRect(0, tft.height() - 12, tft.width(), 12, 0x0320);
  tft.setTextColor(C_DIM);
  tft.setTextSize(1);
  tft.setCursor(4, tft.height() - 10);
  tft.print("|< ");
  tft.setTextColor(C_ACCENT);
  tft.print("II");
  tft.setTextColor(C_DIM);
  tft.print(" >|");
}

// Partial artist refresh (avoids full redraw flicker)
void updateArtist(const String &artist) {
  tft.fillRect(0, 33, tft.width(), 11, C_BG);
  tft.setCursor(6, 33);
  tft.setTextColor(C_ARTIST);
  tft.setTextSize(1);
  tft.print(truncate(artist, tft.width() - 12, 1));
}

void updateTrack(const String &track) {
  tft.fillRect(0, 61, tft.width(), 11, C_BG);
  tft.setCursor(6, 61);
  tft.setTextColor(C_TRACK);
  tft.setTextSize(1);
  tft.print(truncate(track, tft.width() - 12, 1));
}

void updatePlayState(bool playing) {
  drawPlayPauseIcon(6, 82, playing);
  drawNowPlayingIcon(tft.width() - 26, 82, playing);
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN,   OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(BTN_PREV,  INPUT_PULLUP);
  pinMode(BTN_PAUSE, INPUT_PULLUP);
  pinMode(BTN_NEXT,  INPUT_PULLUP);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(3);
  tft.fillScreen(C_BG);

  tft.setTextColor(C_ACCENT);
  tft.setTextSize(2);
  tft.setCursor(14, 40);
  tft.print("Spotify");
  tft.setTextSize(1);
  tft.setTextColor(C_DIM);
  tft.setCursor(28, 62);
  tft.print("connecting...");

  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  tft.fillRect(0, 62, tft.width(), 10, C_BG);
  tft.setTextColor(C_GREEN);
  tft.setCursor(14, 62);
  tft.print(WiFi.localIP().toString());

  sp.begin();
  while (!sp.is_auth()) {
    sp.handle_client();
  }

  digitalWrite(LED_PIN, HIGH);

  drawUI("---", "---", false);
}

// ===== LOOP =====
void loop() {
  sp.handle_client();

  if (updateButton(btnPrev)) {
    sp.skip_to_previous();
    lastPollMs = 0;   // force immediate refresh
  }

  if (updateButton(btnPause)) {
    if (sp.is_playing()) {
      sp.pause_playback();
      lastPlaying = false;
    } else {
      sp.start_a_users_playback();
      lastPlaying = true;
    }
    updatePlayState(lastPlaying);
  }

  if (updateButton(btnNext)) {
    sp.skip_to_next();
    lastPollMs = 0;   // force immediate refresh
  }

  // ── Throttled Spotify poll 
  unsigned long now = millis();
  if (now - lastPollMs < SPOTIFY_POLL_MS) return;
  lastPollMs = now;

  String currentArtist    = sp.current_artist_names();
  String currentTrackname = sp.current_track_name();
  bool   currentPlaying   = sp.is_playing();

  bool artistChanged = isValidSpotifyStr(currentArtist)    && currentArtist    != lastArtist;
  bool trackChanged  = isValidSpotifyStr(currentTrackname) && currentTrackname != lastTrackname;
  bool stateChanged  = currentPlaying != lastPlaying;

  // Full redraw only when both artist and track change (new song)
  if (artistChanged && trackChanged) {
    lastArtist    = currentArtist;
    lastTrackname = currentTrackname;
    lastPlaying   = currentPlaying;
    drawUI(lastArtist, lastTrackname, lastPlaying);
  } else {
    if (artistChanged) { lastArtist = currentArtist;       updateArtist(lastArtist); }
    if (trackChanged)  { lastTrackname = currentTrackname; updateTrack(lastTrackname); }
    if (stateChanged)  { lastPlaying = currentPlaying;     updatePlayState(lastPlaying); }
  }
}
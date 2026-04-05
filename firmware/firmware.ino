#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFi.h>
#include <SpotifyEsp32.h>
#include <SPI.h>

#define TFT_CS 1
#define TFT_RST 2
#define TFT_DC 3
#define TFT_SCLK 4
#define TFT_MOSI 5

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

char* SSID = "INSERT_SSID_HERE";
char* PASSWORD = "INSERT_PASSWORD_HERE";

const char* CLIENT_ID = "CLIENT_ID";
const char* CLIENT_SECRET = "CLIENT_SECRET";

Spotify sp(CLIENT_ID, CLIENT_SECRET);

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  tft.initR(INITR_BLACKTAB); // the type of screen
  tft.setRotation(1); // this makes the screen landscape! remove this line for portrait
  Serial.println("TFT Initialized!");
  tft.fillScreen(ST77XX_BLACK); // make sure there is nothing in the buffer

  WiFi.begin(SSID, PASSWORD); // Attempt to connect to wifi
  Serial.print("Connecting to WiFi..."); // This will print into the console!
  while(WiFi.status() != WL_CONNECTED) // Checking if the wifi has connected
  {
    delay(1000);
    Serial.print("."); // show a loading dot every second
  }
  Serial.printf("\nConnected!\n"); // Wifi connected!

  tft.setCursor(0,0); // make the cursor at the top left
  tft.write(WiFi.localIP().toString().c_str()); // print out IP on the screen

  sp.begin();
  while(!sp.is_auth())
  {
    sp.handle_client();
  }
  Serial.println("Connected to Spotify!");

}

void loop() {
  // put your main code here, to run repeatedly:
  String currentArtist = sp.current_artist_names();
    String currentTrackname = sp.current_track_name();

    if (lastArtist != currentArtist && currentArtist != "Something went wrong" && !currentArtist.isEmpty()) {
        tft.fillScreen(ST77XX_BLACK);
        lastArtist = currentArtist;
        Serial.println("Artist: " + lastArtist);
        tft.setCursor(10,10);
        tft.write(lastArtist.c_str());
    }

    if (lastTrackname != currentTrackname && currentTrackname != "Something went wrong" && currentTrackname != "null") {
        lastTrackname = currentTrackname;
        Serial.println("Track: " + lastTrackname);
        tft.setCursor(10,40);
        tft.write(lastTrackname.c_str());
    }
    delay(2000);
}


#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"

#define TFT_DC 4
#define TFT_CS 15
#define TFT_MOSI 13
#define TFT_CLK 14
#define TFT_MISO 12
#define TFT_RST 2

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
const char *ssid = "06-281";
const char *password = "ofthefuture";

#define GEN_ENUM(v) v
#define GEN_STRING(v) #v

#define STATES(g) \
  g(BOOT), \
  g(SETUP), \
  g(CONNECT_AP), \
  g(CONNECTING_AP), \
  g(CONNECTED_AP), \
  g(CONNECT_CLIENT), \
  g(CONNECTING_CLIENT), \
  g(CONNECTED_CLIENT), \
  g(MAX)


class FSM {
  public:
    enum State {
      STATES(GEN_ENUM)
    };

    static const char* StateStr[MAX + 1];
};

const char* FSM::StateStr[MAX + 1] = {
  STATES(GEN_STRING)
};

uint8_t state, last_state;
String try_ssid, try_password;
uint8_t try_connect;

ESP8266WebServer server(80);

const char NO_CONNECT_PAGE[] PROGMEM =
  "<html>"
  "  <body>"
  "    <h1>Standalone Mode</h1>"
  "    <h2>Connect to AP</h2>"
  "    <form action=\"/connect\" method=\"get\">"
  "      AP: <input type=\"text\" name=\"ap\"><br/>"
  "      Password: <input type=\"text\" name=\"password\"></br>"
  "      <input type=\"submit\" value=\"Submit\">"
  "    </form>"
  "  </body>"
  "</html>";

void handleRoot() {
  server.send(200, "text/html", NO_CONNECT_PAGE);
}

void handleConnect() {
  String result;
  try_ssid = server.arg("ap");
  try_password = server.arg("password");

  result = "<html><h1>Got " + try_ssid + ": " + try_password + "</h1></html>";
  server.send(200, "text/html", result);
  try_connect = true;
}

void setup() {
  Serial.begin(9600);
  Serial.println("Setup");

  last_state = FSM::BOOT;
  state = FSM::SETUP;
  try_connect = false;

  tft.begin();
  tft.setRotation(1);
  tft.setTextColor(ILI9341_YELLOW); tft.setTextSize(2);

  tft.fillScreen(ILI9341_BLACK);
  tft.println("Startup");
}

void loop() {
  if (try_connect) {
    if (state == FSM::CONNECTED_AP) {
      //WiFi.softAPdisconnect();
    }
    state = FSM::CONNECT_CLIENT;
    try_connect = true;
  }

  if (state == FSM::SETUP) {
    WiFi.softAP(ssid, password);
    server.on("/", handleRoot);
    server.on("/connect", handleConnect);

    server.begin();
    state = FSM::CONNECTING_AP;
  }
  else if (state == FSM::CONNECT_AP || state == FSM::CONNECTING_AP) {
    IPAddress myIP = WiFi.softAPIP();
    tft.print("AP Created: ");
    tft.println(myIP);
    state = FSM::CONNECTED_AP;
  }
  else if (state == FSM::CONNECT_CLIENT) {
    WiFi.begin(try_ssid.c_str(), try_password.c_str());
    state = FSM::CONNECTING_CLIENT;
  }
  else if (state == FSM::CONNECTING_CLIENT) {
    uint8_t status = WiFi.waitForConnectResult();
    if(status == WIFI_STA) {
      state = FSM::CONNECTED_CLIENT;
      tft.print("Connected: ");
      tft.println(WiFi.localIP());
    } else {
      state = FSM::CONNECT_AP;
    }
  }
  else if (state == FSM::CONNECTED_CLIENT) {
    if(WiFi.status() == WL_CONNECTION_LOST) {
      try_connect = true;
      state = FSM::CONNECT_CLIENT;
    }
  }

  server.handleClient();
  Serial.println(WiFi.status());
  
  if (last_state != state) {
    tft.print(FSM::StateStr[last_state]);
    tft.print(" -> ");
    tft.println(FSM::StateStr[state]);
    last_state = state;
  }
}
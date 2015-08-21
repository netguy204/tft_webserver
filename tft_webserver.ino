
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <Wstring.h>

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
  g(INTERNET), \
  g(NOINTERNET), \
  g(FETCHING), \
  g(MAX)


// persisted info
struct Info {
  char ssid[32];
  char password[32];
  char message[512];
  char msghost[32];
  char msgpath[32];
  int8_t login_valid : 1;
};

class FSM {
  public:
    enum State {
      STATES(GEN_ENUM)
    };

    static const char* StateStr[MAX + 1];
    static Info info;
};

const char* FSM::StateStr[MAX + 1] = {
  STATES(GEN_STRING)
};


Info FSM::info;

void readInfo() {
  Info* infop = &FSM::info;
  uint8_t* infob = (uint8_t*)infop;
  
  for(uint16_t ii = 0; ii < sizeof(Info); ++ii) {
    infob[ii] = EEPROM.read(ii);
  }
}

void writeInfo() {
  Info* infop = &FSM::info;
  uint8_t* infob = (uint8_t*)infop;
  
  for(uint16_t ii = 0; ii < sizeof(Info); ++ii) {
    EEPROM.write(ii, infob[ii]);
  }
  EEPROM.commit();
}

void urldecode2(char *dst, const char *src)
{
  char a, b;
  while (*src) {
    if ((*src == '%') &&
      ((a = src[1]) && (b = src[2])) &&
      (isxdigit(a) && isxdigit(b))) {
      if (a >= 'a')
        a -= 'a'-'A';
      if (a >= 'A')
        a -= ('A' - 10);
      else
        a -= '0';
      if (b >= 'a')
        b -= 'a'-'A';
      if (b >= 'A')
        b -= ('A' - 10);
      else
        b -= '0';
      *dst++ = 16*a+b;
      src+=3;
    } 
    else if((*src == '+')) {
      *dst++ = ' ';
      src++;
    }
    else {
      *dst++ = *src++;
    }
  }
  *dst++ = '\0';
}


uint8_t state, last_state;

ESP8266WebServer server(80);
WiFiClient client;

const char PAGE_HEADER[] PROGMEM =
  "<html>"
  "  <body>"
  "    <h1>Office Screen</h1>";

const char PAGE_FOOTER[] PROGMEM =
  "  </body>"
  "</html>";
  
const char NEW_MESSAGE_HEADER[] PROGMEM =
  "    <h2>Update Message</h2>"
  "    <form action=\"/message\" method=\"get\">"
  "      Message: <textarea rows=\"4\" cols=\"50\" name=\"message\">";
  
const char NEW_MESSAGE_FOOTER[] PROGMEM =
  "      </textarea><br/>"
  "      <input type=\"submit\" value=\"Submit\">"
  "    </form>";

const char LOGIN_SECTION[] PROGMEM =
  "    <h2>Login to AP</h2>"
  "    <form action=\"/login\" method=\"get\">"
  "      AP Name: <input name=\"ap\"><br/>"
  "      Password: <input name=\"password\"></br/>"
  "      Message HOST: <input name=\"msghost\"><br/>"
  "      Message Path: <input name=\"msgpath\"><br/>"
  "      <input type=\"submit\" value=\"Submit\">"
  "    </form>";

class __FlashStringHelper;
#define FPSTR(pstr_pointer) (reinterpret_cast<const __FlashStringHelper *>(pstr_pointer))

String newMessageChunk() {
  String result = FPSTR(NEW_MESSAGE_HEADER);
  result += FSM::info.message;
  result += FPSTR(NEW_MESSAGE_FOOTER);
  return result;
}

String newLoginChunk() {
  return FPSTR(LOGIN_SECTION);
}

String newMessagePage() {
  String result = FPSTR(PAGE_HEADER);
  result += newMessageChunk();
  if(!FSM::info.login_valid) {
    result += newLoginChunk();
  } else {
    result += "<form action=\"/logout\" method=\"get\">";
    result += "  <input type=\"submit\" value=\"Logout\">";
    result += "</form>";
  }
  result += FPSTR(PAGE_FOOTER);
  return result;
}

void handleRoot() {
  server.send(200, "text/html", newMessagePage());
}

void handleMessage() {
  char msg[512];
  urldecode2(msg, server.arg("message").c_str());
  memcpy(FSM::info.message, msg, sizeof(msg));
  writeInfo();
  
  tft.fillScreen(ILI9341_BLUE);
  tft.setCursor(0, 0);
  tft.print(msg);
  
  server.send(200, "text/html", newMessagePage());
}

void handleLogin() {
  urldecode2(FSM::info.ssid, server.arg("ap").c_str());
  urldecode2(FSM::info.password, server.arg("password").c_str());
  urldecode2(FSM::info.msghost, server.arg("msghost").c_str());
  urldecode2(FSM::info.msgpath, server.arg("msgpath").c_str());

  FSM::info.login_valid = true;
  writeInfo();
  server.send(200, "text/html", "<html><body><h1>Attempting login</h1></body></html>");
}

void handleLogout() {
  FSM::info.login_valid = false;
  writeInfo();
  server.send(200, "text/html", "<html><body><h1>Logging out</h1></body></html>");
}


void handleClearInfo() {
  memset(FSM::info.ssid, 0, sizeof(FSM::info.ssid));
  memset(FSM::info.password, 0, sizeof(FSM::info.password));
  memset(FSM::info.msghost, 0, sizeof(FSM::info.msghost));
  FSM::info.login_valid = false;
  writeInfo();
  ESP.restart();
  server.send(200, "text/html", "<html><body><h1>Clearing info, Restarting...</h1></body></html>");
}

void setup() {
  Serial.begin(9600);
  Serial.println("Setup");

  EEPROM.begin(sizeof(Info));
  readInfo();
  Serial.println("Loaded Info");
  
  last_state = FSM::BOOT;
  state = FSM::SETUP;

  tft.begin();
  tft.setRotation(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);

  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0, 0);
  tft.println("Startup");
}

uint32_t fetch_delay;
void timerReset() {
  fetch_delay = millis();
}
uint32_t timerValue() {
  return millis() - fetch_delay;
}

void loop() {
  if (state == FSM::SETUP) {
    server.on("/", handleRoot);
    server.on("/message", handleMessage);
    server.on("/login", handleLogin);
    server.on("/logout", handleLogout);
    server.on("/clear", handleClearInfo);

    server.begin();
    if(FSM::info.login_valid) {
      state = FSM::CONNECT_CLIENT;
    } else {
      state = FSM::CONNECT_AP;
    }
  }
  else if (state == FSM::CONNECT_AP) {
    WiFi.softAP(ssid, password);
    state = FSM::CONNECTING_AP;
  }
  else if (state == FSM::CONNECTING_AP) {
    IPAddress myIP = WiFi.softAPIP();
    tft.print("AP Created: ");
    tft.println(myIP);
    state = FSM::CONNECTED_AP;
  }
  else if (state == FSM::CONNECTED_AP && FSM::info.login_valid) {
    // reboot which will make us reconnect as a client
    ESP.restart();
  }
  else if (state == FSM::CONNECT_CLIENT) {
    WiFi.begin(FSM::info.ssid, FSM::info.password);
    state = FSM::CONNECTING_CLIENT;
  }
  else if (state == FSM::CONNECTING_CLIENT) {
    uint8_t status = WiFi.waitForConnectResult();
    if(status == WL_CONNECTED) {
      state = FSM::CONNECTED_CLIENT;
      tft.print("Connected: ");
      tft.println(WiFi.localIP());
    } else {
      // failed to connect, invalidate credentials and restart
      FSM::info.login_valid = false;
      writeInfo();
      ESP.restart();
    }
  }
  else if (state == FSM::CONNECTED_CLIENT) {
    if(client.connect(FSM::info.msghost, 80)) {
      timerReset();
      state = FSM::INTERNET;
    } else {
      state = FSM::NOINTERNET;
    }
  }
  else if (state == FSM::INTERNET && !client) {
    // disconnected
    state = FSM::CONNECTED_CLIENT;
    timerReset();
  }
  else if (state == FSM::INTERNET && timerValue() > 1000) {
    client.print(String("GET ") + FSM::info.msgpath + " HTTP/1.1\r\n" +
                 "Host: " + FSM::info.msghost + "\r\n" +
                 "Connection: close\r\n\r\n");
    state = FSM::FETCHING;
    timerReset();
  }
  else if (state == FSM::FETCHING && timerValue() > 1000) {
    while(client.available()) {
      String line = client.readStringUntil('\r');
      Serial.println(line);
    }
    state = FSM::CONNECTED_CLIENT;
    timerReset();
  }
  else if (state == FSM::NOINTERNET && timerValue() > 10000) {
    state = FSM::CONNECTED_CLIENT;
  } 
  else if ((state == FSM::INTERNET || state == FSM::NOINTERNET) && !FSM::info.login_valid) {
    // reboot into an AP
    ESP.restart();
  }

  server.handleClient();

  
  if (last_state != state) {
    tft.print(FSM::StateStr[last_state]);
    tft.print(" -> ");
    tft.println(FSM::StateStr[state]);
    last_state = state;
  }
}

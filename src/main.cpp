#include <Arduino.h>
#include <ESP8266WiFi.h>
//#include <WiFi.h>
#include <DNSServer.h>
#include <FS.h>
//#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <Adafruit_GFX.h>      // include Adafruit graphics library
#include <Adafruit_ST7735.h>   // include Adafruit ST7735 TFT library
#include <Fonts/FreeSansBold24pt7b.h>
//#include <MAX31855.h>
#include <MAX31855soft.h>

// Constants
const char *ssid = "Golem-AP";
const char *password =  "golgolem";
const char *msg_toggle_led = "toggleLED";
const char *msg_get_led = "getLEDState";
const int dns_port = 53;
const int http_port = 80;
const int ws_port = 1337;
const int led_pin = LED_BUILTIN;
const int thermocouple_so = D6;
const int thermocouple_cs = D0;
const int thermocouple_sck = D8;

MAX31855soft myMAX31855(thermocouple_cs, thermocouple_so, thermocouple_sck);

IPAddress apIP(192, 168, 4, 1);

// Globals
DNSServer dnsServer;
AsyncWebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(1337);
char msg_buf[10];
int led_state = 0;
int32_t rawData = 0;
float thermocouple_temp = 0;
uint32_t tft_refresh_timestamp = 0;

// ST7735 TFT module connections
#define TFT_RST   D4     // TFT RST pin is connected to NodeMCU pin D4 (GPIO2)
#define TFT_CS    D3     // TFT CS  pin is connected to NodeMCU pin D4 (GPIO0)
#define TFT_DC    D2     // TFT DC  pin is connected to NodeMCU pin D4 (GPIO4)
// initialize ST7735 TFT library with hardware SPI module
// SCK (CLK) ---> NodeMCU pin D5 (GPIO14)
// MOSI(DIN) ---> NodeMCU pin D7 (GPIO13)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);


// Callback: receiving any WebSocket message
void onWebSocketEvent(uint8_t client_num,
                      WStype_t type,
                      uint8_t * payload,
                      size_t length) {

  // Figure out the type of WebSocket event
  switch(type) {

    // Client has disconnected
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", client_num);
      break;

    // New client has connected
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(client_num);
        Serial.printf("[%u] Connection from ", client_num);
        Serial.println(ip.toString());
      }
      break;

    // Handle text messages from client
    case WStype_TEXT:

      // Print out raw message
      Serial.printf("[%u] Received text: %s\n", client_num, payload);

      // Toggle LED
      if ( strcmp((char *)payload, "toggleLED") == 0 ) {
        led_state = led_state ? 0 : 1;
        Serial.printf("Toggling LED to %u\n", led_state);
        digitalWrite(led_pin, led_state);

      // Report the state of the LED
      } else if ( strcmp((char *)payload, "getLEDState") == 0 ) {
        sprintf(msg_buf, "%d", led_state);
        Serial.printf("Sending to [%u]: %s\n", client_num, msg_buf);
        webSocket.sendTXT(client_num, msg_buf);

      // Message not recognized
      } else {
        Serial.println("[%u] Message not recognized");
      }
      break;

    // For everything else: do nothing
    case WStype_BIN:
    case WStype_ERROR:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    default:
      break;
  }
}

// Callback: send homepage
void onIndexRequest(AsyncWebServerRequest *request) {
  IPAddress remote_ip = request->client()->remoteIP();
  Serial.println("[" + remote_ip.toString() +
                  "] HTTP GET request of " + request->url());
  request->send(SPIFFS, "/index.html", "text/html");
}

// Callback: send style sheet
void onCSSRequest(AsyncWebServerRequest *request) {
  IPAddress remote_ip = request->client()->remoteIP();
  Serial.println("[" + remote_ip.toString() +
                  "] HTTP GET request of " + request->url());
  request->send(SPIFFS, "/style.css", "text/css");
}

// Callback: send 404 if requested file does not exist
void onPageNotFound(AsyncWebServerRequest *request) {
  IPAddress remote_ip = request->client()->remoteIP();
  Serial.println("[" + remote_ip.toString() +
                  "] HTTP GET request of " + request->url());
  request->send(404, "text/plain", "Not found");
}

void readThermocouple() {
  while (myMAX31855.detectThermocouple() != MAX31855_THERMOCOUPLE_OK)
  {
    switch (myMAX31855.detectThermocouple())
    {
      case MAX31855_THERMOCOUPLE_SHORT_TO_VCC:
        Serial.println(F("Thermocouple short to VCC"));
        break;

      case MAX31855_THERMOCOUPLE_SHORT_TO_GND:
        Serial.println(F("Thermocouple short to GND"));
        break;

      case MAX31855_THERMOCOUPLE_NOT_CONNECTED:
        Serial.println(F("Thermocouple not connected"));
        break;

      case MAX31855_THERMOCOUPLE_UNKNOWN:
        Serial.println(F("Thermocouple unknown error"));
        break;

      case MAX31855_THERMOCOUPLE_READ_FAIL:
        Serial.println(F("Thermocouple read error, check chip & cable"));
        break;
    }
    delay(5000);
  }

  rawData = myMAX31855.readRawData();

  Serial.print(F("Chip ID: "));
  Serial.println(myMAX31855.getChipID(rawData)); //if ChipID != 31855, then you have read fail=2 or unknown error=2000

  Serial.print(F("Cold Junction: "));
  Serial.println(myMAX31855.getColdJunctionTemperature(rawData));

  Serial.print(F("Thermocouple: "));
  thermocouple_temp = myMAX31855.getTemperature(rawData);
  Serial.println(thermocouple_temp);
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("\nGolem setup"));

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(2);
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextWrap(true);
  tft.println("GOLEM");

  //pinMode(LED_BUILTIN, OUTPUT);

  myMAX31855.begin();
  while (myMAX31855.getChipID() != MAX31855_ID)
  {
    Serial.println(F("MAX31855 error")); //(F()) saves string to flash & keeps dynamic memory free
    delay(5000);
  }
  Serial.println(F("MAX31855 OK"));

  // Make sure we can read the file system
  if( !SPIFFS.begin()){
    Serial.println("Error mounting SPIFFS");
    while(1);
  }

  // Start access point
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (WiFi.softAP(ssid, password)) {
    Serial.println();
    Serial.println("AP started");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("Password: ");
    Serial.println(password);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
  }

  // if DNSServer is started with "*" for domain name, it will reply with
  // provided IP to all DNS request
  if (dnsServer.start(dns_port, "*", apIP)){
    Serial.println("DNS captive portal started");
  }

  // On HTTP request for root, provide index.html file
  server.on("/", HTTP_GET, onIndexRequest);
  server.on("/golem", HTTP_GET, onIndexRequest);
  server.on("/generate_204", HTTP_GET, onIndexRequest);
  server.on("/gen_204", HTTP_GET, onIndexRequest);
  server.on("/L0", HTTP_GET, onIndexRequest);
  server.on("/L2", HTTP_GET, onIndexRequest);
  server.on("/ALL", HTTP_GET, onIndexRequest);
  server.on("/bag", HTTP_GET, onIndexRequest);

  // On HTTP request for style sheet, provide style.css
  server.on("/style.css", HTTP_GET, onCSSRequest);

  // Handle requests for pages that do not exist
  server.onNotFound(onPageNotFound);

  // Start web server
  server.begin();
  Serial.print("Webserver started on port ");
  Serial.println(http_port);

  // Start WebSocket server and assign callback
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  Serial.print("Websocketserver started on port ");
  Serial.println(ws_port);

  /*for (int n=0; n<4; n++) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
  }*/
  Serial.println(F("Setup complete"));
}

void loop() {
  if (tft_refresh_timestamp < millis()) {
    tft_refresh_timestamp = millis() + 1000;
    
    readThermocouple();
  
    tft.fillScreen(ST7735_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(ST7735_WHITE);
    tft.setTextWrap(true);
    tft.println("GOLEM");

    tft.setCursor(0, 35);
    tft.setFont(&FreeSansBold24pt7b);
    tft.setTextColor(ST7735_YELLOW);
    tft.print(thermocouple_temp, 0);
    tft.setFont();
  }

  dnsServer.processNextRequest();
  webSocket.loop();
  delay(10);
}
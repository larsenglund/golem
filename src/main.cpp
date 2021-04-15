#include <Arduino.h>
//#include <ESP8266WiFi.h>
#include <WiFi.h>
#include <DNSServer.h>
//#include <FS.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
//#include <Adafruit_GFX.h>      // include Adafruit graphics library
//#include <Adafruit_ST7735.h>   // include Adafruit ST7735 TFT library
//#include <Fonts/Picopixel.h>
//#include <Fonts/FreeSansBold24pt7b.h>
//#include <MAX31855.h>
#include <MAX31855soft.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <SD.h>

// Constants
const char *ssid = "Golem-AP";
const char *password =  "golgolem";
const char *msg_toggle_led = "toggleLED";
const char *msg_get_led = "getLEDState";
const int dns_port = 53;
const int http_port = 80;
const int ws_port = 1337;
const int led_pin = 2;
const int thermocouple_so = 25; //21;
const int thermocouple_cs = 26;
const int thermocouple_sck = 27; //22;
const int kiln_ssr = 27;
const int max_num_segments = 10;
const int minutes_per_pixel = 3;
const int degrees_per_pixel = 50;

SPIClass SDSPI(HSPI);

MAX31855soft myMAX31855(thermocouple_cs, thermocouple_so, thermocouple_sck);

IPAddress apIP(192, 168, 4, 1);

// Globals
DNSServer dnsServer;
AsyncWebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(1337);
char msg_buf[256];
char tmp_buf[64];
int led_state = 0;
int kiln_ssr_state = 0;
int32_t rawData = 0;
float thermocouple_temp = 0;
uint32_t tft_refresh_timestamp = 0;
int num_segments = 5;
int current_segment = 2;
File file_current_state;
int segment_rate[max_num_segments] = {100, 200, 300, 400, 0, 0, 0, 0, 0, 0};
int segment_target[max_num_segments] = {200, 300, 900, 1200, 0, 0, 0, 0, 0, 0};
int segment_time[max_num_segments] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
char profile_name[32] = "no profile";
bool websocket_connected = false;

// ST7735 TFT module connections
/*#define TFT_RST   -1      // D4 TFT RST pin is connected to NodeMCU pin D4 (GPIO2)
#define TFT_CS    5     // D3 TFT CS  pin is connected to NodeMCU pin D4 (GPIO0)
#define TFT_DC    2     // D2 TFT DC  pin is connected to NodeMCU pin D4 (GPIO4)
#define TFT_MOSI  23
#define TFT_MISO  -1                                                                                                                                                                                                                                                                                                                                                                           
#define TFT_SCLK  18*/
// initialize ST7735 TFT library with hardware SPI module
// SCK (CLK) ---> NodeMCU pin D5 (GPIO14)
// MOSI(DIN) ---> NodeMCU pin D7 (GPIO13)
//Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
TFT_eSPI tft = TFT_eSPI();


void sendCurrentState(int client_num) {
  Serial.printf("sendCurrentState to %d\n", client_num);
  int pos = sprintf(msg_buf, "CURR,%s,%d", profile_name, current_segment);
  for (int n=0; n<num_segments; n++) {
    pos += sprintf(&msg_buf[pos], ",%d,%d", segment_rate[n], segment_target[n]);
  }
  Serial.printf("Sending to [%d]: %s\n", client_num, msg_buf);
  webSocket.sendTXT(client_num, msg_buf);
}

void sendProfileList(int client_num) {
  Serial.printf("sendProfileList to %d\n", client_num);
  /*int pos = sprintf(msg_buf, "LIST");
  Dir dir = SPIFFS.openDir("/prog");
  int n=0;
  while (dir.next()) {
    Serial.print("  ");
    Serial.print(dir.fileName());
    Serial.print(" ");
    pos += sprintf(&msg_buf[pos], ",%s", dir.fileName().c_str());
    if(dir.fileSize()) {
        File f = dir.openFile("r");
        Serial.println(f.size());
        f.close();
        n++;
    }
  }
  Serial.printf("Found %d firing profiles\n", n);
  Serial.printf("Sending to [%d]: %s\n", client_num, msg_buf);
  webSocket.sendTXT(client_num, msg_buf);*/
}

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
      websocket_connected = false;
      break;

    // New client has connected
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(client_num);
        Serial.printf("[%u] Connection from ", client_num);
        Serial.println(ip.toString());
        websocket_connected = true;
        sendCurrentState(client_num);
        sendProfileList(client_num);
      }
      break;

    // Handle text messages from client
    case WStype_TEXT:

      // Print out raw message
      Serial.printf("[%u] Received text: '%s'\n", client_num, payload);

      // Toggle kiln
      if ( strncmp((char *)payload, "toggleKiln", 10) == 0 ) {
        kiln_ssr_state = kiln_ssr_state ? 0 : 1;
        Serial.printf("Toggling kiln to %u\n", kiln_ssr_state);
        digitalWrite(kiln_ssr, kiln_ssr_state ? 0 : 1);

      // Report the state of the kiln
      } else if ( strncmp((char *)payload, "getKilnState", 12) == 0 ) {
        sprintf(msg_buf, "%d", led_state);
        Serial.printf("Sending to [%u]: %s\n", client_num, msg_buf);
        webSocket.sendTXT(client_num, msg_buf);

      } else if ( strncmp((char *)payload, "saveProfile", 11) == 0 ) {
        char* tok = strtok((char *)payload, ":");
        tok = strtok(0, ":");
        sprintf(tmp_buf, "/prog/%s", tok);
        tok = strtok(0, ":");
        File file = SPIFFS.open(tmp_buf, "w");
        if (!file) {
          Serial.println("Error opening file for writing");
          //return;
        }
        else {
          Serial.printf("Writing %s to %s", tok, tmp_buf);
          file.print(tok);
        } 

      } else { // Message not recognized
        Serial.printf("[%u] Message not recognized", client_num);
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

void readThermocouple(bool debug = false) {
  while (myMAX31855.detectThermocouple() != MAX31855_THERMOCOUPLE_OK)
  {
    if (!debug) break;
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
  thermocouple_temp = myMAX31855.getTemperature(rawData);

  if (debug) {
    Serial.print(F("Chip ID: "));
    Serial.println(myMAX31855.getChipID(rawData)); //if ChipID != 31855, then you have read fail=2 or unknown error=2000

    Serial.print(F("Cold Junction: "));
    Serial.println(myMAX31855.getColdJunctionTemperature(rawData));

    Serial.print(F("Thermocouple: "));
    Serial.println(thermocouple_temp);
  }
}

void setSegmentColor(int n) {
  if (n == current_segment) {
    tft.setTextColor(TFT_GREEN);
  }
  else if (n < current_segment) {
    tft.setTextColor(TFT_DARKGREEN);
  }
  else if (n > num_segments) {
    tft.setTextColor(TFT_NAVY);
  }
  else {
    tft.setTextColor(TFT_DARKGREY);
  }
}

void drawProfileGraph() {
  int prev_segment_target = 0;
  int segment_time;
  int prev_x = 0;
  int prev_y = 0;
  int new_x, new_y;
  for (int n=0; n<num_segments; n++) {
    if (segment_rate[n] != 0) {
      segment_time = abs(60 * (segment_target[n] - prev_segment_target)/segment_rate[n]);
    }
    else {
      segment_time = 0;
    }
    new_x = prev_x + segment_time/minutes_per_pixel;
    new_y = segment_target[n]/degrees_per_pixel;
    if (n%2 == 1) {}
    tft.drawLine(prev_x, 127-prev_y, new_x, 127-new_y, (n%2 == 1 ? TFT_MAGENTA : TFT_PURPLE));
    prev_x = new_x;
    prev_y = new_y;
    prev_segment_target = segment_target[n];
  }
}


void printDirectory(File dir, int numTabs) {

  while (true) {

    File entry =  dir.openNextFile();

    if (! entry) {

      // no more files

      break;

    }

    for (uint8_t i = 0; i < numTabs; i++) {

      Serial.print('\t');

    }

    Serial.print(entry.name());

    if (entry.isDirectory()) {

      Serial.println("/");

      printDirectory(entry, numTabs + 1);

    } else {

      // files have sizes, directories do not

      Serial.print("\t\t");

      Serial.println(entry.size(), DEC);

      int max_line_len = 32;
      char line[32];
      int i = 0;
      int ln = 1;
      char ch;
      while (true) {
        ch = entry.read();
        line[i++] = ch;
        if (i >= max_line_len-1) {
          Serial.print(F("ERROR: File line to long! "));
          Serial.println(i);
          break;
        }
        if (ch == '\n' || !entry.available()) {
          line[i] = '\0';
          // Print line number.
          Serial.print(ln++);
          Serial.print(": ");
          Serial.print(line);
          if (line[i - 1] != '\n') {
            // Line is too long or last line is missing nl.
            Serial.println(F(" <-- missing nl"));
          }
          i = 0;
        }
        if (!entry.available()) {
          break;
        }
      }

    }

    entry.close();

  }
}



void setup() {
  Serial.begin(115200);
  Serial.println(F("\nGolem setup"));
  //Serial.print(F("LARSTEST: "));
  //Serial.println(LARSTEST);

  //tft.initR(INITR_BLACKTAB);
  //tft.initR(INITR_BLACKTAB);
  #ifdef ST7735_DRIVER 
  Serial.println("ST7735_DRIVER");
  #endif
  #ifdef ST7735_GREENTAB128 
  Serial.println("ST7735_GREENTAB128");
  #endif
  Serial.println(TFT_ESPI_VERSION);
  tft.init();
  //tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextWrap(false);
  //tft.setFont(&Picopixel);
  tft.setTextColor(TFT_WHITE);
  tft.println("Golem setup...");
  //delay(3000);

  SDSPI.begin(14, 32, 13, 15);//sck, miso, mosi, -1);
  if (!SD.begin(15, SDSPI)) {
    Serial.println("Card Mount Failed");
  }
  else {
    File root;
    root = SD.open("/kiln");
    printDirectory(root, 0);
  }

  //pinMode(LED_BUILTIN, OUTPUT);
  pinMode(kiln_ssr, OUTPUT);
  digitalWrite(kiln_ssr, HIGH);
  
  myMAX31855.begin();
  while (myMAX31855.getChipID() != MAX31855_ID)
  {
    Serial.println(F("MAX31855 error")); //(F()) saves string to flash & keeps dynamic memory free
    delay(5000);
  }
  Serial.println(F("MAX31855 OK"));
  readThermocouple(true);

  // Make sure we can read the file system
  /*if( !SPIFFS.begin()){
    Serial.println("Error mounting SPIFFS");
    while(1);
  }
  Serial.println("Listing firing profiles:");
  Dir dir = SPIFFS.openDir("/prog");
  int n=0;
  while (dir.next()) {
    Serial.print("  ");
    Serial.print(dir.fileName());
    Serial.print(" ");
    if(dir.fileSize()) {
        File f = dir.openFile("r");
        Serial.println(f.size());
        f.close();
    1    n++;
    }
  }
  Serial.printf("Found %d firing profiles\n", n);
  file_current_state = SPIFFS.open("/state/current", "a+");
  if (file_current_state.size() == 0) {
    Serial.println("No current state stored");
  }
  else {
    char buffer[32];
    int len = file_current_state.readBytesUntil('\n', buffer, sizeof(buffer)-1);
    buffer[len] = '\0';
    Serial.printf("Last profile was %s", buffer);
  }*/

  // Start access point
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (WiFi.softAP(ssid, password)) {
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

    sprintf(msg_buf, "T %d", (int)(thermocouple_temp+0.5));
    webSocket.broadcastTXT(msg_buf);
  
    tft.fillScreen(ST7735_BLACK);

    tft.setCursor(0, 0);
    //tft.setFont(&Picopixel);
    tft.setTextColor(ST7735_WHITE);
    //tft.println("KILN CELSIUS");

    //tft.setCursor(0, 45);
    //tft.setFont(&FreeSansBold24pt7b);
    tft.setTextSize(3);
    tft.setTextColor(ST7735_YELLOW);
    tft.printf("%4d", (int)(thermocouple_temp+0.5));
    //tft.println("1337");
    tft.setTextSize(1);

    tft.drawLine(72,0,72,32,TFT_GREEN);

    tft.setCursor(74, 0);
    if (kiln_ssr_state == 1) {
      tft.setTextColor(TFT_GREEN);
    }
    else {
      tft.setTextColor(TFT_RED);
    }
    tft.print("HEAT ");
    tft.setTextColor(websocket_connected ? TFT_GREEN : TFT_RED);
    tft.print("WS");
    tft.setCursor(74, 8);
    tft.setTextColor(TFT_GREEN);
    tft.print("PROG");
    tft.setCursor(74, 16);
    tft.printf("%4.1f/%4.1f", 1.2, 2.5);
    tft.setCursor(74, 24);
    tft.printf("%4.1f/%4.1f", 2.2, 4.5);

    tft.drawLine(0,32,128,32,TFT_GREEN);

    tft.setCursor(0, 34);
    tft.print(profile_name);
    tft.setCursor(0, 42);
    tft.setTextColor(TFT_GREEN);
    //tft.setFont(&Picopixel);
    for (int n=1; n<max_num_segments; n+=2) {
      setSegmentColor(n);
      tft.printf("%2d", n);
      if (n == current_segment) {
        tft.setTextColor(TFT_WHITE);
      }
      tft.printf("%4d", segment_rate[n-1]);
      setSegmentColor(n);
      tft.printf("%4d ", segment_target[n-1]);
      setSegmentColor(n+1);
      tft.printf("%2d", n+1);
      if (n+1 == current_segment) {
        tft.setTextColor(TFT_WHITE);
      }
      tft.printf("%4d", segment_rate[n]);
      setSegmentColor(n+1);
      tft.printf("%4d\n", segment_target[n]);
    }

    tft.drawLine(0,82,128,82,ST7735_GREEN);

    drawProfileGraph();
  }

  dnsServer.processNextRequest();
  webSocket.loop();
  delay(10);
}

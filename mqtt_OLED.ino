

/*
It's finally here!
*/

#include <EEPROM.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>

#include <Time.h>         //http://www.arduino.cc/playground/Code/Time
#include <Timezone.h>     //https://github.com/LelandSindt/Timezone

#include <PubSubClient.h> //https://github.com/knolleary/pubsubclient/releases/tag/2.4

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_RESET 15
Adafruit_SSD1306 display(OLED_RESET);

#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

bool spacestate;
bool klok_ok = false;
uint16_t humidity = 0;
uint16_t co2 = 0;

// WiFi settings
char ssid[] = "Luchthaven";  //  your network SSID (name)
char pass[] = "";       // your network password

// Timezone Rules for Europe
// European Daylight time begins on the last sunday of March
// European Standard time begins on the last sunday of October
// I hope i got the correct hour set for the time-change rule (5th parameter in the rule).

TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, +120};    //Daylight time = UTC +2 hours
TimeChangeRule CET = {"CET", Last, Sun, Oct, 3, +60};     //Standard time = UTC +1 hours
Timezone myTZ(CEST, CET);
TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev
time_t utc, local;

// NTP Server settings and preparations
unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "pool.ntp.org";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP udp;

// MQTT Server settings and preparations
const char* mqtt_server = "test.mosquitto.org";
WiFiClient espClient;
PubSubClient client(mqtt_server, 1883, onMqttMessage, espClient);
long lastReconnectAttempt = 0;

void setup()
{
  Serial.begin(115200);
  Serial.println();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);


  display.clearDisplay();
  display.display();
  delay(1);


  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("Connecting");
  display.display();
  delay(1);
  while (WiFi.status() != WL_CONNECTED) {

    display.print(".");
    display.display();
    delay(50);
  }


  Serial.println("WiFi connected");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connected!");
  display.println(WiFi.SSID());
  display.println(WiFi.localIP());
  display.print(WiFi.RSSI());
  display.println(" dBm");
  display.display();
  delay(1);

  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  udp.begin(localPort);
  ntpsync();

  spacestate = HIGH; // so we get display before receiving MQTT Space State message

}

void loop()
{
  if (!client.connected()) {
    Serial.println(".");
    long verstreken = millis();
    if (verstreken - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = verstreken;
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // Client connected
    client.loop();
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  printDate(now());
  display.setCursor(78, 0);
  printTime(now());

  display.setCursor(0,8);
  display.print("hum: ");
  display.print(humidity);
  display.println("%");
  display.print("CO2: ");
  display.print(co2);
  display.println(" ppm");
  
  



    display.display();
    delay(1);
  if (spacestate == HIGH) {

} else {

}

// NTP sync every 3 hours.
if (hour(now()) % 3 == 0 && minute(now()) == 0 && second(now()) == 0) {
  klok_ok = false;
}

if (!klok_ok) {
  ntpsync();
  }
}







boolean reconnect() {
  if (client.connect("ESP-krant")) {
    // Once connected, publish an announcement...
    client.publish("revspace/espkrant", "hello world");
    // ... and resubscribe
    client.subscribe("revspace/state");
    client.loop();
    client.subscribe("revspace/button/nomz");
    client.loop();
    client.subscribe("revspace/button/doorbell");
    client.loop();

    client.subscribe("revspace/sensors/co2");
    client.loop();
    client.subscribe("revspace/sensors/humidity");
    client.loop();

  }
  return client.connected();
}



boolean ntpsync() {

  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server

  delay(500);

  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
    klok_ok = false;
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);

    local = myTZ.toLocal(epoch, &tcr);

    setTime(local);
    klok_ok = true;
  }
}



// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}


void printTime(time_t t)
{

  if (hour(t) < 10) {
    display.print("0");
  }
  display.print(hour(t));
  display.print(":");

  if (minute(t) < 10) {
    display.print("0");
  }
  display.print(minute(t));
  display.print(":");
  if (second(t) < 10) {
    display.print("0");
  }
  display.println(second(t));
}

void printDate(time_t t)
{
  display.print(day(t));
  display.print("-");
  display.print(month(t));
  display.print("-");
  display.print(year(t));
}


void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  uint16_t spaceCnt;
  uint8_t numCnt = 0;
  char bericht[50] = "";

  Serial.print("received topic: ");
  Serial.println(topic);
  Serial.print("length: ");
  Serial.println(length);
  Serial.print("payload: ");

  Serial.println(bericht);
  Serial.println();

  // Lets select a topic/payload handler
  // Some topics (buttons for example) don't need a specific payload handled, just a reaction to the topic. Saves a lot of time!


  // Space State
  if (strcmp(topic, "revspace/state") == 0) {
    for (uint8_t pos = 0; pos < length; pos++) {
      bericht[pos] = payload[pos];
    }

    if (strcmp(bericht, "open") == 0) {
      Serial.println("Revspace is open");
      if (spacestate == LOW) {
        spacestate = HIGH;
      }
    } else {
      // If it ain't open, it's closed! (saves a lot of time doing strcmp).
      Serial.println("Revspace is dicht");
      if (spacestate == HIGH) {
        spacestate = LOW;
      }
    }
  }

  // NOMZ because we are hungry! Lets join the blinking lights parade!
  if (strcmp(topic, "revspace/button/nomz") == 0) {

  }

  // DOORBELL
  if (strcmp(topic, "revspace/button/doorbell") == 0) {

  }

  // CO2 measurements and alerts. Set to Revspace default like Ledbanner
  if (strcmp(topic, "revspace/sensors/co2") == 0) {
    char num[4] = "";
    spaceCnt = 0;
    numCnt = 0;

    while (((uint8_t)payload[spaceCnt] != 32) && (spaceCnt <= length) && (numCnt < 4)) {
      num[numCnt] = payload[spaceCnt];
      numCnt++;
      spaceCnt++;
    }
    if (numCnt > 0) {
      co2 = atoi(&num[0]);
    }
    Serial.print("co2: ");
    Serial.print(co2);
    Serial.println(" PPM");

    if (co2 > 1600) {

    }
  }

  // Humidity measurements and alerts. Set to Revspace default like Ledbanner
  if (strcmp(topic, "revspace/sensors/humidity") == 0) {
    char num[2] = "";
    spaceCnt = 0;
    numCnt = 0;


    while (((uint8_t)payload[spaceCnt] != 46) && (spaceCnt <= length) && (numCnt < 2)) {
      num[numCnt] = payload[spaceCnt];
      numCnt++;
      spaceCnt++;
    }
    if (numCnt > 0) {
      humidity = atoi(&num[0]);
    }
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println("");
    if (humidity < 32) {

    }
  }

  Serial.println();
}





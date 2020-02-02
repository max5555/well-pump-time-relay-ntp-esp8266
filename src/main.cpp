#include <Arduino.h>
#include <TimeLib.h>
#include "WifiConfig.h"
#include <NtpClientLib.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h> // Библиотека для OTA-прошивки

#ifndef YOUR_WIFI_SSID
#define YOUR_WIFI_SSID "YOUR_WIFI_SSID"
#define YOUR_WIFI_PASSWD "YOUR_WIFI_PASSWD"
#endif // !YOUR_WIFI_SSID

#define OTA_HOSNAME "well_pump_timer"

#define ONBOARDLED D4 // Built in LED on ESP-12/ESP-07

#define FIRST_RELAY D1
#define SECOND_RELAY D2

#define SHOW_TIME_PERIOD 10       //sec
#define NTP_TIMEOUT 5000          // ms Response timeout for NTP requests //1500 говорят минимальное 2000
#define NTP_SYNC_PERIOD_MAX 43200 // 24*60*60  sec
#define LOOP_DELAY_MAX 30         // 24*60*60 sec

int ntp_sync_period = 63;
int loop_delay = 1;

int8_t timeZone = 2;
int8_t minutesTimeZone = 0;
const PROGMEM char *ntpServer = "ua.pool.ntp.org"; //"europe.pool.ntp.org"; //"ua.pool.ntp.org"; //"time.google.com"; //"ua.pool.ntp.org";//"pool.ntp.org";
//pool1.ntp.od.ua

bool wifiFirstConnected = false;
bool FirstStart = true;
String ip;

WiFiClient Client;

void onSTAConnected(WiFiEventStationModeConnected ipInfo)
{
  Serial.printf("Connected to %s\r\n", ipInfo.ssid.c_str());
}

// Start NTP only after IP network is connected
void onSTAGotIP(WiFiEventStationModeGotIP ipInfo)
{
  Serial.printf("Got IP: %s\r\n", ipInfo.ip.toString().c_str());
  Serial.printf("Connected: %s\r\n", WiFi.status() == WL_CONNECTED ? "yes" : "no");
  digitalWrite(ONBOARDLED, LOW); // Turn on LED
  wifiFirstConnected = true;
}

// Manage network disconnection
void onSTADisconnected(WiFiEventStationModeDisconnected event_info)
{
  Serial.printf("Disconnected from SSID: %s\n", event_info.ssid.c_str());
  Serial.printf("Reason: %d\n", event_info.reason);
  digitalWrite(ONBOARDLED, HIGH); // Turn off LED
  //NTP.stop(); // NTP sync can be disabled to avoid sync errors
  WiFi.reconnect();
}

void processSyncEvent(NTPSyncEvent_t ntpEvent)
{
  if (ntpEvent < 0)
  {
    Serial.printf("Time Sync error: %d\n", ntpEvent);
    if (ntpEvent == noResponse)
      Serial.println("NTP server not reachable");
    else if (ntpEvent == invalidAddress)
      Serial.println("Invalid NTP server address");
    else if (ntpEvent == errorSending)
      Serial.println("Error sending request");
    else if (ntpEvent == responseError)
      Serial.println("NTP response error");
  }
  else
  {
    if (ntpEvent == timeSyncd)
    {
      Serial.print("Got NTP time: ");
      Serial.println(NTP.getTimeDateString(NTP.getLastNTPSync()));
    }
  }
}

boolean syncEventTriggered = false; // True if a time even has been triggered
NTPSyncEvent_t ntpEvent;            // Last triggered event

void setup()
{
  delay(1000);
  static WiFiEventHandler e1, e2, e3;

  Serial.begin(115200);
  //Serial.setDebugOutput(true);
  delay(500);
  Serial.flush();
  WiFi.mode(WIFI_STA);
  WiFi.begin(YOUR_WIFI_SSID, YOUR_WIFI_PASSWD);

  pinMode(ONBOARDLED, OUTPUT);   // Onboard LED
  digitalWrite(ONBOARDLED, LOW); // Switch on LED

  NTP.onNTPSyncEvent([](NTPSyncEvent_t event) {
    ntpEvent = event;
    syncEventTriggered = true;
  });

  e1 = WiFi.onStationModeGotIP(onSTAGotIP); // As soon WiFi is connected, start NTP Client
  e2 = WiFi.onStationModeDisconnected(onSTADisconnected);
  e3 = WiFi.onStationModeConnected(onSTAConnected);

  pinMode(FIRST_RELAY, OUTPUT);

  ArduinoOTA.setHostname(OTA_HOSNAME); // Задаем имя сетевого порта
  //     ArduinoOTA.setPassword((const char *)"0000"); // Задаем пароль доступа для удаленной прошивки

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

bool startNTP()
{
  Serial.println();
  Serial.println("*** startNTP ***");
  NTP.begin(ntpServer, timeZone, true, minutesTimeZone);
  //NTP.begin("pool.ntp.org", 2, true);
  delay(3000); // there seems to be a 1 second delay before first sync will be attempted, delay 2 seconds allows request to be made and received
  int counter = 1;
  Serial.print("NTP.getLastNTPSync() = ");
  Serial.println(NTP.getLastNTPSync());
  while (!NTP.getLastNTPSync() && counter <= 3)
  {
    NTP.begin(ntpServer, timeZone, true, minutesTimeZone);
    Serial.print("NTP CHECK: #");
    Serial.println(counter);
    counter += 1;
    delay(counter * 2000);
  };
  NTP.setInterval(ntp_sync_period); // in seconds
  if (now() < 100000)
  {
    return false;
  }
  else
  {
    return true;
  }
}

void TimeValidator()
{ //проверяем время, если неправильное - перезагружаемся

  Serial.println("TimeValidator");
  for (int ectr = 1; ectr < 4; ectr++)
  {
    ip = WiFi.localIP().toString();
    if (now() < 100000 and (ip != "0.0.0.0"))
    {
      bool isntpok = startNTP();
      if (isntpok)
      {
        return;
      }
      Serial.print("Wrong UNIX time: now() = ");
      //Serial.println(NTP.getTimeStr());
      Serial.println(now());
      Serial.print("ip = ");
      Serial.println(ip);
      Serial.print("ectr = ");
      Serial.println(ectr);
      Serial.print("delay ");
      Serial.print(10000 * ectr);
      Serial.println(" sec");
      delay(30000 * ectr);
    }
    else
    {
      return;
    }
  }
  Serial.println("**** restart **** "); //перезагружаемся только при 3-х ошибках подряд
  delay(2000);

  //            WiFi.forceSleepBegin(); wdt_reset(); ESP.restart(); while(1)wdt_reset();
  //            ESP.reset();
  ESP.restart();
}

void loop()
{

  if (FirstStart)
  {
    Serial.println();
    Serial.println("*** FirstStart ***");
    Serial.println();
    Serial.println (" *** demo ***");
    delay (1000);
    // демонстрируем, что работает
    digitalWrite(FIRST_RELAY, HIGH); delay(1000); digitalWrite(FIRST_RELAY, LOW);
    digitalWrite(SECOND_RELAY, HIGH); delay(500); digitalWrite(SECOND_RELAY, LOW);
  }

  digitalWrite(ONBOARDLED, LOW);

  ArduinoOTA.handle(); // Всегда готовы к прошивке

  static int i = 0;
  static unsigned long last_show_time = 0;

  if (wifiFirstConnected)
  {
    Serial.println("*** wifiFirstConnected ***");
    wifiFirstConnected = false;
    NTP.setInterval(63);            //60 * 5 + 3    //63 Changes sync period. New interval in seconds.
    NTP.setNTPTimeout(NTP_TIMEOUT); //Configure response timeout for NTP requests milliseconds
    startNTP();
    //NTP.begin(ntpServer, timeZone, true, minutesTimeZone);
    NTP.getTimeDateString(); //dummy
  }

  if (syncEventTriggered)
  {
    processSyncEvent(ntpEvent);
    syncEventTriggered = false;
  }

  if ((millis() - last_show_time) > SHOW_TIME_PERIOD or FirstStart)
  {
    //Serial.println(millis() - last_show_time);
    last_show_time = millis();
    Serial.println();
    Serial.print("i = ");
    Serial.print(i);
    Serial.print(" ");
    Serial.print(NTP.getTimeDateString());
    Serial.print(" ");
    Serial.print(NTP.isSummerTime() ? "Summer Time. " : "Winter Time. ");
    Serial.print("WiFi is ");
    Serial.print(WiFi.isConnected() ? "connected" : "not connected");
    Serial.print(". ");
    Serial.print("Uptime: ");
    Serial.print(NTP.getUptimeString());
    Serial.print(" since ");
    Serial.println(NTP.getTimeDateString(NTP.getFirstSync()).c_str());

    Serial.printf("ESP8266 Chip id = %06X\n", ESP.getChipId());
    Serial.printf("WiFi.status () = %d", WiFi.status());
    Serial.println(", WiFi.localIP() = " + WiFi.localIP().toString());
    //        Serial.printf ("Free heap: %u\n", ESP.getFreeHeap ());
    i++;
  }

  //TimeValidator();

  if (now() > 100000 and ip != "0.0.0.0" and ntp_sync_period < NTP_SYNC_PERIOD_MAX)
  { //постепенно увеличиваем период обновлений до суток
    ntp_sync_period += 63;
    Serial.print("ntp_sync_period = ");
    Serial.println(ntp_sync_period);
    NTP.setInterval(ntp_sync_period); // in seconds
    if (loop_delay < LOOP_DELAY_MAX)
    {                  
      loop_delay += 1; //sec //постепенно увеличиваем период цикла
    }
  }
  else if (now() < 100000 and ip != "0.0.0.0")
  {
    TimeValidator();
  }

  if (hour() < 4)
  {
    digitalWrite(FIRST_RELAY, HIGH);
    Serial.println("*********** ON ***********");
  }
  else
  {
    digitalWrite(FIRST_RELAY, LOW);
    Serial.println("*********** OFF ***********");
  }


  // digitalWrite(FIRST_RELAY, HIGH);
  // digitalWrite(ONBOARDLED, LOW); // 
  // delay(1000);
  // digitalWrite(FIRST_RELAY, LOW);
  // digitalWrite(ONBOARDLED, HIGH);

  Serial.print("loop_delay = ");
  Serial.print(loop_delay);
  Serial.print("(");
  Serial.print(LOOP_DELAY_MAX);
  Serial.println(") sec");
  Serial.println();
  digitalWrite(ONBOARDLED, HIGH); // Turn off LED
  delay(loop_delay * 1000); //задержка большого цикла
  FirstStart = false;
  
}

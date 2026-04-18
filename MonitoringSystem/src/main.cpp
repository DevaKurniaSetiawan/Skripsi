#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <HTTPClient.h>
#include <time.h>
#include <Fuzzy.h>
#include "Fuzzy_Set.h"
#include "Fuzzy_Rule.h"

/* ================= FUZZY OBJECT & SETS ================= */
Fuzzy *fuzzy = new Fuzzy();
FuzzySets fuzzySets;
float pumpValue = 0;
float heaterValue = 0;
const int totalFuzzyRules = 27;
String activeFuzzyRulesJson = "[]";
int activeFuzzyRulesCount = 0;

/* ================= Modul RTC & LCD ================= */
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 20, 4);

/* ================= WiFi ================= */
const char *ssid = "adilgaming";
const char *password = "12345678";

/* ================= Telegram ================= */
const char *botToken = "xxxx";
const char *chatID = "xxxx";
WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);

bool telegramEnabled = true;
unsigned long telegramDelayMs = 300000;

/* ================= NTP / Timezone ================= */
const char *ntpServer = "pool.ntp.org";
const long gmtOffsetSec = 7 * 3600; // WIB (UTC+7), ubah jika zona waktu berbeda
const int daylightOffsetSec = 0;
const unsigned long wifiConnectTimeoutMs = 10000; // Timeout untuk koneksi WiFi, dalam milidetik
const unsigned long wifiRetryIntervalMs = 15000;  // Interval antara percobaan koneksi WiFi ulang jika gagal, dalam milidetik
const unsigned long ntpRetryIntervalMs = 60000;   // Interval antara percobaan sinkronisasi NTP ulang jika gagal, dalam milidetik
unsigned long lastWiFiRetry = 0;
unsigned long lastNtpAttempt = 0;
bool rtcNtpSynced = false;

/* ================= Server ================= */
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 300000; // 5 menit
const char *serverUrl = "http://192.168.137.1:3000/kirim-sensor";

/* ================= Pin ================= */
const int oneWireBus = 18; // Pin data sensor suhu
const int phPin = 35;      // GPIO35 ADC1 aman untuk WiFi
#define turbidityPin 34

const int relayPump = 33;
const int relayHeater1 = 32;
const int relayHeater2 = 13;
const int relayHeater3 = 14;
const uint8_t relayActiveState = LOW;    // Modul relay aktif-LOW (IN=LOW -> relay ON)
const uint8_t relayInactiveState = HIGH; // IN=HIGH -> relay OFF

/* ================= Sensor ================= */
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

/* ================= Variabel Sensor ================= */
float temperatureC;
float phValue = 0.0;
float lastValidPhValue = 7.0;
bool hasValidPhValue = false;
uint8_t phInvalidStreak = 0;
int turbidityNTU = 0;

/* ================= Kalibrasi ================= */
const int phNumSamples = 35;
int phBufferArr[phNumSamples];

const int phNumPoints = 3;
const float phReferenceADC[phNumPoints] = {2049, 1924, 1819};
const float phReferencePhs[phNumPoints] = {4.01, 6.86, 9.18};
const float phAdcValidMargin = 60.0;
const uint8_t phInvalidStreakLimit = 3;
const float phFallbackValue = 7.0;

int clearWaterADC = 850;
int mediumWaterADC = 560;
int dirtyWaterADC = 450;
// harus  sering di kalibrasi
const int numSamples = 20;

unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 1000;

/* ============================================================
                        FUZZY PROCESS
   ============================================================*/
void prosesFuzzy()
{
  fuzzy->setInput(1, temperatureC);
  fuzzy->setInput(2, turbidityNTU);
  fuzzy->setInput(3, phValue);

  fuzzy->fuzzify();

  pumpValue = fuzzy->defuzzify(1);
  heaterValue = fuzzy->defuzzify(2);

  // Logika NO: aktuator ON saat relay aktif (modul aktif-LOW).
  digitalWrite(relayPump, pumpValue > 0.5 ? relayActiveState : relayInactiveState);

  digitalWrite(relayHeater1, heaterValue > 0.5 ? relayActiveState : relayInactiveState);
  digitalWrite(relayHeater2, heaterValue > 0.5 ? relayActiveState : relayInactiveState);
  digitalWrite(relayHeater3, heaterValue > 0.5 ? relayActiveState : relayInactiveState);

  activeFuzzyRulesJson = "[";
  activeFuzzyRulesCount = 0;
  for (int i = 1; i <= totalFuzzyRules; i++)
  {
    if (fuzzy->isFiredRule(i))
    {
      if (activeFuzzyRulesCount > 0)
        activeFuzzyRulesJson += ",";
      activeFuzzyRulesJson += "\"R" + String(i) + "\"";
      activeFuzzyRulesCount++;
    }
  }
  activeFuzzyRulesJson += "]";

  Serial.printf("FUZZY -> T=%.2f pH=%.2f NTU=%d | Pump=%.2f Heater=%.2f\n",
                temperatureC, phValue, turbidityNTU, pumpValue, heaterValue);
  Serial.printf("RULE AKTIF -> %s (count=%d)\n", activeFuzzyRulesJson.c_str(), activeFuzzyRulesCount);
}

/* ================= SENSOR ================= */

void bacaSuhu()
{
  sensors.requestTemperatures();
  temperatureC = sensors.getTempCByIndex(0);

  if (temperatureC == DEVICE_DISCONNECTED_C)
  {
    Serial.println("Sensor suhu tidak terhubung!");
    return;
  }

  if (telegramEnabled && WiFi.status() == WL_CONNECTED && temperatureC > 40.0)
    bot.sendMessage(chatID, "Suhu terlalu tinggi!", "");
}

void bacaPH()
{
  for (int i = 0; i < phNumSamples; i++)
  {
    phBufferArr[i] = analogRead(phPin);
    delay(10);
  }

  for (int i = 0; i < phNumSamples - 1; i++)
  {
    for (int j = i + 1; j < phNumSamples; j++)
    {
      if (phBufferArr[i] > phBufferArr[j])
      {
        int temp = phBufferArr[i];
        phBufferArr[i] = phBufferArr[j];
        phBufferArr[j] = temp;
      }
    }
  }

  long sum = 0;
  for (int i = 10; i < 20; i++)
  {
    sum += phBufferArr[i];
  }

  float adc = sum / 10.0;
  const float phAdcMinValid = phReferenceADC[phNumPoints - 1] - phAdcValidMargin;
  const float phAdcMaxValid = phReferenceADC[0] + phAdcValidMargin;
  bool adcInValidRange = adc >= phAdcMinValid && adc <= phAdcMaxValid;

  if (!adcInValidRange)
  {
    phInvalidStreak++;
    if (phInvalidStreak >= phInvalidStreakLimit)
    {
      hasValidPhValue = false;
      phValue = phFallbackValue;
      Serial.printf("Sensor pH error (adc=%.1f di luar %.1f..%.1f), fallback pH=%.2f\n",
                    adc, phAdcMinValid, phAdcMaxValid, phValue);
    }
    else
    {
      phValue = hasValidPhValue ? lastValidPhValue : phFallbackValue;
      Serial.printf("ADC pH mencurigakan (adc=%.1f), tahan nilai sebelumnya (%u/%u)\n",
                    adc, phInvalidStreak, phInvalidStreakLimit);
    }
    return;
  }
  phInvalidStreak = 0;

  bool mapped = false;
  if (adc >= phReferenceADC[0])
  {
    phValue = phReferencePhs[0];
    mapped = true;
  }
  else if (adc <= phReferenceADC[phNumPoints - 1])
  {
    phValue = phReferencePhs[phNumPoints - 1];
    mapped = true;
  }
  else
  {
    for (int i = 0; i < phNumPoints - 1; i++)
    {
      if (adc <= phReferenceADC[i] && adc >= phReferenceADC[i + 1])
      {
        phValue = phReferencePhs[i] +
                  (phReferencePhs[i + 1] - phReferencePhs[i]) *
                      ((adc - phReferenceADC[i]) /
                       (phReferenceADC[i + 1] - phReferenceADC[i]));
        mapped = true;
        break;
      }
    }
  }

  if (!mapped)
  {
    phValue = hasValidPhValue ? lastValidPhValue : phFallbackValue;
    Serial.println("ADC pH di luar rentang kalibrasi, pakai pH terakhir valid.");
    return;
  }

  lastValidPhValue = phValue;
  hasValidPhValue = true;
}

void bacaKekeruhan()
{
  long totalADC = 0;
  for (int i = 0; i < numSamples; i++)
  {
    totalADC += analogRead(turbidityPin);
    delay(10);
  }

  float avgADC = totalADC / (float)numSamples;
  float ntu;

  if (avgADC >= clearWaterADC)
  {
    ntu = 0;
  }
  else if (avgADC <= dirtyWaterADC)
  {
    ntu = 100;
  }
  else if (avgADC >= mediumWaterADC)
  {
    ntu = (float)(clearWaterADC - avgADC) *
          (50.0 / (clearWaterADC - mediumWaterADC));
  }
  else
  {
    ntu = 50.0 + (float)(mediumWaterADC - avgADC) *
                     (50.0 / (mediumWaterADC - dirtyWaterADC));
  }

  turbidityNTU = round(ntu);
}

/* ================= LCD ================= */
void print2Digit(uint8_t v)
{
  if (v < 10)
    lcd.print('0');
  lcd.print(v);
}

void tampilkanLCD()
{
  DateTime now = rtc.now();

  lcd.setCursor(0, 0);
  lcd.print("Waktu ");
  print2Digit(now.hour());
  lcd.print(":");
  print2Digit(now.minute());
  lcd.print(":");
  print2Digit(now.second());
  lcd.print("   ");

  lcd.setCursor(0, 1);
  lcd.print("T:");
  lcd.print(temperatureC, 1);
  lcd.print(" pH:");
  lcd.print(phValue, 1);
  lcd.print("     ");

  lcd.setCursor(0, 2);
  lcd.print("NTU:");
  lcd.print(turbidityNTU);
  lcd.print("           ");

  lcd.setCursor(0, 3);
  lcd.print("P:");
  lcd.print(pumpValue > 0.5 ? "ON " : "OFF");
  lcd.print(" H:");
  lcd.print(heaterValue > 0.5 ? "ON " : "OFF");
  lcd.print("       ");
}

bool syncRTCFromNTP(unsigned long timeoutMs = 15000)
{
  configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);

  struct tm timeinfo;
  unsigned long start = millis();

  while (!getLocalTime(&timeinfo, 1000))
  {
    if (millis() - start > timeoutMs)
      return false;
  }

  rtc.adjust(DateTime(
      timeinfo.tm_year + 1900,
      timeinfo.tm_mon + 1,
      timeinfo.tm_mday,
      timeinfo.tm_hour,
      timeinfo.tm_min,
      timeinfo.tm_sec));

  return true;
}

void connectWiFiWithTimeout()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < wifiConnectTimeoutMs))
  {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    client.setInsecure();
    Serial.println("WiFi terhubung.");
  }
  else
  {
    Serial.println("WiFi tidak terhubung. Sistem tetap jalan (offline mode).");
  }

  lastWiFiRetry = millis();
}

void maintainWiFiConnection()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    if (!rtcNtpSynced && (lastNtpAttempt == 0 || millis() - lastNtpAttempt >= ntpRetryIntervalMs))
    {
      lastNtpAttempt = millis();

      if (syncRTCFromNTP(3000))
      {
        Serial.println("RTC sinkron dari NTP (WIB).");
        rtcNtpSynced = true;
      }
      else
      {
        Serial.println("Gagal sinkron NTP, pakai waktu RTC saat ini.");
      }
    }
    return;
  }

  if (millis() - lastWiFiRetry >= wifiRetryIntervalMs)
  {
    lastWiFiRetry = millis();
    Serial.println("Mencoba reconnect WiFi...");
    WiFi.reconnect();
  }
}

/* ================= SERVER ================= */
void kirimDataKeServer()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Gagal kirim: WiFi belum terhubung.");
    return;
  }

  HTTPClient http;
  http.begin(serverUrl);
  http.setTimeout(5000); // 5 detik timeout
  http.addHeader("Content-Type", "application/json");

  DateTime now = rtc.now();                                      // Ambil waktu saat ini dari RTC
  bool waterpumpOn = digitalRead(relayPump) == relayActiveState; // cek status aktuator
  bool heaterOn = digitalRead(relayHeater1) == relayActiveState;
  String json = "{";
  json += "\"temperature\":" + String(temperatureC, 2) + ",";
  json += "\"ph\":" + String(phValue, 2) + ",";
  json += "\"turbidity\":" + String(turbidityNTU) + ",";
  json += "\"fuzzy_pump_value\":" + String(pumpValue, 4) + ",";
  json += "\"fuzzy_heater_value\":" + String(heaterValue, 4) + ",";
  json += "\"fuzzy_rules_count\":" + String(activeFuzzyRulesCount) + ",";
  json += "\"fuzzy_rules\":" + activeFuzzyRulesJson + ",";
  json += "\"waterpump\":\"" + String(waterpumpOn ? "ON" : "OFF") + "\",";
  json += "\"heater\":\"" + String(heaterOn ? "ON" : "OFF") + "\",";
  json += "\"timestamp\":\"" + now.timestamp(DateTime::TIMESTAMP_TIME) + "\"}";

  Serial.println("Mengirim data ke server...");
  Serial.print("URL: ");
  Serial.println(serverUrl);
  Serial.print("IP ESP32: ");
  Serial.println(WiFi.localIP());
  Serial.print("Payload: ");
  Serial.println(json);

  int httpCode = http.POST(json);
  String responseBody = http.getString();

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);
  if (httpCode > 0)
  {
    Serial.print("Response: ");
    Serial.println(responseBody);
  }
  else
  {
    Serial.print("HTTP Error: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

/* ================= SETUP ================= */
void setup()
{
  Serial.begin(115200);
  Wire.begin(21, 22); // SDA, SCL
  analogReadResolution(12);
  analogSetPinAttenuation(phPin, ADC_11db);

  if (!rtc.begin())
  {
    Serial.println("RTC tidak terdeteksi!");
    while (1)
      delay(10);
  }

  if (rtc.lostPower())
  {
    Serial.println("RTC kehilangan daya, set waktu dari waktu kompilasi.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  sensors.begin();

  pinMode(relayPump, OUTPUT);
  pinMode(relayHeater1, OUTPUT);
  pinMode(relayHeater2, OUTPUT);
  pinMode(relayHeater3, OUTPUT);
  digitalWrite(relayPump, relayInactiveState);
  digitalWrite(relayHeater1, relayInactiveState);
  digitalWrite(relayHeater2, relayInactiveState);
  digitalWrite(relayHeater3, relayInactiveState);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  connectWiFiWithTimeout();

  /* ==== FUZZY INIT ==== */
  fuzzySets = setupFuzzySystem(fuzzy);
  setupRules(fuzzy, fuzzySets);
}

/* ================= LOOP ================= */
void loop()
{
  maintainWiFiConnection();

  bacaSuhu();
  bacaPH();
  bacaKekeruhan();

  prosesFuzzy();

  if (millis() - lastSendTime > sendInterval)
  {
    lastSendTime = millis();
    kirimDataKeServer();
  }

  if (millis() - lastUpdateTime > updateInterval)
  {
    lastUpdateTime = millis();
    tampilkanLCD();
  }
}

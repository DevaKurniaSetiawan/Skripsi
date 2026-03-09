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

/* ================= Modul RTC & LCD ================= */
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 20, 4);

/* ================= WiFi ================= */
const char *ssid = "Gasspoll";
const char *password = "nikisinten12";

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
const unsigned long wifiConnectTimeoutMs = 10000;
const unsigned long wifiRetryIntervalMs = 15000;
const unsigned long ntpRetryIntervalMs = 60000;
unsigned long lastWiFiRetry = 0;
unsigned long lastNtpAttempt = 0;
bool rtcNtpSynced = false;

/* ================= Server ================= */
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 300000; // 5 menit

/* ================= Pin ================= */
const int oneWireBus = 18; // Pin data sensor suhu
const int phPin = 35;      // GPIO35 ADC1 aman untuk WiFi
#define turbidityPin 34

const int relayPump = 33;
const int relayHeater1 = 32;
const int relayHeater2 = 13;
const int relayHeater3 = 14;

/* ================= Sensor ================= */
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

/* ================= Variabel Sensor ================= */
float temperatureC;
float phValue = 0.0;
int turbidityNTU = 0;

/* ================= Kalibrasi ================= */
const int phNumSamples = 50;
int phBufferArr[phNumSamples];

const int phNumPoints = 3;
const float phReferenceADC[phNumPoints] = {1240, 1210, 1127};
const float phReferencePhs[phNumPoints] = {4.01, 6.86, 9.18};

float clearWaterVoltage = 1.96;
float dirtyWaterVoltage = 0.9;
const int numSamples = 10;

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

  // Relay Pump (aktif HIGH)
  digitalWrite(relayPump, pumpValue > 0.5 ? HIGH : LOW);

  // Relay Heater (aktif LOW)
  digitalWrite(relayHeater1, heaterValue > 0.5 ? LOW : HIGH);
  digitalWrite(relayHeater2, heaterValue > 0.5 ? LOW : HIGH);
  digitalWrite(relayHeater3, heaterValue > 0.5 ? LOW : HIGH);

  Serial.printf("FUZZY -> T=%.2f pH=%.2f NTU=%d | Pump=%.2f Heater=%.2f\n",
                temperatureC, phValue, turbidityNTU, pumpValue, heaterValue);
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

  if (adc <= 0)
  {
    phValue = 0;
    return;
  }

  if (adc >= phReferenceADC[0])
  {
    phValue = phReferencePhs[0];
  }
  else if (adc <= phReferenceADC[phNumPoints - 1])
  {
    phValue = phReferencePhs[phNumPoints - 1];
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
        break;
      }
    }
  }
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
  float voltage = avgADC * (3.3 / 4095.0);

  // Konversi ke skala FIS 0-100
  if (voltage >= clearWaterVoltage)
    turbidityNTU = 0;
  else if (voltage <= dirtyWaterVoltage)
    turbidityNTU = 100;
  else
    turbidityNTU = (clearWaterVoltage - voltage) *
                   (100.0 / (clearWaterVoltage - dirtyWaterVoltage));
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
    return;

  HTTPClient http;
  http.begin("http://192.168.1.5:5000/kirim-sensor");
  http.addHeader("Content-Type", "application/json");

  DateTime now = rtc.now();                          // Ambil waktu saat ini dari RTC
  bool waterpumpOn = digitalRead(relayPump) == HIGH; // ceck status akuator
  bool heaterOn = digitalRead(relayHeater1) == LOW;
  String json = "{";
  json += "\"temperature\":" + String(temperatureC, 2) + ",";
  json += "\"ph\":" + String(phValue, 2) + ",";
  json += "\"turbidity\":" + String(turbidityNTU) + ",";
  json += "\"waterpump\":\"" + String(waterpumpOn ? "ON" : "OFF") + "\",";
  json += "\"heater\":\"" + String(heaterOn ? "ON" : "OFF") + "\",";
  json += "\"timestamp\":\"" + now.timestamp(DateTime::TIMESTAMP_TIME) + "\"}";
  http.POST(json);
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

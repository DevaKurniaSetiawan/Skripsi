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
const char *ssid = "KontrakSyariah";
const char *password = "pastikumlot";

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

/* ================= Server ================= */
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 300000;

/* ================= Pin ================= */
const int oneWireBus = 18; // Pin data sensor suhu
const int phPin = 35;
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
float referencePhs[4] = {7.0, 6.86, 4.01, 9.18};
float referenceVoltages[4] = {2.5, 2.48, 3.0, 2.2};
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

  if (telegramEnabled && temperatureC > 40.0)
    bot.sendMessage(chatID, "Suhu terlalu tinggi!", "");
}

void bacaPH()
{
  int phRaw = analogRead(phPin);
  float voltage = phRaw * (3.3 / 4095.0);

  for (int i = 0; i < 3; i++)
  {
    if (voltage >= referenceVoltages[i + 1] && voltage <= referenceVoltages[i])
    {
      float slope = (referencePhs[i] - referencePhs[i + 1]) /
                    (referenceVoltages[i] - referenceVoltages[i + 1]);
      float intercept = referencePhs[i] - slope * referenceVoltages[i];
      phValue = slope * voltage + intercept;
      break;
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

/* ================= SERVER ================= */
void kirimDataKeServer()
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  HTTPClient http;
  http.begin("http://192.168.1.74:5000/kirim-sensor");
  http.addHeader("Content-Type", "application/json");

  DateTime now = rtc.now();

  String json = "{";
  json += "\"temperature\":" + String(temperatureC, 2) + ",";
  json += "\"ph\":" + String(phValue, 2) + ",";
  json += "\"turbidity\":" + String(turbidityNTU) + ",";
  json += "\"timestamp\":\"" + now.timestamp(DateTime::TIMESTAMP_TIME) + "\"}";

  http.POST(json);
  http.end();
}

/* ================= SETUP ================= */
void setup()
{
  Serial.begin(115200);
  Wire.begin(21, 22); // SDA, SCL

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

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);
  client.setInsecure();

  if (syncRTCFromNTP())
    Serial.println("RTC sinkron dari NTP (WIB).");
  else
    Serial.println("Gagal sinkron NTP, pakai waktu RTC saat ini.");

  /* ==== FUZZY INIT ==== */
  fuzzySets = setupFuzzySystem(fuzzy);
  setupRules(fuzzy, fuzzySets);
}

/* ================= LOOP ================= */
void loop()
{
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

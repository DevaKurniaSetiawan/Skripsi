#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <ESP32Servo.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Inisialisasi modul RTC, Servo, LCD, dan lainn
RTC_DS3231 rtc;
Servo servo;
LiquidCrystal_I2C lcd(0x27, 20, 4); // Alamat I2C LCD (biasanya 0x27 atau 0x3F)

// WiFi credentials
const char *ssid = "ZidanGanteng";
const char *password = "Zidan12345";

// Telegram credentials
const char *botToken = "7724394328:AAHKW3-a5x5t-9mKw219rSlkykb4uSav3So"; // Token API bot
const char *chatID = "1110592780";                                       // ID Chat Anda

WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);

// Turbidity sensor pin
#define turbidityPin 34 // Pin analog untuk sensor kekeruhan
int readADC = 0;        // Variabel untuk nilai ADC
float turbidityNTU = 0; // Nilai kekeruhan dalam NTU (float!)

// Pin servo
const int servoPin = 25;
String lastFeedTime = "Belum ada";

// Pin sensor ultrasonik
const int trigPin = 26;
const int echoPin = 27;

// Sensor suhu pin
const int oneWireBus = 18; // Pin data untuk DS18B20

// PH 4520C pin
const int phPin = 35; // Pin analog untuk sensor pH

const int relayPump = 33; // Pin untuk relay pompa DC

// Inisialisasi OneWire dan DallasTemperature
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

// Relay pins
const int relay1 = 33;
const int relay2 = 32; // Heater pada channel 2
const int relay3 = 13;
const int relay4 = 14; // Pompa DC pada channel 4

// Variabel untuk jarak, level pakan, suhu, pH
const float containerHeight = 20.0; // Tinggi wadah dalam cm
float distance = 0.0;
float feedLevel = 0.0; // Persentase pakan tersisa
float temperatureC;    // Suhu dalam Celsius
float phValue = 0.0;   // Nilai pH

// Kalibrasi sensor pH
float referencePhs[4] = {7.0, 6.86, 4.01, 9.18};    // Nilai pH referensi
float referenceVoltages[4] = {2.5, 2.48, 3.0, 2.2}; // Tegangan referensi dari sensor untuk pH tertentu dalam volt

// Waktu pemberian makan (24-hour format) // sesuaikan dengan kebutuhan
const int feedTimes[][2] = {
    {06, 21}, // Pagi
              // {01, 16}, // Siang
              // {0, 0}    // Sore
};
const int feedCount = sizeof(feedTimes) / sizeof(feedTimes[0]); // Jumlah waktu pakan

// Variabel untuk melacak apakah pakan sudah diberikan pada waktu tertentu
bool sudahPakanDiberikan[feedCount] = {false};

// Struct untuk output fuzzy
struct FuzzyOutput
{
  float heater;
  float pump;
};

// Deklarasi fungsi
void kirimDataKeServer();
void beriMakanIkan();
void cekVolumePakan();
void bacaSuhu();
void bacaPH();
void bacaKekeruhan();
void tampilkanLCD(DateTime now);
FuzzyOutput fuzzyLogicControl(float temperature, float ph, float turbidity);
float adcToNTU(int adcValue);

unsigned long lastUpdateTime = 0;          // Waktu terakhir pembaruan tampilan
const unsigned long updateInterval = 5000; // Interval pembaruan tampilan (ms)

// Fungsi konversi ADC ke NTU (kalibrasi lebih akurat)
float adcToNTU(int adcValue)
{
  if (adcValue >= 1117)
  {
    return 1.0;
  }
  else if (adcValue >= 970)
  {
    return 1.0 + (15.0 - 1.0) * (1117 - adcValue) / (1117.0 - 970.0);
  }
  else if (adcValue >= 500)
  {
    return 15.0 + (300.0 - 15.0) * (970 - adcValue) / (970.0 - 500.0);
  }
  else
  {
    return 300.0;
  }
}

void setup()
{
  Serial.begin(9600);

  // Inisialisasi Wire untuk I2C
  Wire.begin(21, 22); // SDA = 21, SCL = 22

  // Inisialisasi RTC
  if (!rtc.begin())
  {
    Serial.println("RTC tidak terdeteksi!");
    while (1)
      ;
  }
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Sinkronkan waktu RTC

  // Inisialisasi Servo
  servo.attach(servoPin);
  servo.write(0); // Servo di posisi awal

  // Inisialisasi Pin Ultrasonik
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // Inisialisasi sensor DS18B20
  sensors.begin();

  // Inisialisasi LCD
  lcd.begin(20, 4); // LCD menggunakan bus I2C
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Koneksi ke WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Terhubung ke WiFi");

  // Konfigurasi untuk koneksi aman
  client.setInsecure();

  // Inisialisasi relay
  pinMode(relay1, OUTPUT);
  pinMode(relay2, OUTPUT);
  pinMode(relay3, OUTPUT);
  pinMode(relay4, OUTPUT);

  digitalWrite(relay1, LOW);
  digitalWrite(relay2, LOW);
  digitalWrite(relay3, LOW);
  digitalWrite(relay4, LOW);
}

void loop()
{
  // Dapatkan waktu saat ini dari RTC
  DateTime now = rtc.now();

  // Cek apakah waktu sekarang sesuai dengan waktu pemberian makan
  for (int i = 0; i < feedCount; i++)
  {
    if (now.hour() == feedTimes[i][0] && now.minute() == feedTimes[i][1])
    {
      if (!sudahPakanDiberikan[i])
      {
        beriMakanIkan();
        sudahPakanDiberikan[i] = true; // Tandai bahwa pemberian makan sudah dilakukan
      }
    }
    else
    {
      // Reset status jika waktu sudah berganti untuk mengizinkan pemberian makan berikutnya
      sudahPakanDiberikan[i] = false;
    }
  }

  // Periksa volume pakan
  cekVolumePakan();

  // Periksa suhu
  bacaSuhu();

  // Periksa nilai pH
  bacaPH();

  // Periksa kekeruhan air
  bacaKekeruhan();

  // Kirim data ke server
  kirimDataKeServer();

  // Perbarui tampilan LCD berdasarkan interval
  if (millis() - lastUpdateTime >= updateInterval)
  {
    lastUpdateTime = millis();
    tampilkanLCD(now);
  }
}

void beriMakanIkan()
{
  Serial.println("Memberi makan ikan...");
  servo.write(90); // Gerakkan servo untuk membuka pakan
  delay(5000);     // Tunggu 5 detik
  servo.write(0);  // Kembali ke posisi awal
  Serial.println("Pemberian makan selesai.");

  // Kirim pesan ke Telegram
  String message = "Waktu pemberian makan : ";
  message += rtc.now().timestamp(DateTime::TIMESTAMP_TIME);
  bot.sendMessage(chatID, message, "");
  lastFeedTime = rtc.now().timestamp(DateTime::TIMESTAMP_TIME);
}

void cekVolumePakan()
{
  // Kirim sinyal trig
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Hitung waktu pantulan echo
  long duration = pulseIn(echoPin, HIGH);

  // Hitung jarak
  distance = duration * 0.034 / 2.0;

  // Hitung level pakan dalam persentase
  feedLevel = ((containerHeight - distance) / containerHeight) * 100.0;

  Serial.print("Jarak ke pakan: ");
  Serial.print(distance);
  Serial.print(" cm, Level pakan: ");
  Serial.print(feedLevel);
  Serial.println(" %");

  if (feedLevel < 20.0)
  {
    String warning = "Volume Pakan Rendah: ";
    warning += String(feedLevel, 1);
    warning += "% segera isi ulang!";
    bot.sendMessage(chatID, warning, "");
    delay(6000);
  }
}

void bacaSuhu()
{
  sensors.requestTemperatures();
  temperatureC = sensors.getTempCByIndex(0);

  if (temperatureC == DEVICE_DISCONNECTED_C)
  {
    Serial.println("Error: Sensor tidak terhubung!");
  }
  else
  {
    Serial.print("Suhu saat ini: ");
    Serial.print(temperatureC);
    Serial.println(" °C");

    FuzzyOutput output = fuzzyLogicControl(temperatureC, phValue, turbidityNTU);

    if (output.heater > 0.5)
    {
      digitalWrite(relay2, HIGH); // Nyalakan heater (relay2)
      digitalWrite(relay3, HIGH); // Nyalakan heater (relay3)
      digitalWrite(relay4, HIGH); // Nyalakan heater (relay4)
      Serial.println("Heater ON (relay2, relay3, relay4)");
    }
    else
    {
      digitalWrite(relay2, LOW); // Matikan heater (relay2)
      digitalWrite(relay3, LOW); // Matikan heater (relay3)
      digitalWrite(relay4, LOW); // Matikan heater (relay4)
      Serial.println("Heater OFF (relay2, relay3, relay4)");
    }

    // Kontrol pompa berdasarkan fuzzy
    if (output.pump > 0.5)
    {
      digitalWrite(relayPump, HIGH);
      Serial.println("Pompa ON ");
    }
    else
    {
      digitalWrite(relayPump, LOW);
      Serial.println("Pompa OFF");
    }

    if (temperatureC > 30.0)
    {
      String warning = "Peringatan: Suhu terlalu tinggi! ";
      warning += String(temperatureC, 1);
      warning += " °C";
      bot.sendMessage(chatID, warning, "");
      delay(6000);
    }
  }
}

void bacaPH()
{
  int phRaw = analogRead(phPin);
  float voltage = phRaw * (3.3 / 4095.0);

  // Interpolasi nilai pH berdasarkan tegangan
  for (int i = 0; i < 3; i++)
  {
    if (voltage >= referenceVoltages[i + 1] && voltage <= referenceVoltages[i])
    {
      float slope = (referencePhs[i] - referencePhs[i + 1]) / (referenceVoltages[i] - referenceVoltages[i + 1]);
      float intercept = referencePhs[i] - slope * referenceVoltages[i];
      phValue = slope * voltage + intercept;
      break;
    }
  }

  Serial.print("Nilai pH saat ini: ");
  Serial.print(phValue, 2);
  Serial.print(" (Tegangan: ");
  Serial.print(voltage, 2);
  Serial.println(" V)");

  if (phValue < 6.0 || phValue > 8.5)
  {
    String warning = "Peringatan: Nilai pH tidak normal! pH = ";
    warning += String(phValue, 2);
    bot.sendMessage(chatID, warning, "");
    delay(5000);
  }
}

void bacaKekeruhan()
{
  readADC = analogRead(turbidityPin);

  // Cek jika sensor tidak terpasang (nilai ADC 0)
  if (readADC == 0)
  {
    turbidityNTU = 0;
    Serial.println("Sensor kekeruhan tidak terpasang! Nilai NTU: 000");
  }
  else
  {
    turbidityNTU = adcToNTU(readADC);

    Serial.print("Kekeruhan: ");
    Serial.print(turbidityNTU, 1);
    Serial.print(" NTU - ");

    if (turbidityNTU < 10.0)
    {
      Serial.println("Air Bersih");
    }
    else if (turbidityNTU <= 15.0)
    {
      Serial.println("Air Normal");
    }
    else if (turbidityNTU > 200.0)
    {
      Serial.println("Air Kotor");
    }
    else
    {
      Serial.println("Air Keruh");
    }

    if (turbidityNTU > 200.0)
    {
      String warning = "Peringatan: Air terlalu kotor! Nilai NTU: ";
      warning += String(turbidityNTU, 1);
      bot.sendMessage(chatID, warning, "");
      delay(5000);
    }

    // Kontrol pompa di sini di-nonaktifkan agar tidak konflik dengan fuzzy
    // if (turbidityNTU > 200.0)
    // {
    //   Serial.println("Pompa ON");
    //   digitalWrite(relayPump, HIGH);
    // }
    // else
    // {
    //   Serial.println("Pompa OFF");
    //   digitalWrite(relayPump, LOW);
    // }
  }
}

void tampilkanLCD(DateTime now)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Waktu: ");
  lcd.print(now.hour());
  lcd.print(":");
  if (now.minute() < 10)
    lcd.print("0");
  lcd.print(now.minute());
  lcd.print(":");
  if (now.second() < 10)
    lcd.print("0");
  lcd.print(now.second());

  lcd.setCursor(0, 1);
  lcd.print("Pakan:");
  lcd.print(feedLevel, 1);
  lcd.print("%");

  lcd.setCursor(0, 2);
  lcd.print("Suhu:");
  lcd.print(temperatureC, 1);
  lcd.print(" C");

  lcd.setCursor(0, 3);
  lcd.print("PH :");
  lcd.print(phValue, 2);
  lcd.setCursor(10, 3);
  lcd.print("NTU:");
  lcd.print(turbidityNTU, 1); // Tampilkan 1 angka di belakang koma
}
// methode fuzzy logic Mamdani
//  Fuzzy logic sudah disesuaikan dengan range NTU baru
FuzzyOutput fuzzyLogicControl(float temperature, float ph, float turbidity)
{
  // Membership values
  float tempLow, tempNormal, tempHigh;
  float phAcidic, phNormal, phAlkaline;
  float turbClean, turbNormal, turbDirty;

  // Fuzzy membership functions for temperature
  tempLow = max(0.0f, min(1.0f, (30.0f - temperature) / 10.0f));
  tempNormal = max(0.0f, min(1.0f, min((temperature - 30.0f) / 10.0f, (35.0f - temperature) / 10.0f)));
  tempHigh = max(0.0f, min(1.0f, (temperature - 35.0f) / 10.0f));

  // Fuzzy membership functions for pH
  phAcidic = max(0.0f, min(1.0f, (6.0f - ph) / 1.0f));
  phNormal = max(0.0f, min(1.0f, min((ph - 6.0f) / 1.5f, (8.5f - ph) / 1.5f)));
  phAlkaline = max(0.0f, min(1.0f, (ph - 8.5f) / 1.0f));

  // Fuzzy membership functions for turbidity (NTU)
  turbClean = max(0.0f, min(1.0f, (100.0f - turbidity) / 10.0f));                                  // <10 bersih
  turbNormal = max(0.0f, min(1.0f, min((turbidity - 100.0f) / 5.0f, (15.0f - turbidity) / 5.0f))); // 10-15 normal
  turbDirty = max(0.0f, min(1.0f, (turbidity - 200.0f) / 10.0f));                                  // >200 kotor

  // Rules: Heater hanya ON jika suhu rendah (<20°C) dan air bersih/normal
  float heaterOn = tempLow * max(turbClean, turbNormal);
  float heaterOff = max(
      tempHigh,
      min(tempNormal, max(phNormal, turbClean)));

  // Defuzzification heater
  float heater = 0.0f;
  if ((heaterOn + heaterOff) != 0)
    heater = (heaterOn * 1.0f + heaterOff * 0.0f) / (heaterOn + heaterOff);

  // Defuzzification pump: ON jika NTU > 100
  float pump = (turbidity > 100.0f) ? 1.0f : 0.0f;

  FuzzyOutput result;
  result.heater = heater;
  result.pump = pump;
  return result;
}
// kirim data ke server web
void kirimDataKeServer()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.begin("http://192.168.1.98:5000/kirim-sensor"); // Ganti IP jika perlu
    http.addHeader("Content-Type", "application/json");

    DateTime now = rtc.now(); // Ambil waktu saat ini
    String timestamp = now.timestamp(DateTime::TIMESTAMP_TIME);

    String jsonData = "{";
    jsonData += "\"temperature\":" + String(temperatureC, 2) + ",";
    jsonData += "\"ph\":" + String(phValue, 2) + ",";
    jsonData += "\"turbidity\":" + String(turbidityNTU, 1) + ",";
    jsonData += "\"timestamp\":\"" + timestamp + "\",";
    jsonData += "\"last_feed_time\":\"" + lastFeedTime + "\",";
    jsonData += "\"feedLevel\":" + String(feedLevel, 1) + ",";

    // Feed times array
    jsonData += "\"feedTimes\":[";
    for (int i = 0; i < feedCount; i++)
    {
      jsonData += "{\"hour\":" + String(feedTimes[i][0]) +
                  ",\"minute\":" + String(feedTimes[i][1]) + "}";
      if (i < feedCount - 1)
        jsonData += ",";
    }
    jsonData += "]}"; // ← Tutup array dan objek JSON

    Serial.println("JSON yang dikirim:");
    Serial.println(jsonData); // Untuk debugging

    int httpResponseCode = http.POST(jsonData);
    if (httpResponseCode > 0)
    {
      Serial.print("Data terkirim. Response: ");
      Serial.println(http.getString());
    }
    else
    {
      Serial.print("Gagal kirim data. Kode: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
  else
  {
    Serial.println("WiFi belum terkoneksi!");
  }
}

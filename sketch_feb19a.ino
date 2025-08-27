#include <WiFi.h>                 // Library untuk koneksi WiFi pada ESP32
#include <HTTPClient.h>           // Library untuk HTTP request
#include <OneWire.h>              // Library untuk komunikasi dengan sensor DS18B20
#include <DallasTemperature.h>    // Library untuk sensor suhu DS18B20
#include <NTPClient.h>            // Library untuk mendapatkan waktu dari server NTP
#include <WiFiUdp.h>              // Library untuk komunikasi UDP (digunakan oleh NTPClient)

// Definisi pin sensor dan relay
#define PH_PIN 35       // Pin input untuk sensor pH
#define TDS_PIN 32      // Pin input untuk sensor TDS
#define ONE_WIRE_BUS 4  // Pin untuk sensor suhu DS18B20 (OneWire)
#define RELAY_PUMP1 19  // Pin untuk mengontrol Pompa Nutrisi A
#define RELAY_PUMP2 18  // Pin untuk mengontrol Pompa Nutrisi B
#define RELAY_PUMP3 5   // Pin untuk mengontrol Pompa pH UP
#define RELAY_PUMP4 17  // Pin untuk mengontrol Pompa pH DOWN

// Konfigurasi WiFi
const char* ssid = "Oppo Reno6";       // Nama jaringan WiFi
const char* password = "12345670";    // Password jaringan WiFi

// Konfigurasi Antares API
const char* antaresUrl = "https://platform.antares.id:8443/~/antares-cse/antares-id/TESHidroponik/HasilSensor"; // URL endpoint Antares
const char* antaresOrigin = "c3d8f2397d912e73:0c9b7c4c1b564a5b";  // API key untuk autentikasi ke Antares

// Konstanta Kalibrasi TDS
#define TDS_FACTOR 0.5      // Faktor pengurangan sesuai spesifikasi sensor
#define TDS_KALIBRASI 1.0   // Faktor kalibrasi berdasarkan pengukuran manual

OneWire oneWire(ONE_WIRE_BUS);                 // Inisialisasi komunikasi OneWire untuk sensor DS18B20
DallasTemperature sensors(&oneWire);           // Inisialisasi sensor suhu DS18B20
WiFiUDP ntpUDP;                                // Inisialisasi komunikasi UDP untuk NTP
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000);  // Konfigurasi NTP untuk zona waktu GMT+7 (25200 detik)

// Variabel untuk menyimpan hasil sensor
float pHValue = 0;       
int tdsValue = 0;
float waterTemp = 25.0; // Suhu air default sebelum pembacaan pertama

// Fungsi untuk menghitung nilai pH dari nilai analog
float calculatePH(int analogValue) {
  float voltage = analogValue * (3.3 / 4095.0);  // Konversi nilai ADC (12-bit) ke tegangan (3.3V)
  return 3.5 * voltage + 1.0;  // Persamaan linier untuk mengonversi tegangan ke nilai pH
}

// Fungsi untuk menghitung nilai TDS dari nilai analog dan suhu air
int calculateTDS(int analogValue, float temp) {
  float voltage = analogValue * (3.3 / 4095.0);  // Konversi nilai ADC ke tegangan
  
  // Menghitung nilai EC berdasarkan tegangan output sensor TDS
  float ec = (133.42 * voltage * voltage * voltage - 255.86 * voltage * voltage + 857.39 * voltage) * TDS_FACTOR;
  
  // Kompensasi suhu dengan rumus konduktivitas listrik
  float ecCompensated = ec / (1.0 + 0.02 * (temp - 25.0));
  
  // Kalibrasi berdasarkan nilai referensi manual
  return int(ecCompensated * TDS_KALIBRASI);
}

// Fungsi untuk menghubungkan ESP32 ke jaringan WiFi
void setupWiFi() {
  WiFi.begin(ssid, password); // Mulai koneksi WiFi
  while (WiFi.status() != WL_CONNECTED) { // Tunggu hingga koneksi berhasil
    delay(1000);
    Serial.print("."); // Tampilkan progress di Serial Monitor
  }
  Serial.println("\nWiFi Terhubung!"); // Konfirmasi koneksi berhasil
}

// Fungsi untuk mengirim data ke Antares
void sendToAntares(bool saveToDB) {
  if (WiFi.status() == WL_CONNECTED) { // Periksa apakah WiFi terhubung
    HTTPClient http;  // Inisialisasi HTTP client
    http.begin(antaresUrl);  // Tentukan URL tujuan
    http.addHeader("Content-Type", "application/json;ty=4"); // Tambah header request
    http.addHeader("X-M2M-Origin", antaresOrigin); // Tambah API key
    http.addHeader("Accept", "application/json"); // Tambah header untuk format data

    // Format data dalam JSON untuk dikirim ke Antares
    String jsonData = "{\"m2m:cin\": {\"con\": \"{\\\"pH\\\": " + String(pHValue) + 
                      ", \\\"TDS\\\": " + String(tdsValue) + 
                      ", \\\"SuhuAir\\\": " + String(waterTemp) + 
                      ", \\\"save\\\": " + (saveToDB ? "true" : "false") + "}\"}}";

    int httpResponseCode = http.POST(jsonData); // Kirim data
    Serial.println(httpResponseCode == 201 ? "Data berhasil terkirim ke Antares!" : "Gagal mengirim data."); // Cek hasil pengiriman
    http.end(); // Akhiri koneksi HTTP
  } else {
    Serial.println("WiFi tidak terhubung."); // Tampilkan pesan jika WiFi tidak terhubung
  }
}

void setup() {
  Serial.begin(115200); // Inisialisasi komunikasi serial
  pinMode(PH_PIN, INPUT); // Atur pin sensor pH sebagai input
  pinMode(TDS_PIN, INPUT); // Atur pin sensor TDS sebagai input
  pinMode(RELAY_PUMP1, OUTPUT); // Atur pin relay pompa Nutrisi A sebagai output
  pinMode(RELAY_PUMP2, OUTPUT); // Atur pin relay pompa Nutrisi B sebagai output
  pinMode(RELAY_PUMP3, OUTPUT); // Atur pin relay pompa pH UP sebagai output
  pinMode(RELAY_PUMP4, OUTPUT); // Atur pin relay pompa pH DOWN sebagai output
  digitalWrite(RELAY_PUMP1, HIGH); // Matikan semua relay saat mulai
  digitalWrite(RELAY_PUMP2, HIGH);
  digitalWrite(RELAY_PUMP3, HIGH);
  digitalWrite(RELAY_PUMP4, HIGH);
  sensors.begin(); // Mulai sensor suhu DS18B20
  setupWiFi(); // Hubungkan ke WiFi
  timeClient.begin(); // Mulai NTP untuk mendapatkan waktu
}

void loop() {
  timeClient.update(); // Update waktu dari server NTP
  int currentHour = timeClient.getHours(); // Ambil jam saat ini
  int currentMinute = timeClient.getMinutes(); // Ambil menit saat ini

  // Baca data dari sensor
  int phAnalog = analogRead(PH_PIN); // Baca nilai analog dari sensor pH
  int tdsAnalog = analogRead(TDS_PIN); // Baca nilai analog dari sensor TDS
  sensors.requestTemperatures(); // Minta pembacaan suhu dari DS18B20
  waterTemp = sensors.getTempCByIndex(0); // Simpan suhu yang dibaca

  pHValue = calculatePH(phAnalog); // Hitung nilai pH berdasarkan pembacaan sensor
  tdsValue = calculateTDS(tdsAnalog, waterTemp); // Hitung nilai TDS berdasarkan pembacaan sensor dan suhu

  // Menentukan apakah data harus disimpan ke database
  bool saveToDB = (currentHour >= 18 && currentHour < 22 && currentMinute % 5 == 0);

  sendToAntares(saveToDB); // Kirim data ke Antares

  // Kontrol pompa berdasarkan nilai sensor
  digitalWrite(RELAY_PUMP1, tdsValue < 540 ? LOW : HIGH); // Nyalakan pompa Nutrisi A jika TDS terlalu rendah
  digitalWrite(RELAY_PUMP2, tdsValue > 800 ? LOW : HIGH); // Nyalakan pompa Nutrisi B jika TDS terlalu tinggi
  digitalWrite(RELAY_PUMP3, pHValue < 6 ? LOW : HIGH); // Nyalakan pompa pH UP jika pH terlalu rendah
  digitalWrite(RELAY_PUMP4, pHValue > 7 ? LOW : HIGH); // Nyalakan pompa pH DOWN jika pH terlalu tinggi

  Serial.print("pH: "); Serial.print(pHValue);
  Serial.print(" | TDS: "); Serial.print(tdsValue);
  Serial.print(" | Suhu Air: "); Serial.println(waterTemp);

  delay(5000); // Tunggu 5 detik sebelum membaca ulang
}

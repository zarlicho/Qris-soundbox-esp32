#include "Adafruit_MQTT_Client.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "Adafruit_MQTT.h"
#include "ArduinoJson.h"
#include "Arduino.h"
#include <Arduino.h>
#include <LittleFS.h>
#include "driver/i2s.h"

// dac connection
#define I2S_DOUT  22
#define I2S_BCLK  26
#define I2S_LRC   25

// ============Adafruit MQTT CONFIG============
#define AIO_SERVER "io.adafruit.com"
#define AIO_SERVERPORT 1883
#define AIO_USERNAME "your adafruit_username"
#define AIO_KEY "your adafruit_key"
#define FeedName "your feed_name" // kalau pakai adafruit mqtt, biasanya formatnya username/feeds/feedname

// ---------- WAV parsing ----------
struct WavFmt {
  uint16_t audioFormat;   // 1 = PCM
  uint16_t numChannels;   // 1 = mono, 2 = stereo
  uint32_t sampleRate;    // e.g. 16000
  uint16_t bitsPerSample; // 16
  uint32_t dataOffset;    // posisi awal data PCM
  uint32_t dataSize;      // ukuran data PCM (byte)
};

// PIN CONFIG
int button = 12;
int button2 = 13;
float volume = 5.0f;
long buttonTimer = 0;
long longPressTime = 5000;
bool volActive = false;
boolean buttonActive = false;
boolean longPressActive = false;

// MQTT CONTROL
unsigned long lastMQTTCheck = 0;
unsigned long lastMQTTConnectAttempt = 0;
const unsigned long mqttInterval = 100;
const unsigned long mqttReconnectInterval = 30000; // 30 sec
bool mqttConnected = false;

// ---- WiFi reconnect control ----
unsigned long lastWiFiReconnectAttempt = 0;
const unsigned long wifiReconnectInterval = 10000; // 10s
bool portalRequested = false;

bool hasSavedWiFi() {
  // Deteksi kredensial tersimpan di NVS
  // WiFi.SSID() biasanya berisi SSID terakhir yang tersimpan
  return WiFi.psk().length() > 0;
}

void tryWiFiReconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Lost. Reconnecting...");
    // panggil reconnect cepat
    WiFi.reconnect();
  }
}

// OBJECTS
WiFiClient client;
WiFiClientSecure clientSecure;
HTTPClient http;
WiFiManager wifiManager;

// mqtt config for adafruit only
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);
Adafruit_MQTT_Subscribe sndBox = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/" FeedName);

StaticJsonDocument<1264> doc;

// AIRTABLE CONFIG
const char* airtableToken = "your_airtable_token";  // Personal Access Token
const char* baseId = "your_airtable_base_id";       // Base ID
const char* tableName = "your_airtable_table_name";
String airtableURL = "https://api.airtable.com/v0/" + String(baseId) + "/" + tableName;

// Custom CSS for dark mode and styling
const char* customCSS = R"rawliteral(
<style>
  :root {
    --bg-primary: #1a1a1a;
    --bg-secondary: #2d2d2d;
    --text-primary: #ffffff;
    --text-secondary: #b0b0b0;
    --accent-color: #00d4aa;
    --accent-hover: #00b894;
    --border-color: #404040;
    --input-bg: #333333;
    --danger-color: #ff6b6b;
  }
  
  * {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
  }
  
  body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background: linear-gradient(135deg, var(--bg-primary) 0%, var(--bg-secondary) 100%);
    color: var(--text-primary);
    min-height: 100vh;
    padding: 20px;
    line-height: 1.6;
  }
  
  .container {
    max-width: 500px;
    margin: 0 auto;
    background: var(--bg-secondary);
    border-radius: 15px;
    box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5);
    overflow: hidden;
  }
  
  .header {
    background: linear-gradient(135deg, var(--accent-color), var(--accent-hover));
    padding: 30px 20px;
    text-align: center;
    color: white;
  }
  
  .header h1 {
    font-size: 2.2em;
    margin-bottom: 10px;
    font-weight: 300;
  }
  
  .header p {
    opacity: 0.9;
    font-size: 1.1em;
  }
  
  .content {
    padding: 30px;
  }
  
  h2, h3 {
    color: var(--accent-color);
    margin-bottom: 20px;
    font-weight: 400;
  }
  
  .btn {
    display: inline-block;
    background: var(--accent-color);
    color: white;
    padding: 12px 30px;
    text-decoration: none;
    border-radius: 25px;
    border: none;
    cursor: pointer;
    font-size: 1em;
    transition: all 0.3s ease;
    margin: 10px 5px;
    min-width: 120px;
  }
  
  .btn:hover {
    background: var(--accent-hover);
    transform: translateY(-2px);
    box-shadow: 0 5px 15px rgba(0, 212, 170, 0.3);
  }
  
  .btn-secondary {
    background: transparent;
    border: 2px solid var(--accent-color);
    color: var(--accent-color);
  }
  
  .btn-secondary:hover {
    background: var(--accent-color);
    color: white;
  }
  
  input[type="text"], input[type="password"], select {
    width: 100%;
    padding: 15px;
    background: var(--input-bg);
    border: 2px solid var(--border-color);
    border-radius: 8px;
    color: var(--text-primary);
    font-size: 1em;
    margin: 10px 0;
    transition: border-color 0.3s ease;
  }
  
  input[type="text"]:focus, input[type="password"]:focus, select:focus {
    outline: none;
    border-color: var(--accent-color);
    box-shadow: 0 0 10px rgba(0, 212, 170, 0.2);
  }
  
  .status-indicator {
    display: inline-block;
    width: 12px;
    height: 12px;
    border-radius: 50%;
    margin-right: 10px;
    background: var(--danger-color);
    animation: pulse 2s infinite;
  }
  
  .status-indicator.connected {
    background: var(--accent-color);
  }
  
  @keyframes pulse {
    0% { opacity: 1; }
    50% { opacity: 0.5; }
    100% { opacity: 1; }
  }
  
  .info-card {
    background: var(--bg-primary);
    padding: 20px;
    border-radius: 10px;
    margin: 20px 0;
    border-left: 4px solid var(--accent-color);
  }
  
  .network-list {
    max-height: 300px;
    overflow-y: auto;
  }
  
  .network-item {
    padding: 15px;
    border-bottom: 1px solid var(--border-color);
    cursor: pointer;
    transition: background 0.3s ease;
  }
  
  .network-item:hover {
    background: var(--bg-primary);
  }
  
  .signal-strength {
    float: right;
    color: var(--text-secondary);
  }
  
  .footer {
    text-align: center;
    padding: 20px;
    color: var(--text-secondary);
    border-top: 1px solid var(--border-color);
  }
  
  @media (max-width: 600px) {
    body { padding: 10px; }
    .container { margin: 0; }
    .header h1 { font-size: 1.8em; }
    .content { padding: 20px; }
  }
</style>
)rawliteral";

// Landing page HTML
const char* landingPageHTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Config</title>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>Soundbox Settings</h1>
    </div>
    
    <div class="content">
      <div class="info-card">
        <h3><span class="status-indicator"></span>Device Status</h3>
        <p><strong>Device ID:</strong> BYR-AJA001</p>
        <p><strong>Status:</strong> Ready for Configuration</p>
      </div>
      
      <div style="text-align: center; margin: 30px 0;">
        <p style="color: var(--text-secondary); margin-bottom: 30px;">
          Settings Wifi untuk device Qris soundbox
        </p>
        
        <a href="/wifi" class="btn">🔧 Konfigurasi WiFi</a>
        
      </div>
      
      <div class="info-card">
        <h3>Langkah-langkah</h3>
        <ol style="color: var(--text-secondary); padding-left: 20px;">
          <li>Klik "Konfigurasi WiFi"</li>
          <li>Pilih Wifi yang tersedia di list</li>
          <li>Masukan password</li>
          <li>Klik save dan tunggu hingga terhubung</li>
        </ol>
      </div>
    </div>
    
    <div class="footer">
      <p>Qris Soundbox BayarAja</p>
    </div>
  </div>
</body>
</html>
)rawliteral";

bool parseWavHeader(File &f, WavFmt &fmt) {
  f.seek(0);
  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12) return false;
  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return false;

  bool gotFmt = false, gotData = false;
  uint32_t pos = 12;
  while (pos + 8 <= f.size()) {
    f.seek(pos);
    char chunkId[5] = {0};
    f.read((uint8_t*)chunkId, 4);
    uint32_t chunkSize;
    f.read((uint8_t*)&chunkSize, 4);

    pos += 8;
    if (!memcmp(chunkId, "fmt ", 4)) {
      if (chunkSize < 16) return false;
      uint16_t audioFormat, numChannels, bitsPerSample, blockAlign;
      uint32_t sampleRate, byteRate;
      f.read((uint8_t*)&audioFormat, 2);
      f.read((uint8_t*)&numChannels, 2);
      f.read((uint8_t*)&sampleRate, 4);
      f.read((uint8_t*)&byteRate, 4);
      f.read((uint8_t*)&blockAlign, 2);
      f.read((uint8_t*)&bitsPerSample, 2);
      // skip extra fmt bytes if any
      if (chunkSize > 16) f.seek(pos + chunkSize);

      fmt.audioFormat   = audioFormat;
      fmt.numChannels   = numChannels;
      fmt.sampleRate    = sampleRate;
      fmt.bitsPerSample = bitsPerSample;
      gotFmt = true;
      pos += chunkSize;
    } else if (!memcmp(chunkId, "data", 4)) {
      fmt.dataOffset = pos;
      fmt.dataSize   = chunkSize;
      gotData = true;
      break; // data chunk terakhir yang kita butuhkan
    } else {
      // lewati chunk lain (LIST, fact, dsb.)
      pos += chunkSize;
    }
    // align ke genap
    if (chunkSize & 1) pos++;
  }
  return gotFmt && gotData && fmt.audioFormat == 1 && fmt.bitsPerSample == 16;
}

// ---------- Volume ----------
inline void apply_volume(uint8_t* buffer, size_t bytesRead, float vol) {
  int16_t* s = (int16_t*)buffer;
  size_t n = bytesRead / 2;
  for (size_t i = 0; i < n; i++) {
    int32_t v = (int32_t)(s[i] * vol);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    s[i] = (int16_t)v;
  }
}

// edited i2s_init
void i2s_init(uint32_t sample_rate, int channels) {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = (int)sample_rate,                    // pakai dari WAV
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = channels == 2
        ? I2S_CHANNEL_FMT_RIGHT_LEFT
        : I2S_CHANNEL_FMT_ONLY_RIGHT,                   // atau ONLY_LEFT
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,  // cukup STAND_I2S
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len   = 256,                               // dulu 64
    .use_apll = true,                                   // clock lebih stabil
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  // i2s_pin_config_t pin_config = { I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_PIN_NO_CHANGE };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num  = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };


  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_set_clk(I2S_NUM_0, sample_rate, I2S_BITS_PER_SAMPLE_16BIT,
              (channels == 2) ? I2S_CHANNEL_STEREO : I2S_CHANNEL_MONO);
}

void setupCustomWiFiManager() {
  // Set custom menu items including home/landing page
  std::vector<const char *> menu = {"wifi", "info", "sep", "restart", "exit"};
  wifiManager.setMenu(menu);

  // Set custom CSS for dark mode
  wifiManager.setCustomHeadElement(customCSS);

  // Add custom landing page as root
  wifiManager.setWebServerCallback([&]() {
    wifiManager.server->on("/", [&]() {
      wifiManager.server->send(200, "text/html", String(customCSS) + landingPageHTML);
    });
    
    // Custom info page
    wifiManager.server->on("/info", [&]() {
      String infoHTML = String(customCSS) + R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
          <meta charset="UTF-8">
          <meta name="viewport" content="width=device-width, initial-scale=1.0">
          <title>Device Information</title>
        </head>
        <body>
          <div class="container">
            <div class="header">
              <h1>📊 Device Information</h1>
              <p>System Status and Details</p>
            </div>
            
            <div class="content">
              <div class="info-card">
                <h3>💾 System Information</h3>
                <p><strong>Device ID:</strong> BYR-AJA001</p>
                <p><strong>Merchant ID:</strong> )rawliteral" + subMerchantId + R"rawliteral(</p>
                <p><strong>Volume Level:</strong> )rawliteral" + String(volume) + R"rawliteral(/30</p>
              </div>
              
              <div style="text-align: center; margin: 30px 0;">
                <a href="/" class="btn">🏠 Home</a>
                <a href="/wifi" class="btn">🔧 WiFi Config</a>
              </div>
            </div>
            
            <div class="footer">
              <p>Qris Soundbox BayarAja | Real-time Data</p>
            </div>
          </div>
        </body>
        </html>
      )rawliteral";
      
      wifiManager.server->send(200, "text/html", infoHTML);
    });
  });

  // Configure WiFiManager settings
  wifiManager.setConfigPortalTimeout(300); // 5 minutes timeout
  wifiManager.setConnectTimeout(20);       // 20 seconds to connect
  wifiManager.setAPCallback([](WiFiManager *myWiFiManager) {
    Serial.println("Config portal started");
    Serial.print("Connect to: ");
    Serial.println(myWiFiManager->getConfigPortalSSID());
    Serial.print("Go to: http://");
    Serial.println(WiFi.softAPIP());
  });

  // Set custom AP name and password (optional)
  wifiManager.setAPStaticIPConfig(IPAddress(192, 168, 1, 1), 
                         IPAddress(192, 168, 1, 1), 
                         IPAddress(255, 255, 255, 0));
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(button, INPUT_PULLUP);
  pinMode(button2, INPUT_PULLUP);

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    return;
  }

  // WiFi basic init
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);

  // WiFiManager setup & styling
  setupCustomWiFiManager();
  wifiManager.setWiFiAutoReconnect(true);

  // KUNCI: matikan fallback portal otomatis.
  wifiManager.setEnableConfigPortal(false);
  // Waktu connect pendek saja; selebihnya kita handle sendiri.
  wifiManager.setConnectTimeout(15);

  play(19);

  bool connected = false;

  if (hasSavedWiFi()) {
    Serial.println("[WiFi] Saved credentials detected. Fast connect...");
    // Coba pakai kredensial tersimpan TANPA membuka portal.
    connected = wifiManager.autoConnect(); // dengan enableConfigPortal(false) ini tidak akan buka portal
    if (!connected) {
      Serial.println("[WiFi] Fast connect failed, will retry in loop (no portal).");
    }
  } else {
    Serial.println("[WiFi] No saved credentials. Starting config portal...");
    // First-time provisioning: buka portal sekali ini.
    // (Kalau mau SSID/Password portal custom, pakai param di sini)
    connected = wifiManager.startConfigPortal("BYRaja-dvc-01", "12345678");
  }

  if (!connected && !hasSavedWiFi()) {
    // Kalau user kabur dari portal tanpa save, aman: kita reboot saja
    Serial.println("[WiFi] No creds saved. Restarting...");
    delay(1000);
    ESP.restart();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected successfully!");
    Serial.print("IP address: "); Serial.println(WiFi.localIP());
    Serial.print("MAC address: "); Serial.println(WiFi.macAddress());
  } else {
    Serial.println("[WiFi] Not connected yet. Will keep retrying silently.");
  }

  // MQTT init
  mqtt.subscribe(&sndBox);
  clientSecure.setInsecure();
  http.addHeader("Content-Type", "application/json");

  MQTT_connect();
}

// ================= LOOP =================
void loop() {
  ResetSettings(); // long-press bisa minta portal on-demand

  unsigned long now = millis();

  // Reconnect WiFi diam-diam tiap 10 detik kalau putus
  if (now - lastWiFiReconnectAttempt >= wifiReconnectInterval) {
    lastWiFiReconnectAttempt = now;
    if (WiFi.status() != WL_CONNECTED) {
      tryWiFiReconnect();
    }
  }

  // MQTT reconnect logic (punyamu)
  if (!mqttConnected && (now - lastMQTTConnectAttempt >= mqttReconnectInterval)) {
    Serial.println("Attempting MQTT reconnection...");
    MQTT_connect();
    lastMQTTConnectAttempt = now;
  }

  if (mqttConnected && (now - lastMQTTCheck >= mqttInterval)) {
    lastMQTTCheck = now;

    if (!mqtt.connected()) {
      Serial.println("MQTT connection lost!");
      mqttConnected = false;
      lastMQTTConnectAttempt = now;
      return;
    }

    Adafruit_MQTT_Subscribe *subscription = mqtt.readSubscription(0);
    if (subscription && subscription == &sndBox) {
      const char *document = (const char *)sndBox.lastread;
      GetPay(document);
    }
  }
}

// ================= OTHER FUNCTIONS =================
void GetPay(const char *document) {
  size_t documentLength = strlen(document);
  if (documentLength > 0) {
    DeserializationError error = deserializeJson(doc, document);
    if (!error) {
      // ============FOR midtrans============
      if (doc["transaction_status"] == "settlement"){
        long amount = doc["gross_amount"];
        String created = doc["transaction_time"];
        const char *status = doc["transaction_status"];
        const char *source = doc["payment_type"];
        const char *nameHolder = doc["transaction_id"];
        
        Serial.print("PAYMENT SUCCESS WITH ");
        Serial.println(source);
        Serial.println();
        Serial.print(F("amount: "));
        Serial.println(amount);
        Serial.print(F("status: "));
        Serial.println(status);
        Serial.println(created);
        num2sound((int)amount);
        sendDataToAirtable(created, nameHolder, status, source, amount);
      }
    }
  }
}

void MQTT_connect() {
  int8_t ret;
  if (mqtt.connected()) {
    mqttConnected = true;
    return;
  }
  Serial.print("Connecting to MQTT... ");
  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0 && retries > 0) {
    Serial.println(mqtt.connectErrorString(ret));
    mqtt.disconnect();
    delay(2000);
    retries--;
  }
  if (ret == 0) {
    Serial.println("MQTT Connected!");
    mqttConnected = true;
    play(23);
  } else {
    Serial.println("MQTT connection failed.");
    mqttConnected = false;
  }
}

void play(int fileNumber) {
  String path = String("/")+ String(fileNumber) + ".wav";
  File file = LittleFS.open(path, "r");
  if (!file) { Serial.printf("Open fail: %s\n", path); return; }

  WavFmt fmt;
  if (!parseWavHeader(file, fmt)) {
    Serial.println("WAV parse error (harus PCM 16-bit).");
    file.close();
    return;
  }

  // init I2S sesuai file
  i2s_driver_uninstall(I2S_NUM_0); // bersihkan jika ada
  i2s_init(fmt.sampleRate, fmt.numChannels);

  // lompat ke awal data PCM sebenarnya
  file.seek(fmt.dataOffset);

  // buffer lebih besar → stabil
  static uint8_t buffer[2048];
  Serial.printf("Play %s @ %lu Hz, %u ch, 16-bit, data=%lu bytes, vol=%.2f\n",
                path, (unsigned long)fmt.sampleRate, fmt.numChannels,
                (unsigned long)fmt.dataSize, volume);

  uint32_t remaining = fmt.dataSize;
  while (remaining > 0) {
    size_t toRead = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
    size_t bytesRead = file.read(buffer, toRead);
    if (bytesRead == 0) break;

    apply_volume(buffer, bytesRead, volume);

    size_t bytesWritten = 0;
    i2s_write(I2S_NUM_0, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
    remaining -= bytesRead;
  }

  file.close();
  Serial.println("Done.");
}

/*
void play(int fileNumber) {
  String myAudio = String("/01/")+ String(fileNumber) + ".wav";
  File file = SD.open(myAudio);
  if (!file) {
    Serial.println("Failed to open file");
    return; // Changed from while(1) to return to prevent infinite loop
  }

  skip_wav_header(file);

  uint8_t buffer[1024];
  Serial.println("Playing WAV...");
  while (file.available()) {
    size_t bytesRead = file.read(buffer, sizeof(buffer));
    size_t bytesWritten;
    i2s_write(I2S_NUM_0, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
  }

  file.close();
  Serial.println("Playback done.");
}
*/

void num2sound(int bill) {
  play(20);
  String blng = Terbilang(bill) + " 22";
  for (int i = 0; i < blng.length(); i++) {
    String word = "";
    while (i < blng.length() && blng[i] != ' ') {
      word += blng[i];
      i++;
    }
    if (word != "") {
      play(word.toInt());
      Serial.print("FileName: ");
      Serial.println(word.toInt());
      if (i == blng.length());
    }
  }
}

String Terbilang(int n) {
  String hasil = "";
  if (n == 0) return hasil;
  if (n >= 0 && n <= 11) hasil = String(n);
  else if (n < 20) hasil = Terbilang(n % 10) + " 0012 ";
  else if (n < 100) hasil = Terbilang(n / 10) + " 0014 " + Terbilang(n % 10);
  else if (n < 200) hasil = " 0017 " + Terbilang(n - 100);
  else if (n < 1000) hasil = Terbilang(n / 100) + " 0015 " + Terbilang(n % 100);
  else if (n < 2000) hasil = " 0018 " + Terbilang(n - 1000);
  else if (n < 1000000) hasil = Terbilang(n / 1000) + " 0016 " + Terbilang(n % 1000);
  else if (n < 1000000000) hasil = Terbilang(n / 1000000) + " 0013 " + Terbilang(n % 1000000);
  return hasil;
}

void sendDataToAirtable(String created, String transactionID, String paymentStatus, String paymentType, long amount) {
  StaticJsonDocument<512> jsonDoc;
  JsonObject fields = jsonDoc.createNestedObject("fields");

  fields["Created"] = created;
  fields["TransactionID"] = transactionID;
  fields["PaymentStatus"] = paymentStatus;
  fields["PaymentType"] = paymentType;
  fields["Amount"] = amount;  // ✅ now this is a number

  String jsonPayload;
  serializeJson(jsonDoc, jsonPayload);

  Serial.println("Sending payload:");
  Serial.println(jsonPayload);

  http.begin(clientSecure, airtableURL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(airtableToken));

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    Serial.println("Response:");
    Serial.println(response);
  } else {
    Serial.printf("POST failed. Error code: %d\n", httpResponseCode);
  }

  http.end();
  http.begin(clientSecure, airtableURL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(airtableToken));
}

void ResetSettings() {
  // Long-press pada 'button' untuk reset WiFi & buka portal
  if (digitalRead(button) == LOW) {
    if (!buttonActive) {
      buttonActive = true;
      buttonTimer = millis();
    }
    if ((millis() - buttonTimer > longPressTime) && !longPressActive) {
      play(24);
      longPressActive = true;

      Serial.println("[WiFi] Clearing saved credentials...");
      wifiManager.resetSettings();         // hapus kredensial dari NVS
      WiFi.disconnect(true, true);         // benar-benar putus dan hapus

      delay(500);

      // Mulai portal secara eksplisit (blocking) supaya user langsung set ulang
      Serial.println("[WiFi] Starting config portal by user request...");
      // Styling & menu tetap berlaku dari setupCustomWiFiManager()
      // Matikan disable portal agar bisa tampil sekarang
      wifiManager.setEnableConfigPortal(true);
      bool ok = wifiManager.startConfigPortal("BYRaja-dvc-01", "12345678");

      if (ok) {
        Serial.println("[WiFi] New credentials saved. Rebooting...");
        delay(500);
        ESP.restart();
      } else {
        Serial.println("[WiFi] Portal closed without saving. Staying idle.");
        // Setelah batal, nonaktifkan lagi fallback portal untuk normal run
        wifiManager.setEnableConfigPortal(false);
      }
    }
  } else {
    buttonActive = false;
    longPressActive = false;
  }

  // (Bagian tombol volume punyamu – aku biarkan seperti semula)
  if (digitalRead(button) == LOW) {
    if (!volActive) { volActive = true; }
  } else if (digitalRead(button2) == LOW) {
    if (!volActive) { volActive = true; }
  } else {
    volActive = false;
  }
}
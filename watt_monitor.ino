#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include "SPIFFSIni.h"
#include "favicon_data.h"
#include <math.h>
#include <driver/adc.h>

// ==================== 設定 ====================
const uint32_t ADC_MAX = 4095;
const double ESP32_VCC = 3.3;
const double VOLTAGE = 100.0;
const double calibration = 15.0;

const int adc_pins[4] = {32, 33, 34, 35};  // CH1〜CH4

const int LED_PIN = 2;
const int SW_PIN = 0;
const int wifi_timeout_sec = 10;
const int wifi_retry_count = 3;
const unsigned long wifi_check_interval = 60000; // 1分（60,000ms）
const unsigned long wifi_recconect_interval = 30000;  // 30[s]
unsigned long last_wiFi_check = 0;

// 永続化設定（デフォルト値）
int num_channel = 4;
float watt_gain = 1.00;
float watt_bias = -5.0;


// ==================== グローバル ====================
WebServer server(80);

double offset[4] = {ADC_MAX / 2.0, ADC_MAX / 2.0, ADC_MAX / 2.0, ADC_MAX / 2.0};

// 各チャネルの最新値と1分移動平均（60要素FIFO）
double latest_power[4] = {0};
double avg_power[4] = {0};
double power_fifo[4][60] = {{0}};
int fifo_index[4] = {0};

SPIFFSIni config("/config.ini", true);

// ==================== 電力測定タスク (Core 0) ====================
void powerTask(void *pvParameters) {
  const uint16_t samples = 1024;

  while (true) {
    for (int ch = 0; ch < num_channel; ch++) {
      double sum_squares = 0.0;
      int pin = adc_pins[ch];

      for (uint16_t i = 0; i < samples; i++) {
        double val = (double)analogRead(pin);
        offset[ch] += (val - offset[ch]) / 4096.0;
        double filtered = val - offset[ch];
        sum_squares += filtered * filtered;
      }

      double coeff = calibration * (ESP32_VCC / ADC_MAX);
      double Irms = coeff * sqrt(sum_squares / samples);
      if (Irms < 0.0) Irms = 0.0;

      double raw_power = Irms * VOLTAGE;
      double corrected = raw_power * watt_gain + watt_bias;
      if (corrected < 0.0) corrected = 0.0;

      // FIFO更新 & 移動平均計算
      power_fifo[ch][fifo_index[ch]] = corrected;
      fifo_index[ch] = (fifo_index[ch] + 1) % 60;

      double sum = 0.0;
      for (int i = 0; i < 60; i++) sum += power_fifo[ch][i];
      avg_power[ch] = sum / 60.0;

      latest_power[ch] = corrected;
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // 1秒間隔
  }
}

// ==================== WebページHTML ====================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <title>ESP32 Watt Monitor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; text-align: center; margin: 20px; }
    table { margin: 20px auto; border-collapse: collapse; }
    th, td { padding: 10px; border: 1px solid #ccc; }
    .value { font-size: 1.5em; font-weight: bold; }
  </style>
</head>
<body>
  <h1>ESP32 Watt Monitor</h1>
  <table>
    <tr><th>Channel</th><th>Now (W)</th><th>1min Avg (W)</th></tr>
    <tr><td>CH1</td><td class="value" id="p1">-</td><td class="value" id="a1">-</td></tr>
    <tr><td>CH2</td><td class="value" id="p2">-</td><td class="value" id="a2">-</td></tr>
    <tr><td>CH3</td><td class="value" id="p3">-</td><td class="value" id="a3">-</td></tr>
    <tr><td>CH4</td><td class="value" id="p4">-</td><td class="value" id="a4">-</td></tr>
  </table>
  <p>Last update: <span id="time">-</span></p>
  <p><a href="/config">Config Page</a></p>

<script>
function update() {
  fetch('/api/power')
    .then(r => r.json())
    .then(data => {
      const numChannels = data.latest.length;
      for (let i = 1; i <= numChannels; i++) {
        document.getElementById('p' + i).textContent = data.latest[i-1].toFixed(1);
        document.getElementById('a' + i).textContent = data.avg[i-1].toFixed(1);
      }
      document.getElementById('time').textContent = new Date().toLocaleTimeString();
    });
}
setInterval(update, 1000);
update();
</script>
</body>
</html>
)rawliteral";

const char config_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <title>Config</title>
  <style> body { font-family: Arial; text-align: center; margin: 40px; } </style>
</head>
<body>
<h1>Config</h1>
<form action="/save" method="post">
  Channels (1-4): <input type="number" name="num_channel" min="1" max="4" value="%NUM_CHANNEL%"><br><br>
  Watt Gain: <input type="text" name="watt_gain" value="%WATT_GAIN%"><br><br>
  Watt Bias: <input type="text" name="watt_bias" value="%WATT_BIAS%"><br><br>
  <input type="submit" value="Save">
</form>
<p><a href="/">Back to Monitor</a></p>
<div id="msg"></div>
</body>
</html>
)rawliteral";

String serial_input_sync(String msg) {
    Serial.println(msg);
    while (Serial.available() == 0) {}
    String input_str = Serial.readStringUntil('\n');
    input_str.trim();
    return input_str;
}

// ==================== setup ====================
void setup() {
  Serial.begin(115200);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  if (config.exist("num_channel")) num_channel = config.read("num_channel").toInt();
  if (config.exist("watt_gain")) watt_gain = config.read("watt_gain").toFloat();
  if (config.exist("watt_bias")) watt_bias = config.read("watt_bias").toFloat();
  num_channel = constrain(num_channel, 1, 4);

  // ADC設定
  analogReadResolution(12);
  for (int i = 0; i < 4; i++) {
    adc1_config_channel_atten((adc1_channel_t)(adc_pins[i] - 32 + ADC1_CHANNEL_0), ADC_ATTEN_DB_11);
  }

  /* MACアドレス表示 */
  {
    uint8_t baseMac[6];
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    char baseMacChr[18] = {0};
    sprintf(baseMacChr, "%02X:%02X:%02X:%02X:%02X:%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
    Serial.print("MAC: ");
    Serial.println(baseMacChr);
  }

  /* Wifi接続設定 */
  String ssid = config.read("ssid");
  String pass = config.read("pass");
  String ipaddr = config.read("ipaddr");
  String gateway = config.read("gateway");
  String subnet = config.read("subnet");
  String dnsaddr = config.read("dnsaddr.");

  /*reset wifi settings*/
  Serial.println("To reset the SSID, press the y key within 3 seconds.");
  delay(3*1000);
  if (Serial.available() > 0) {
    int inmyte = Serial.read();
    if (inmyte == 'y') {
      ssid = "";
      pass = "";
    }
    String frush_str = Serial.readString(); // flush buffer
  }

  bool new_ssid_pass = false;
  if (ssid == "" || pass == "") {
    while(true) {
      Serial.println("input wifi setting. ssid and pass.");
      ssid = serial_input_sync("ssid?");
      pass = serial_input_sync("pass?");
      String confirm_msg = "ssid:" + ssid + "  pass:" + pass + "\r\n";
      if (serial_input_sync("use fixed ip? (yes/no)") == "yes") {
        ipaddr = serial_input_sync("ip addr?");
        gateway = serial_input_sync("gateway?");
        subnet = serial_input_sync("subnet mask?");
        dnsaddr = serial_input_sync("dnsaddr addr?");
        confirm_msg +=
          "ipaddr:" + ipaddr + "  gateway:" + gateway + "\r\n" +
          "subnet:" + subnet + "  dnsaddr:" + dnsaddr +"\r\n";
      }
      String yes_no = serial_input_sync(confirm_msg + "OK? (yes/no)");
      if (yes_no == "yes" || yes_no == "y") {
        new_ssid_pass = true;
        break;
      }
    }
  }

  Serial.println("WiFi.begin");
  const char* ssid_c_str = ssid.c_str();
  const char* pass_c_str = pass.c_str();
  IPAddress ip_ipaddr;
  IPAddress ip_gateway;
  IPAddress ip_subnet;
  IPAddress ip_dnsaddr;
  bool is_ipaddr = ip_ipaddr.fromString(ipaddr);
  bool is_gateway = ip_gateway.fromString(gateway);
  bool is_subnet = ip_subnet.fromString(subnet);
  bool is_dnsaddr = ip_dnsaddr.fromString(dnsaddr);
  if (is_ipaddr && is_gateway && is_subnet && is_dnsaddr) {
    Serial.println("fixed ip");
    Serial.println(ip_ipaddr);
    Serial.println(ip_gateway);
    Serial.println(ip_subnet);
    Serial.println(ip_dnsaddr);
    delay(100);
    if (!WiFi.config(ip_ipaddr, ip_gateway, ip_subnet, ip_dnsaddr)) {
      Serial.println("STA Failed to configure");
      Serial.println("Restart ...");
      esp_restart();
    }
  }
  int wifi_status = WL_DISCONNECTED;
  WiFi.begin(ssid_c_str, pass_c_str);
  for(int retry=0; retry<wifi_retry_count; retry++) {
    for (int i=0; (i < wifi_timeout_sec*2) && (wifi_status!= WL_CONNECTED); i++) {
      Serial.print(".");
      wifi_status = WiFi.status();
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(400);
    }
    if (wifi_status == WL_CONNECTED) {
      break;
    } else {
      Serial.println("\nReconnecting to WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
    }
  }

  if (wifi_status != WL_CONNECTED) {
    Serial.println("Wifi connect failed.");
    Serial.println("Restart ...");
    esp_restart();
    return;
  } else {
    Serial.println("WiFi connected!!");
    Serial.print("ssid: ");
    Serial.println(ssid);
    String current_ipaddr = WiFi.localIP().toString();
    Serial.print("ip addr: ");
    Serial.println(current_ipaddr);
    if (new_ssid_pass) {
      config.write("ssid", ssid);
      config.write("pass", pass);
      config.write("ipaddr", ipaddr);
      config.write("gateway", gateway);
      config.write("subnet", subnet);
      config.write("dnsaddr.", dnsaddr);
    }
  }

  // ====== Webサーバー設定（同期型）======
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", index_html);
  });

  server.on("/api/power", HTTP_GET, []() {
    String json = "{\"latest\":[";
    for (int i = 0; i < num_channel; i++) {
      json += String(latest_power[i], 1);
      if (i < num_channel - 1) json += ",";
    }
    json += "],\"avg\":[";
    for (int i = 0; i < num_channel; i++) {
      json += String(avg_power[i], 1);
      if (i < num_channel - 1) json += ",";
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  server.on("/favicon.ico", HTTP_GET, []() {
    server.send_P(200, "image/png", (const char*)favicon_data, favicon_size);
  });

  server.on("/config", HTTP_GET, []() {
    String html = String(config_html);
    html.replace("%NUM_CHANNEL%", String(num_channel));
    html.replace("%WATT_GAIN%", String(watt_gain, 4));
    html.replace("%WATT_BIAS%", String(watt_bias, 4));
    server.send(200, "text/html", html.c_str());
  });

  server.on("/save", HTTP_POST, []() {
  String message = "<h1 style='color:green;'>Configuration Saved Successfully!</h1>"
                   "<p>Settings have been applied immediately.</p>"
                   "<p><a href='/'>Back to Monitor</a> | <a href='/config'>Back to Config</a></p>";

    bool changed = false;

    if (server.hasArg("num_channel")) {
      int new_val = server.arg("num_channel").toInt();
      if (new_val >= 1 && new_val <= 4 && new_val != num_channel) {
        num_channel = new_val;
        config.write("num_channel", String(num_channel));
        changed = true;
      }
    }
    if (server.hasArg("watt_gain")) {
      float new_val = server.arg("watt_gain").toFloat();
      if (new_val != watt_gain) {
        watt_gain = new_val;
        config.write("watt_gain", String(watt_gain));
        changed = true;
      }
    }
    if (server.hasArg("watt_bias")) {
      float new_val = server.arg("watt_bias").toFloat();
      if (new_val != watt_bias) {
        watt_bias = new_val;
        config.write("watt_bias", String(watt_bias));
        changed = true;
      }
    }

    if (!changed) {
      message = "<h1 style='color:orange;'>No changes detected.</h1>"
                "<p><a href='/config'>Back to Config</a></p>";
    }

    server.send(200, "text/html", message);
  });

  server.begin();
  Serial.println("HTTP server started");

  // 電力測定タスク起動
  xTaskCreatePinnedToCore(powerTask, "PowerTask", 10000, NULL, 1, NULL, 0);
}

static int blinkLED = 0;
void loop() {
  digitalWrite(LED_PIN, (++blinkLED&1));
  server.handleClient();
  delay(1);

  // 1分ごとにWiFi接続を確認
  unsigned long currentMillis = millis();
  if (currentMillis - last_wiFi_check >= wifi_check_interval) {
      last_wiFi_check = currentMillis;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected. Attempting to reconnect...");
        WiFi.disconnect();
        WiFi.reconnect();
        
        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && 
               millis() - startAttemptTime < wifi_recconect_interval) {
            delay(100);
            Serial.print(".");
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi reconnected");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("\nFailed to reconnect to WiFi");
        }
    }
  }

}
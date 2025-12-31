#include <driver/adc.h>
#include <math.h>
#include "SPIFFSIni.h"
#include <WiFi.h>

/*電力系定義*/
const int ADC_PIN = 34;
const uint32_t ADC_MAX = 4095;
const double ESP32_VCC = 3.3;
const double VOLTAGE = 100.0;
const double calibration = 15.0;
double offset = ADC_MAX / 2.0;
int num_channel = 4;

/* 電力線形補正初期値 */
float gain = 1.060;
float bias = -42.0;

/*Wifi系定義*/
const int wifi_timeout_sec = 10;
const int wifi_retry_count = 3;

/*gpio定義*/
const int LED_PIN = 2;
const int SW_PIN = 0;

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(90000);
  SPIFFSIni config("/config.ini", true);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(SW_PIN, INPUT_PULLUP);

  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);

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
}

String serial_input_sync(String msg) {
    Serial.println(msg);
    while (Serial.available() == 0) {}
    String input_str = Serial.readStringUntil('\n');
    input_str.trim();
    return input_str;
}


double getIrms(uint16_t samples, int adc_pin) {
  double sum_squares = 0.0;
  for (uint16_t i = 0; i < samples; i++) {
    double val = (double)analogRead(adc_pin);
    offset += (val - offset) / 4096.0;
    double filtered = val - offset;
    sum_squares += filtered * filtered;
  }

  double coeff = calibration * (ESP32_VCC / ADC_MAX);
  double Irms = coeff * sqrt(sum_squares / samples);

  if (Irms < 0.0) Irms = 0.0;

  return Irms;
}

void loop() {
  double Irms = getIrms(1024, ADC_PIN);
  double power = Irms * VOLTAGE;

  double corrected_power = power * gain + bias;
  if (corrected_power < 0.0) corrected_power = 0.0;

  Serial.print("Current: ");
  Serial.print(Irms, 3);
  Serial.print(" A  |  Power: ");
  Serial.print(power, 1);
  Serial.print(" W  |  Corrected: ");
  Serial.print(corrected_power, 1);
  Serial.println(" W");

  delay(1000);
}
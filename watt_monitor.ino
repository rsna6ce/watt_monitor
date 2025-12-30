#include <driver/adc.h>
#include <math.h>

const int ADC_PIN = 34;
const uint32_t ADC_MAX = 4095;  // 12bit
const double ESP32_VCC = 3.3;
const double VOLTAGE = 100.0;
const double calibration = 15.0;  // 実測に合った値としてそのまま使用

double offset = ADC_MAX / 2.0;

// 線形補正（最小限に抑えておく）
const float gain = 1.060;
const float bias = -42.0;

void setup() {
  Serial.begin(115200);

  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);

  Serial.println("ESP32 + SCT-013-015 電力測定（最終版・MITライセンス）");
  Serial.println("Current (A) | Power (W)");
}

double getIrms(uint16_t samples, int adc_pin) {
  double sum_squares = 0.0;

  for (uint16_t i = 0; i < samples; i++) {
    double val = (double)analogRead(adc_pin);

    // EmonLib-esp32準拠の緩やかフィルタ
    offset += (val - offset) / 4096.0;

    double filtered = val - offset;
    sum_squares += filtered * filtered;
  }

  // あなたのオリジナルで実測に最適だったスケーリング
  double coeff = calibration * (ESP32_VCC / ADC_MAX);
  double Irms = coeff * sqrt(sum_squares / samples);

  if (Irms < 0.0) Irms = 0.0;

  return Irms;
}

void loop() {
  double Irms = getIrms(1024, ADC_PIN);

  double power = Irms * VOLTAGE;

  // 必要なら最小限の線形補正（まずはgain=1.0, bias=0.0でテスト）
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
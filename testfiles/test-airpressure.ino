/*
 * 气压传感器读取程序 (Arduino Mega 2560) - 带启动基线校准
 * 传感器输出：0.5~4.5V 对应 -100~300 kPa
 * 接口：A0, A1, A2, A3
 * 
 * 校准逻辑：开机后自动采集前50组数据（每组读取所有4个通道），
 * 分别计算每个通道的压力平均值作为基线偏移。
 * 后续实时压力 = 原始压力 - 基线压力（显示相对变化量）
 */

// 定义模拟输入引脚
const int sensorPins[] = {A0, A1, A2, A3};
const int numSensors = 4;

// 传感器量程参数
const float V_MIN = 0.5;      // 最小输出电压 (V)
const float V_MAX = 4.5;      // 最大输出电压 (V)
const float P_MIN = -100.0;   // 最小压力 (kPa)
const float P_MAX = 300.0;    // 最大压力 (kPa)

// 转换斜率 (kPa/V)
const float slope = (P_MAX - P_MIN) / (V_MAX - V_MIN); // = 100 kPa/V

// 基线压力数组（存储每个通道的偏移量，单位 kPa）
float basePressure[numSensors] = {0, 0, 0, 0};

// 校准参数
const int CALIBRATION_SAMPLES = 50;   // 采集50组数据进行校准

void setup() {
  Serial.begin(115200);
  analogReference(DEFAULT);    // 使用5V参考电压

  Serial.println("=== Pressure Sensor Calibration ===");
  Serial.print("Collecting "); Serial.print(CALIBRATION_SAMPLES);
  Serial.println(" samples for baseline...");

  // ---------- 基线校准 ----------
  // 临时累加器
  float sumPressure[numSensors] = {0.0, 0.0, 0.0, 0.0};

  for (int sample = 0; sample < CALIBRATION_SAMPLES; sample++) {
    for (int i = 0; i < numSensors; i++) {
      int adc = analogRead(sensorPins[i]);
      float voltage = adc * (5.0 / 1023.0);
      float pressure = slope * (voltage - V_MIN) + P_MIN;
      sumPressure[i] += pressure;
    }
    delay(10); // 轻微延迟，确保采样间隔均匀（可选）
  }

  // 计算各通道基线平均值
  for (int i = 0; i < numSensors; i++) {
    basePressure[i] = sumPressure[i] / CALIBRATION_SAMPLES;
  }

  // 打印校准结果
  Serial.println("Calibration complete. Baseline pressures (kPa):");
  for (int i = 0; i < numSensors; i++) {
    Serial.print("A"); Serial.print(i); Serial.print(": ");
    Serial.println(basePressure[i], 2);
  }
  Serial.println("--- Now outputting calibrated (relative) pressures ---");
  Serial.println("Channel\tVoltage(V)\tPressure(kPa)\tRelative(kPa)");
}

void loop() {
  // 读取各通道原始数据并计算校准后的压力
  for (int i = 0; i < numSensors; i++) {
    int adcValue = analogRead(sensorPins[i]);
    float voltage = adcValue * (5.0 / 1023.0);
    float rawPressure = slope * (voltage - V_MIN) + P_MIN;
    float calibratedPressure = rawPressure - basePressure[i]; // 减去基线

    // 输出：通道、电压、原始压力（供参考）、校准后压力
    Serial.print("A"); Serial.print(i);
    Serial.print("\t");
    Serial.print(voltage, 3);
    Serial.print("\t\t");
    Serial.print(rawPressure, 2);
    Serial.print("\t\t");
    Serial.println(calibratedPressure, 2);
  }

  Serial.println(); // 空行分隔
  delay(500);       // 每500ms更新一次
}

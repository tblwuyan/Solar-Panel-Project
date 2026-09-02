// JY901 库已注释（不再使用）
// #include <Wire.h>
// #include <JY901.h>

// 定义四个气压传感器引脚（A0~A3）
const int pressureSensorPins[] = {A0, A1, A2, A3};
const int numSensors = 4;

// 传感器量程参数（0.5~4.5V 对应 -100~300 kPa）
const float V_MIN = 0.5;
const float V_MAX = 4.5;
const float P_MIN = -100.0;
const float P_MAX = 300.0;
const float SLOPE = (P_MAX - P_MIN) / (V_MAX - V_MIN); // = 100 kPa/V

void setup() {
  Serial.begin(115200);        // 调试串口
  // Serial1.begin(115200);   // JY901 串口（已注释）
}

void loop() {
  float pressures[4];

  // 依次读取四个气压传感器
  for (int i = 0; i < numSensors; i++) {
    int adc = analogRead(pressureSensorPins[i]);
    float voltage = adc * (5.0 / 1023.0);
    pressures[i] = (voltage - V_MIN) * SLOPE + P_MIN; // 线性转换
  }

  // 串口输出四个压力值（保留2位小数）
  Serial.print("Pressure0, ");
  Serial.print(pressures[0], 2);
  Serial.print(" kPa");

  Serial.print(", Pressure1, ");
  Serial.print(pressures[1], 2);
  Serial.print(" kPa");

  Serial.print(", Pressure2, ");
  Serial.print(pressures[2], 2);
  Serial.print(" kPa");

  Serial.print(", Pressure3, ");
  Serial.print(pressures[3], 2);
  Serial.println(" kPa");

  delay(10); // 采样间隔 10ms
}

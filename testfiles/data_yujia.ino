#include <Wire.h>
#include <JY901.h>

// 定义气压传感器连接的模拟输入引脚
const int pressureSensorPin = A0;
const int pressureSensorPin1 = A1;
const int forceSensorPin = A2; // 定义力传感器连接的模拟输入引脚
const int disSensorPin = A3;   // 定义位移传感器连接的模拟输入引脚

unsigned long lastReadTime = 0; // 上次读取时间

void setup() {
  Serial.begin(115200);  // 用于调试输出
  Serial1.begin(115200); // JY901的波特率
  setJY901Rate(100);     // 设置JY901的采集速率为100Hz
}

void setJY901Rate(int rate) {
  byte rateCommand[4] = {0x55, 0x03, 0x00, 0x00}; // 默认指令格式
  rateCommand[2] = rate; // 设置采集速率值
  rateCommand[3] = 0x55 ^ 0x03 ^ rate; // 校验和计算
  Serial1.write(rateCommand, 4); // 发送指令
  delay(10); // 等待模块响应
}

void loop() {
  // ----- 始终处理 JY901 串口数据（保证角度实时更新） -----
  while (Serial1.available()) {
    JY901.CopeSerialData(Serial1.read());
  }

  // ----- 每 200ms 执行一次传感器读取和串口输出 -----
  if (millis() - lastReadTime >= 200) {
    lastReadTime = millis(); // 更新时间戳

    // 读取气压传感器1
    int sensorValue = analogRead(pressureSensorPin);
    float voltage = sensorValue * (5.0 / 1023.0);
    float pressure = (voltage - 0.5) * (400.0 / 4.0) - 100.0;

    // 读取气压传感器2
    int sensorValue1 = analogRead(pressureSensorPin1);
    float voltage1 = sensorValue1 * (5.0 / 1023.0);
    float pressure1 = (voltage1 - 0.5) * (400.0 / 4.0) - 100.0;

    // 读取力传感器
    int forceSensorValue = analogRead(forceSensorPin);
    float forceVoltage = forceSensorValue * (5.0 / 1023.0);
    float force = forceVoltage * 10;  // 50V量程是10，20V量程是4

    // 读取位移传感器
    int disSensorValue = analogRead(disSensorPin);
    float disVoltage = disSensorValue * (5.0 / 1023.0);
    float dis = disVoltage * 100;

    // 输出所有数据（单行）
    Serial.print("Pressure, ");
    Serial.print(pressure);
    Serial.print(" kPa");

    Serial.print(", Pressure1, ");
    Serial.print(pressure1);
    Serial.print(" kPa");

    // 角度数据（从JY901结构体读取）
    Serial.print(", Angle, ");
    Serial.print((float)JY901.stcAngle.Angle[0] / 32768 * 180); // 横滚角
    Serial.print(", ");
    Serial.print((float)JY901.stcAngle.Angle[1] / 32768 * 180); // 俯仰角
    Serial.print(", ");
    Serial.print((float)JY901.stcAngle.Angle[2] / 32768 * 180); // 偏航角

    Serial.print(", Force, ");
    Serial.print(force);
    Serial.print(" N");

    Serial.print(", dis, ");
    Serial.print(dis);
    Serial.println(" mm");
  }
}

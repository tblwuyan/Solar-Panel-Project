#include <Wire.h>
#include <JY901.h>          // JY901 姿态库

// ========== 光强部分 ==========
#define TCA_ADDR        0x70
#define ADDRESS_BH1750FVI 0x23
#define ONE_TIME_H_RESOLUTION_MODE 0x20

void selectChannel(uint8_t ch) {
  if(ch > 7) return;
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << ch);
  Wire.endTransmission();
}

void closeAllCh() {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(0x00);
  Wire.endTransmission();
}

unsigned int readBH1750() {
  byte highByte = 0, lowByte = 0;
  unsigned int sensorOut = 0, illuminance = 0;
  (void)Wire.beginTransmission(ADDRESS_BH1750FVI);
  Wire.write(ONE_TIME_H_RESOLUTION_MODE);
  Wire.endTransmission();
  delay(180);
  uint8_t recvCnt = Wire.requestFrom(ADDRESS_BH1750FVI, 2);
  if (recvCnt >= 2) {
    highByte = Wire.read();
    lowByte = Wire.read();
    sensorOut = (highByte << 8) | lowByte;
    illuminance = sensorOut / 1.2;
  }
  return illuminance;
}

// ========== 气动控制部分 ==========
const int PUMP_IN   = 22;
const int PUMP_OUT  = 23;
const int VALVE_IN[4]  = {24, 25, 26, 27};
const int VALVE_OUT[4] = {28, 29, 30, 31};
const int SENSOR_PINS[4] = {A0, A1, A2, A3};

// 气压转换量程参数（0.5~4.5V ↔ -100~300 kPa）
const float V_MIN = 0.5;
const float V_MAX = 4.5;
const float P_MIN = -100.0;
const float P_MAX = 300.0;
const float SLOPE = (P_MAX - P_MIN) / (V_MAX - V_MIN); // 100

const char DIR_CHARS[4] = {'F', 'R', 'B', 'L'};

struct Chamber {
  bool manualActive;
  bool manualMode;
  float target;
  float current;
  bool autoActive;
  bool autoMode;
};
Chamber chambers[4];

// ========== JY901 相关 ==========
void setJY901Rate(int rate) {
  byte rateCommand[4] = {0x55, 0x03, 0x00, 0x00};
  rateCommand[2] = rate;
  rateCommand[3] = 0x55 ^ 0x03 ^ rate;
  Serial1.write(rateCommand, 4);
  delay(10);
}

// ========== 辅助函数 ==========
int dirToIndex(char c) {
  switch(c) {
    case 'F': return 0;
    case 'R': return 1;
    case 'B': return 2;
    case 'L': return 3;
    default: return -1;
  }
}

void parseCommand(String cmd) {
  if (cmd.startsWith("MAN,")) {
    int first = cmd.indexOf(',');
    int second = cmd.indexOf(',', first+1);
    if (second == -1) return;
    char dir = cmd.charAt(first+1);
    char mode = cmd.charAt(second+1);
    int idx = dirToIndex(dir);
    if (idx == -1) return;
    chambers[idx].target = 0.0;
    chambers[idx].autoActive = false;
    chambers[idx].manualActive = true;
    chambers[idx].manualMode = (mode == 'C' || mode == 'c');
  }
  else if (cmd.startsWith("DEACT,")) {
    int first = cmd.indexOf(',');
    if (first == -1) return;
    char dir = cmd.charAt(first+1);
    int idx = dirToIndex(dir);
    if (idx == -1) return;
    chambers[idx].manualActive = false;
  }
  else if (cmd.startsWith("SET,")) {
    int first = cmd.indexOf(',');
    int second = cmd.indexOf(',', first+1);
    if (second == -1) return;
    char dir = cmd.charAt(first+1);
    float target = cmd.substring(second+1).toFloat();
    int idx = dirToIndex(dir);
    if (idx == -1) return;
    chambers[idx].manualActive = false;
    chambers[idx].target = target;
  }
  else if (cmd == "OFF" || cmd == "STOP") {
    for (int i = 22; i <= 31; i++) digitalWrite(i, LOW);
    for (int i = 0; i < 4; i++) {
      chambers[i].manualActive = false;
      chambers[i].target = 0.0;
      chambers[i].autoActive = false;
    }
  }
}

void sendPressureData() {
  String data = "P:";
  for (int i = 0; i < 4; i++) {
    data += String(DIR_CHARS[i]) + "=" + String(chambers[i].current, 1);
    if (i < 3) data += ",";
  }
  Serial.println(data);
}

// ========== 发送角度数据（新增调试行） ==========
void sendAngleData() {
  float roll  = (float)JY901.stcAngle.Angle[0] / 32768.0 * 180.0;
  float pitch = (float)JY901.stcAngle.Angle[1] / 32768.0 * 180.0;
  float yaw   = (float)JY901.stcAngle.Angle[2] / 32768.0 * 180.0;
  
  // ① 机器解析格式（供上位机使用）
  Serial.print("A:roll=");  Serial.print(roll, 1);
  Serial.print(",pitch="); Serial.print(pitch, 1);
  Serial.print(",yaw=");   Serial.println(yaw, 1);

  // ② 人类可读调试信息（串口监视器观察，上位机会忽略此行）
  Serial.print("角度(°) → 横滚: "); Serial.print(roll, 1);
  Serial.print("  俯仰: "); Serial.print(pitch, 1);
  Serial.print("  偏航: "); Serial.println(yaw, 1);
}

// ========== 主程序 ==========
void setup() {
  Serial.begin(9600);          // 上位机通信（命令 + 气压/角度数据）
  Serial1.begin(115200);       // JY901 串口
  setJY901Rate(100);           // 设置 JY901 输出 100Hz

  Wire.begin();                // I2C（光强）

  // 初始化 IO
  for (int i = 22; i <= 31; i++) {
    pinMode(i, OUTPUT);
    digitalWrite(i, LOW);
  }
  for (int i = 0; i < 4; i++) {
    chambers[i].manualActive = false;
    chambers[i].manualMode = true;
    chambers[i].target = 0.0;
    chambers[i].current = 0.0;
    chambers[i].autoActive = false;
    chambers[i].autoMode = true;
  }

  delay(800);
  Serial.println("System Ready");
}

void loop() {
  // 1. 读取气压传感器（精确转换）
  for (int i = 0; i < 4; i++) {
    int raw = analogRead(SENSOR_PINS[i]);
    float voltage = raw * 5.0 / 1023.0;
    chambers[i].current = (voltage - V_MIN) * SLOPE + P_MIN;
  }

  // 2. 光强检测与控制
  static unsigned long lastLightRead = 0;
  if (millis() - lastLightRead > 1000) {
    lastLightRead = millis();
    selectChannel(0);
    unsigned int lux = readBH1750();
    closeAllCh();

    if (lux > 3500) {
      chambers[0].manualActive = false;
      chambers[0].target = -150.0;   // 吸气目标
    } else {
      if (chambers[0].target < 0) {
        chambers[0].target = 0.0;
        chambers[0].autoActive = false;
      }
    }
  }

  // 3. 自动闭环控制
  for (int i = 0; i < 4; i++) {
    if (chambers[i].target != 0.0) {
      float diff = chambers[i].target - chambers[i].current;
      if (fabs(diff) > 0.5) {
        chambers[i].autoActive = true;
        chambers[i].autoMode = (diff > 0);
      } else {
        chambers[i].autoActive = false;
      }
    } else {
      chambers[i].autoActive = false;
    }
  }

  // 4. 阀与泵控制（手动优先）
  for (int i = 0; i < 4; i++) {
    bool valveIn = false, valveOut = false;
    if (chambers[i].manualActive) {
      if (chambers[i].manualMode) valveIn = true;
      else valveOut = true;
    } else if (chambers[i].autoActive) {
      if (chambers[i].autoMode) valveIn = true;
      else valveOut = true;
    }
    digitalWrite(VALVE_IN[i], valveIn ? HIGH : LOW);
    digitalWrite(VALVE_OUT[i], valveOut ? HIGH : LOW);
  }

  bool needCharge = false, needExhaust = false;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(VALVE_IN[i]) == HIGH) needCharge = true;
    if (digitalRead(VALVE_OUT[i]) == HIGH) needExhaust = true;
  }
  digitalWrite(PUMP_IN, needCharge ? HIGH : LOW);
  digitalWrite(PUMP_OUT, needExhaust ? HIGH : LOW);

  // 5. 处理串口命令（上位机）
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    parseCommand(cmd);
  }

  // 6. 处理 JY901 串口数据（实时更新角度结构体）
  while (Serial1.available()) {
    JY901.CopeSerialData(Serial1.read());
  }

  // 7. 定期发送气压和角度数据（每 200ms）
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 200) {
    lastSend = millis();
    sendPressureData();   // 气压数据行（P:...）
    sendAngleData();      // 角度数据行（A:...  + 调试行）
  }
}
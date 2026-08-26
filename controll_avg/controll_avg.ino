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
  Wire.beginTransmission(ADDRESS_BH1750FVI);
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

void readAllLight(unsigned int lightVal[4]) {
  for (int ch = 0; ch < 4; ch++) {
    selectChannel(ch);
    lightVal[ch] = readBH1750();
    closeAllCh();
  }
}

// ========== 气动控制部分 ==========
const int PUMP_IN   = 22;
const int PUMP_OUT  = 23;
const int VALVE_IN[4]  = {24, 25, 26, 27};
const int VALVE_OUT[4] = {28, 29, 30, 31};
const int SENSOR_PINS[4] = {A0, A1, A2, A3};

const float V_MIN = 0.5;
const float V_MAX = 4.5;
const float P_MIN = -100.0;
const float P_MAX = 300.0;
const float SLOPE = (P_MAX - P_MIN) / (V_MAX - V_MIN);

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

// ========== 角度归零变量 ==========
float roll_offset = 0.0;
float pitch_offset = 0.0;
float yaw_offset = 0.0;
bool zero_calibrating = false;
int zero_count = 0;
float sum_roll = 0.0;
float sum_pitch = 0.0;
float sum_yaw = 0.0;

// ========== 光强与驱动器映射 ==========
const int lightToDrive[4][2] = {
  {2, 3},   // 光强0 → 驱动器3,4
  {1, 2},   // 光强1 → 驱动器2,3
  {0, 1},   // 光强2 → 驱动器1,2
  {0, 3}    // 光强3 → 驱动器1,4
};

// ========== 存储光强值 ==========
unsigned int lightValues[4] = {0, 0, 0, 0};

// ========== 新增：一键放气相关 ==========
bool ventActive = false;
unsigned long ventStartTime = 0;
const unsigned long VENT_DURATION = 3000;  // 放气持续3秒

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
    ventActive = false;  // 取消任何放气状态
  }
  else if (cmd == "ZERO") {
    zero_calibrating = true;
    zero_count = 0;
    sum_roll = 0.0;
    sum_pitch = 0.0;
    sum_yaw = 0.0;
    Serial.println("ZERO_START");
  }
  else if (cmd == "VENT") {
    // 启动一键放气
    ventActive = true;
    ventStartTime = millis();
    Serial.println("VENT_START");
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

void sendLightData() {
  String data = "L:";
  for (int i = 0; i < 4; i++) {
    data += String(i) + "=" + String(lightValues[i]);
    if (i < 3) data += ",";
  }
  Serial.println(data);
}

void sendAngleData() {
  float roll_raw  = (float)JY901.stcAngle.Angle[0] / 32768.0 * 180.0;
  float pitch_raw = (float)JY901.stcAngle.Angle[1] / 32768.0 * 180.0;
  float yaw_raw   = (float)JY901.stcAngle.Angle[2] / 32768.0 * 180.0;

  float roll_adj  = roll_raw  - roll_offset;
  float pitch_adj = pitch_raw - pitch_offset;
  float yaw_adj   = yaw_raw   - yaw_offset;

  Serial.print("A:roll=");  Serial.print(roll_adj, 1);
  Serial.print(",pitch="); Serial.print(pitch_adj, 1);
  Serial.print(",yaw=");   Serial.println(yaw_adj, 1);
}

// ========== 主程序 ==========
void setup() {
  Serial.begin(9600);
  Serial1.begin(115200);
  setJY901Rate(100);
  Wire.begin();

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
  // 1. 读取气压
  for (int i = 0; i < 4; i++) {
    int raw = analogRead(SENSOR_PINS[i]);
    float voltage = raw * 5.0 / 1023.0;
    chambers[i].current = (voltage - V_MIN) * SLOPE + P_MIN;
  }

  // 2. 光强检测与控制（每 500ms）
  static unsigned long lastLightRead = 0;
  if (millis() - lastLightRead > 500) {
    lastLightRead = millis();
    unsigned int light[4];
    readAllLight(light);
    for (int i = 0; i < 4; i++) lightValues[i] = light[i];

    // 计算均值
    float avg = 0.0;
    for (int i = 0; i < 4; i++) avg += light[i];
    avg /= 4.0;

    // 检查是否所有气压 < -30 kPa
    bool allBelowMinus30 = true;
    for (int i = 0; i < 4; i++) {
      if (chambers[i].current >= -30.0) {
        allBelowMinus30 = false;
        break;
      }
    }

    if (allBelowMinus30) {
      // 低压保护：强制所有驱动器目标为 0（充气）
      for (int i = 0; i < 4; i++) {
        chambers[i].manualActive = false;
        chambers[i].target = 0.0;
      }
    } else {
      // 正常光强控制（新逻辑）
      for (int driveIdx = 0; driveIdx < 4; driveIdx++) {
        if (chambers[driveIdx].manualActive) continue;

        bool shouldExhaust = false;
        bool shouldStop = true;

        for (int l = 0; l < 4; l++) {
          for (int k = 0; k < 2; k++) {
            if (lightToDrive[l][k] == driveIdx) {
              if (light[l] > avg + 500.0) {
                shouldExhaust = true;
              }
              if (!(light[l] >= avg - 200.0 && light[l] <= avg + 200.0)) {
                shouldStop = false;
              }
              break;
            }
          }
        }

        if (shouldExhaust) {
          chambers[driveIdx].target = -80.0;
        } else if (shouldStop) {
          chambers[driveIdx].target = 0.0;
        }
        // 否则保持原 target
      }
    }
  }

  // 3. 自动闭环控制（根据 target 决定充/放气）
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

  // 4. 阀与泵控制
  // ---- 检查一键放气状态 ----
  if (ventActive) {
    if (millis() - ventStartTime < VENT_DURATION) {
      // 强制放气：打开所有充气阀，关闭放气阀和两个泵
      for (int i = 0; i < 4; i++) {
        digitalWrite(VALVE_IN[i], HIGH);
        digitalWrite(VALVE_OUT[i], LOW);
      }
      digitalWrite(PUMP_IN, LOW);
      digitalWrite(PUMP_OUT, LOW);
      // 同时将目标清零，避免恢复后立即动作
      for (int i = 0; i < 4; i++) {
        chambers[i].target = 0.0;
        chambers[i].autoActive = false;
      }
    } else {
      ventActive = false;
      Serial.println("VENT_DONE");
    }
    // 跳过下面的正常阀控制，因为已强制设置
    // 但为了保险，直接跳过后面的阀设置（使用 goto 或条件结构）
    // 此处我们使用 if-else 结构，在放气激活时跳过正常控制
  } else {
    // 正常阀控制
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
  }

  // 5. 处理串口命令
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    parseCommand(cmd);
  }

  // 6. 处理 JY901
  while (Serial1.available()) {
    JY901.CopeSerialData(Serial1.read());
  }

  // 7. 每 200ms 发送数据 + 归零采样
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 200) {
    lastSend = millis();

    if (zero_calibrating) {
      float r = (float)JY901.stcAngle.Angle[0] / 32768.0 * 180.0;
      float p = (float)JY901.stcAngle.Angle[1] / 32768.0 * 180.0;
      float y = (float)JY901.stcAngle.Angle[2] / 32768.0 * 180.0;
      sum_roll += r;
      sum_pitch += p;
      sum_yaw += y;
      zero_count++;

      if (zero_count >= 50) {
        roll_offset = sum_roll / 50.0;
        pitch_offset = sum_pitch / 50.0;
        yaw_offset = sum_yaw / 50.0;
        zero_calibrating = false;
        Serial.println("ZERO_DONE");
      }
    }

    sendPressureData();
    sendLightData();
    sendAngleData();
  }
}
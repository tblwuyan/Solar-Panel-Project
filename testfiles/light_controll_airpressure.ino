#include <Wire.h>
#include <JY901.h>          // JY901 姿态库

// ========== TCA9548A 多路复用器与 BH1750 光强传感器 ==========
#define TCA_ADDR        0x70
#define BH1750_ADDR     0x23
#define ONE_TIME_H_RESOLUTION_MODE 0x20

// ========== 气动控制引脚 ==========
const int PUMP_IN   = 22;   // 充气泵
const int PUMP_OUT  = 23;   // 排气泵（抽气）
const int VALVE_IN[4]  = {24, 25, 26, 27};   // 充气阀
const int VALVE_OUT[4] = {28, 29, 30, 31};   // 放气阀
const int SENSOR_PINS[4] = {A0, A1, A2, A3}; // 气压传感器

// ========== 气压转换参数（0.5~4.5V ↔ -100~300 kPa） ==========
const float V_MIN = 0.5;
const float V_MAX = 4.5;
const float P_MIN = -100.0;
const float P_MAX = 300.0;
const float SLOPE = (P_MAX - P_MIN) / (V_MAX - V_MIN); // 100

const char DIR_CHARS[4] = {'F', 'R', 'B', 'L'};

// ========== 驱动器（气室）结构体 ==========
struct Chamber {
  bool manualActive;       // 手动模式激活
  bool manualMode;         // true=充气，false=放气
  float target;            // 目标气压（kPa）
  float current;           // 当前气压（kPa）
  bool autoActive;         // 自动调节激活
  bool autoMode;           // true=充气，false=放气

  // 新增：光强触发抽气控制
  bool lightActive;        // 是否正在执行光强触发的抽气
  bool lightMode;          // true=充气，false=放气（此处固定为放气）
  unsigned long lightStart;// 抽气开始时间（毫秒）
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

// ========== TCA9548A 通道选择 ==========
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

// ========== BH1750 读取（单通道） ==========
unsigned int readBH1750() {
  byte highByte = 0, lowByte = 0;
  unsigned int sensorOut = 0, illuminance = 0;
  Wire.beginTransmission(BH1750_ADDR);
  Wire.write(ONE_TIME_H_RESOLUTION_MODE);
  Wire.endTransmission();
  delay(180);
  uint8_t recvCnt = Wire.requestFrom(BH1750_ADDR, 2);
  if (recvCnt >= 2) {
    highByte = Wire.read();
    lowByte = Wire.read();
    sensorOut = (highByte << 8) | lowByte;
    illuminance = sensorOut / 1.2;
  }
  return illuminance;
}

// ========== 辅助函数：方向字符转索引 ==========
int dirToIndex(char c) {
  switch(c) {
    case 'F': return 0;
    case 'R': return 1;
    case 'B': return 2;
    case 'L': return 3;
    default: return -1;
  }
}

// ========== 串口命令解析 ==========
void parseCommand(String cmd) {
  if (cmd.startsWith("MAN,")) {
    int first = cmd.indexOf(',');
    int second = cmd.indexOf(',', first+1);
    if (second == -1) return;
    char dir = cmd.charAt(first+1);
    char mode = cmd.charAt(second+1);
    int idx = dirToIndex(dir);
    if (idx == -1) return;
    // 手动命令覆盖光强控制
    chambers[idx].lightActive = false;
    chambers[idx].target = 0.0;
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
    chambers[idx].lightActive = false; // 同时取消光强控制
  }
  else if (cmd.startsWith("SET,")) {
    int first = cmd.indexOf(',');
    int second = cmd.indexOf(',', first+1);
    if (second == -1) return;
    char dir = cmd.charAt(first+1);
    float target = cmd.substring(second+1).toFloat();
    int idx = dirToIndex(dir);
    if (idx == -1) return;
    // 设置目标，取消光强控制
    chambers[idx].lightActive = false;
    chambers[idx].manualActive = false;
    chambers[idx].target = target;
  }
  else if (cmd == "OFF" || cmd == "STOP") {
    for (int i = 22; i <= 31; i++) digitalWrite(i, LOW);
    for (int i = 0; i < 4; i++) {
      chambers[i].manualActive = false;
      chambers[i].lightActive = false;
      chambers[i].target = 0.0;
      chambers[i].autoActive = false;
    }
  }
  else if (cmd == "ZERO") {
    zero_calibrating = true;
    zero_count = 0;
    sum_roll = 0.0;
    sum_pitch = 0.0;
    sum_yaw = 0.0;
    Serial.println("ZERO_START");
  }
}

// ========== 发送气压数据 ==========
void sendPressureData() {
  String data = "P:";
  for (int i = 0; i < 4; i++) {
    data += String(DIR_CHARS[i]) + "=" + String(chambers[i].current, 1);
    if (i < 3) data += ",";
  }
  Serial.println(data);
}

// ========== 发送角度数据（带归零偏移） ==========
void sendAngleData() {
  float roll_raw  = (float)JY901.stcAngle.Angle[0] / 32768.0 * 180.0;
  float pitch_raw = (float)JY901.stcAngle.Angle[1] / 32768.0 * 180.0;
  float yaw_raw   = (float)JY901.stcAngle.Angle[2] / 32768.0 * 180.0;

  float roll_adj  = roll_raw  - roll_offset;
  float pitch_adj = pitch_raw - pitch_offset;
  float yaw_adj   = yaw_raw   - yaw_offset;

  // 机器解析格式
  Serial.print("A:roll=");  Serial.print(roll_adj, 1);
  Serial.print(",pitch="); Serial.print(pitch_adj, 1);
  Serial.print(",yaw=");   Serial.println(yaw_adj, 1);

  // 人类可读调试
  Serial.print("角度(°) → 横滚: "); Serial.print(roll_adj, 1);
  Serial.print("  俯仰: "); Serial.print(pitch_adj, 1);
  Serial.print("  偏航: "); Serial.println(yaw_adj, 1);
}

// ========== 主程序 setup ==========
void setup() {
  Serial.begin(9600);
  Serial1.begin(115200);
  setJY901Rate(100);

  Wire.begin();

  // 初始化所有气动引脚为输出
  for (int i = 22; i <= 31; i++) {
    pinMode(i, OUTPUT);
    digitalWrite(i, LOW);
  }

  // 初始化结构体
  for (int i = 0; i < 4; i++) {
    chambers[i].manualActive = false;
    chambers[i].manualMode = true;
    chambers[i].target = 0.0;
    chambers[i].current = 0.0;
    chambers[i].autoActive = false;
    chambers[i].autoMode = true;
    chambers[i].lightActive = false;
    chambers[i].lightMode = false;    // 固定为放气
    chambers[i].lightStart = 0;
  }

  delay(800);
  Serial.println("System Ready");
}

// ========== 主循环 loop ==========
void loop() {
  // ---------- 1. 读取气压传感器 ----------
  for (int i = 0; i < 4; i++) {
    int raw = analogRead(SENSOR_PINS[i]);
    float voltage = raw * 5.0 / 1023.0;
    chambers[i].current = (voltage - V_MIN) * SLOPE + P_MIN;
  }

  // ---------- 2. 光强传感器读取与控制（每 1 秒执行一次） ----------
  static unsigned long lastLightRead = 0;
  if (millis() - lastLightRead > 1000) {
    lastLightRead = millis();

    unsigned int lux[4];
    // 依次读取四个通道的光强
    for (int ch = 0; ch < 4; ch++) {
      selectChannel(ch);
      lux[ch] = readBH1750();
      closeAllCh();  // 关闭所有通道，避免串扰
    }

    // 找出最大光强值（排除零值，防止传感器故障误触发）
    unsigned int maxLux = 0;
    for (int i = 0; i < 4; i++) {
      if (lux[i] > maxLux) maxLux = lux[i];
    }

    // 标记哪些通道为最大值
    bool isMax[4] = {false, false, false, false};
    if (maxLux > 0) {   // 只有有效读数才触发
      for (int i = 0; i < 4; i++) {
        if (lux[i] == maxLux) isMax[i] = true;
      }
    }

    // 驱动器与光强通道的映射关系：
    // 光强0 → 驱动器3,4  (索引2,3)
    // 光强1 → 驱动器2,3  (索引1,2)
    // 光强2 → 驱动器1,2  (索引0,1)
    // 光强3 → 驱动器1,4  (索引0,3)
    // 每个驱动器对应两个光强通道，任一通道为最大即触发
    int driverLightMap[4][2] = {
      {2, 3},   // 驱动器0（F）对应光强2和3
      {1, 2},   // 驱动器1（R）对应光强1和2
      {0, 1},   // 驱动器2（B）对应光强0和1
      {0, 3}    // 驱动器3（L）对应光强0和3
    };

    // 处理每个驱动器的抽气逻辑
    for (int i = 0; i < 4; i++) {
      bool trigger = isMax[ driverLightMap[i][0] ] || isMax[ driverLightMap[i][1] ];

      if (trigger) {
        // 如果该驱动器尚未处于光强抽气状态，则启动抽气
        if (!chambers[i].lightActive) {
          // 清除手动和自动控制，以免冲突
          chambers[i].manualActive = false;
          chambers[i].target = 0.0;          // 预设归零目标
          // 启动光强抽气（放气）
          chambers[i].lightActive = true;
          chambers[i].lightMode = false;     // 放气
          chambers[i].lightStart = millis();
        } else {
          // 如果已处于抽气状态，检查是否满 5 秒
          if (millis() - chambers[i].lightStart >= 5000) {
            // 5 秒到，停止抽气，让自动调节归零
            chambers[i].lightActive = false;
            chambers[i].target = 0.0;        // 自动调节会将其归零
          }
        }
      } else {
        // 不再触发（光强不再最大）
        if (chambers[i].lightActive) {
          // 立即停止抽气，并启动归零
          chambers[i].lightActive = false;
          chambers[i].target = 0.0;
        }
      }
    }
  }

  // ---------- 3. 自动闭环控制（支持 target=0 时的调节） ----------
  for (int i = 0; i < 4; i++) {
    // 只有未处于光强抽气模式时，自动控制才生效
    if (!chambers[i].lightActive) {
      float diff = chambers[i].target - chambers[i].current;
      if (fabs(diff) > 0.5) {
        chambers[i].autoActive = true;
        chambers[i].autoMode = (diff > 0);   // 正差需充气，负差需放气
      } else {
        chambers[i].autoActive = false;
      }
    } else {
      // 光强抽气时禁用自动控制
      chambers[i].autoActive = false;
    }
  }

  // ---------- 4. 阀门与泵控制（优先级：光强 > 手动 > 自动） ----------
  for (int i = 0; i < 4; i++) {
    bool valveIn = false, valveOut = false;

    if (chambers[i].lightActive) {
      // 光强控制优先
      if (chambers[i].lightMode) valveIn = true;   // 充气（未使用）
      else valveOut = true;                         // 放气（抽气）
    } else if (chambers[i].manualActive) {
      if (chambers[i].manualMode) valveIn = true;
      else valveOut = true;
    } else if (chambers[i].autoActive) {
      if (chambers[i].autoMode) valveIn = true;
      else valveOut = true;
    }

    digitalWrite(VALVE_IN[i], valveIn ? HIGH : LOW);
    digitalWrite(VALVE_OUT[i], valveOut ? HIGH : LOW);
  }

  // 泵控制：只要有任何充气阀打开则启动充气泵，有任何放气阀打开则启动排气泵
  bool needCharge = false, needExhaust = false;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(VALVE_IN[i]) == HIGH) needCharge = true;
    if (digitalRead(VALVE_OUT[i]) == HIGH) needExhaust = true;
  }
  digitalWrite(PUMP_IN, needCharge ? HIGH : LOW);
  digitalWrite(PUMP_OUT, needExhaust ? HIGH : LOW);

  // ---------- 5. 处理串口命令 ----------
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    parseCommand(cmd);
  }

  // ---------- 6. 处理 JY901 串口数据 ----------
  while (Serial1.available()) {
    JY901.CopeSerialData(Serial1.read());
  }

  // ---------- 7. 每 200ms 发送数据及角度归零采样 ----------
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 200) {
    lastSend = millis();

    // ---- 角度归零采样 ----
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

    // ---- 发送气压和角度数据 ----
    sendPressureData();
    sendAngleData();
  }
}

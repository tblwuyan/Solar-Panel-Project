#include <Wire.h>
#include <JY901.h>          // JY901 姿态库

// ========== TCA9548A 与 BH1750 ==========
#define TCA_ADDR        0x70
#define BH1750_ADDR     0x23
#define ONE_TIME_H_RESOLUTION_MODE 0x20

// ========== 气动控制引脚 ==========
const int PUMP_SUCTION = 23;           // 吸气泵（主动抽气）
// const int PUMP_IN   = 22;           // 充气泵（本系统未使用）

const int VALVE_SUCTION[4] = {28, 29, 30, 31};   // 吸气阀（主动抽气）
const int VALVE_EXHAUST[4] = {24, 25, 26, 27};   // 放气阀（自然放气）

const int SENSOR_PINS[4] = {A0, A1, A2, A3};     // 气压传感器

// ========== 气压转换参数（0.5~4.5V ↔ -100~300 kPa） ==========
const float V_MIN = 0.5;
const float V_MAX = 4.5;
const float P_MIN = -100.0;
const float P_MAX = 300.0;
const float SLOPE = (P_MAX - P_MIN) / (V_MAX - V_MIN); // 100

const char DIR_CHARS[4] = {'F', 'R', 'B', 'L'};

// ========== 光强控制参数 ==========
const int LUX_THRESHOLD = 1500;          // 光强阈值 (lx)
const float PRESSURE_TARGET = -30.0;     // 吸气目标压力 (kPa)
const unsigned long EXHAUST_DURATION = 5000; // 放气持续时间 (ms)

// ========== 驱动器状态机 ==========
enum DriveState {
  IDLE,
  SUCTION,      // 主动抽气（吸气阀 + 吸气泵）
  EXHAUST       // 自然放气（仅放气阀）
};

struct Chamber {
  float current;                // 当前气压 (kPa)
  DriveState state;             // 当前状态
  unsigned long stateStartTime; // 状态开始时间（用于放气计时）
};
Chamber chambers[4];

// ========== 光强全局存储 ==========
unsigned int luxValues[4] = {0, 0, 0, 0};

// ========== 角度归零变量 ==========
float roll_offset = 0.0;
float pitch_offset = 0.0;
float yaw_offset = 0.0;
bool zero_calibrating = false;
int zero_count = 0;
float sum_roll = 0.0;
float sum_pitch = 0.0;
float sum_yaw = 0.0;

// ========== JY901 速率设置 ==========
void setJY901Rate(int rate) {
  byte rateCommand[4] = {0x55, 0x03, 0x00, 0x00};
  rateCommand[2] = rate;
  rateCommand[3] = 0x55 ^ 0x03 ^ rate;
  Serial1.write(rateCommand, 4);
  delay(10);
}

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

// ========== 串口命令解析 ==========
void parseCommand(String cmd) {
  cmd.trim();
  if (cmd == "STOP" || cmd == "OFF") {
    // 关闭所有阀门和泵
    for (int i = 0; i < 4; i++) {
      digitalWrite(VALVE_SUCTION[i], LOW);
      digitalWrite(VALVE_EXHAUST[i], LOW);
      chambers[i].state = IDLE;
    }
    digitalWrite(PUMP_SUCTION, LOW);
    Serial.println("STOP_OK");
  }
  else if (cmd == "ZERO") {
    zero_calibrating = true;
    zero_count = 0;
    sum_roll = sum_pitch = sum_yaw = 0.0;
    Serial.println("ZERO_START");
  }
  else if (cmd == "RELEASE") {
    // 一键放气：打开所有放气阀（不启动泵）持续 5 秒
    for (int i = 0; i < 4; i++) {
      chambers[i].state = EXHAUST;
      chambers[i].stateStartTime = millis();
    }
    Serial.println("RELEASE_START");
  }
}

// ========== 数据发送函数 ==========
void sendPressureData() {
  String data = "P:";
  for (int i = 0; i < 4; i++) {
    data += String(DIR_CHARS[i]) + "=" + String(chambers[i].current, 1);
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

void sendLightData() {
  String data = "L:";
  for (int i = 0; i < 4; i++) {
    data += String(i) + "=" + String(luxValues[i]);
    if (i < 3) data += ",";
  }
  Serial.println(data);
}

// ========== 更新执行器（阀门和泵） ==========
void updateActuators() {
  // 先全部关闭
  for (int i = 0; i < 4; i++) {
    digitalWrite(VALVE_SUCTION[i], LOW);
    digitalWrite(VALVE_EXHAUST[i], LOW);
  }
  digitalWrite(PUMP_SUCTION, LOW);

  bool needPump = false;

  for (int i = 0; i < 4; i++) {
    if (chambers[i].state == SUCTION) {
      digitalWrite(VALVE_SUCTION[i], HIGH);
      needPump = true;
    } else if (chambers[i].state == EXHAUST) {
      digitalWrite(VALVE_EXHAUST[i], HIGH);
      // 不启动泵
    }
  }

  if (needPump) digitalWrite(PUMP_SUCTION, HIGH);
}

// ========== 初始化 ==========
void setup() {
  Serial.begin(9600);
  Serial1.begin(115200);
  setJY901Rate(100);
  Wire.begin();

  // 初始化引脚
  pinMode(PUMP_SUCTION, OUTPUT);
  digitalWrite(PUMP_SUCTION, LOW);
  for (int i = 0; i < 4; i++) {
    pinMode(VALVE_SUCTION[i], OUTPUT);
    pinMode(VALVE_EXHAUST[i], OUTPUT);
    digitalWrite(VALVE_SUCTION[i], LOW);
    digitalWrite(VALVE_EXHAUST[i], LOW);
  }

  // 初始化结构体
  for (int i = 0; i < 4; i++) {
    chambers[i].current = 0.0;
    chambers[i].state = IDLE;
    chambers[i].stateStartTime = 0;
  }

  delay(800);
  Serial.println("System Ready");
}

// ========== 主循环 ==========
void loop() {
  // 1. 读取气压（每次循环）
  for (int i = 0; i < 4; i++) {
    int raw = analogRead(SENSOR_PINS[i]);
    float voltage = raw * 5.0 / 1023.0;
    chambers[i].current = (voltage - V_MIN) * SLOPE + P_MIN;
  }

  // 2. 光强读取与控制（每 100ms）
  static unsigned long lastLightRead = 0;
  if (millis() - lastLightRead >= 100) {
    lastLightRead = millis();

    // 读取所有光强
    for (int ch = 0; ch < 4; ch++) {
      selectChannel(ch);
      luxValues[ch] = readBH1750();
      closeAllCh();
    }

    // 驱动器与光强映射
    int driverLightMap[4][2] = {
      {2, 3},   // F
      {1, 2},   // R
      {0, 1},   // B
      {0, 3}    // L
    };

    for (int i = 0; i < 4; i++) {
      int l0 = driverLightMap[i][0];
      int l1 = driverLightMap[i][1];
      bool over = (luxValues[l0] > LUX_THRESHOLD) || (luxValues[l1] > LUX_THRESHOLD);

      Chamber &ch = chambers[i];

      // 状态转换逻辑
      if (over) {
        // 光强高 -> 应吸气
        if (ch.state == IDLE || ch.state == EXHAUST) {
          ch.state = SUCTION;
          ch.stateStartTime = millis(); // 重置计时
        }
        // 如果已在 SUCTION，检查是否达到目标压力
        if (ch.state == SUCTION && ch.current <= PRESSURE_TARGET) {
          // 达到目标，停止吸气（转为 IDLE）
          ch.state = IDLE;
          // 注意：光强仍高，但压力已达标，故停止。若之后压力回升，会在下一次循环再次进入 SUCTION（因为 over 仍为 true）
        }
      } else {
        // 光强低 -> 应放气
        if (ch.state == IDLE) {
          // 开始放气
          ch.state = EXHAUST;
          ch.stateStartTime = millis();
        } else if (ch.state == SUCTION) {
          // 从吸气切换为放气
          ch.state = EXHAUST;
          ch.stateStartTime = millis();
        } else if (ch.state == EXHAUST) {
          // 检查放气是否已持续 5 秒
          if (millis() - ch.stateStartTime >= EXHAUST_DURATION) {
            ch.state = IDLE;
          }
        }
      }
    }
  }

  // 3. 更新执行器
  updateActuators();

  // 4. 处理串口命令
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    parseCommand(cmd);
  }

  // 5. 处理 JY901 数据
  while (Serial1.available()) {
    JY901.CopeSerialData(Serial1.read());
  }

  // 6. 数据发送（每 200ms）
  static unsigned long lastSend = 0;
  if (millis() - lastSend >= 200) {
    lastSend = millis();

    // 角度归零采样
    if (zero_calibrating) {
      float r = (float)JY901.stcAngle.Angle[0] / 32768.0 * 180.0;
      float p = (float)JY901.stcAngle.Angle[1] / 32768.0 * 180.0;
      float y = (float)JY901.stcAngle.Angle[2] / 32768.0 * 180.0;
      sum_roll += r; sum_pitch += p; sum_yaw += y;
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
    sendAngleData();
    sendLightData();
  }
}
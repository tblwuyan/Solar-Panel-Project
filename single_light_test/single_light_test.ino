#include <Wire.h>

// ========== TCA9548A 与 BH1750 ==========
#define TCA_ADDR        0x70
#define BH1750_ADDR     0x23
#define ONE_TIME_H_RESOLUTION_MODE 0x20

// ========== 气动引脚 ==========
const int PUMP_OUT  = 23;   // 仅保留排气泵
const int VALVE_IN[4]  = {24, 25, 26, 27};   // 充气阀（外部气源）
const int VALVE_OUT[4] = {28, 29, 30, 31};   // 放气阀
const int SENSOR_PINS[4] = {A0, A1, A2, A3}; // 气压传感器

// ========== 气压转换 ==========
const float V_MIN = 0.5, V_MAX = 4.5;
const float P_MIN = -100.0, P_MAX = 300.0;
const float SLOPE = (P_MAX - P_MIN) / (V_MAX - V_MIN); // 100

// ========== 映射表：光强 → 驱动器索引 ==========
const int lightToDrivers[4][2] = {
  {2, 3},   // 光强0 → 驱动器3、4
  {1, 2},   // 光强1 → 驱动器2、3
  {0, 1},   // 光强2 → 驱动器1、2
  {0, 3}    // 光强3 → 驱动器1、4
};
const int NUM_DRIVERS_PER_LIGHT = 2;

// ========== 状态变量 ==========
bool lightActive[4] = {false, false, false, false}; // 光强是否 > 1500
float currentPressure[4] = {0.0, 0.0, 0.0, 0.0};

// ========== 基础函数 ==========
void selectChannel(uint8_t ch) {
  if(ch > 7) return;
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << ch);
  Wire.endTransmission();
}

unsigned int readBH1750() {
  Wire.beginTransmission(BH1750_ADDR);
  Wire.write(ONE_TIME_H_RESOLUTION_MODE);
  Wire.endTransmission();
  delay(180);
  uint8_t recvCnt = Wire.requestFrom(BH1750_ADDR, 2);
  if (recvCnt >= 2) {
    uint16_t val = (Wire.read() << 8) | Wire.read();
    return val / 1.2;
  }
  return 0;
}

void readAllPressures() {
  for (int i = 0; i < 4; i++) {
    int raw = analogRead(SENSOR_PINS[i]);
    float voltage = raw * 5.0 / 1023.0;
    currentPressure[i] = (voltage - V_MIN) * SLOPE + P_MIN;
  }
}

void setValves(int idx, bool charge, bool exhaust) {
  digitalWrite(VALVE_IN[idx],  charge ? HIGH : LOW);
  digitalWrite(VALVE_OUT[idx], exhaust ? HIGH : LOW);
}

void updatePumps() {
  // 仅控制排气泵：只要有任何放气阀打开，就启动排气泵
  bool needExhaust = false;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(VALVE_OUT[i]) == HIGH) {
      needExhaust = true;
      break;
    }
  }
  digitalWrite(PUMP_OUT, needExhaust ? HIGH : LOW);
  // 充气泵已删除，不做任何控制
}

void allValvesOff() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(VALVE_IN[i], LOW);
    digitalWrite(VALVE_OUT[i], LOW);
  }
  digitalWrite(PUMP_OUT, LOW);
}

void regulatePressure(int idx) {
  float diff = 0.0 - currentPressure[idx];  // 目标为0
  if (fabs(diff) > 5.0) {
    if (diff > 0) {
      setValves(idx, true, false);   // 需要充气：打开充气阀（外部气源）
    } else {
      setValves(idx, false, true);   // 需要放气：打开放气阀
    }
  } else {
    setValves(idx, false, false);    // 关闭阀门
  }
}

// ========== 检查某个驱动器是否被任意光强触发 ==========
bool isDriverTriggered(int driver) {
  for (int light = 0; light < 4; light++) {
    for (int k = 0; k < NUM_DRIVERS_PER_LIGHT; k++) {
      if (lightToDrivers[light][k] == driver && lightActive[light]) {
        return true;
      }
    }
  }
  return false;
}

// ========== 主程序 ==========
void setup() {
  Serial.begin(9600);
  Wire.begin();

  // 初始化引脚（注意引脚22不再使用）
  for (int i = 23; i <= 31; i++) {
    pinMode(i, OUTPUT);
    digitalWrite(i, LOW);
  }
  allValvesOff();
  Serial.println("System Ready (充气泵已移除)");
}

void loop() {
  readAllPressures();

  static unsigned long lastRead = 0;
  unsigned long now = millis();

  // ----- 读取四个光强并更新触发标志 -----
  if (now - lastRead >= 200) {
    lastRead = now;

    unsigned int lux[4];
    for (int ch = 0; ch < 4; ch++) {
      selectChannel(ch);
      lux[ch] = readBH1750();
    }

    for (int i = 0; i < 4; i++) {
      lightActive[i] = (lux[i] > 1500);
    }

    // 调试打印光强
    Serial.print("Lux: ");
    for (int i = 0; i < 4; i++) {
      Serial.print(lux[i]); Serial.print(" ");
    }
    Serial.println();
  }

  // ----- 控制四个驱动器 -----
  allValvesOff(); // 先全部关闭，再按需打开

  for (int driver = 0; driver < 4; driver++) {
    bool triggered = isDriverTriggered(driver);

    if (triggered) {
      // 触发状态：根据气压决定是否抽气
      if (currentPressure[driver] >= -20.0) {
        // 气压未达到 -20，继续抽气（放气）
        setValves(driver, false, true);
      } else {
        // 气压已低于 -20，停止抽气（阀门保持关闭），不归零
        // 此时阀门已关闭（allValvesOff已执行）
      }
      // 注意：触发状态下不执行归零
    } else {
      // 未触发：执行归零调节（充气或放气）
      regulatePressure(driver);
    }
  }

  updatePumps();

  // ----- 打印气压（调试） -----
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 500) {
    lastPrint = now;
    Serial.print("P: ");
    for (int i = 0; i < 4; i++) {
      Serial.print("D"); Serial.print(i); Serial.print("="); Serial.print(currentPressure[i], 1); Serial.print(" ");
    }
    Serial.println();
  }

  delay(10);
}
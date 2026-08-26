#include <Wire.h>

// ========== 光强部分（来自 LIGHT1.ino）==========
#define TCA_ADDR        0x70
#define ADDRESS_BH1750FVI 0x23
#define ONE_TIME_H_RESOLUTION_MODE 0x20

// 切换 TCA9548A 通道
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

// 读取 BH1750 光强 (单位 lx)
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

// ========== 气动部分（来自 pumps-with-airpressure.ino）==========
const int PUMP_IN   = 22;   // 充气泵（本程序未使用）
const int PUMP_OUT  = 23;   // 吸气泵（放气泵）
const int VALVE_IN[4]  = {24, 25, 26, 27};   // 充气阀
const int VALVE_OUT[4] = {28, 29, 30, 31};   // 放气阀
const int SENSOR_PINS[4] = {A0, A1, A2, A3};

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

// ========== 主程序 ==========
void setup() {
  Serial.begin(9600);        // 气压控制串口
  Wire.begin();              // I2C 初始化（光强）

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
  // 1. 读取所有气压传感器
  for (int i = 0; i < 4; i++) {
    int raw = analogRead(SENSOR_PINS[i]);
    float voltage = raw * 5.0 / 1023.0;
    chambers[i].current = 100.0 * voltage - 150.0; // kPa
  }

  // 2. 光强检测与控制（新增逻辑）
  static unsigned long lastLightRead = 0;
  if (millis() - lastLightRead > 1000) { // 每秒读一次
    lastLightRead = millis();
    selectChannel(0);                // 选择通道0（0号光强传感器）
    unsigned int lux = readBH1750();
    closeAllCh();

    if (lux > 3500) {
      // 光强超标，设定前腔（F）目标为 -20 kPa
      // 关闭手动，使自动生效
      chambers[0].manualActive = false;
      chambers[0].target = -150.0;    // 负值表示吸气
    } else {
      // 光强正常，清除前腔目标（停止自动控制）
      // 但若已到 -20，停止；若未到则停止动作
      if (chambers[0].target < 0) {  // 仅当之前设过负目标才清除
        chambers[0].target = 0.0;
        chambers[0].autoActive = false;
      }
    }
  }

  // 3. 自动闭环控制（根据目标）
  for (int i = 0; i < 4; i++) {
    if (chambers[i].target != 0.0) {
      float diff = chambers[i].target - chambers[i].current;
      if (fabs(diff) > 0.5) {
        chambers[i].autoActive = true;
        chambers[i].autoMode = (diff > 0); // 正→充气，负→吸气
      } else {
        chambers[i].autoActive = false;
      }
    } else {
      chambers[i].autoActive = false;
    }
  }

  // 4. 合并输出：手动优先
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

  // 5. 总泵控制
  bool needCharge = false, needExhaust = false;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(VALVE_IN[i]) == HIGH) needCharge = true;
    if (digitalRead(VALVE_OUT[i]) == HIGH) needExhaust = true;
  }
  digitalWrite(PUMP_IN, needCharge ? HIGH : LOW);   // 充气泵（22）
  digitalWrite(PUMP_OUT, needExhaust ? HIGH : LOW); // 吸气泵（23）——放气泵

  // 6. 串口命令处理
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    parseCommand(cmd);
  }

  // 7. 定期发送气压数据（200ms）
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 200) {
    lastSend = millis();
    sendPressureData();
  }
}
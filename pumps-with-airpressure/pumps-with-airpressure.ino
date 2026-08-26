// 引脚定义
const int PUMP_IN   = 22;   // 充气泵
const int PUMP_OUT  = 23;   // 吸气泵
const int VALVE_IN[4]  = {24, 25, 26, 27};   // 充气阀 (前,右,后,左)
const int VALVE_OUT[4] = {28, 29, 30, 31};   // 放气阀 (前,右,后,左)
const int SENSOR_PINS[4] = {A0, A1, A2, A3};

const char DIR_CHARS[4] = {'F', 'R', 'B', 'L'};

struct Chamber {
  bool manualActive;      // 手动激活标志
  bool manualMode;        // true=充气, false=吸气
  float target;           // 自动目标 (0表示无目标)
  float current;          // 当前压力
  bool autoActive;        // 自动是否正在动作 (用于闭环)
  bool autoMode;          // 自动模式 (true充气, false吸气)
};

Chamber chambers[4];

void setup() {
  Serial.begin(9600);
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
}

void loop() {
  // 1. 读取传感器
  for (int i = 0; i < 4; i++) {
    int raw = analogRead(SENSOR_PINS[i]);
    float voltage = raw * 5.0 / 1023.0;
    chambers[i].current = 100.0 * voltage - 150.0; // kPa
  }

  // 2. 自动闭环控制 (根据目标)
  for (int i = 0; i < 4; i++) {
    if (chambers[i].target != 0.0) {
      float diff = chambers[i].target - chambers[i].current;
      if (fabs(diff) > 0.5) {
        chambers[i].autoActive = true;
        chambers[i].autoMode = (diff > 0); // 正差需充气
      } else {
        chambers[i].autoActive = false;    // 达到目标
      }
    } else {
      chambers[i].autoActive = false;      // 无目标
    }
  }

  // 3. 合并输出：手动优先于自动 (若手动激活，则忽略自动)
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

  // 4. 总泵控制 (根据是否有阀打开)
  bool needCharge = false, needExhaust = false;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(VALVE_IN[i]) == HIGH) needCharge = true;
    if (digitalRead(VALVE_OUT[i]) == HIGH) needExhaust = true;
  }
  digitalWrite(PUMP_IN, needCharge ? HIGH : LOW);
  digitalWrite(PUMP_OUT, needExhaust ? HIGH : LOW);

  // 5. 串口命令解析
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    parseCommand(cmd);
  }

  // 6. 发送压力数据 (每200ms)
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 200) {
    lastSend = millis();
    sendPressureData();
  }
}

void parseCommand(String cmd) {
  // 手动启动: MAN,<dir>,<C/I>
  if (cmd.startsWith("MAN,")) {
    int first = cmd.indexOf(',');
    int second = cmd.indexOf(',', first+1);
    if (second == -1) return;
    char dir = cmd.charAt(first+1);
    char mode = cmd.charAt(second+1);
    int idx = dirToIndex(dir);
    if (idx == -1) return;
    // 清除自动目标，关闭自动
    chambers[idx].target = 0.0;
    chambers[idx].autoActive = false;
    // 激活手动
    chambers[idx].manualActive = true;
    chambers[idx].manualMode = (mode == 'C' || mode == 'c');
  }
  // 手动停止: DEACT,<dir>
  else if (cmd.startsWith("DEACT,")) {
    int first = cmd.indexOf(',');
    if (first == -1) return;
    char dir = cmd.charAt(first+1);
    int idx = dirToIndex(dir);
    if (idx == -1) return;
    chambers[idx].manualActive = false;
    // 若之前有自动目标，将自动恢复 (目标保持不变，但被手动清零过，所以不会恢复)
    // 注意：手动启动时已清除了目标，所以停止手动后目标为0，自动不会启动
  }
  // 设定自动目标: SET,<dir>,<target>
  else if (cmd.startsWith("SET,")) {
    int first = cmd.indexOf(',');
    int second = cmd.indexOf(',', first+1);
    if (second == -1) return;
    char dir = cmd.charAt(first+1);
    float target = cmd.substring(second+1).toFloat();
    int idx = dirToIndex(dir);
    if (idx == -1) return;
    // 关闭手动
    chambers[idx].manualActive = false;
    // 设置目标
    chambers[idx].target = target;
    // autoActive 将在主循环中根据目标与当前压力判断
  }
  // 全部关闭
  else if (cmd == "OFF" || cmd == "STOP") {
    for (int i = 22; i <= 31; i++) digitalWrite(i, LOW);
    for (int i = 0; i < 4; i++) {
      chambers[i].manualActive = false;
      chambers[i].target = 0.0;
      chambers[i].autoActive = false;
    }
  }
}

int dirToIndex(char c) {
  switch(c) {
    case 'F': return 0;
    case 'R': return 1;
    case 'B': return 2;
    case 'L': return 3;
    default: return -1;
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
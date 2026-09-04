// 引脚定义
const int PUMP_IN   = 22;   // 充气泵
const int PUMP_OUT  = 23;   // 吸气泵
// 充气阀：前、右、后、左
const int VALVE_IN[4]  = {24, 25, 26, 27};
// 放气阀：前、右、后、左
const int VALVE_OUT[4] = {28, 29, 30, 31};

enum Direction { FRONT, RIGHT, BACK, LEFT };
enum Mode { CHARGE, EXHAUST };

void setup() {
  Serial.begin(9600);
  for (int i = 22; i <= 31; i++) {
    pinMode(i, OUTPUT);
    digitalWrite(i, LOW);
  }
}

void loop() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    if (cmd.length() == 0) return;

    // 处理关闭命令（新增）
    if (cmd == "OFF") {
      allOff();
      Serial.println("OFF");
      return;
    }

    // 处理停止命令（兼容旧版 ST）
    if (cmd == "ST") {
      allOff();
      Serial.println("STOP");
      return;
    }

    if (cmd.length() < 2) return;

    char dirChar = cmd.charAt(0);
    char modeChar = cmd.charAt(1);

    int dir;
    switch (dirChar) {
      case 'F': dir = FRONT; break;
      case 'R': dir = RIGHT; break;
      case 'B': dir = BACK; break;
      case 'L': dir = LEFT; break;
      default: return;
    }

    int mode;
    switch (modeChar) {
      case 'C': mode = CHARGE; break;
      case 'I': mode = EXHAUST; break;
      default: return;
    }

    allOff();

    if (mode == CHARGE) {
      digitalWrite(PUMP_IN, HIGH);
      digitalWrite(VALVE_IN[dir], HIGH);
    } else {
      digitalWrite(PUMP_OUT, HIGH);
      digitalWrite(VALVE_OUT[dir], HIGH);
    }

    Serial.print("OK: ");
    Serial.print(dirChar);
    Serial.println(modeChar);
  }
}

void allOff() {
  for (int i = 22; i <= 31; i++) {
    digitalWrite(i, LOW);
  }
}

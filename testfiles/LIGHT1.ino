#include <Wire.h>

#define TCA_ADDR        0x70
#define ADDRESS_BH1750FVI 0x23
#define ONE_TIME_H_RESOLUTION_MODE 0x20

// 切换指定通道 0~7
void selectChannel(uint8_t ch)
{
  if(ch > 7) return;
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << ch);
  Wire.endTransmission();
}

// 关闭全部通道，释放I2C总线
void closeAllCh()
{
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(0x00);
  Wire.endTransmission();
}

// BH1750读取函数，修复返回值报错
unsigned int readBH1750()
{
  byte highByte = 0;
  byte lowByte = 0;
  unsigned int sensorOut = 0;
  unsigned int illuminance = 0;

  // 强制忽略返回值，消除编译报错
  (void)Wire.beginTransmission(ADDRESS_BH1750FVI);
  Wire.write(ONE_TIME_H_RESOLUTION_MODE);
  Wire.endTransmission();

  delay(180);
  // 接收requestFrom返回字节数，无报错
  uint8_t recvCnt = Wire.requestFrom(ADDRESS_BH1750FVI, 2);

  if (recvCnt >= 2)
  {
    highByte = Wire.read();
    lowByte = Wire.read();
    sensorOut = (highByte << 8) | lowByte;
    illuminance = sensorOut / 1.2;
  }
  else
  {
    illuminance = 0;
  }
  return illuminance;
}

void setup() {
  Wire.begin();
  Serial.begin(115200);
  delay(800);
  Serial.println("TCA9548A 四路BH1750 程序启动");
}

void loop() {
  // 通道0
  selectChannel(0);
  unsigned int lux1 = readBH1750();
  closeAllCh();

  // 通道1
  selectChannel(1);
  unsigned int lux2 = readBH1750();
  closeAllCh();

  // 通道2
  selectChannel(2);
  unsigned int lux3 = readBH1750();
  closeAllCh();

  // 通道3
  selectChannel(3);
  unsigned int lux4 = readBH1750();
  closeAllCh();

  Serial.print("通道0：");Serial.print(lux1);Serial.print(" lx | ");
  Serial.print("通道1：");Serial.print(lux2);Serial.print(" lx | ");
  Serial.print("通道2：");Serial.print(lux3);Serial.print(" lx | ");
  Serial.print("通道3：");Serial.print(lux4);Serial.println(" lx");

  Serial.println("----------------------------------------");
  delay(1000);
}

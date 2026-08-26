#include <Wire.h>

#define TCA9548A_ADDR 0x70
#define BH1750_ADDR   0x23
#define ONE_TIME_H_RESOLUTION_MODE 0x20

// 选择TCA指定通道（0~7）
void selectTCAChannel(uint8_t ch)
{
  if(ch > 7) return;
  Wire.beginTransmission(TCA9548A_ADDR);
  Wire.write(1 << ch);
  Wire.endTransmission();
}

// 读取单个BH1750光照值
unsigned int readBH1750(uint8_t addr)
{
  byte highByte = 0, lowByte = 0;
  unsigned int sensorOut, illuminance;

  Wire.beginTransmission(addr);
  Wire.write(ONE_TIME_H_RESOLUTION_MODE);
  Wire.endTransmission();

  delay(180);
  Wire.requestFrom(addr, 2);

  if (Wire.available() >= 2)
  {
    highByte = Wire.read();
    lowByte = Wire.read();
    sensorOut = (highByte << 8) | lowByte;
    illuminance = sensorOut / 1.2;
  }
  else
  {
    illuminance = 0; // 读取失败返回0，方便排查故障
  }
  return illuminance;
}

void setup()
{
  Wire.begin();
  Serial.begin(115200);
}

void loop()
{
  // 依次切换4个通道读取传感器
  selectTCAChannel(0);
  unsigned int lux1 = readBH1750(BH1750_ADDR);

  selectTCAChannel(1);
  unsigned int lux2 = readBH1750(BH1750_ADDR);

  selectTCAChannel(2);
  unsigned int lux3 = readBH1750(BH1750_ADDR);

  selectTCAChannel(3);
  unsigned int lux4 = readBH1750(BH1750_ADDR);

  Serial.print("通道0 光照：");Serial.print(lux1);Serial.println(" lx");
  Serial.print("通道1 光照：");Serial.print(lux2);Serial.println(" lx");
  Serial.print("通道2 光照：");Serial.print(lux3);Serial.println(" lx");
  Serial.print("通道3 光照：");Serial.print(lux4);Serial.println(" lx");
  Serial.println("--------------------------------");
  delay(1000);
}
#include "JY901_2.h"
#include <string.h>
#include <Wire.h>

CJY901_2::CJY901_2()
{
    ucDevAddr = 0x50;
}

void CJY901_2::StartIIC()
{
    ucDevAddr = 0x50;
    Wire.begin();
}

void CJY901_2::StartIIC(unsigned char ucAddr)
{
    ucDevAddr = ucAddr;
    Wire.begin();
}

void CJY901_2::CopeSerialData(unsigned char ucData)
{
    static unsigned char ucRxBuffer[250];
    static unsigned char ucRxCnt = 0;    
    
    ucRxBuffer[ucRxCnt++]=ucData;
    if (ucRxBuffer[0]!=0x55) 
    {
        ucRxCnt=0;
        return;
    }
    if (ucRxCnt<11) {return;}
    else
    {
        switch(ucRxBuffer[1])
        {
            case 0x50:    memcpy(&stcTime,&ucRxBuffer[2],8);break;
            case 0x51:    memcpy(&stcAcc,&ucRxBuffer[2],8);break;
            case 0x52:    memcpy(&stcGyro,&ucRxBuffer[2],8);break;
            case 0x53:    memcpy(&stcAngle,&ucRxBuffer[2],8);break;
            case 0x54:    memcpy(&stcMag,&ucRxBuffer[2],8);break;
            case 0x55:    memcpy(&stcDStatus,&ucRxBuffer[2],8);break;
            case 0x56:    memcpy(&stcPress,&ucRxBuffer[2],8);break;
            case 0x57:    memcpy(&stcLonLat,&ucRxBuffer[2],8);break;
            case 0x58:    memcpy(&stcGPSV,&ucRxBuffer[2],8);break;
            case 0x59:    memcpy(&stcQuater,&ucRxBuffer[2],8);break;
            case 0x5a:    memcpy(&stcSN,&ucRxBuffer[2],8);break;
        }
        ucRxCnt=0;
    }
}

void CJY901_2::readRegisters(unsigned char deviceAddr, unsigned char addressToRead, 
                           unsigned char bytesToRead, char * dest)
{
    Wire.beginTransmission(deviceAddr);
    Wire.write(addressToRead);
    Wire.endTransmission(false); // Keep connection active

    Wire.requestFrom(deviceAddr, bytesToRead);
    while(Wire.available() < bytesToRead); // Wait for all bytes

    for(int x = 0; x < bytesToRead; x++)
        dest[x] = Wire.read();    
}

void CJY901_2::writeRegister(unsigned char deviceAddr, unsigned char addressToWrite,
                           unsigned char bytesToRead, char *dataToWrite)
{
    Wire.beginTransmission(deviceAddr);
    Wire.write(addressToWrite);
    for(int i = 0; i < bytesToRead; i++)
        Wire.write(dataToWrite[i]);
    Wire.endTransmission();
}

short CJY901_2::ReadWord(unsigned char ucAddr)
{
    short sResult;
    readRegisters(ucDevAddr, ucAddr, 2, (char *)&sResult);
    return sResult;
}

void CJY901_2::WriteWord(unsigned char ucAddr, short sData)
{    
    writeRegister(ucDevAddr, ucAddr, 2, (char *)&sData);
}

void CJY901_2::ReadData(unsigned char ucAddr, unsigned char ucLength, char chrData[])
{
    readRegisters(ucDevAddr, ucAddr, ucLength, chrData);
}

void CJY901_2::GetTime()
{
    readRegisters(ucDevAddr, YYMM_2, 8, (char*)&stcTime);    
}

void CJY901_2::GetAcc()
{
    readRegisters(ucDevAddr, AX_2, 6, (char *)&stcAcc);
}

void CJY901_2::GetGyro()
{
    readRegisters(ucDevAddr, GX_2, 6, (char *)&stcGyro);
}

void CJY901_2::GetAngle()
{
    readRegisters(ucDevAddr, Roll_2, 6, (char *)&stcAngle);
}

void CJY901_2::GetMag()
{
    readRegisters(ucDevAddr, HX_2, 6, (char *)&stcMag);
}

void CJY901_2::GetPress()
{
    readRegisters(ucDevAddr, PressureL_2, 8, (char *)&stcPress);
}

void CJY901_2::GetDStatus()
{
    readRegisters(ucDevAddr, D0Status_2, 8, (char *)&stcDStatus);
}

void CJY901_2::GetLonLat()
{
    readRegisters(ucDevAddr, LonL_2, 8, (char *)&stcLonLat);
}

void CJY901_2::GetGPSV()
{
    readRegisters(ucDevAddr, GPSHeight_2, 8, (char *)&stcGPSV);
}

CJY901_2 JY901_2;
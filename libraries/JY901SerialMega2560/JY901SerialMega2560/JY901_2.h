#ifndef JY901_2_h
#define JY901_2_h

// 重命名所有定义以避免冲突
#define SAVE_2           0x00
#define CALSW_2         0x01
#define RSW_2           0x02
#define RRATE_2         0x03
#define BAUD_2          0x04
#define AXOFFSET_2      0x05
#define AYOFFSET_2      0x06
#define AZOFFSET_2      0x07
#define GXOFFSET_2      0x08
#define GYOFFSET_2      0x09
#define GZOFFSET_2      0x0a
#define HXOFFSET_2      0x0b
#define HYOFFSET_2      0x0c
#define HZOFFSET_2      0x0d
#define D0MODE_2        0x0e
#define D1MODE_2        0x0f
#define D2MODE_2        0x10
#define D3MODE_2        0x11
#define D0PWMH_2        0x12
#define D1PWMH_2        0x13
#define D2PWMH_2        0x14
#define D3PWMH_2        0x15
#define D0PWMT_2        0x16
#define D1PWMT_2        0x17
#define D2PWMT_2        0x18
#define D3PWMT_2        0x19
#define IICADDR_2       0x1a
#define LEDOFF_2        0x1b
#define GPSBAUD_2       0x1c

#define YYMM_2          0x30
#define DDHH_2          0x31
#define MMSS_2          0x32
#define MS_2            0x33
#define AX_2            0x34
#define AY_2            0x35
#define AZ_2            0x36
#define GX_2            0x37
#define GY_2            0x38
#define GZ_2            0x39
#define HX_2            0x3a
#define HY_2            0x3b
#define HZ_2            0x3c            
#define Roll_2          0x3d
#define Pitch_2         0x3e
#define Yaw_2           0x3f
#define TEMP_2          0x40
#define D0Status_2      0x41
#define D1Status_2      0x42
#define D2Status_2      0x43
#define D3Status_2      0x44
#define PressureL_2     0x45
#define PressureH_2     0x46
#define HeightL_2       0x47
#define HeightH_2       0x48
#define LonL_2          0x49
#define LonH_2          0x4a
#define LatL_2          0x4b
#define LatH_2          0x4c
#define GPSHeight_2     0x4d
#define GPSYAW_2        0x4e
#define GPSVL_2         0x4f
#define GPSVH_2         0x50
      
#define DIO_MODE_AIN_2  0
#define DIO_MODE_DIN_2  1
#define DIO_MODE_DOH_2  2
#define DIO_MODE_DOL_2  3
#define DIO_MODE_DOPWM_2 4
#define DIO_MODE_GPS_2  5        

// 重命名所有结构体
struct STime_2
{
    unsigned char ucYear;
    unsigned char ucMonth;
    unsigned char ucDay;
    unsigned char ucHour;
    unsigned char ucMinute;
    unsigned char ucSecond;
    unsigned short usMiliSecond;
};

struct SAcc_2
{
    short a[3];
    short T;
};

struct SGyro_2
{
    short w[3];
    short T;
};

struct SAngle_2
{
    short Angle[3];
    short T;
};

struct SMag_2
{
    short h[3];
    short T;
};

struct SDStatus_2
{
    short sDStatus[4];
};

struct SPress_2
{
    long lPressure;
    long lAltitude;
};

struct SLonLat_2
{
    long lLon;
    long lLat;
};

struct SGPSV_2
{
    short sGPSHeight;
    short sGPSYaw;
    long lGPSVelocity;
};

struct SQuater_2
{
    short q0;
    short q1;
    short q2;
    short q3;
};

struct SSN_2
{
    short sSVNum;
    short sPDOP;
    short sHDOP;
    short sVDOP;
};

// 重命名类
class CJY901_2 
{
public: 
    STime_2     stcTime;
    SAcc_2      stcAcc;
    SGyro_2     stcGyro;
    SAngle_2    stcAngle;
    SMag_2      stcMag;
    SDStatus_2  stcDStatus;
    SPress_2    stcPress;
    SLonLat_2   stcLonLat;
    SGPSV_2     stcGPSV;
    SQuater_2   stcQuater;
    SSN_2       stcSN;
    
    CJY901_2(); 
    void StartIIC();
    void StartIIC(unsigned char ucAddr);
    void CopeSerialData(unsigned char ucData);
    short ReadWord(unsigned char ucAddr);
    void WriteWord(unsigned char ucAddr,short sData);
    void ReadData(unsigned char ucAddr,unsigned char ucLength,char chrData[]);
    void GetTime();
    void GetAcc();
    void GetGyro();
    void GetAngle();
    void GetMag();
    void GetPress();
    void GetDStatus();
    void GetLonLat();
    void GetGPSV();
    
private: 
    unsigned char ucDevAddr; 
    void readRegisters(unsigned char deviceAddr,unsigned char addressToRead, unsigned char bytesToRead, char * dest);
    void writeRegister(unsigned char deviceAddr,unsigned char addressToWrite,unsigned char bytesToRead, char *dataToWrite);
};

extern CJY901_2 JY901_2;

#endif
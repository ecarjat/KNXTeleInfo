#ifndef TELEINFO_H
#define TELEINFO_H
#include <Arduino.h>
#include <knx.h>
#include "RTCKnx.h"

#define HISTORY_FLASH_START 0
#define TELEINFO_BUFFERSIZE 512U
#define HISTORY_MANUALWRITE_TEMPO (60 * 60 * 1000) // 1 hour
#define ADPS_REPEAT_PERIOD (10 * 1000)             // Repeat ADPS > 0 every 10s

class TeleInfo
{
private:
    SerialUART mSerial;
    unsigned long speed;
    uint16_t config;
    char mBuffer[TELEINFO_BUFFERSIZE]; // No '\0'
    int mBufferLen = 0;
    RTCKnx rtc;

    struct
    {
        uint32_t period;
        uint32_t realTimeTimeout;
    } mParams;

    enum TarifBlock
    {
        Base = 0,
        HC,
        HP,
        /*BLUE, WHITE, RED,*/ TARIFCOUNT
    };

    struct
    {
        uint16_t realTimeOnOff;
        uint16_t realTimeOnOffState;
        struct
        {
            uint16_t today;
            uint16_t yesterday;
            uint16_t thisMonth;
            uint16_t lastMonth;
            uint16_t thisYear;
            uint16_t lastYear;
        } tariff[TARIFCOUNT];
    } mGO;

    uint32_t mRealTimeTimer = 0;
    uint32_t mHistoryLastValue[TARIFCOUNT] = {0};
    uint32_t mHistoryLastSent = 0;
    uint32_t mLastReception = 0;
    uint32_t mLastManualHistoryInit = 0;
    struct
    {
        RTCKnx::DateTime lastSave;
        struct
        {
            uint32_t index;
            uint32_t yesterday;
            uint32_t lastMonth;
            uint32_t lastYear;
            uint32_t dayM2;
            uint32_t monthM2;
            uint32_t yearM2;
        } tariff[TARIFCOUNT];
    } mHistory = {0};

public:
    struct TeleInfoDataType
    {
        const char *key;
        uint8_t keySize : 4;
        enum Type
        {
            INT = 0,
            STRING,
            OPTARIF,
            PTEC,
            DEMAIN,
            HHPHC
        } type : 4;
        uint8_t size;
        struct
        {
            short mainGroup;
            short subGroup;
        } dpt;
    };

private:
#define Dpt(M, S) \
    {             \
        M, S      \
    }

    const TeleInfoDataType TeleInfoParam[29] PROGMEM = {
        {PSTR("ADCO "), 5, TeleInfoDataType::STRING, 12, DPT_String_ASCII},
        {PSTR("OPTARIF "), 8, TeleInfoDataType::OPTARIF, 4, DPT_Value_1_Ucount},
        {PSTR("ISOUSC "), 7, TeleInfoDataType::INT, 2, DPT_Value_Electric_Current},
        {PSTR("BASE "), 5, TeleInfoDataType::INT, 9, DPT_ActiveEnergy},
        {PSTR("HCHC "), 5, TeleInfoDataType::INT, 9, DPT_ActiveEnergy},
        {PSTR("HCHP "), 5, TeleInfoDataType::INT, 9, DPT_ActiveEnergy},
        {PSTR("EJPHN "), 6, TeleInfoDataType::INT, 9, DPT_ActiveEnergy},
        {PSTR("EJPHPM "), 7, TeleInfoDataType::INT, 9, DPT_ActiveEnergy},
        {PSTR("BBRHCJB "), 8, TeleInfoDataType::INT, 9, DPT_ActiveEnergy},
        {PSTR("BBRHPJB "), 8, TeleInfoDataType::INT, 9, DPT_ActiveEnergy},
        {PSTR("BBRHCJW "), 8, TeleInfoDataType::INT, 9, DPT_ActiveEnergy},
        {PSTR("BBRHPJW "), 8, TeleInfoDataType::INT, 9, DPT_ActiveEnergy},
        {PSTR("BBRHCJR "), 8, TeleInfoDataType::INT, 9, DPT_ActiveEnergy},
        {PSTR("BBRHPJR "), 8, TeleInfoDataType::INT, 9, DPT_ActiveEnergy},
        {PSTR("PEJP "), 5, TeleInfoDataType::INT, 2, DPT_TimePeriodMin},
        {PSTR("PTEC "), 5, TeleInfoDataType::PTEC, 4, DPT_Value_1_Ucount},
        {PSTR("DEMAIN "), 7, TeleInfoDataType::DEMAIN, 4, DPT_Value_1_Ucount},
        {PSTR("IINST "), 6, TeleInfoDataType::INT, 3, DPT_Value_Electric_Current},
        {PSTR("ADPS "), 5, TeleInfoDataType::INT, 3, DPT_Value_Electric_Current},
        {PSTR("IMAX "), 5, TeleInfoDataType::INT, 3, DPT_Value_Electric_Current},
        {PSTR("PAPP "), 5, TeleInfoDataType::INT, 5, DPT_Value_2_Count}, // VA
        {PSTR("HHPHC "), 6, TeleInfoDataType::HHPHC, 1, DPT_Char_ASCII},
        {PSTR("IINST1 "), 7, TeleInfoDataType::INT, 3, DPT_Value_Electric_Current},
        {PSTR("IINST2 "), 7, TeleInfoDataType::INT, 3, DPT_Value_Electric_Current},
        {PSTR("IINST3 "), 7, TeleInfoDataType::INT, 3, DPT_Value_Electric_Current},
        {PSTR("IMAX1 "), 6, TeleInfoDataType::INT, 3, DPT_Value_Electric_Current},
        {PSTR("IMAX2 "), 6, TeleInfoDataType::INT, 3, DPT_Value_Electric_Current},
        {PSTR("IMAX3 "), 6, TeleInfoDataType::INT, 3, DPT_Value_Electric_Current},
        {PSTR("PMAX "), 5, TeleInfoDataType::INT, 5, DPT_Value_Power}};

#undef Dpt

    static const unsigned int TeleInfoCount = sizeof(TeleInfoParam) / sizeof(TeleInfoParam[0]);

public:
    struct TeleInfoDataStruct
    {
        uint16_t goSend;
        const TeleInfoDataType *conf;
        union
        {
            char str[13];
            uint32_t num;
        } value;
        uint32_t lastSendValueCheckSum;
        uint32_t lastChange;
        uint32_t lastSend;
    } mTeleInfoData[TeleInfoCount] = {0};

private:
    // Hold the memory buffer for all teleinfo
    static inline bool validChecksum(const char *begin, const char *end);
    static inline uint32_t simpleChecksum(const char *str);
    static inline KNXValue value(const TeleInfoDataStruct &val);
    static inline bool value(TeleInfo::TeleInfoDataStruct &val, const char *begin, const char *end);

public:
    TeleInfo(RTCKnx *_rtc, SerialUART *suart, unsigned long _baud, uint16_t _config);
    void init(int baseAddr, uint16_t baseGO);
    void setHistory(uint32_t ref, uint32_t &dest, uint32_t src, int idxTariff, RTCKnx::DateChange periodToEmit);
    uint32_t lastReception() const;
    void loop();
    void currentIndexes(uint32_t index[TARIFCOUNT]) const;
    void newDate(RTCKnx::DateChange change);
    void validateHistory();
    void restoreHistory();
    void saveHistory();
    void resetHistory();
    void resyncHistoryGroupObjects();
};
#endif
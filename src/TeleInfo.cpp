#include <Arduino.h>
#include <knx.h>
#include <EEPROM.h>
#include "TeleInfo.h"

#define FOURCC(a, b, c, d) (((((uint32_t)(a)) << 24) | (((uint32_t)(b)) << 16) | (((uint32_t)(c)) << 8) | (d)))

TeleInfo::TeleInfo(RTCKnx *_rtc, SerialUART *suart, unsigned long _baud, uint16_t _config) : mSerial(*suart), rtc(*_rtc)
{
    speed = _baud;
    config = _config;
}

KNXValue TeleInfo::value(const TeleInfo::TeleInfoDataStruct &val)
{
    switch (val.conf->type)
    {
    default:
    case TeleInfoDataType::INT:
        return KNXValue(val.value.num);
    case TeleInfoDataType::STRING:
        return KNXValue(val.value.str);
    case TeleInfoDataType::OPTARIF:
        switch (val.value.num & 0xffffff00)
        {
        case FOURCC('B', 'A', 'S', 0) /*BASE*/:
        default:
            return KNXValue((uint8_t)0);
        case FOURCC('H', 'C', '.', 0) /*HC..*/:
            return KNXValue((uint8_t)1);
        case FOURCC('E', 'J', 'P', 0) /*EJP.*/:
            return KNXValue((uint8_t)2);
        case FOURCC('B', 'B', 'R', 0) /*BBRx*/:
            return KNXValue((uint8_t)(val.value.num & 0x3f));
            //                                  - Bit 5: toujours 1
            //                                  - Bit 4-3: programme circuit 1: 01-11 _ programme A-C
            //                                  - Bit 2-0: programme circuit 2: 000-111 _ programme P0-P7
        }
    case TeleInfoDataType::PTEC:
        switch (val.value.num)
        {
        case FOURCC('T', 'H', '.', '.') /*Toutes les Heures*/:
        default:
            return KNXValue((uint8_t)0);
        case FOURCC('H', 'C', '.', '.') /*Heures Creuses*/:
            return KNXValue((uint8_t)1);
        case FOURCC('H', 'P', '.', '.') /*Heures Pleines*/:
            return KNXValue((uint8_t)2);
        case FOURCC('H', 'N', '.', '.') /*Heures Normales*/:
            return KNXValue((uint8_t)3);
        case FOURCC('P', 'M', '.', '.') /*Heures de Pointe Mobile*/:
            return KNXValue((uint8_t)4);
        case FOURCC('H', 'C', 'J', 'B') /*Heures Creuses Jours Bleus*/:
            return KNXValue((uint8_t)5);
        case FOURCC('H', 'C', 'J', 'W') /*Heures Creuses Jours Blancs*/:
            return KNXValue((uint8_t)6);
        case FOURCC('H', 'C', 'J', 'R') /*Heures Creuses Jours Rouges*/:
            return KNXValue((uint8_t)7);
        case FOURCC('H', 'P', 'J', 'B') /*Heures Pleines Jours Bleus*/:
            return KNXValue((uint8_t)8);
        case FOURCC('H', 'P', 'J', 'W') /*Heures Pleines Jours Blancs*/:
            return KNXValue((uint8_t)9);
        case FOURCC('H', 'P', 'J', 'R') /*Heures Pleines Jours Rouges*/:
            return KNXValue((uint8_t)10);
        }
    case TeleInfoDataType::DEMAIN:
        switch (val.value.num)
        {
        case FOURCC('-', '-', '-', '-'):
        default:
            return KNXValue((uint8_t)0);
        case FOURCC('B', 'L', 'E', 'U'):
            return KNXValue((uint8_t)1);
        case FOURCC('B', 'L', 'A', 'N'):
            return KNXValue((uint8_t)2);
        case FOURCC('R', 'O', 'U', 'G'):
            return KNXValue((uint8_t)3);
        }
    case TeleInfoDataType::HHPHC:
        return KNXValue((uint8_t)val.value.num);
    }
}

bool TeleInfo::value(TeleInfo::TeleInfoDataStruct &val, const char *begin, const char *end)
{
    begin += val.conf->keySize;
    const char *vEnd = begin + val.conf->size;
    if (vEnd < end)
    {
        if (val.conf->type == TeleInfo::TeleInfoDataType::STRING)
        {
            const uint8_t size = val.conf->size;
            if (memcmp(val.value.str, begin, size) != 0)
            {
                memcpy(val.value.str, begin, size);
                val.value.str[size] = '\0';
                return true;
            }
        }
        else if (val.conf->type == TeleInfo::TeleInfoDataType::INT)
        {
            uint32_t _value = 0;
            for (; begin != vEnd; ++begin)
            {
                const unsigned char v = (const unsigned char)(*begin - '0');
                if (v > 9)
                    break;
                _value = _value * 10 + v;
            }
            if (val.value.num != _value)
            {
                val.value.num = _value;
                return true;
            }
        }
        else
        {
            uint32_t _value = 0;
            for (; begin != vEnd; ++begin)
            {
                _value = (_value << 8) | *(uint8_t *)begin;
            }
            if (val.value.num != _value)
            {
                val.value.num = _value;
                return true;
            }
        }
    }
    return false;
}

bool TeleInfo::validChecksum(const char *begin, const char *end)
{
    uint16_t sum = 0;
    uint8_t spaceFound = 0;
    for (; begin != end; ++begin)
    {
        const char c = *begin;
        if (c == ' ')
        {
            ++spaceFound;
        }
        else if (spaceFound == 2)
        {
            // checksum after second space
            return (((uint8_t)(sum - ' ') & 0x3F) + 0x20) == c;
        }
        sum += c;
    }
    return false;
}

uint32_t TeleInfo::simpleChecksum(const char *str)
{
    uint32_t result = 0;
    while (*str)
        result += result + *str++;
    return result;
}

void TeleInfo::init(int baseAddr, uint16_t baseGO)
{
    mParams.period = knx.paramInt(baseAddr) * 1000;                   // In Seconds
    mParams.realTimeTimeout = knx.paramInt(baseAddr + 4) * 60 * 1000; // In Minutes
    if (mLastReception == 0)
    { // Cold reset
        restoreHistory();
    }
    knx.getGroupObject(mGO.realTimeOnOff = ++baseGO).dataPointType(DPT_Switch);
    knx.getGroupObject(mGO.realTimeOnOff).callback([this](GroupObject &go)
                                                   { mRealTimeTimer = go.value() ? rtc.millis() | 1 : 0; });
    knx.getGroupObject(mGO.realTimeOnOffState = ++baseGO).dataPointType(DPT_Switch);
    knx.getGroupObject(mGO.realTimeOnOffState).valueNoSend(mRealTimeTimer != 0);
    for (int i = 0; i < TARIFCOUNT; ++i)
    {
        knx.getGroupObject(mGO.tariff[i].today = ++baseGO).dataPointType(DPT_ActiveEnergy);
        knx.getGroupObject(mGO.tariff[i].yesterday = ++baseGO).dataPointType(DPT_ActiveEnergy);
        knx.getGroupObject(mGO.tariff[i].thisMonth = ++baseGO).dataPointType(DPT_ActiveEnergy);
        knx.getGroupObject(mGO.tariff[i].lastMonth = ++baseGO).dataPointType(DPT_ActiveEnergy);
        knx.getGroupObject(mGO.tariff[i].thisYear = ++baseGO).dataPointType(DPT_ActiveEnergy);
        knx.getGroupObject(mGO.tariff[i].lastYear = ++baseGO).dataPointType(DPT_ActiveEnergy);
        knx.getGroupObject(mGO.tariff[i].today).callback([this, i](GroupObject &go)
                                                         { setHistory(mHistory.tariff[i].index, mHistory.tariff[i].yesterday, go.value(), i, RTCKnx::Day); });
        knx.getGroupObject(mGO.tariff[i].yesterday).callback([this, i](GroupObject &go)
                                                             { setHistory(mHistory.tariff[i].yesterday, mHistory.tariff[i].dayM2, go.value(), i, RTCKnx::Day); });
        knx.getGroupObject(mGO.tariff[i].thisMonth).callback([this, i](GroupObject &go)
                                                             { setHistory(mHistory.tariff[i].index, mHistory.tariff[i].lastMonth, go.value(), i, RTCKnx::Month); });
        knx.getGroupObject(mGO.tariff[i].lastMonth).callback([this, i](GroupObject &go)
                                                             { setHistory(mHistory.tariff[i].lastMonth, mHistory.tariff[i].monthM2, go.value(), i, RTCKnx::Month); });
        knx.getGroupObject(mGO.tariff[i].thisYear).callback([this, i](GroupObject &go)
                                                            { setHistory(mHistory.tariff[i].index, mHistory.tariff[i].lastYear, go.value(), i, RTCKnx::Year); });
        knx.getGroupObject(mGO.tariff[i].lastYear).callback([this, i](GroupObject &go)
                                                            { setHistory(mHistory.tariff[i].lastYear, mHistory.tariff[i].yearM2, go.value(), i, RTCKnx::Year); });
    }

    resyncHistoryGroupObjects();
    const TeleInfoDataType *param = TeleInfoParam;
    for (TeleInfoDataStruct *data = mTeleInfoData; data != mTeleInfoData + TeleInfoCount; ++data, ++param)
    {
        data->conf = param;
        data->value.num = 0;
        knx.getGroupObject(data->goSend = ++baseGO).dataPointType(Dpt(data->conf->dpt.mainGroup, data->conf->dpt.subGroup));
        knx.getGroupObject(data->goSend).valueNoSend(value(*data));
    }
    mBufferLen = 0;

    mSerial.begin(speed, config);
    while (!mSerial)
    {
        delay(10);
    }
}
void TeleInfo::setHistory(uint32_t ref, uint32_t &dest, uint32_t src, int idxTariff, RTCKnx::DateChange periodToEmit)
{
    if (ref - dest == src || src == dest)
        return;
    dest = src;
    resyncHistoryGroupObjects();
    if (periodToEmit == RTCKnx::Day)
    {
        if (mHistory.tariff[idxTariff].index != 0 && mHistory.tariff[idxTariff].yesterday != 0)
            knx.getGroupObject(mGO.tariff[idxTariff].today).objectWritten();
        if (mHistory.tariff[idxTariff].yesterday != 0 && mHistory.tariff[idxTariff].dayM2 != 0)
            knx.getGroupObject(mGO.tariff[idxTariff].yesterday).objectWritten();
    }
    else if (periodToEmit == RTCKnx::Month)
    {
        if (mHistory.tariff[idxTariff].index != 0 && mHistory.tariff[idxTariff].lastMonth != 0)
            knx.getGroupObject(mGO.tariff[idxTariff].thisMonth).objectWritten();
        if (mHistory.tariff[idxTariff].lastMonth != 0 && mHistory.tariff[idxTariff].monthM2 != 0)
            knx.getGroupObject(mGO.tariff[idxTariff].lastMonth).objectWritten();
    }
    else if (periodToEmit == RTCKnx::Year)
    {
        if (mHistory.tariff[idxTariff].index != 0 && mHistory.tariff[idxTariff].lastYear != 0)
            knx.getGroupObject(mGO.tariff[idxTariff].thisYear).objectWritten();
        if (mHistory.tariff[idxTariff].lastYear != 0 && mHistory.tariff[idxTariff].yearM2 != 0)
            knx.getGroupObject(mGO.tariff[idxTariff].lastYear).objectWritten();
    }
    mLastManualHistoryInit = rtc.millis();
}

uint32_t TeleInfo::lastReception() const { return mLastReception; }

void TeleInfo::loop()
{
    uint32_t current = rtc.millis() | 1;
    bool isRealTime = knx.getGroupObject(mGO.realTimeOnOffState).value();
    if (mRealTimeTimer && (mParams.realTimeTimeout == 0 || current - mRealTimeTimer < mParams.realTimeTimeout))
    {
        if (!isRealTime)
        {
            knx.getGroupObject(mGO.realTimeOnOffState).value(true);
            isRealTime = true;
        }
    }
    else
    {
        if (isRealTime)
        {
            knx.getGroupObject(mGO.realTimeOnOffState).value(false);
            isRealTime = false;
        }
        mRealTimeTimer = 0;
    }
    if (mLastManualHistoryInit && current - mLastManualHistoryInit > HISTORY_MANUALWRITE_TEMPO)
    {
        saveHistory();
        mLastManualHistoryInit = 0;
    }
    for (;;)
    {
        unsigned int pending = mSerial.available();
        if (pending == 0)
            break;

        while (pending > 0)
        {
            //Serial.println("Serial Data received");
            if (mBufferLen == TELEINFO_BUFFERSIZE)
            {
                mBufferLen = 0; // Security - Reset buffer if full with dummies
                break;
            }
            unsigned int rcv = 0, ready = MIN(TELEINFO_BUFFERSIZE - mBufferLen, pending);
            char *ptr = mBuffer + mBufferLen;
            while (rcv < ready)
            {
                int c = mSerial.read();
                if (c < 0)
                {
                    break;
                }
                *ptr++ = (char)c;
                ++rcv;
            }
            if (rcv == 0)
                break;
            pending -= rcv;
            mBufferLen += rcv;
            const char *currentBuffer = mBuffer;
            for (;;)
            {
                // extract first line if
                const char *eol = currentBuffer;
                for (; eol != mBuffer + mBufferLen; ++eol)
                {
                    if (*eol == '\x0d')
                    {
                        break;
                    }
                }
                if (eol == mBuffer + mBufferLen)
                    break;
                // search first valid character
                for (; currentBuffer != eol; ++currentBuffer)
                {
                    const char c = *currentBuffer;
                    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ')
                    {
                        break;
                    }
                }
                const unsigned int lineLen = eol - currentBuffer;
                if (TeleInfo::validChecksum(currentBuffer, eol))
                {
                    mLastReception = current;
                    for (TeleInfoDataStruct *data = mTeleInfoData; data != mTeleInfoData + TeleInfoCount; ++data)
                    {
                        if (lineLen > data->conf->keySize && memcmp(currentBuffer, data->conf->key, data->conf->keySize) == 0)
                        {
                            if (TeleInfo::value(*data, currentBuffer, eol))
                            {
                                data->lastChange = current;
                                knx.getGroupObject(data->goSend).valueNoSend(TeleInfo::value(*data));
                            }
                            break;
                        }
                    }
                }
                currentBuffer = eol + 1;
            }
            mBufferLen -= currentBuffer - mBuffer;
            memmove(mBuffer, currentBuffer, mBufferLen);
        }
    }

    // Update ADPS (forced) when IINST or ISOUSC changed before ADPS (ADPS = MAX(0, IINST - ISOUSC));
    const TeleInfoDataStruct &isousc = mTeleInfoData[2 /* ISOUSC*/];
    if (isousc.lastChange != 0)
    {
        TeleInfoDataStruct &adps = mTeleInfoData[18 /* ADPS*/];
        const TeleInfoDataStruct *iinsts[] = {&mTeleInfoData[17 /* IINST*/], &mTeleInfoData[22 /* IINST1*/], &mTeleInfoData[23 /* IINST2*/], &mTeleInfoData[24 /* IINST3*/]};
        const TeleInfoDataStruct *maxiinst = nullptr;
        for (size_t i = 0; i < sizeof(iinsts) / sizeof(iinsts[0]); ++i)
        {
            if (!maxiinst || maxiinst->value.num < iinsts[i]->value.num)
            {
                maxiinst = iinsts[i];
            }
        }
        if (maxiinst && maxiinst->lastChange != 0 && (current == maxiinst->lastChange || current == isousc.lastChange))
        {
            uint32_t adpsValue = maxiinst->value.num > isousc.value.num ? maxiinst->value.num - isousc.value.num : 0;
            if (adps.value.num != adpsValue)
            {
                adps.value.num = adpsValue;
                adps.lastChange = current;
                knx.getGroupObject(adps.goSend).valueNoSend(adpsValue);
            }
        }
        if (current == adps.lastChange || (adps.value.num > 0 && current - adps.lastSend > ADPS_REPEAT_PERIOD))
        {
            adps.lastSendValueCheckSum = adps.value.num;
            knx.getGroupObject(adps.goSend).objectWritten(); // Emit is forced
            adps.lastSend = current;
        }
    }

    // Send if value has changed and period is over
    for (TeleInfoDataStruct *data = mTeleInfoData; data != mTeleInfoData + TeleInfoCount; ++data)
    {
        if (data->lastChange != data->lastSend && (isRealTime || current - data->lastSend > mParams.period))
        {
            uint32_t chksum = data->conf->type == TeleInfoDataType::STRING ? simpleChecksum(data->value.str) : data->value.num;
            if (chksum != data->lastSendValueCheckSum)
            {
                data->lastSendValueCheckSum = chksum;
                knx.getGroupObject(data->goSend).objectWritten();
                data->lastSend = current;
            }
        }
    }

    // Update history
    if (mTeleInfoData[1 /* OPTARIF */].lastChange != 0)
    {
        uint32_t index[TARIFCOUNT] = {0};
        currentIndexes(index);
        for (int i = 0; i < TARIFCOUNT; ++i)
        {
            mHistory.tariff[i].index = index[i];
            if (index[i] == 0 || !rtc.isValid())
                continue;
            if (index[i] >= mHistory.tariff[i].yesterday)
            {
                if (mHistory.tariff[i].yesterday == 0)
                    mHistory.tariff[i].yesterday = index[i];
                knx.getGroupObject(mGO.tariff[i].today).valueNoSend(index[i] - mHistory.tariff[i].yesterday);
            }
            if (index[i] >= mHistory.tariff[i].lastMonth)
            {
                if (mHistory.tariff[i].lastMonth == 0)
                    mHistory.tariff[i].lastMonth = index[i];
                knx.getGroupObject(mGO.tariff[i].thisMonth).valueNoSend(index[i] - mHistory.tariff[i].lastMonth);
            }
            if (index[i] >= mHistory.tariff[i].lastYear)
            {
                if (mHistory.tariff[i].lastYear == 0)
                    mHistory.tariff[i].lastYear = index[i];
                knx.getGroupObject(mGO.tariff[i].thisYear).valueNoSend(index[i] - mHistory.tariff[i].lastYear);
            }
        }
        if (rtc.isValid() && (isRealTime || current - mHistoryLastSent > mParams.period))
        {
            for (int i = 0; i < TARIFCOUNT; ++i)
            {
                if (index[i] != mHistoryLastValue[i])
                {
                    knx.getGroupObject(mGO.tariff[i].today).objectWritten();
                    knx.getGroupObject(mGO.tariff[i].thisMonth).objectWritten();
                    knx.getGroupObject(mGO.tariff[i].thisYear).objectWritten();
                    mHistoryLastSent = current;
                    mHistoryLastValue[i] = index[i];
                }
            }
        }
    }
}
void TeleInfo::currentIndexes(uint32_t index[TARIFCOUNT]) const
{
    // depending on OPTARIF
    uint8_t optarif = value(mTeleInfoData[1 /* OPTARIF */]);
    switch (optarif)
    {
    case 0:
    case 1 /* Base */:
        index[Base] = mTeleInfoData[3 /* BASE */].value.num;
        break;
    case 2 /* HCHP */:
        index[HC] = mTeleInfoData[4 /* HCHC */].value.num;
        index[HP] = mTeleInfoData[5 /* HCHP */].value.num;
        index[Base] = index[HC] + index[HP];
        break;
    case 3 /* EJP */:
        index[HC] = mTeleInfoData[6 /* EJPHN */].value.num;
        index[HP] = mTeleInfoData[7 /* EJPHPM */].value.num;
        index[Base] = index[HC] + index[HP];
        break;
    default /* Tempo */:
    {
        uint32_t blueHC = mTeleInfoData[8 /* BBRHCJB */].value.num, blueHP = mTeleInfoData[9 /* BBRHPJB */].value.num,
                 whiteHC = mTeleInfoData[10 /* BBRHCJW */].value.num, whiteHP = mTeleInfoData[11 /* BBRHPJW */].value.num,
                 redHC = mTeleInfoData[12 /* BBRHCJR */].value.num, redHP = mTeleInfoData[13 /* BBRHPJR */].value.num;
        index[HC] = blueHC + whiteHC + redHC;
        index[HP] = blueHP + whiteHP + redHP;
        // index[BLUE] = blueHC + blueHP; index[WHITE] = whiteHC + whiteHP; index[RED] = redHC + redHP;
        index[Base] = index[HC] + index[HP];
    };
    break;
    }
}
void TeleInfo::newDate(RTCKnx::DateChange change)
{
    if (change == RTCKnx::Init)
    {
        validateHistory();
        return;
    }
    switch (change)
    {
    case RTCKnx::Year:
        for (int i = 0; i < TARIFCOUNT; ++i)
        {
            mHistory.tariff[i].yearM2 = mHistory.tariff[i].lastYear;
            mHistory.tariff[i].lastYear = mHistory.tariff[i].index;
            if (mHistory.tariff[i].yearM2 != 0)
                knx.getGroupObject(mGO.tariff[i].lastYear).value(mHistory.tariff[i].lastYear - mHistory.tariff[i].yearM2);
        }
        [[fallthrough]];
    case RTCKnx::Month:
        for (int i = 0; i < TARIFCOUNT; ++i)
        {
            mHistory.tariff[i].monthM2 = mHistory.tariff[i].lastMonth;
            mHistory.tariff[i].lastMonth = mHistory.tariff[i].index;
            if (mHistory.tariff[i].monthM2 != 0)
                knx.getGroupObject(mGO.tariff[i].lastMonth).value(mHistory.tariff[i].lastMonth - mHistory.tariff[i].monthM2);
        }
        saveHistory(); // Save only each month (due to flash write cycle limited to 10000)
        [[fallthrough]];
    case RTCKnx::Day:
        for (int i = 0; i < TARIFCOUNT; ++i)
        {
            mHistory.tariff[i].dayM2 = mHistory.tariff[i].yesterday;
            mHistory.tariff[i].yesterday = mHistory.tariff[i].index;
            if (mHistory.tariff[i].dayM2 != 0)
                knx.getGroupObject(mGO.tariff[i].yesterday).value(mHistory.tariff[i].yesterday - mHistory.tariff[i].dayM2);
        }
        [[fallthrough]];
    default:;
    }
}
void TeleInfo::validateHistory()
{
    if (mHistory.lastSave.tm_mday == 0)
        return;
    const RTCKnx::DateTime &currentDateTime = rtc.dateTime();
    if (mHistory.lastSave.tm_year != currentDateTime.tm_year)
    {
        mHistory = {0};
    }
    else if (mHistory.lastSave.tm_mon != currentDateTime.tm_mon)
    {
        for (int i = 0; i < TARIFCOUNT; ++i)
        {
            mHistory.tariff[i].lastMonth = mHistory.tariff[i].monthM2 = mHistory.tariff[i].yesterday = mHistory.tariff[i].dayM2 = 0;
        }
    }
    else if (mHistory.lastSave.tm_mday != currentDateTime.tm_mday)
    {
        for (int i = 0; i < TARIFCOUNT; ++i)
        {
            mHistory.tariff[i].yesterday = mHistory.tariff[i].dayM2 = 0;
        }
    }
}

void TeleInfo::restoreHistory()
{

    uint8_t checksum = 0, mask = 0xff, mask2 = 0;
    for (size_t i = 0; i < sizeof(mHistory); ++i)
    {
        const uint8_t v = *((uint8_t *)&mHistory + i) = EEPROM.read(HISTORY_FLASH_START + i);
        mask &= v;
        mask2 |= v;
        checksum ^= v;
    }
    if (mask == 0xff || mask2 == 0 || checksum != EEPROM.read(HISTORY_FLASH_START + sizeof(mHistory)))
    {
        mHistory = {0};
    }
}
void TeleInfo::saveHistory()
{

    if (mHistoryLastValue[Base] == 0)
        return; // Nothing sent, nothing to store...
    const RTCKnx::DateTime &dateTime = rtc.dateTime();
    mHistory.lastSave = dateTime;
    uint8_t checksum = 0;
    for (size_t i = 0; i < sizeof(mHistory); ++i)
    {
        EEPROM.write(HISTORY_FLASH_START + i, *((uint8_t *)&mHistory + i));
        checksum ^= *((uint8_t *)&mHistory + i);
    }
    if (checksum != EEPROM.read(HISTORY_FLASH_START + sizeof(mHistory)))
    {
        EEPROM.write(HISTORY_FLASH_START + sizeof(mHistory), checksum);
        EEPROM.commit();
    }
}
void TeleInfo::resetHistory()
{

    mHistory = {0};
    saveHistory();
    resyncHistoryGroupObjects();
}
void TeleInfo::resyncHistoryGroupObjects()
{

    for (int i = 0; i < TARIFCOUNT; ++i)
    {
        knx.getGroupObject(mGO.tariff[i].today).valueNoSend(mHistory.tariff[i].yesterday != 0 ? mHistory.tariff[i].index - mHistory.tariff[i].yesterday : (uint32_t)0);
        knx.getGroupObject(mGO.tariff[i].yesterday).valueNoSend(mHistory.tariff[i].dayM2 != 0 ? mHistory.tariff[i].yesterday - mHistory.tariff[i].dayM2 : (uint32_t)0);
        knx.getGroupObject(mGO.tariff[i].thisMonth).valueNoSend(mHistory.tariff[i].lastMonth != 0 ? mHistory.tariff[i].index - mHistory.tariff[i].lastMonth : (uint32_t)0);
        knx.getGroupObject(mGO.tariff[i].lastMonth).valueNoSend(mHistory.tariff[i].monthM2 != 0 ? mHistory.tariff[i].lastMonth - mHistory.tariff[i].monthM2 : (uint32_t)0);
        knx.getGroupObject(mGO.tariff[i].thisYear).valueNoSend(mHistory.tariff[i].lastYear != 0 ? mHistory.tariff[i].index - mHistory.tariff[i].lastYear : (uint32_t)0);
        knx.getGroupObject(mGO.tariff[i].lastYear).valueNoSend(mHistory.tariff[i].yearM2 != 0 ? mHistory.tariff[i].lastYear - mHistory.tariff[i].yearM2 : (uint32_t)0);
    }
}

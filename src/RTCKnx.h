
#ifndef RTCKNX_H
#define RTCKNX_H

#include <Arduino.h>
#include <knx.h>

class RTCKnx
{
    void setAndAjust();
    struct
    {
        uint32_t period;
    } mParams;
    struct
    {
        uint16_t date;
        uint16_t time;
        uint16_t dateTime;
        uint16_t dateTimeStatus;
    } m_GO;

    uint32_t mPersistentTimer = 0; // Should stay after reset
    uint32_t mTimerOffset = 0;
    struct
    {
        int64_t num = 1, denum = 1;
    } mCorr;

    uint32_t mShift = 0;
    uint32_t mLastSync = 0;
    uint32_t mDelay = 0;
    uint32_t mLastRequested = 0;

public:
    RTCKnx(){};
    void init(int baseAddr, uint16_t baseGO);
    enum DateChange
    {
        Init = -2,
        Same = -1,
        Day = 0,
        Month,
        Year
    };
    typedef struct
    {
        uint16_t tm_sec /*[0-59]*/, tm_min /*[0-59]*/, tm_hour /*[0-23]*/, tm_mday /*[1-31]*/, tm_mon /*[0-11]*/, tm_year /*Year*/;
    } DateTime;
    const DateTime &dateTime();
    void updateStatus();
    static int64_t secondsSinceReference(const DateTime &dt);
    void loop();
    void setNotifier(const std::function<void(DateChange)> &notifier);
    enum
    {
        NBGO = sizeof(m_GO) / sizeof(uint16_t),
        SIZEPARAMS = sizeof(mParams)
    };
    uint32_t millis();
    bool isValid() const;

private:
    std::function<void(DateChange)> mDayCallback;
    DateTime mDateTimeStamp = {0, 0, 0xffff, 0, 0, 0};
    DateTime mLastEmittedDay = {0};
    DateTime mLastDateTime = {0};
};

#endif
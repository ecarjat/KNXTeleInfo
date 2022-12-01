#include <Arduino.h>
#include <knx.h>
#include "RTCKnx.h"

void RTCKnx::setAndAjust()
{
    if (isValid())
    {
        uint32_t t = RTCKnx::millis();
        if (mLastSync != 0)
        {
            const int64_t num = 1000 * (secondsSinceReference(mDateTimeStamp) - secondsSinceReference(mLastDateTime)) * mCorr.denum / mCorr.num;
            const int64_t denum = ((num & ~(int64_t)UINT32_MAX)) + (t - mLastSync);
            if (num != 0 && denum != 0 && num * 10 >= denum * 9 && num * 10 <= denum * 11)
            {
                mCorr.num = num;
                mCorr.denum = denum;
            }
        }
        bool bInit = mShift == 0;
        mLastSync = mShift = t | 1;
        mLastDateTime = mDateTimeStamp;
        if (bInit && mDayCallback)
        {
            mDayCallback(Init);
        }
    }
}

void RTCKnx::init(int baseAddr, uint16_t baseGO)
{
    if (mPersistentTimer != 0)
        new (&mDayCallback) std::function<void(DateChange)>();
    mLastSync = mLastRequested = 0;
    mTimerOffset = mPersistentTimer;                     // Load last timer before reset
    mParams.period = knx.paramInt(baseAddr) * 60 * 1000; // In minutes
    knx.getGroupObject(m_GO.date = ++baseGO).dataPointType(DPT_Date);
    knx.getGroupObject(m_GO.date).callback([this](GroupObject &go)
                                           {
            const struct tm date = go.value();
            mDateTimeStamp.tm_year = date.tm_year; mDateTimeStamp.tm_mon = date.tm_mon - 1; mDateTimeStamp.tm_mday = date.tm_mday;
            setAndAjust(); });
    knx.getGroupObject(m_GO.time = ++baseGO).dataPointType(Dpt(10, 1, 1) /*DPT_TimeOfDay*/);
    knx.getGroupObject(m_GO.time).callback([this](GroupObject &go)
                                           {
            const struct tm time = go.value();
            mDateTimeStamp.tm_hour = time.tm_hour; mDateTimeStamp.tm_min = time.tm_min; mDateTimeStamp.tm_sec = time.tm_sec;
            setAndAjust(); });
    knx.getGroupObject(m_GO.dateTime = ++baseGO).dataPointType(DPT_DateTime);
    knx.getGroupObject(m_GO.dateTime).callback([this](GroupObject &go)
                                               {
            const struct tm time = go.value();
            mDateTimeStamp.tm_year = time.tm_year; mDateTimeStamp.tm_mon = time.tm_mon - 1; mDateTimeStamp.tm_mday = time.tm_mday; 
            mDateTimeStamp.tm_hour = time.tm_hour; mDateTimeStamp.tm_min = time.tm_min; mDateTimeStamp.tm_sec = time.tm_sec;
            setAndAjust(); });
    knx.getGroupObject(m_GO.dateTimeStatus = ++baseGO).dataPointType(DPT_DateTime);
    if (isValid())
        updateStatus();
}

const RTCKnx::DateTime &RTCKnx::dateTime()
{
    // Adjust clock deviation
    if (mShift != 0)
    {
        uint32_t current = RTCKnx::millis() | 1;
        if ((current - mShift) < 1000)
            return mDateTimeStamp;
        int32_t adjmSec = (current - mShift) * mCorr.num / mCorr.denum;
        int32_t restmSec = adjmSec % 1000;
        mDateTimeStamp.tm_sec += adjmSec / 1000;
        mShift = (current - restmSec) | 1;
        if (mDateTimeStamp.tm_sec >= 60)
        {
            mDateTimeStamp.tm_min += mDateTimeStamp.tm_sec / 60;
            mDateTimeStamp.tm_sec = mDateTimeStamp.tm_sec % 60;
        }
        if (mDateTimeStamp.tm_min >= 60)
        {
            mDateTimeStamp.tm_hour += mDateTimeStamp.tm_min / 60;
            mDateTimeStamp.tm_min = mDateTimeStamp.tm_min % 60;
        }
        if (mDateTimeStamp.tm_hour >= 24)
        {
            mDateTimeStamp.tm_mday += mDateTimeStamp.tm_hour / 24;
            mDateTimeStamp.tm_hour = mDateTimeStamp.tm_hour % 24;
        }
        bool bEnd = false;
        while (!bEnd)
        {
            switch (mDateTimeStamp.tm_mon)
            {
            case 0:
            case 2:
            case 4:
            case 6:
            case 7:
            case 9:
            case 11:
            {
                if (mDateTimeStamp.tm_mday > 31)
                {
                    ++mDateTimeStamp.tm_mon;
                    mDateTimeStamp.tm_mday -= 31;
                }
                else
                    bEnd = true;
            };
            break;
            case 1:
            {
                int februaryDays = ((mDateTimeStamp.tm_year & 3) == 0) && (((mDateTimeStamp.tm_year % 100) != 0) || ((mDateTimeStamp.tm_year % 400) == 0)) ? 29 : 28;
                if (mDateTimeStamp.tm_mday > februaryDays)
                {
                    ++mDateTimeStamp.tm_mon;
                    mDateTimeStamp.tm_mday -= februaryDays;
                }
                else
                    bEnd = true;
            };
            break;
            case 3:
            case 5:
            case 8:
            case 10:
            {
                if (mDateTimeStamp.tm_mday > 30)
                {
                    ++mDateTimeStamp.tm_mon;
                    mDateTimeStamp.tm_mday -= 30;
                }
                else
                    bEnd = true;
            };
            break;
            default:
                ++mDateTimeStamp.tm_year;
                mDateTimeStamp.tm_mon -= 12;
            }
        }
        updateStatus();
    }
    return mDateTimeStamp;
}
void RTCKnx::updateStatus()
{
    knx.getGroupObject(m_GO.dateTimeStatus).valueNoSend(tm{mDateTimeStamp.tm_sec, mDateTimeStamp.tm_min, mDateTimeStamp.tm_hour, mDateTimeStamp.tm_mday, mDateTimeStamp.tm_mon + 1, mDateTimeStamp.tm_year ? mDateTimeStamp.tm_year : 1900, 0, 0, 0});
}

int64_t RTCKnx::secondsSinceReference(const RTCKnx::DateTime &dt)
{
    enum CumulatedDays
    {
        Jan = 31,
        Feb = Jan + 28,
        Mar = Feb + 31,
        Apr = Mar + 30,
        May = Apr + 31,
        Jun = May + 30,
        Jul = Jun + 31,
        Aug = Jul + 31,
        Sep = Aug + 30,
        Oct = Sep + 31,
        Nov = Oct + 30,
        Dec = Nov + 31
    };
    static const uint16_t daysToMonth[] = {0, Jan, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov};
    uint16_t leapYears = dt.tm_year;
    if (dt.tm_mon < 2)
        --leapYears; // Check if the current year needs to be considered for the count of leap years or not
    leapYears = leapYears / 4 - leapYears / 100 + leapYears / 400;
    return (int64_t)dt.tm_sec + (int64_t)dt.tm_min * 60 + (int64_t)dt.tm_hour * 60 * 60 + ((int64_t)dt.tm_mday - 1 + daysToMonth[dt.tm_mon % 12] + leapYears + (int64_t)(dt.tm_year + dt.tm_mon / 12 - 2020) * 365) * 60 * 60 * 24;
}
void RTCKnx::loop()
{
    uint32_t currentMillis = RTCKnx::millis();
    if (currentMillis - mDelay < 100)
        return;
    mDelay = currentMillis;
    // Ask Date/Time from the bus when required
    if (mParams.period != 0 && (mLastRequested == 0 || ((currentMillis - mLastSync) > mParams.period && (currentMillis - mLastRequested) > mParams.period)))
    {
        knx.getGroupObject(m_GO.date).requestObjectRead();
        knx.getGroupObject(m_GO.time).requestObjectRead();
        knx.getGroupObject(m_GO.dateTime).requestObjectRead();
        mLastRequested = currentMillis;
    }
    if (mDateTimeStamp.tm_mday == 0 || !mDayCallback)
        return;
    const DateTime &currentDateTime = dateTime();
    if (mLastEmittedDay.tm_mday == 0)
    {
        mLastEmittedDay = currentDateTime;
        return;
    }
    DateChange change = Same;
    if (currentDateTime.tm_year == mLastEmittedDay.tm_year && currentDateTime.tm_mon == mLastEmittedDay.tm_mon && currentDateTime.tm_mday > mLastEmittedDay.tm_mday)
    {
        change = Day;
    }
    else if (currentDateTime.tm_year == mLastEmittedDay.tm_year && currentDateTime.tm_mon > mLastEmittedDay.tm_mon)
    {
        change = Month;
    }
    else if (currentDateTime.tm_year > mLastEmittedDay.tm_year)
    {
        change = Year;
    }
    if (change != Same)
    {
        mDayCallback(change);
        mLastEmittedDay = currentDateTime;
    }
}

void RTCKnx::setNotifier(const std::function<void(DateChange)> &notifier) { mDayCallback = notifier; }
uint32_t RTCKnx::millis() { return mPersistentTimer = mTimerOffset + ::millis(); }
bool RTCKnx::isValid() const { return mDateTimeStamp.tm_mday != 0 && mDateTimeStamp.tm_hour != 0xffff; } // Date + Time must be both set

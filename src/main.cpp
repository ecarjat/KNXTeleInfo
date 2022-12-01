/*
 * TeleInfo KNX
 *  Can be used with French TeleInfo systems
 *  Compatible with Linky in "Historic" mode and "Blue" electric meters
 *  GPL-3.0 License
 * Copyright 2020-2021 ZapDesign Innovative - Author: Eric Trinh
 */

#include <Arduino.h>
#include <knx.h>

#include <hardware/uart.h>

#include <pico/stdlib.h>
#include <hardware/pll.h>
#include <hardware/clocks.h>
#include <hardware/structs/pll.h>
#include <hardware/structs/clocks.h>

#include "RTCKnx.h"
#include "TeleInfo.h"

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_REVISION 0

#define PIN_PROG_SWITCH 5
#define PIN_PROG_LED 11
#define PROG_TIMEOUT (15 * 60 * 1000) // 15 mins
#define PIN_TPUART_RX 13              // stm32 knx uses Serial2 (pins 16,17)
#define PIN_TPUART_TX 12

#define TELEINFO_UART_SPEED 1200        //
#define TELEINFO_UART_CONFIG SERIAL_7E1 // SERIAL_7E1
#define TELEINFO_UART_RX 25
#define TELEINFO_UART_TX 24

#define HISTORY_RESET_PROG_SWITCH_DELAY 4000  // 4s
#define HISTORY_RESET_LED_BLINKING_PERIOD 512 // 0.512s
#define RECEPTION_LED_BLINKING_PERIOD 512     // 0.512s

// Restore ram after reset (brownout)
#define INIT_MASK 0x12345678
volatile uint32_t Inited __attribute__((section(".noinit")));

uint8_t rtcHolder[sizeof(RTCKnx)] __attribute__((section(".noinit")));
RTCKnx &rtc = *(RTCKnx *)rtcHolder;

uint8_t teleinfoHolder[sizeof(TeleInfo)] __attribute__((section(".noinit")));
TeleInfo &teleinfo = *(TeleInfo *)teleinfoHolder;

extern "C" void SystemClock_Config(void)
{
    // Nothing for default 4MHz MSI Clock
    // Required to downclock the 80MHz clock to be below 4096 * 16 * 1200 (TeleInfo BaudRate) due to limitation in UART_BRR 12bit register
}

static SerialUART serialTpuart(uart0, PIN_TPUART_TX, PIN_TPUART_RX);
static SerialUART serialTeleInfo(uart1, TELEINFO_UART_TX, TELEINFO_UART_RX);

void setup()
{

    // Serial.begin(115200);
    // ArduinoPlatform::SerialDebug = &Serial;
    // while(!Serial){
    //      delay(10);
    //  }
    //  delay(300);
    //  Serial.println("Loading ....");

    if (Inited != INIT_MASK)
    {
        new (&rtc) RTCKnx();
        new (&teleinfo) TeleInfo(&rtc, &serialTeleInfo, TELEINFO_UART_SPEED, TELEINFO_UART_CONFIG);
        Inited = INIT_MASK;
    }

   


    knx.platform().knxUart(&serialTpuart);
    knx.ledPin(PIN_PROG_LED);
    knx.ledPinActiveOn(HIGH);
    knx.buttonPin(PIN_PROG_SWITCH);


    // Init device
    knx.version((VERSION_MAJOR << 6) | (VERSION_MINOR & 0x3F)); // PID_VERSION
    knx.orderNumber((const uint8_t *)"ZDI-TINFO1");             // PID_ORDER_INFO
    knx.manufacturerId(0xfa);                                   // PID_SERIAL_NUMBER (2 first bytes) - 0xfa for KNX Association
    knx.hardwareType((const uint8_t *)"M-07B0");                // PID_HARDWARE_TYPE

    // read adress table, association table, groupobject table and parameters from eeprom
    knx.readMemory();
    
    if (knx.configured())
    {
        rtc.init(0, 0);
        teleinfo.init(RTCKnx::SIZEPARAMS, RTCKnx::NBGO);
        rtc.setNotifier(std::bind(&TeleInfo::newDate, &teleinfo, std::placeholders::_1));
        // attachInterrupt(PIN_TPUART_SAVE, std::bind(&TeleInfo::saveHistory, &teleinfo), LOW);    // 2ms to save history before shutdown - likely not enough
    }

    // start the framework.
    knx.start();

}

int led = false;
bool progButtonState;
void loop()
{
    
    // don't delay here too much. Otherwise you might loose packages or mess up the timing with ETS
    knx.loop();
    // only run the application code if the device was configured with ETS
    if (knx.configured())
    {
        teleinfo.loop();
        rtc.loop();
    }
    

    uint32_t currentMillis = rtc.millis();
    PinStatus prog_pin = digitalRead(PIN_PROG_SWITCH);
    if(prog_pin == HIGH){
        progButtonState = false;
    }
    else {
        progButtonState = true;
    }

    /*
        // Handle Reset History by long prog button press
        static uint32_t progButtonPressedTimer = 0;
        static bool historyReset = false;

        if (!progButtonState && currentMillis - progButtonPressedTimer > 200) {
            progButtonPressedTimer = 0;
            if (historyReset) {
                digitalWrite(PIN_PROG_LED, LOW);
            }
            historyReset = false;
        }
        else {
            if (progButtonPressedTimer == 0) {
                progButtonPressedTimer = currentMillis;
            }
            else {
                uint32_t delay = currentMillis - progButtonPressedTimer;
                if (delay > HISTORY_RESET_PROG_SWITCH_DELAY) {
                    if (!historyReset) {
                        knx.progMode(false);
                        teleinfo.resetHistory();
                        historyReset = true;
                    }
                    if (historyReset) {
                        digitalWrite(PIN_PROG_LED, (delay/HISTORY_RESET_LED_BLINKING_PERIOD)&1);
                    }
                }
            }
        }

        // Handle Prog Mode timeout
        static uint32_t timerProgMode = 0;
        if (knx.progMode()) {
            if (timerProgMode == 0) {
                timerProgMode = currentMillis;
            }
            else {
                if (currentMillis - timerProgMode > PROG_TIMEOUT) {
                    knx.progMode(false);
                    timerProgMode = 0;
                }
            }
        }
        else {
            timerProgMode = 0;
        }
    */

    // Handle reception blinking led (2s cycle with 0.5s On while receiving Teleinfo data)
    
    if (!knx.progMode() && !progButtonState)
    {
        //Serial.println(currentMillis - teleinfo.lastReception());
        if (currentMillis - teleinfo.lastReception() < RECEPTION_LED_BLINKING_PERIOD * 2)
        {
            digitalWrite(PIN_PROG_LED, ((currentMillis / RECEPTION_LED_BLINKING_PERIOD) & 3) == 0);
        }
        else
        {
            digitalWrite(PIN_PROG_LED, LOW);
        }
    }
    
}

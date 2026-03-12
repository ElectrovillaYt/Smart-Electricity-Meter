#include <Arduino.h>
#include <ESP8266WiFi.h>

// =======================
// PICO GPIO 12- ESP RX
// PICO GPIO 13- ESP TX
// =======================

#define PICO_FEEDBACK 4 // (GPIO-4) - D2
#define BLYNK_FLAG 5    // (GPIO-5) - D1

// Blynk Credentials
#define BLYNK_TEMPLATE_ID "BLYNK_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "BLYNK_TEMPLATE_NAME"
#define BLYNK_AUTH_TOKEN "BLYNK_AUTH_TOKEN"

#include <BlynkSimpleEsp8266.h>

char ssid[] = "WiFi_SSID"; // Replace with you WiFi SSID/Name (2.4Ghz band only!)
char pass[] = "WiFi_PASSWORD";        // Replace with you WiFi Password

static float prev_unit = 0;
static float prev_bill = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long long syncHault = 0;

char rxBuff[32];
unsigned int idx = 0;

void FlashStatus(unsigned long durationMS = 0UL, bool status = 0)
{
    static bool LED_ON = false; // ACTIVE-LOW
    static unsigned long long Flash_timer = 0;
    if (durationMS == 0)
    {
        digitalWrite(LED_BUILTIN, !status); // ACTIVE-LOW
    }
    else
    {
        if (millis() - Flash_timer > durationMS)
        {
            Flash_timer = millis();
            LED_ON = !LED_ON;
            digitalWrite(LED_BUILTIN, LED_ON);
        }
    }
}

void setup()
{
    pinMode(BLYNK_FLAG, OUTPUT);
    pinMode(PICO_FEEDBACK, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);
    FlashStatus(0, 0);
    delay(100);
    Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
}

BLYNK_CONNECTED()
{
    Blynk.syncVirtual(V2, V1, V0);
    digitalWrite(BLYNK_FLAG, 1);
    FlashStatus(0, 1);
}

BLYNK_WRITE(V0) // Switch 1 - V0
{
    int state = param.asInt();
    Serial.write(state ? "1\n" : "2\n", 2);
}

BLYNK_WRITE(V1) // Switch 2 - V1
{
    int state = param.asInt();
    Serial.write(state ? "3\n" : "4\n", 2);
}

BLYNK_WRITE(V2) // Total Units (Kw/h) -  V2
{
    const char *state = param.asString();
    Serial.write(state);
    Serial.write('\n');
}

void loop()
{
    static bool Retry = false;
    static bool picoPrev = false;

    bool picoNow = digitalRead(PICO_FEEDBACK);
    if (picoNow && !picoPrev)
    {
        syncHault = millis();
    }
    picoPrev = picoNow;

    Blynk.run();

    if (Blynk.connected())
    {
        if (digitalRead(PICO_FEEDBACK))
        {
            if (Retry && picoNow)
            {
                if (millis() - syncHault > 5000)
                {
                    syncHault = millis();
                    FlashStatus(0, 1);
                    Blynk.syncVirtual(V2, V1, V0);
                    Retry = false;
                }
            }
            else
            {
                // ---------- ALWAYS read UART ----------
                while (Serial.available())
                {
                    char c = Serial.read();

                    if (c == '\n')
                    {
                        rxBuff[idx] = '\0';
                        idx = 0;

                        float units = atof(rxBuff);

                        // Bill Callculation
                        float bill =
                            units >= 301 ? units * 12.83 : units <= 100 ? units * 4.43
                                                                        : units * 9.64;

                        char totalBill[32];
                        snprintf(totalBill, sizeof(totalBill), "%.2f", bill);
                        if (atof(totalBill) != prev_bill)
                        {
                            prev_bill = atof(totalBill);
                            Blynk.virtualWrite(V3, totalBill);
                        }
                        if (units != prev_unit)
                        {
                            prev_unit = units;
                            Blynk.virtualWrite(V2, units);
                        }
                    }
                    else if (idx < sizeof(rxBuff) - 1)
                    {
                        rxBuff[idx++] = c;
                    }
                }
            }
        }
        else
        {
            Retry = true;
            FlashStatus(500);
        }
    }

    // ---------- reconnect logic ----------
    if (!Blynk.connected())
    {
        unsigned long now = millis();
        Retry = true;
        if (now - lastReconnectAttempt > 5000)
        {
            lastReconnectAttempt = now;

            FlashStatus(50);
            idx = 0; // buffer index clear
            digitalWrite(BLYNK_FLAG, 0);
            Blynk.connect();
        }
    }
}

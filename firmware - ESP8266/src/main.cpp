#include <Arduino.h>
#include <ESP8266WiFi.h>

// =======================
// PICO GPIO 12- ESP RX
// PICO GPIO 13- ESP TX
// =======================

#define UART_FLAG 5 // (GPIO5) - D1

// Blynk Credentials
#define BLYNK_TEMPLATE_ID "BLYNK_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "BLYNK_TEMPLATE_NAME"
#define BLYNK_AUTH_TOKEN "BLYNK_AUTH_TOKEN"

#include <BlynkSimpleEsp8266.h>

char ssid[] = "WiFi SSID";     // Replace with you WiFi SSID/Name (2.4Ghz band only!)
char pass[] = "WiFi Password"; // Replace with you WiFi Password

char rxBuff[32];
unsigned int idx = 0;

unsigned long lastReconnectAttempt = 0;

void setup()
{
    pinMode(UART_FLAG, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);
    digitalWrite(LED_BUILTIN, 1);
    delay(100);
    Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
}

BLYNK_CONNECTED()
{
    Blynk.syncVirtual(V0, V1, V2);

    digitalWrite(UART_FLAG, 1);
    digitalWrite(LED_BUILTIN, 0);
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
    Blynk.run();

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

            Blynk.virtualWrite(V2, units);
            Blynk.virtualWrite(V3, totalBill);
        }
        else if (idx < sizeof(rxBuff) - 1)
        {
            rxBuff[idx++] = c;
        }
    }

    // ---------- reconnect logic ----------
    if (!Blynk.connected())
    {
        unsigned long now = millis();

        if (now - lastReconnectAttempt > 5000)
        {
            lastReconnectAttempt = now;

            digitalWrite(UART_FLAG, 0);
            idx = 0; // buffer index clear
            digitalWrite(LED_BUILTIN, 1);
            Blynk.connect();
        }
    }
}

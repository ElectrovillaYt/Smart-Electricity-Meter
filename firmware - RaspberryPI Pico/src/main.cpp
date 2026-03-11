#include <Arduino.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

hd44780_I2Cexp lcd;

char Mobile_Num[] = "+91XXXXXXXXXX"; // Receiving Mobile no.

#define METER_CONSTANT 3200 // Your meter constant value (igiven in imp/Kwh)

// LCD pins (Connections)
// ========================//
// LCD SDA - GPIO 4
// LCD SCL - GPIO 5
// ========================//

// ESP8266 UART PIN
#define esp Serial1
#define TXPin_1 12
#define RXPin_1 13

// SIM800L UART PIN
#define sim800 Serial2
#define TXPin_2 8
#define RXPin_2 9

// CAL_LED (Meter IMPULSE PINS)
#define CAL_IN 14
#define CAL_STATE_LED LED_BUILTIN // OnBoard LED

// Output Relays
#define Out1 6 // Relay 1
#define Out2 7 // Relay 2

// Flag pin to check if INternet is Up!
#define ESP_BOOT_FLAG 10

//  Display Timeout helpers
#define DISPLAY_UPDATE_MS 1000 // Display Value update timing (default 1s)
#define DSIPLAY_SHFT_MS 10000  // Timing to Shift between shift Unit and Bill (default 10s)

#define Msg_Timeout 60000 // 60s (1 min) waiting between each sms

// status LED
static unsigned long ledTimer = 0;
static bool ledOn = false;

// Boot flags
static unsigned int Boot_Ittr_Count = 0;
static bool ready = false;
static bool initPromptShowed = false;
// Display
static unsigned long lastDisplayUpdate = 0;
static unsigned long lastDisplayStateUpdate = 0;
int displayStateOption = 0;

// Buffers and Helpers
char rxBuff[32];
char buff[32];
char totalBill[32];
unsigned int idx = 0;
unsigned long lastSend = 0;

struct Sim
{
private:
  unsigned long lastSMS = 0;
  enum GSMState
  {
    GSM_WAIT_BOOT,
    GSM_INIT,
    GSM_WAIT_INIT,
    GSM_SET_TEXT,
    GSM_WAIT_TEXT,
    GSM_READY,
    GSM_SEND_CMD,
    GSM_WAIT_PROMPT,
    GSM_SEND_MSG,
    GSM_WAIT_SEND,
    GSM_CHECK_REG,
    GSM_WAIT_REG
  };
  GSMState state = GSM_WAIT_BOOT;
  unsigned long smsTimeout = 0;
  uint16_t bufIndex = 0;

  void sendCommand(const char *cmd)
  {
    sim800.println(cmd);
  }

  bool checkResponse(const char *keyword)
  {
    if (strstr(buffer, keyword) != NULL)
    {
      bufIndex = 0;
      buffer[0] = '\0';
      return true;
    }
    return false;
  }

public:
  char buffer[256];
  unsigned long stateTimer = 0;
  void readSerial()
  {
    while (sim800.available())
    {
      char c = sim800.read();
      if (bufIndex < sizeof(buffer) - 1)
      {
        buffer[bufIndex++] = c;
        buffer[bufIndex] = '\0';
      }
    }
  }

  void sendSMS(const char *PHONE_NUMBER, const char *MESSAGE)
  {
    switch (state)
    {
    case GSM_WAIT_BOOT:
      if (millis() - stateTimer > 10000)
        state = GSM_INIT;
      break;

    case GSM_INIT:
      sendCommand("AT");
      state = GSM_WAIT_INIT;
      stateTimer = millis();
      break;

    case GSM_WAIT_INIT:
      if (checkResponse("OK"))
        state = GSM_SET_TEXT;
      else if (millis() - stateTimer > 2000)
        state = GSM_INIT;
      break;

    case GSM_CHECK_REG:
      sendCommand("AT+CREG?");
      state = GSM_WAIT_REG;
      stateTimer = millis();
      break;

    case GSM_WAIT_REG:
      if (strstr(buffer, "0,1") || strstr(buffer, "0,5"))
      {
        bufIndex = 0;
        buffer[0] = '\0';
        state = GSM_READY;
      }
      else if (millis() - stateTimer > 2000)
      {
        state = GSM_CHECK_REG;
      }
      break;

    case GSM_SET_TEXT:
      sendCommand("AT+CMGF=1");
      state = GSM_WAIT_TEXT;
      stateTimer = millis();
      break;

    case GSM_WAIT_TEXT:
      if (checkResponse("OK"))
        state = GSM_CHECK_REG;
      else if (millis() - stateTimer > 2000)
        state = GSM_SET_TEXT;
      break;

    case GSM_READY:
      if (ready && millis() - lastSMS >= Msg_Timeout)
      {
        lastSMS = millis();
        state = GSM_SEND_CMD;
      }
      break;

    case GSM_SEND_CMD:
      bufIndex = 0;
      buffer[0] = '\0';
      sim800.print("AT+CMGS=\"");
      sim800.print(PHONE_NUMBER);
      sim800.println("\"");
      state = GSM_WAIT_PROMPT;
      stateTimer = millis();
      bufIndex = 0;
      break;

    case GSM_WAIT_PROMPT:
      if (checkResponse(">"))
        state = GSM_SEND_MSG;
      else if (millis() - stateTimer > 5000)
        state = GSM_READY;
      break;

    case GSM_SEND_MSG:
      sim800.print(MESSAGE);
      sim800.write(26);
      bufIndex = 0;
      buffer[0] = '\0';
      state = GSM_WAIT_SEND;
      stateTimer = millis();
      break;

    case GSM_WAIT_SEND:
      if (checkResponse("+CMGS"))
        state = GSM_READY;
      else if (checkResponse("ERROR") || checkResponse("+CMS ERROR"))
        state = GSM_READY;
      else if (millis() - stateTimer > 15000)
        state = GSM_READY;

      break;
    }
  }
};

struct Meter
{
private:
  uint64_t readPulseSafe()
  {
    noInterrupts();
    uint64_t copy = pulseCount;
    interrupts();
    return copy;
  }

  uint64_t GetEnergy_01mWh()
  {
    uint64_t pulses = readPulseSafe();
    return (pulses * 10000000ULL) / METER_CONSTANT;
  }

public:
  volatile uint64_t pulseCount = 0;
  volatile bool pulseFlag = false;

  void GetkWhString(char *buf)
  {
    uint64_t e = GetEnergy_01mWh();

    uint64_t kwh_int = e / 10000000ULL;
    uint64_t kwh_dec = (e % 10000000ULL) / 100000ULL;

    snprintf(buf, 20, "%llu.%02llu",
             (unsigned long long)kwh_int,
             (unsigned long long)kwh_dec);
  }

  uint64_t kWhString_ToPulses(const char *s)
  {
    uint64_t kwh_int = 0;
    uint64_t kwh_dec = 0;

    // parser
    sscanf(s, "%llu.%llu",
           (unsigned long long *)&kwh_int,
           (unsigned long long *)&kwh_dec);

    // convert to 0.01 mWh units (Milli-watt/hour)
    uint64_t energy01mWh =
        kwh_int * 10000000ULL + kwh_dec * 100000ULL; //  2 decimal places
    // coverting to imp count
    return (energy01mWh * METER_CONSTANT) / 10000000ULL;
  }
};

Meter meter;

void onPulse()
{
  static uint32_t last = 0;
  uint32_t now = micros();
  if (now - last > 5000)
  {
    meter.pulseCount++;
    meter.pulseFlag = true;
    last = now;
  }
}

bool isNumber(const char *s)
{
  if (!isdigit(*s) && *s != '.')
    return false;

  while (*s)
  {
    if (!isdigit(*s) && *s != '.')
      return false;
    s++;
  }
  return true;
}

void DsiplayOut(char *Valbuff, char *Valbuff2, unsigned int option = 0)
{
  if (Boot_Ittr_Count > 0)
  {
    lcd.setCursor(0, 0);
    lcd.print(option == 0 ? "Energy Used:    " : "Total Bill:     ");

    lcd.setCursor(0, 1);
    lcd.print(option == 0 ? Valbuff : Valbuff2);
    lcd.print(option == 0 ? " kWh   " : " Rs    "); // overwrite old chars
  }
  else
  {
    lcd.setCursor(0, 0);
    lcd.print("Syncing Values..");
  }
}

Sim sim;

void setup()
{
  // ESP8266 UART
  esp.setRX(RXPin_1);
  esp.setTX(TXPin_1);
  esp.begin(115200);

  // INPUT PINS
  pinMode(CAL_IN, INPUT_PULLUP);
  // ESP_UART_Flag Input pin
  pinMode(ESP_BOOT_FLAG, INPUT);

  // OUTPUTS PINS
  pinMode(CAL_STATE_LED, OUTPUT);
  // Output relays
  pinMode(Out1, OUTPUT);
  pinMode(Out2, OUTPUT);

  // Set output pins High for default LOW state of ACTIVE-LOW Relay Modules
  digitalWrite(Out1, 1);
  digitalWrite(Out2, 1);
  digitalWrite(CAL_STATE_LED, 0);

  // I2C Display init
  Wire.begin(); // I2C GPIO - 4, 5

  int status = lcd.begin(16, 2);
  if (status)
  {
    digitalWrite(CAL_STATE_LED, 1); // Boot sig
    while (1)
      ;
  }
  //  SIM800L UART
  sim800.setRX(RXPin_2);
  sim800.setTX(TXPin_2);
  sim800.begin(9600);
  sim.buffer[0] = '\0';
  sim.stateTimer = millis();

  digitalWrite(CAL_STATE_LED, 0); // toggle back led to LOW for normal use
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("BOOTING SYSTEM..");
}

void loop()
{
  sim.readSerial();
  if (ready)
  {
    if (!initPromptShowed)
    {
      while (esp.available())
        esp.read(); // clear garbage from buff
      lcd.setCursor(0, 0);
      lcd.print("BOOT SUCCESS !!!");
      // Interrupt setup
      attachInterrupt(digitalPinToInterrupt(CAL_IN), onPulse, FALLING);
      digitalWrite(CAL_STATE_LED, 0); // toggle back led to LOW for normal use
      delay(50);
      lcd.clear();
      initPromptShowed = true;
    }

    if (digitalRead(ESP_BOOT_FLAG) == 1)
    { // Callback loop check for ESP866 connection

      if (Boot_Ittr_Count == 0)
      {
        while (esp.available())
          esp.read(); // clear garbage from buff
        delay(20);
      }
      // ---- Meter pulse LED Trigger ----
      if (meter.pulseFlag)
      {
        meter.pulseFlag = false;
        digitalWrite(CAL_STATE_LED, 1);
        ledOn = true;
        ledTimer = millis();
      }
      if (ledOn && millis() - ledTimer > 50)
      {
        digitalWrite(CAL_STATE_LED, 0);
        ledOn = false;
      }

      // SIM800L Send SMS
      char sms[256];
      snprintf(sms, sizeof(sms),
               "Meter Reading: %s kWh, Bill: Rs %s. Pay soon.",
               strlen(buff) > 0 ? buff : "0.0", strlen(totalBill) > 0 ? totalBill : "0.0");
      sim.sendSMS(Mobile_Num, sms);

      // ---- Display ----
      if (millis() - lastDisplayStateUpdate > DSIPLAY_SHFT_MS)
      {
        lastDisplayStateUpdate = millis();
        displayStateOption = !displayStateOption;
      }

      if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_MS)
      {
        lastDisplayUpdate = millis();
        DsiplayOut(buff, totalBill, displayStateOption);
      }

      // ---- Send units to ESP every 1 second ----
      if (millis() - lastSend > 1000)
      {
        lastSend = millis();
        meter.GetkWhString(buff);
        float units = atof(buff);
        // Bill Callculation
        float bill =
            units >= 301 ? units * 12.83 : units <= 100 ? units * 4.43
                                                        : units * 9.64;

        snprintf(totalBill, sizeof(totalBill), "%.2f", bill);
        if (Boot_Ittr_Count != 0)
        {
          esp.write(buff, strlen(buff));
          esp.write('\n');
        }
      }

      // ---- UART receive ----
      while (esp.available())
      {
        char c = esp.read();
        if (c == '\r')
          continue;
        if (c == '\n')
        {
          if (idx == 0)
            continue;
          rxBuff[idx] = '\0'; // complete message
          // ---- Process message ----
          if (strlen(rxBuff) == 1 && isdigit(rxBuff[0]))
          {
            switch (rxBuff[0])
            {
            case '1':
              digitalWrite(Out1, 0);
              break;
            case '2':
              digitalWrite(Out1, 1);
              break;
            case '3':
              digitalWrite(Out2, 0);
              break;
            case '4':
              digitalWrite(Out2, 1);
              break;
            }
          }
          else if (strlen(rxBuff) > 1 && isNumber(rxBuff))
          {
            // assume unit sync string
            strncpy(buff, rxBuff, sizeof(buff) - 1);
            buff[sizeof(buff) - 1] = '\0';
            meter.pulseCount = meter.kWhString_ToPulses(rxBuff);
            Boot_Ittr_Count = 1;
          }
          idx = 0;
          continue;
        }
        else if (idx < sizeof(rxBuff) - 1)
        {
          rxBuff[idx++] = c;
        }
        else
        {
          idx = 0; // discard overflowed frame
        }
      }
    }
    else
    { // keep All Switches OFF until ESP UART Starts
      digitalWrite(Out1, 1);
      digitalWrite(Out2, 1);
    }
  }
  else
  {
    digitalWrite(Out1, 1);
    digitalWrite(Out2, 1);
    digitalWrite(CAL_STATE_LED, 1);
    Boot_Ittr_Count = 0;
    idx = 0;
    ready = false;
    if (digitalRead(ESP_BOOT_FLAG))
    {
      ready = true;
    }
  }
}
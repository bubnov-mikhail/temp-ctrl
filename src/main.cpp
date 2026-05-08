#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <EEPROM.h>
#include <Nokia_LCD.h>

// Pins

// Display 5110
#define DEFAULT_CONTRAST 55
#define PIN_NOKIA_DC 4   // Data/Command select (D/C)
#define PIN_NOKIA_BL 9   // Backlight
#define PIN_NOKIA_CE 8   // Chip Select // 8
#define PIN_NOKIA_CLK 13 // Clk
#define PIN_NOKIA_DIN 11 // DIn
#define PIN_NOKIA_RST A1 // Reset

// Sensors
#define DHTTYPE DHT11
#define DHTPIN1 6
#define DHTPIN2 7

// Fan ctrl
#define FAN_PIN 10

// Sound alarm
#define BUZZER_PIN 5

// Encoder
// #define EB_NO_FOR           // отключить поддержку pressFor/holdFor/stepFor и счётчик степов (экономит 2 байта оперативки)
// #define EB_NO_CALLBACK      // отключить обработчик событий attach (экономит 2 байта оперативки)
// #define EB_NO_COUNTER       // отключить счётчик энкодера (экономит 4 байта оперативки)

// #define EB_DEB_TIME 20      // таймаут гашения дребезга кнопки (кнопка)
// #define EB_CLICK_TIME 50   // таймаут ожидания кликов (кнопка)
// #define EB_HOLD_TIME 1000    // таймаут удержания (кнопка)
// #define EB_STEP_TIME 200    // таймаут импульсного удержания (кнопка)
// #define EB_FAST_TIME 30     // таймаут быстрого поворота (энкодер)
// #define EB_TOUT_TIME 1000   // таймаут действия (кнопка и энкодер)

#define ENC_CLK 2
#define ENC_DT 3 // 3
#define ENC_SW A0

#include <EncButton.h>
EncButton eb(ENC_DT, ENC_CLK, ENC_SW, INPUT, INPUT_PULLUP); // + режим пинов кнопки

void checkEncoder();
void drawUI(float avg, int speed);
void eisr();

Nokia_LCD lcd(PIN_NOKIA_CLK, PIN_NOKIA_DIN, PIN_NOKIA_DC, PIN_NOKIA_CE, PIN_NOKIA_RST, PIN_NOKIA_BL);
DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);

// States
bool isAutoMode = true;
bool isMenuMode = false;
bool isHoldButton = false;
int menuState = 0;
int manualSpeed = 50;
unsigned long btnTimer = 0;
unsigned long encoderTimer = 0;

float tempMin, tempMax;
float t1, t2, h1, h2, t1_old, t2_old;
unsigned long lastTrendCheck = 0;
const unsigned long lastTrendTimout = 30000;
unsigned long lastDHTRead = 0;
const unsigned long lastDHTReadTimeout = 2000;
int t1_trend = 0, t2_trend = 0;

// GFX
const unsigned char trendNone[] = {0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char trendUp[] = {0x08, 0x04, 0x7e, 0x04, 0x08};
const unsigned char trendDown[] = {0x10, 0x20, 0x7e, 0x20, 0x10};
const unsigned char celciusSign[] = {0x07, 0x05, 0x07};

const unsigned char progressBorder[] = {0xff};
const unsigned char progressEmpty[] = {0x81};
const unsigned char progressFill[] = {0xbd};

int contrast = DEFAULT_CONTRAST;
unsigned long displayTimer = 0;
const unsigned long displayUpdateTimout = 300;
const unsigned long backlightTimeout = 10000;

void setup()
{
  dht1.begin();
  dht2.begin();

  pinMode(FAN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  eb.setBtnLevel(LOW);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), eisr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT), eisr, CHANGE);
  eb.setEncISR(true);

  pinMode(ENC_CLK, INPUT);
  pinMode(ENC_DT, INPUT);
  pinMode(ENC_SW, INPUT_PULLUP);

  digitalWrite(BUZZER_PIN, false);

  EEPROM.get(0, tempMin);
  EEPROM.get(4, tempMax);
  EEPROM.get(8, contrast);

  if (isnan(tempMin) || tempMin < 10)
  {
    tempMin = 30.0;
  }
  if (isnan(tempMax) || tempMax > 90)
  {
    tempMax = 45.0;
  }
  if (isnan(contrast) || contrast < 20 || contrast > 80)
  {
    contrast = DEFAULT_CONTRAST;
  }

  lcd.setBacklight(true);
  lcd.begin();
  lcd.clear();
  lcd.setContrast(contrast);
}

void eisr() {
    eb.tickISR();
}

void checkEncoder()
{
  eb.tick();
  if (eb.holding())
  {
    if (!isHoldButton)
    {
      isHoldButton = true;
      isMenuMode = !isMenuMode;
      lcd.clear();
    }
  }

  if (eb.release())
  {
    btnTimer = millis();
    if (isHoldButton)
    {
      if (!isMenuMode)
      {
        EEPROM.put(0, tempMin);
        EEPROM.put(4, tempMax);
        lcd.clear();
      }
    }
    else
    {
      if (isMenuMode)
      {
        menuState = !menuState;
      }
      else
      {
        isAutoMode = !isAutoMode;
      }
    }

    isHoldButton = false;
  }

  if (eb.turn())
  {
    encoderTimer = millis();
    float dir = eb.dir();

    if (isMenuMode)
    {
      if (menuState == 0)
      {
        tempMin = constrain(tempMin + dir, 10.0, tempMax - 2.0);
      }
      else
      {
        tempMax = constrain(tempMax + dir, tempMin + 2.0, 80.0);
      }
    }
    else if (!isAutoMode)
    {
      manualSpeed = manualSpeed + int(dir) * 5;
      if (manualSpeed < 0)
      {
        manualSpeed = 0;
      }
      else if (manualSpeed > 255)
      {
        manualSpeed = 255;
      }
    }
  }
}

void loop()
{
  if (millis() - btnTimer > backlightTimeout && millis() - encoderTimer > backlightTimeout)
  {
    lcd.setBacklight(false);
  }
  else
  {
    lcd.setBacklight(true);
  }

  checkEncoder();

  // Check sensors once per 2 seconds
  if (millis() - lastDHTRead > lastDHTReadTimeout)
  {
    t1 = dht1.readTemperature();
    if (t1 == NAN)
    {
      t1 = 0;
    }
    h1 = dht1.readHumidity();
    t2 = dht2.readTemperature();
    if (t2 == NAN)
    {
      t2 = 0;
    }
    h2 = dht2.readHumidity();
    lastDHTRead = millis();

    if (millis() - lastTrendCheck > lastTrendTimout)
    {
      t1_trend = (t1 > t1_old + 0.9) ? 1 : (t1 < t1_old - 0.9 ? -1 : 0);
      t2_trend = (t2 > t2_old + 0.9) ? 1 : (t2 < t2_old - 0.9 ? -1 : 0);
      t1_old = t1;
      t2_old = t2;
      lastTrendCheck = millis();
    }
  }

  float avgTemp = (t1 + t2) / 2.0;
  int targetSpeed = isAutoMode
                        ? (avgTemp <= tempMin ? 0 : (avgTemp >= tempMax ? 255 : map(int(avgTemp), int(tempMin), int(tempMax), 0, 255)))
                        : manualSpeed;

  if (avgTemp > tempMax + 5 && targetSpeed == 255)
  {
    tone(BUZZER_PIN, 1500, 50);
  }

  if (targetSpeed > 0) {
    analogWrite(FAN_PIN, map(targetSpeed, 0, 255, 50, 255));
  } else {
    digitalWrite(FAN_PIN, false);
  }
  drawUI(avgTemp, targetSpeed);
}

void drawUI(float avg, int speed)
{
  
  lcd.setCursor(0, 0);
  
  if (isMenuMode)
  {
    lcd.setInverted(true);
    lcd.print("---- SETUP ---");
    
    lcd.setInverted(false);
    lcd.setCursor(0, 1);
    
    lcd.print(menuState == 0 ? ">MinT: " : " MinT: ");
    
    lcd.print(int(tempMin));
    
    lcd.draw(celciusSign, 3, false);
    
    lcd.setCursor(0, 2);
    lcd.print(menuState == 1 ? ">MaxT: " : " MaxT: ");
    
    lcd.print(int(tempMax));
    
    lcd.draw(celciusSign, 3, false);
    
    lcd.setCursor(0, 3);
    lcd.print("              ");
    
    lcd.setCursor(0, 5);
    
    lcd.print("Hold to Save  ");

    return;
  }

  lcd.setInverted(true);
  lcd.print(isAutoMode ? "---- AUTO ----" : "--- MANUAL ---");
  
  lcd.setInverted(false);

  lcd.setCursor(0, 1);
  
  lcd.print("T1:");
  
  lcd.print(int(t1));
  
  lcd.draw(celciusSign, 3, false);
  
  if (t1_trend == 0)
  {
    lcd.draw(trendNone, 5, false);
  }
  else
  {
    lcd.draw(t1_trend > 0 ? trendUp : trendDown, 5, false);
  }
  
  lcd.print(" H1:");
  
  lcd.print(int(h1));
  
  lcd.print("%");
  

  lcd.setCursor(0, 2);
  lcd.print("T2:");
  
  lcd.print(int(t2));
  
  lcd.draw(celciusSign, 3, false);
  
  if (t2_trend == 0)
  {
    lcd.draw(trendNone, 5, false);
  }
  else
  {
    lcd.draw(t2_trend > 0 ? trendUp : trendDown, 5, false);
  }
  
  lcd.print(" H2:");
  
  lcd.print(int(h2));
  
  lcd.print("%");
  

  lcd.setCursor(0, 3);
  lcd.print("Avg T: ");
  
  lcd.print(int(avg));
  
  lcd.draw(celciusSign, 3, false);
  

  lcd.setCursor(0, 5);
  
  lcd.print("FAN: "); // 25 pxls
  
  lcd.draw(progressBorder, 1, false);
  
  // 57 px ls left
  int speedInPxls = map(speed, 0, 255, 0, 52);
  for (int i = 0; i < 52; i++)
  {
    if (speedInPxls <= i)
    {
      lcd.draw(progressEmpty, 1, false);
    }
    else
    {
      lcd.draw(progressFill, 1, false);
    }
    
  }

  lcd.draw(progressBorder, 1, false); // 84
}

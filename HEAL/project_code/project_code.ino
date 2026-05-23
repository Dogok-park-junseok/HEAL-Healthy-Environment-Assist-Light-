#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>

// -------------------- 핀 설정 --------------------
const int LDR_PIN = A1;             // 조도센서, A1핀
const int POWER_BTN_PIN = 4;        // 전원 버튼, D4핀

const int LAMP_PIN = 6;             // 메인 네오픽셀, D6핀
const int MAIN_LED_COUNT = 8;

const int STATUS_LED_PIN = 9;       // 상태 LED, D9핀
const int STATUS_LED_COUNT = 1;

const int BT_RX_PIN = 2;            // HC-06 TX -> 2
const int BT_TX_PIN = 3;            // HC-06 RX <- 3

#define DHTPIN 12
#define DHTTYPE DHT11

// -------------------- 객체 생성 --------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHTPIN, DHTTYPE);

Adafruit_NeoPixel mainLed(MAIN_LED_COUNT, LAMP_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel statusLed(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

SoftwareSerial bt(BT_RX_PIN, BT_TX_PIN);

// -------------------- 상태 변수 --------------------
bool powerOn = false;
bool lastPowerOn = false;

int lastPowerBtnState = HIGH;

int ldrValue = 0;
float temperature = 0;
float humidity = 0;

String brightLabel = "BRIGHT";
String overallStatus = "OFF";

unsigned long lastSensorMs = 0;
unsigned long lastLcdToggleMs = 0;
bool showBrightnessLine = true;

// =======================================================
// setup
// =======================================================
void setup() {
  Serial.begin(9600);
  bt.begin(9600);

  pinMode(POWER_BTN_PIN, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Stand OFF");
  lcd.setCursor(0, 1);
  lcd.print("Press Power Btn");

  dht.begin();

  mainLed.begin();
  statusLed.begin();

  setMainLampBrightness(0);

  statusLed.setBrightness(30);
  statusLed.setPixelColor(0, 0, 0, 0);
  statusLed.show();
}

// =======================================================
// loop
// =======================================================
void loop() {

  // ---------------- 전원 버튼 처리 ----------------
  int pNow = digitalRead(POWER_BTN_PIN);

  if (pNow == LOW && lastPowerBtnState == HIGH) {
    powerOn = !powerOn;
    delay(150);
  }

  lastPowerBtnState = pNow;

  // ---------------- 블루투스 처리 ----------------
  if (bt.available()) {
    char c = bt.read();

    if (c == '1') powerOn = true;
    if (c == '0') powerOn = false;
  }

  // ---------------- 전원 상태 변화 처리 ----------------
  if (powerOn != lastPowerOn) {

    if (!powerOn) {
      turnOffMainLamp();

      statusLed.setPixelColor(0, 0, 0, 0);
      statusLed.show();

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Stand OFF");
      lcd.setCursor(0, 1);
      lcd.print("Press Power Btn");

    } else {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Stand ON");
      lcd.setCursor(0, 1);
      lcd.print("Loading...");
      delay(1500);
      lcd.clear();
    }

    lastPowerOn = powerOn;
  }

  if (!powerOn) return;

  // ---------------- 센서 읽기 (1초마다) ----------------
  unsigned long now = millis();

  if (now - lastSensorMs >= 1000) {
    lastSensorMs = now;

    ldrValue = analogRead(LDR_PIN);

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      temperature = t;
      humidity = h;
    }

    int tInt = (int)temperature;
    int hInt = (int)humidity;

    brightLabel = getBrightnessLabel(ldrValue);
    overallStatus = getOverallStatus(temperature, humidity, ldrValue);

    updateLampBrightness(ldrValue);
    updateStatusLed(overallStatus, ldrValue);

    sendDataToBt(ldrValue, tInt, hInt, overallStatus);
  }

  // ---------------- LCD 출력 ----------------
  int tInt = (int)temperature;
  int hInt = (int)humidity;

  updateLcd(overallStatus, brightLabel, tInt, hInt);
}

// =======================================================
// 밝기 라벨
// 실제 장치 기준:
// LDR 값이 낮을수록 어두움
// LDR 값이 높을수록 밝음
// =======================================================
String getBrightnessLabel(int ldr) {
  if (ldr < 400) return "DARK";
  else if (ldr < 700) return "NORMAL";
  else return "BRIGHT";
}

// =======================================================
// 전체 상태 계산
// GOOD 조건:
// 온도: 20도 이상 26도 이하
// 습도: 10% 이상 60% 이하
// 조도: 400 이상
// =======================================================
String getOverallStatus(float t, float h, int ldr) {
  bool tempGood = (t >= 20 && t <= 26);
  bool humiGood = (h >= 10 && h <= 60);
  bool lightGood = (ldr >= 400);

  if (tempGood && humiGood && lightGood) return "GOOD";

  int bad = 0;

  if (!tempGood) bad++;
  if (!humiGood) bad++;
  if (!lightGood) bad++;

  return (bad >= 2) ? "BAD" : "NORMAL";
}

// =======================================================
// 메인 LED 자동 밝기 조절
// 어두움 -> LED 밝기 100
// 보통   -> LED 밝기 50
// 밝음   -> LED 밝기 1
// =======================================================
void updateLampBrightness(int ldr) {
  uint8_t brightness;

  if (ldr < 400) {
    brightness = 100;        // 어두움 -> 최대 밝기 100
  } else if (ldr < 700) {
    brightness = 50;         // 보통 -> 중간 밝기 50
  } else {
    brightness = 1;          // 밝음 -> 최소 밝기 1
  }

  setMainLampBrightness(brightness);
}

// =======================================================
// 상태 LED
// GOOD   -> 파란색
// NORMAL -> 초록색
// BAD    -> 빨간색
// =======================================================
void updateStatusLed(const String &status, int ldr) {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;

  if (status == "GOOD") {
    r = 0;
    g = 128;
    b = 255;
  } else if (status == "NORMAL") {
    r = 0;
    g = 255;
    b = 0;
  } else if (status == "BAD") {
    r = 255;
    g = 0;
    b = 0;
  }

  // 상태 LED는 어두울수록 밝게, 밝을수록 어둡게
  uint8_t bright = map(ldr, 0, 1023, 200, 30);

  statusLed.setBrightness(bright);
  statusLed.setPixelColor(0, statusLed.Color(r, g, b));
  statusLed.show();
}

// =======================================================
// LCD 출력
// =======================================================
void updateLcd(const String &status, const String &bright, int t, int h) {

  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(status);

  unsigned long now = millis();

  if (now - lastLcdToggleMs >= 2000) {
    lastLcdToggleMs = now;
    showBrightnessLine = !showBrightnessLine;
  }

  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);

  if (showBrightnessLine) {
    lcd.print("Light:");
    lcd.print(bright);
  } else {
    lcd.print("T:");
    lcd.print(t);
    lcd.print(" H:");
    lcd.print(h);
  }
}

// =======================================================
// 블루투스로 데이터 전송
// =======================================================
void sendDataToBt(int ldr, int t, int h, const String &status) {
  bt.print("LDR:");
  bt.print(ldr);
  bt.print(",T:");
  bt.print(t);
  bt.print(",H:");
  bt.print(h);
  bt.print(",STATUS:");
  bt.println(status);
}

// =======================================================
// 메인 네오픽셀
// =======================================================
void setMainLampBrightness(uint8_t brightness) {
  mainLed.setBrightness(brightness);

  uint32_t color = mainLed.Color(255, 255, 255);

  for (int i = 0; i < MAIN_LED_COUNT; i++) {
    mainLed.setPixelColor(i, color);
  }

  mainLed.show();
}

void turnOffMainLamp() {
  setMainLampBrightness(0);
}
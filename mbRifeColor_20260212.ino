// Arduino Mega2560 Rife Machine - Updated for 2.8" 240x320 ILI9341 TFT (SPI)
// Uses Adafruit_ILI9341 + U8g2_for_Adafruit_GFX for font/UTF8 support

#include <EEPROM.h>
#include <AD9833.h>           // https://github.com/Billwilliams1952/AD9833-Library-Arduino
#include <Adafruit_GFX.h>     // Core graphics
#include <Adafruit_ILI9341.h> // ILI9341 driver
#include <U8g2_for_Adafruit_GFX.h> // U8g2 adapter for TFT
#include <SPI.h>

#define DEBUG 0
#if DEBUG == 1
  #define debug(x)   Serial.print(x)
  #define debugln(x) Serial.println(x)
#else
  #define debug(x)
  #define debugln(x)
#endif

// ILI9341 Pins (Hardware SPI on Mega: SCK=52, MOSI=51)
#define TFT_CS   53
#define TFT_DC   49
#define TFT_RST  48  // Or -1 if not used

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;  // U8g2 overlay for fonts

// English diagnoses (uncomment Cyrillic if needed)
const char* diagnoses[] = {
  "Good Sleep","Alcoholism","Angina","Stomachache","General Pain","Headaches",
  "Infection","Acute pain","Back pain","Arthralgia","Toothache",
  "No appetite","No taste","Motion sickness", "Hoarseness","Gastric Ulcer",
  "Prostate ailments","Deafness","Flu","Hemorrhoids","Kidney stones",
  "Cough","Runny nose","Hair loss","Hypertension","Low pressure",
  "Thyroid Gland Disease","Bad breath","Herpes", "Epilepsy","Constipation",
  "Dizziness","Ascension 1","Ascension 2", "H.Clark Zapper"
};

const int frequencies[] = {
  6,5,4,0,0,0,0,0,0,0,
  10000,0,0,0,0,0,0,0,0,0,
  787,776,727,690,465,428,660,0,0,0,
  10000,3000,95,0,0,0,0,0,0,0,
  3000,2720,95,666,80,40,0,0,0,0,
  10000,144,160,520,304,0,0,0,0,0,
  3000,95,880,1550,802,787,776,727,0,0,
  3000,95,10000,1550,802,880,787,727,690,666,
  787,784,776,728,727,465,432,0,0,0,
  160,500,1600,5000,324,528,0,0,0,0,
  5170,3000,2720,2489,1800,1600,1550,880,832,666,
  10000,465,444,1865,125,95,72,880,787,727,
  10000,20,0,0,0,0,0,0,0,0,
  10000,5000,648,624,600,465,440,648,444,1865,
  880,760,727,0,0,0,0,0,0,0,
  10000,1550,802,880,832,787,727,465,0,0,
  2050,880,1550,802,787,727,465,20,0,0,
  10000,1550,880,802,787,727,20,0,0,0,
  954,889,841,787,763,753,742,523,513,482,
  4474,6117,774,1550,447,880,802,727,0,0,
  10000,444,727,787,880,6000,3000,1552,0,0,
  7760,7344,3702,3672,1550,1500,1234,776,766,728,
  1800,1713,1550,802,800,880,787,727,444,20,
  10000,5000,2720,2170,1552,880,800,787,727,465,
  10000,3176,2112,95,324,528,880,787,727,304,
  727,787,880,0,0,0,0,0,0,0,
  16000,10000,160,80,35,0,0,0,0,0,
  1550,802,880,787,727,0,0,0,0,0,
  2950,1900,1577,1550,1489,1488,629,464,450,383,
  10000,880,802,787,727,700,650,600,210,125,
  3176,1550,880,832,802,787,776,727,444,422,
  1550,880,802,784,787,786,766,522,727,72,
  528,432,0,0,0,0,0,0,0,0,
  963,852,741,639,528,528,417,396,285,174,
  32000,1150,0,0,0,0,0,0,0,0
};

int numberOfDiagnoses = sizeof(diagnoses) / sizeof(diagnoses[0]);

// Pins (same as before)
#define pinEncoderCW      2
#define pinEncoderCCW     3
#define pinBeepOut        4
#define pinShutdown2      5
#define pinShutdown1      6
#define pinGenCS          9
#define pinLcdBacklight  13   // Optional PWM for brightness
#define pinBtnEnter      21
#define pinBatteryLevel  A0

AD9833 gen(pinGenCS);

const int SCROLL_DOWN = 0;
const int SCROLL_UP   = 1;
const int ONE_BEEP    = 1;
const int THREE_BEEPS = 3;

const int PIEZO_BEEP_TONE   = 2200;
const int PEIZO_BEEP_LENGTH = 80;
const int PEIZO_BEEP_PAUSE  = 40;

// Voltage divider
const float R1 = 32000.0;
const float R2 = 8000.0;
float referenceVoltage = 5.0;

byte   selectedItem    = 1;
byte   pageOffset      = 0;
char   treatmentTime[4] = "20";
char*  strComplete     = (char*)"";
bool   isGeneratingFrequency = false;
volatile bool encoderMoved   = false;
volatile bool btnEnterPressed = false;
volatile int  buttonOutput    = 0;

enum { STATE_NORMAL, STATE_SHORT, STATE_LONG };
long LONG_DELTA     = 1200ul;
long DEBOUNCE_DELTA = 25ul;

byte eepromAddress = 0;

// Setup
void setup(void) {
  Serial.begin(115200);

  tft.begin();
  tft.setRotation(1);  // Landscape (adjust 0-3 as needed for your display)
  tft.fillScreen(ILI9341_BLACK);
  u8g2.begin(tft);     // Link U8g2 to TFT
  u8g2.setFontMode(1); // Transparent
  u8g2.setForegroundColor(ILI9341_WHITE);
  u8g2.setBackgroundColor(ILI9341_BLACK);

  pinMode(pinLcdBacklight, OUTPUT);
  analogWrite(pinLcdBacklight, 255);  // Full brightness

  pinMode(pinShutdown1, OUTPUT);
  pinMode(pinShutdown2, OUTPUT);
  digitalWrite(pinShutdown1, HIGH);
  digitalWrite(pinShutdown2, LOW);

  pinMode(pinEncoderCW,  INPUT_PULLUP);
  pinMode(pinEncoderCCW, INPUT_PULLUP);
  pinMode(pinBtnEnter,   INPUT_PULLUP);

  gen.Begin();
  gen.EnableOutput(false);

  // Intro screen
  DisplayIntroScreen();
  delay(1800);

  // Read EEPROM
  byte last = EEPROM.read(eepromAddress);
  if (last > 0 && last <= numberOfDiagnoses) {
    selectedItem = last;
    pageOffset = CalculatePageOffset(selectedItem);
  }

  RedrawMainMenu();

  attachInterrupt(digitalPinToInterrupt(pinEncoderCW),  OnScrollChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinEncoderCCW), OnScrollChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinBtnEnter),   OnButtonPress,  CHANGE);

  debugln("Battery: " + String(MeasureBatteryVoltage(), 2) + " V");
}

// Loop (same as before)
void loop() {
  if (encoderMoved) {
    int8_t delta = AnalyzeEncoderChange();
    if (delta != 0) {
      if (!isGeneratingFrequency) {
        if (delta > 0) ScrollItem(SCROLL_UP);
        else           ScrollItem(SCROLL_DOWN);
      }
    }
  }

  if (btnEnterPressed) {
    switch (buttonOutput) {
      case STATE_SHORT:
        ProcessButtonClick();
        buttonOutput = STATE_NORMAL;
        break;
      case STATE_LONG:
        Shutdown();
        buttonOutput = STATE_NORMAL;
        break;
    }
    btnEnterPressed = false;
  }
}

// Redraw menu
void RedrawMainMenu() {
  tft.fillScreen(ILI9341_BLACK);
  DisplayMainMenu(pageOffset);
  HighlightSelectedItem(selectedItem, pageOffset);
}

// Intro
void DisplayIntroScreen(void) {
  tft.fillScreen(ILI9341_BLUE);
  u8g2.setFont(u8g2_font_helvB14_te);
  u8g2.setForegroundColor(ILI9341_YELLOW);
  u8g2.drawUTF8(20, 50, "Dr. Royal Rife");
  u8g2.drawUTF8(55, 90, "Machine");
  u8g2.setFont(u8g2_font_7x14_tr);
  u8g2.drawUTF8(60, 140, "2024");
  u8g2.setForegroundColor(ILI9341_WHITE);  // Reset
}

// Main menu
void DisplayMainMenu(int pgOffset) {
  // Top bar
  tft.fillRect(0, 0, 320, 18, ILI9341_NAVY);
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawUTF8(4, 2, "Select Program");

  float v = MeasureBatteryVoltage();
  char buf[12];
  snprintf(buf, sizeof(buf), "%.2fV", v);
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawUTF8(320 - u8g2.getUTF8Width(buf) - 4, 3, buf);

  // Items
  int y = 22;
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);  // Supports Cyrillic
  for (int i = pgOffset; i < pgOffset + 12 && i < numberOfDiagnoses; i++) {
    u8g2.drawUTF8(12, y, diagnoses[i]);
    y += 16;
  }
}

// Highlight
void HighlightSelectedItem(byte item, byte offset) {
  int pos = item - offset;
  if (pos < 0 || pos > 11) return;

  int y1 = 22 + pos * 16 - 13;
  tft.drawRect(4, y1, 320-8, 15, ILI9341_WHITE);
  u8g2.setForegroundColor(ILI9341_BLACK);
  u8g2.setBackgroundColor(ILI9341_WHITE);
  u8g2.drawUTF8(12, y1 + 2, diagnoses[item-1]);
  u8g2.setForegroundColor(ILI9341_WHITE);
  u8g2.setBackgroundColor(ILI9341_BLACK);
}

// Scroll
void ScrollItem(bool direction) {
  if (direction == SCROLL_UP)   selectedItem++;
  else                          selectedItem--;

  if (selectedItem < 1)                 selectedItem = numberOfDiagnoses;
  if (selectedItem > numberOfDiagnoses) selectedItem = 1;

  pageOffset = CalculatePageOffset(selectedItem);
  RedrawMainMenu();
}

byte CalculatePageOffset(byte cur) {
  return ((cur-1) / 10) * 10;
}

float MeasureBatteryVoltage() {
  int val = analogRead(pinBatteryLevel);
  float v  = (val * referenceVoltage) / 1023.0;
  return v / (R2 / (R1 + R2));
}

void ProcessButtonClick() {
  if (isGeneratingFrequency) {
    isGeneratingFrequency = false;
    debugln("Abort requested");
    return;
  }

  EEPROM.update(eepromAddress, selectedItem);

  strComplete = (char*)"";
  isGeneratingFrequency = true;

  bool aborted = GenerateFrequency();

  if (aborted) {
    isGeneratingFrequency = false;
    RedrawMainMenu();
  }
}

bool GenerateFrequency() {
  int count = 0;
  for (int i = 0; i < 10; i++) {
    if (frequencies[(selectedItem-1)*10 + i] > 0) count++;
  }
  if (count == 0) return false;

  uint32_t timePerFreq_ms = (atoi(treatmentTime) * 60000UL) / count;

  gen.EnableOutput(true);

  tft.fillScreen(ILI9341_BLACK);

  for (int i = 0; i < count; i++) {
    uint16_t freq = frequencies[(selectedItem-1)*10 + i];
    if (freq == 0) continue;

    gen.ApplySignal(SQUARE_WAVE, REG0, freq);

    String sFreq = String(freq);
    String sSeq  = String(i+1);

    tft.fillScreen(ILI9341_BLACK);
    u8g2.setFont(u8g2_font_helvB12_te);
    u8g2.drawUTF8(20, 40, "Therapy in progress");
    u8g2.setFont(u8g2_font_9x15_tf);
    u8g2.drawUTF8(20, 80,  "Seq: ");   u8g2.print(sSeq.c_str());
    u8g2.drawUTF8(20, 110, "Freq: ");  u8g2.print(sFreq.c_str()); u8g2.print(" Hz");
    u8g2.drawUTF8(20, 140, "Time left: "); u8g2.print(treatmentTime); u8g2.print(" min");

    unsigned long start = millis();
    while (millis() - start < timePerFreq_ms) {
      if (!isGeneratingFrequency) {
        gen.EnableOutput(false);
        return true;
      }
      delay(50);
    }

    PlayTone(ONE_BEEP);
  }

  gen.EnableOutput(false);
  isGeneratingFrequency = false;

  PlayTone(THREE_BEEPS);

  strComplete = (char*)"Finished!";
  tft.fillScreen(ILI9341_GREEN);
  u8g2.setForegroundColor(ILI9341_BLACK);
  u8g2.setFont(u8g2_font_helvB14_te);
  u8g2.drawUTF8(60, 100, strComplete);
  u8g2.setForegroundColor(ILI9341_WHITE);

  delay(2400);
  strComplete = (char*)"";

  RedrawMainMenu();

  return false;
}

void Shutdown() {
  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);
  debugln("Shutdown requested");
}

void PlayTone(int cnt) {
  for (int i = 0; i < cnt; i++) {
    tone(pinBeepOut, PIEZO_BEEP_TONE, PEIZO_BEEP_LENGTH);
    delay(PEIZO_BEEP_LENGTH + PEIZO_BEEP_PAUSE);
  }
}

void OnScrollChange() {
  encoderMoved = true;
}

int8_t AnalyzeEncoderChange() {
  encoderMoved = false;
  static uint8_t lrmem = 3;
  static int lrsum = 0;
  static const int8_t TRANS[] = {0,-1,1,14,1,0,14,-1,-1,14,0,1,14,1,-1,0};

  int8_t l = digitalRead(pinEncoderCW);
  int8_t r = digitalRead(pinEncoderCCW);

  lrmem = ((lrmem & 0x03)<<2) | (l<<1) | r;
  lrsum += TRANS[lrmem];

  if (lrsum % 4 != 0) return 0;
  if (lrsum ==  4) { lrsum = 0; return  1; }
  if (lrsum == -4) { lrsum = 0; return -1; }
  lrsum = 0;
  return 0;
}

void OnButtonPress() {
  static unsigned long tPress = 0;
  static int lastState = HIGH;

  int state = digitalRead(pinBtnEnter);

  if (state != lastState) {
    if (state == LOW) {
      tPress = millis();
    } else {
      unsigned long dur = millis() - tPress;
      if (dur >= LONG_DELTA) {
        buttonOutput = STATE_LONG;
      } else if (dur >= DEBOUNCE_DELTA) {
        buttonOutput = STATE_SHORT;
      }
      btnEnterPressed = true;
    }
    lastState = state;
  }
}

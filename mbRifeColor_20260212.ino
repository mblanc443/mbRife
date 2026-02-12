// Arduino Mega2560 & Mega2560-Pro Rife Machine generator
// Updated for 2.8" 240×320 ILI9341 color TFT (SPI) + touch (touch not used yet)
// - logically bounce-protected encoder
// - battery level meter in top right
// - controls by single encoder: short click = select/start, long click = shutdown
// - last used item remembered in EEPROM
// - UTF8 cyrillic support
// - ILI9341 full buffer mode (fast & flicker-free)

#include <EEPROM.h>
#include <AD9833.h>   // https://github.com/Billwilliams1952/AD9833-Library-Arduino
#include <U8g2lib.h>
#include <SPI.h>

#define DEBUG 0
#if DEBUG == 1
  #define debug(x)   Serial.print(x)
  #define debugln(x) Serial.println(x)
#else
  #define debug(x)
  #define debugln(x)
#endif
          
// ────────ILI9341 240×320  constructor──────
// Most common wiring on Mega (hardware SPI):
//   SCK  → pin 52
//   MOSI → pin 51
//   CS   → pin 53   (or any free pin)
//   DC   → pin 49   (or any free pin)
//   RST  → pin 48   (or -1 = no reset pin, use software reset)
//   LED  → 5V or PWM pin (backlight)

#define TFT_CS   53
#define TFT_DC   49
#define TFT_RST  48     // can be -1 if display has own reset circuit
//
U8G2_ILI9341_240X320_F_4W_HW_SPI u8g2(U8G2_R1, /* cs=*/ TFT_CS, /* dc=*/ TFT_DC, /* reset=*/ TFT_RST);

// ───────────────────────────────────────────────
// English list (uncomment cyrillic block below if needed)
// ───────────────────────────────────────────────
const char* diagnoses[] = {
  "Good Sleep","Alcoholism","Angina","Stomachache","General Pain","Headaches",
  "Infection","Acute pain","Back pain","Arthralgia","Toothache",
  "No appetite","No taste","Motion sickness", "Hoarseness","Gastric Ulcer",
  "Prostate ailments","Deafness","Flu","Hemorrhoids","Kidney stones",
  "Cough","Runny nose","Hair loss","Hypertension","Low pressure",
  "Thyroid Gland Disease","Bad breath","Herpes", "Epilepsy","Constipation",
  "Dizziness","Ascension 1","Ascension 2", "H.Clark Zapper"
};
/*
// Cyrillic version
const char* diagnoses[] = {
  "Хороший сон","Алкоголизм","Стенокардия","Желудочная боль","Общая боль","Головная боль",
  "Инфекция","Острая боль","Боль в спине","Артралгия","Зубная боль",
  "Нет аппетита","Нет вкуса","Морская болезнь","Охриплость","Язва желудка",
  "Недуги простаты", "Глухота","Грипп","Геморой","Камни в почках",
  "Кашель","Насморк","Потеря волос","Высокое давление","Низкое давление",
  "Недуги Щитовидной","Запах изо рта","Герпес","Эпилепсия","Запоры",
  "Головокружение" ,"Вознесение 1","Вознесение 2", "H.Clark Zapper"
};
*/

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

// ───────────────────────────────────────────────
// Pins
// ───────────────────────────────────────────────
#define pinEncoderCW      2
#define pinEncoderCCW     3
#define pinBeepOut        4
#define pinShutdown2      5
#define pinShutdown1      6
#define pinGenCS          9
#define pinLcdBacklight  13   // PWM possible
#define pinBtnEnter      21   // better on interrupt-capable pin
#define pinBatteryLevel  A0

AD9833 gen(pinGenCS);

const int SCROLL_DOWN = 0;
const int SCROLL_UP   = 1;
const int ONE_BEEP    = 1;
const int THREE_BEEPS = 3;

const int PIEZO_BEEP_TONE   = 2200;
const int PEIZO_BEEP_LENGTH = 80;
const int PEIZO_BEEP_PAUSE  = 40;

// Voltage divider example: adjust R1/R2 to your resistors!
const float R1 = 32000.0;
const float R2 = 8000.0;
float referenceVoltage = 5.0;

byte   selectedItem    = 1;
byte   pageOffset      = 0;
char   treatmentTime[4] = "20";     // max 3 chars + \0
char*  strComplete     = (char*)"";
bool   isGeneratingFrequency = false;
volatile bool encoderMoved   = false;
volatile bool btnEnterPressed = false;
volatile int  buttonOutput    = 0;

enum { STATE_NORMAL, STATE_SHORT, STATE_LONG };
long LONG_DELTA    = 1200ul;
long DEBOUNCE_DELTA = 25ul;

byte eepromAddress = 0;

// SETUP
void setup(void) {
  Serial.begin(115200);
  while (!Serial);

  u8g2.begin();
  // u8g2.setContrast(180);   // usually not needed for ILI9341
  u8g2.enableUTF8Print();
  u8g2.setFontPosTop();

  pinMode(pinLcdBacklight, OUTPUT);
  digitalWrite(pinLcdBacklight, HIGH);   // backlight ON

  pinMode(pinShutdown1, OUTPUT);
  pinMode(pinShutdown2, OUTPUT);
  digitalWrite(pinShutdown1, HIGH);
  digitalWrite(pinShutdown2, LOW);

  pinMode(pinEncoderCW,  INPUT_PULLUP);
  pinMode(pinEncoderCCW, INPUT_PULLUP);
  pinMode(pinBtnEnter,   INPUT_PULLUP);

  gen.Begin();
  gen.EnableOutput(false);

  // Intro
  u8g2.firstPage();
  do {
    DisplayIntroScreen();
  } while (u8g2.nextPage());

  delay(1800);

  // Read last selection
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

// LOOP
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

// ───────RedrawMainMenu────────────
void RedrawMainMenu() {
  u8g2.firstPage();
  do {
    DisplayMainMenu(pageOffset);
    HighlightSelectedItem(selectedItem, pageOffset);
  } while (u8g2.nextPage());
}

// ────────DisplayIntroScreen──────────
void DisplayIntroScreen(void) {
  u8g2.setFont(u8g2_font_helvB14_te);
  u8g2.drawStr(20, 50,  "Dr. Royal Rife");
  u8g2.drawStr(55, 90,  "Machine");
  u8g2.setFont(u8g2_font_7x14_tr);
  u8g2.drawStr(60, 140, "2024");
}

// ──────────DisplayMainMenu────────────
void DisplayMainMenu(int pgOffset) {
  u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  u8g2.setDrawColor(1);

  // Top bar
  u8g2.drawBox(0, 0, 240, 18);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawUTF8(4, 2, "Select Program");

  float v = MeasureBatteryVoltage();
  char buf[12];
  snprintf(buf, sizeof(buf), "%.2fV", v);
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(240 - u8g2.getUTF8Width(buf) - 4, 3, buf);

  u8g2.setDrawColor(1);

  // Menu items
  int y = 22;
  for (int i = pgOffset; i < pgOffset + 12 && i < numberOfDiagnoses; i++) {
    u8g2.drawUTF8(12, y, diagnoses[i]);
    y += 16;
  }
}

// ─────────HighlightSelectedItem─────────
void HighlightSelectedItem(byte item, byte offset) {
  int pos = item - offset;
  if (pos < 0 || pos > 11) return;

  int y1 = 22 + pos * 16 - 13;
  u8g2.setDrawColor(1);
  u8g2.drawFrame(4, y1, 240-8, 15);
  u8g2.setDrawColor(2);           // XOR / inverse highlight
  u8g2.drawBox(5, y1+1, 240-10, 13);
  u8g2.setDrawColor(1);
}

// ────────ScrollItem──────────
void ScrollItem(bool direction) {
  if (direction == SCROLL_UP)   selectedItem++;
  else                          selectedItem--;

  if (selectedItem < 1)                 selectedItem = numberOfDiagnoses;
  if (selectedItem > numberOfDiagnoses) selectedItem = 1;

  pageOffset = CalculatePageOffset(selectedItem);
  RedrawMainMenu();
}

byte CalculatePageOffset(byte cur) {
  return ((cur-1) / 10) * 10;   // show 10 items per "page"
}

// ──────────MeasureBatteryVoltage─────────
float MeasureBatteryVoltage() {
  int val = analogRead(pinBatteryLevel);
  float v  = (val * referenceVoltage) / 1023.0;
  return v / (R2 / (R1 + R2));
}

// ───────────ProcessButtonClick───────────
void ProcessButtonClick() {
  if (isGeneratingFrequency) {
    isGeneratingFrequency = false;   // request abort
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

// ──────────GenerateFrequency──────────
bool GenerateFrequency() {
  int count = 0;
  for (int i = 0; i < 10; i++) {
    if (frequencies[(selectedItem-1)*10 + i] > 0) count++;
  }
  if (count == 0) return false;

  uint32_t timePerFreq_ms = (atoi(treatmentTime) * 60000UL) / count;

  gen.EnableOutput(true);

  for (int i = 0; i < count; i++) {
    uint16_t freq = frequencies[(selectedItem-1)*10 + i];
    if (freq == 0) continue;

    gen.ApplySignal(SQUARE_WAVE, REG0, freq);

    String sFreq = String(freq);
    String sSeq  = String(i+1);

    u8g2.firstPage();
    do {
      u8g2.setFont(u8g2_font_helvB12_te);
      u8g2.drawUTF8(20, 40, "Therapy in progress");
      u8g2.setFont(u8g2_font_9x15_tf);
      u8g2.drawUTF8(20, 80,  "Seq: ");   u8g2.print(sSeq);
      u8g2.drawUTF8(20, 110, "Freq: ");  u8g2.print(sFreq); u8g2.print(" Hz");
      u8g2.drawUTF8(20, 140, "Time left: "); u8g2.print(treatmentTime); u8g2.print(" min");
    } while (u8g2.nextPage());

    unsigned long start = millis();
    while (millis() - start < timePerFreq_ms) {
      if (!isGeneratingFrequency) {     // abort requested
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
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_helvB14_te);
    u8g2.drawStr(60, 100, strComplete);
  } while (u8g2.nextPage());

  delay(2400);
  strComplete = (char*)"";

  RedrawMainMenu();

  // Optional auto-shutdown
  // digitalWrite(pinShutdown1, LOW);
  // digitalWrite(pinShutdown2, HIGH);

  return false;
}

// ──────────Shutdown───────────
void Shutdown() {
  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);
  debugln("Shutdown requested");
}

// ─────────PlayTone───────────
void PlayTone(int cnt) {
  for (int i = 0; i < cnt; i++) {
    tone(pinBeepOut, PIEZO_BEEP_TONE, PEIZO_BEEP_LENGTH);
    delay(PEIZO_BEEP_LENGTH + PEIZO_BEEP_PAUSE);
  }
}

// ──── encoder & button ───
void OnScrollChange() {
  encoderMoved = true;
}

// helper call
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

// helper call
void OnButtonPress() {
  static unsigned long tPress = 0;
  static int lastState = HIGH;

  int state = digitalRead(pinBtnEnter);

  if (state != lastState) {
    if (state == LOW) {
      tPress = millis();
    } else {
      // released
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

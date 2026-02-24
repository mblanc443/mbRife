// Uses Adafruit_ILI9341 + U8g2_for_Adafruit_GFX for Cyrillic
// Arduino Mega2560 + 2.8" 320x240 TFT (ILI9341)
// Optimized partial redraw for fast scrolling
#include <EEPROM.h>
#include <AD9833.h>    // https://github.com/Billwilliams1952/AD9833-Library-Arduino
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <U8g2_for_Adafruit_GFX.h>

#define DEBUG 0
#if DEBUG == 1
  #define debug(x) Serial.print(x)
  #define debugln(x) Serial.println(x)
#else
  #define debug(x)
  #define debugln(x)
#endif

// ILI9341 Pins (Hardware SPI on Mega: SCK=52, MOSI=51)
#define TFT_CS   53
#define TFT_DC   48
#define TFT_RST  49

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2gfx;

// Gold color palette
#define GOLD1  tft.color565(255, 215, 0)
#define GOLD2  tft.color565(218, 165, 32)
#define GOLD3  tft.color565(184, 134, 11)
#define GOLD4  tft.color565(255, 236, 139)

// AD9833
#define pinGenCS         9
AD9833 gen(pinGenCS);

// Pins
#define pinEncoderCW     2
#define pinEncoderCCW    3
#define pinBeepOut       4
#define pinShutdown2     5
#define pinShutdown1     6
#define pinBtnEnter     21
#define pinBatteryLevel A0

// Battery voltage divider - tune R1/R2 to match your resistors
const float R1 = 32000.0;
const float R2 = 8000.0;
const float referenceVoltage = 5.0;

// Diagnoses - English version (uncomment Cyrillic block below if preferred)
const char* diagnoses[] = {
  "Good Sleep","Alcoholism","Angina","Stomachache","General Pain","Headaches",
  "Infection","Acute pain","Back pain","Arthralgia","Toothache",
  "No appetite","No taste","Motion sickness", "Hoarseness","Gastric Ulcer",
  "Prostate ailments","Deafness","Flu","Hemorrhoids","Kidney stones",
  "Cough","Runny nose","Hair loss","Hypertension","Low pressure",
  "Thyroid Gland Disease","Bad breath","Herpes", "Epilepsy","Constipation",
  "Dizziness","Accending 1","Accending 2", "H.Clark Zapper"
};

// Cyrillic version
//const char* diagnoses[] = {
//  "Хороший сон","Алкоголизм","Стенокардия","Желудочная боль","Общая боль","Головная боль",
//  "Инфекция","Острая боль","Боль в спине","Артралгия","Зубная боль",
//  "Нет аппетита","Нет вкуса","Морская болезнь","Охриплость","Язва желудка",
//  "Недуги простаты","Глухота","Грипп","Геморой","Камни в почках",
//  "Кашель","Насморк","Потеря волос","Высокое давление","Низкое давление",
//  "Недуги Щитовидной","Запах изо рта","Герпес","Эпилепсия","Запоры",
//  "Головокружение","Вознесение 1","Вознесение 2","H.Clark Zapper"
//}; 

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

const int SCROLL_DOWN = 0;
const int SCROLL_UP   = 1;
const int ONE_BEEP    = 1;
const int THREE_BEEPS = 3;

const int PIEZO_BEEP_TONE   = 2000;
const int PEIZO_BEEP_LENGTH = 1000;
const int PEIZO_BEEP_PAUSE  =  500;

const byte ITEMS_PER_PAGE = 8;

// State variables
byte selectedItem     = 1;
byte prevSelectedItem = 1;          // NEW: track previous for partial update
byte pageOffset       = 0;
char* titleLine       = (char*)"DIAGNOSES:";
char treatmentTime[3] = "20";
char* strComplete     = (char*)"";

bool isGeneratingFrequency = false;
uint16_t intFreqToGenerate = 0;

volatile bool encoderMoved    = false;
volatile bool btnEnterPressed = false;
volatile int  buttonOutput    = 0;

enum {STATE_NORMAL, STATE_SHORT, STATE_LONG};
long LONG_DELTA     = 1500UL;
long DEBOUNCE_DELTA = 30UL;

byte eepromAddress = 0;

// Frame dimensions for color horiz 320x240
const int FRAME_X = 2;
const int FRAME_Y = 2;
const int FRAME_W = 316;
const int FRAME_H = 236;

// List area constants (avoid touching top bar)
const int LIST_Y_START   = 50;     // below title bar + margin
const int ITEM_HEIGHT    = 24;     // line spacing + padding
const int TEXT_Y_OFFSET  = 18;     // baseline adjustment for font

// titlE
const int TITLE_BAR_HIGHT = 30;

// ======= SETUP ====================
void setup() {
  Serial.begin(9600);
  tft.begin();
  tft.setRotation(1);                   // Landscape 320x240
  tft.fillScreen(ILI9341_BLACK);
  //
  u8g2gfx.begin(tft);
  u8g2gfx.setFontMode(1);               // Transparent
  u8g2gfx.setFontDirection(0);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  //
  pinMode(pinShutdown1, OUTPUT);
  pinMode(pinShutdown2, OUTPUT);
  digitalWrite(pinShutdown1, HIGH);
  digitalWrite(pinShutdown2, LOW);
  //
  pinMode(pinEncoderCW,  INPUT_PULLUP);
  pinMode(pinEncoderCCW, INPUT_PULLUP);
  pinMode(pinBtnEnter,   INPUT_PULLUP);
  //
  gen.Begin();
  gen.EnableOutput(false);
  // Show intro
  DisplayIntroScreen(); 
  delay(2500);
  // Draw static top bar ONCE
  DrawTitleBar();
  DrawBattery();
  // Initial menu (full draw)
  DrawList();
  //
  attachInterrupt(digitalPinToInterrupt(pinEncoderCW),  OnScrollChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinEncoderCCW), OnScrollChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinBtnEnter),   OnButtonPress,  CHANGE);
  // Restore last selection
  byte saved = EEPROM.read(eepromAddress);
  if (saved >= 1 && saved <= numberOfDiagnoses) {
    SetSelectedItem(saved);
  }
}

// ======= LOOP ====================
void loop() {
  if (encoderMoved) {
    int8_t dir = AnalyzeEncoderChange();
    if (dir != 0) ScrollItem(dir > 0 ? SCROLL_UP : SCROLL_DOWN);
  }

  if (btnEnterPressed) {
    switch (buttonOutput) {
      case STATE_SHORT: ProcessButtonClick(); buttonOutput = STATE_NORMAL; break;
      case STATE_LONG:  Shutdown();           buttonOutput = STATE_NORMAL; break;
    }
    btnEnterPressed = false;
  }
}

// ======= OPTIMIZED SCROLL HANDLING =======
void ScrollItem(bool direction) {
  prevSelectedItem = selectedItem;  // remember old one

  if (direction == SCROLL_UP) {
    selectedItem++;
    if (selectedItem > numberOfDiagnoses) selectedItem = 1;
  } else {
    selectedItem--;
    if (selectedItem < 1) selectedItem = numberOfDiagnoses;
  }

  byte newPageOffset = CalulatePageOffset(selectedItem);

  if (newPageOffset != pageOffset) {
    // Page changed → full list redraw (but skip title/battery)
    pageOffset = newPageOffset;
    DrawList(); //RedrawListAreaOnly();
  } else {
    // Same page → partial update only
    RedrawSelectionOnly();
  }
}

void DrawList() {
   // Clear list area only
  tft.fillRect(0, TITLE_BAR_HIGHT, 320, 240 - TITLE_BAR_HIGHT, ILI9341_BLACK); 
  //
  u8g2gfx.setFont(u8g2_font_10x20_tf);
  int currentPosY = LIST_Y_START;

  for (byte currentItem = 0; currentItem < ITEMS_PER_PAGE; currentItem++) {
    int diagnoseIndex = pageOffset + currentItem;
    if (diagnoseIndex >= numberOfDiagnoses) break;
    //
    bool isSelected = (diagnoseIndex == (selectedItem - 1));
    //
    if (isSelected) {
      tft.fillRect(8, currentPosY - 19, 304, 24, ILI9341_NAVY); //HIGHLIGHT
      u8g2gfx.setForegroundColor(ILI9341_YELLOW); // change selected to yellow font 
    } else {
      u8g2gfx.setForegroundColor(ILI9341_GREEN);
    }
    u8g2gfx.setCursor(18, currentPosY);
    u8g2gfx.print(diagnoses[diagnoseIndex]);
    // update current item position
    currentPosY += ITEM_HEIGHT;
  }
}

// Partial redraw: only old + new selected item
void RedrawSelectionOnly() {
  u8g2gfx.setFont(u8g2_font_10x20_tf);

  // 1. Un-highlight previous item
  byte prevLocalIdx = prevSelectedItem - 1 - pageOffset;
  int prevY = LIST_Y_START + prevLocalIdx * ITEM_HEIGHT;
  tft.fillRect(8, prevY - 19, 304, 24, ILI9341_BLACK);   // erase highlight
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  u8g2gfx.setCursor(18, prevY);
  u8g2gfx.print(diagnoses[prevSelectedItem - 1]);

  // 2. Highlight new selected item
  byte newLocalIdx = selectedItem - 1 - pageOffset;
  int newY = LIST_Y_START + newLocalIdx * ITEM_HEIGHT;
  tft.fillRect(8, newY - 19, 304, 24, ILI9341_NAVY);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  u8g2gfx.setCursor(18, newY);
  u8g2gfx.print(diagnoses[selectedItem - 1]);
}

// ======= DISPLAY FUNCTIONS (mostly unchanged, but called less) =======
void DisplayIntroScreen(void) {
  tft.fillScreen(ILI9341_BLUE);
  DrawIntroFrame();

  u8g2gfx.setFont(u8g2_font_helvB24_te);
  u8g2gfx.setForegroundColor(GOLD2);
  int textWidth = u8g2gfx.getUTF8Width("Dr. Royal Rife");
  u8g2gfx.setCursor((320 - textWidth) / 2, 80);
  u8g2gfx.print("Dr. Royal Rife");
  textWidth = u8g2gfx.getUTF8Width("Healing Machine");
  u8g2gfx.setCursor((320 - textWidth) / 2, 120);
  u8g2gfx.print("Healing Machine");

  DrawGoldenCrown(160, 135);

  u8g2gfx.setFont(u8g2_font_7x14_tr);
  u8g2gfx.setForegroundColor(ILI9341_MAGENTA);
  textWidth = u8g2gfx.getUTF8Width("by kd2cmo 2026");
  u8g2gfx.setCursor((320 - textWidth) / 2, 190);
  u8g2gfx.print("by kd2cmo 2026");
  textWidth = u8g2gfx.getUTF8Width("Wait...");
  u8g2gfx.setCursor((320 - textWidth) / 2, 210);
  u8g2gfx.print("Wait...");
}

void DrawTitleBar() {
  tft.fillRect(0, 0, 320, TITLE_BAR_HIGHT, ILI9341_BLUE);
  u8g2gfx.setFont(u8g2_font_t0_22b_tf); //u8g2_font_helvB14_te
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  int titleWidth = u8g2gfx.getUTF8Width(titleLine);
  u8g2gfx.setCursor((320 - titleWidth) / 2, 24);
  u8g2gfx.print(titleLine);
}

void DrawBattery() {
  float v = MeasureBatteryVoltage();
  String s = String(v, 1) + "v";
  u8g2gfx.setFont(u8g2_font_9x18B_tf);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  int w = u8g2gfx.getUTF8Width(s.c_str());
  u8g2gfx.setCursor(320 - w - 8, 23);
  u8g2gfx.print(s.c_str());
}

// ======= OTHER FUNCTIONS (unchanged) =======
byte CalulatePageOffset(byte item) {
  return ((item - 1) / ITEMS_PER_PAGE) * ITEMS_PER_PAGE;
}

void SetSelectedItem(byte item) {
  selectedItem = item;
  pageOffset = CalulatePageOffset(item);
  prevSelectedItem = item;  // sync
  DrawList(); // full list on set
}

//  unchanged ...
void DisplayTreatInProgressScreen(String frequency, String seq) {
  tft.fillScreen(ILI9341_BLACK);
  DrawTitleBar();
  DrawBattery();

  u8g2gfx.setFont(u8g2_font_helvB18_te);
  u8g2gfx.setForegroundColor(ILI9341_CYAN);
  int textWidth = u8g2gfx.getUTF8Width("Therapy");
  u8g2gfx.setCursor((320 - textWidth) / 2, 80);
  u8g2gfx.print("Therapy");

  u8g2gfx.setFont(u8g2_font_9x15B_tf);
  u8g2gfx.setForegroundColor(ILI9341_WHITE);
  u8g2gfx.setCursor(45, 120);
  u8g2gfx.print("Time: ");
  u8g2gfx.print(treatmentTime);
  u8g2gfx.print(" minutes");

  if (strlen(strComplete) == 0 && frequency.length() > 0) {
    String line = "Seq: " + seq + "   Freq: " + frequency + " Hz";
    u8g2gfx.setFont(u8g2_font_helvB14_te);
    u8g2gfx.setForegroundColor(ILI9341_GREEN);
    textWidth = u8g2gfx.getUTF8Width(line.c_str());
    u8g2gfx.setCursor((320 - textWidth) / 2, 175);
    u8g2gfx.print(line.c_str());
  }

  if (strlen(strComplete) > 0) {
    u8g2gfx.setFont(u8g2_font_helvB24_te);
    u8g2gfx.setForegroundColor(ILI9341_RED);
    textWidth = u8g2gfx.getUTF8Width(strComplete);
    u8g2gfx.setCursor((320 - textWidth) / 2, 190);
    u8g2gfx.print(strComplete);
  }
}

bool GenerateFrequency() {
  int numFreq = 0;
  for (int i = 0; i < 10; i++) {
    if (frequencies[10 * (selectedItem - 1) + i] > 0) numFreq++;
  }

  if (numFreq == 0) return false;

  float fragmentTime = (atoi(treatmentTime) * 60000UL) / (float)numFreq;

  gen.EnableOutput(true);
  strComplete = (char*)"";

  for (int i = 0; i < numFreq; i++) {
    unsigned long start = millis();
    intFreqToGenerate = frequencies[10 * (selectedItem - 1) + i];
    String freqStr = String(intFreqToGenerate);
    String seqStr  = String(i + 1);
    //
    DisplayTreatInProgressScreen(freqStr, seqStr);
    //
    gen.ApplySignal(SQUARE_WAVE, REG0, intFreqToGenerate);
    //
    while ((millis() - start) < fragmentTime && isGeneratingFrequency) {
      if (btnEnterPressed) {
        gen.EnableOutput(false);
        return true;  // aborted
      }
    }
    //
    PlayTone(ONE_BEEP);
  }
  //
  gen.EnableOutput(false);
  isGeneratingFrequency = false;
  //
  strComplete = (char*)"Finished!";
  PlayTone(THREE_BEEPS);
  DisplayTreatInProgressScreen("", "");
  delay(3000);
  //
  SetSelectedItem(selectedItem);
  //
  strComplete = (char*)"";
  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);

  return false;
}

void ProcessButtonClick() {
  if (!isGeneratingFrequency) {
    EEPROM.update(eepromAddress, selectedItem);
    titleLine = (char*)diagnoses[selectedItem - 1];
    isGeneratingFrequency = true;
    btnEnterPressed = false;
    bool aborted = GenerateFrequency();
    if (aborted) {
      isGeneratingFrequency = false;
      btnEnterPressed = false;
      SetSelectedItem(selectedItem);
      titleLine = (char*)"DIAGNOSES:";
      DrawTitleBar();
      DrawBattery();
    }
  } else {
    btnEnterPressed = true;  // signal abort
  }
}

void Shutdown() {
  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);
  isGeneratingFrequency = false;
  btnEnterPressed = false;
  debugln("Shutdown initiated");
}

// ===== ENCODER / BUTTON ====================
void OnScrollChange() {
  encoderMoved = true;
}

// Buxtronix/Oleg Mazurov algorithm analyzer
int8_t AnalyzeEncoderChange() {
    encoderMoved = false;

    static uint8_t prev_AB = 0b00000011;  // initial state (both high = detent)
    static int8_t  encoderVal = 0;

    // Read current A/B states (CW = pinEncoderCW, CCW = pinEncoderCCW)
    uint8_t A = digitalRead(pinEncoderCW);
    uint8_t B = digitalRead(pinEncoderCCW);
    uint8_t current_AB = (A << 1) | B;     // 00, 01, 10, 11

    // Shift in new state (now 4-bit: old AB + new AB)
    uint8_t transition = (prev_AB << 2) | current_AB;

    // Standard 4-state quadrature lookup table (very forgiving for bounce)
    static const int8_t deltaTable[16] = {
        0,   // 0000 - invalid/stable
       -1,   // 0001 - CCW
        1,   // 0010 - CW
        0,   // 0011 - invalid
        1,   // 0100 - CW
        0,   // 0101 - invalid
        0,   // 0110 - invalid
       -1,   // 0111 - CCW
       -1,   // 1000 - CCW
        0,   // 1001 - invalid
        0,   // 1010 - invalid
        1,   // 1011 - CW
        0,   // 1100 - invalid
        1,   // 1101 - CW
       -1,   // 1110 - CCW
        0    // 1111 - stable
    };

    int8_t delta = deltaTable[transition];
    encoderVal += delta;
    int8_t result = 0;

    // Full-step mode: trigger only on complete detent (most reliable, no skipping)
    if (encoderVal <= -4) { result = -1; encoderVal = 0; }
    if (encoderVal >=  4) { result =  1; encoderVal = 0; }

    // ── Optional: half-step mode (twice as sensitive, can feel "faster")
    if (encoderVal <= -2) { result = -1; encoderVal += 2; }
    if (encoderVal >=  2) { result =  1; encoderVal -= 2; }

    prev_AB = current_AB;

    // Keep your debug print if you want
    if (result != 0) {
      debug("Step: "); debug(result);
      debug("  AB: "); debug(current_AB);  //, BIN);
      debug("  encVal: "); debugln(encoderVal);
    }

    return result;
}

// ===== BUTTON original working logic ===========
void OnButtonPress() {
  int buttonState;
  static int lastButtonStatus = HIGH;
  static unsigned long longTime = 0ul;
  static unsigned long shortTime = 0ul;

  buttonState = digitalRead(pinBtnEnter);
  boolean timeoutShort = (millis() > shortTime);
  boolean timeoutLong  = (millis() > longTime);

  if (buttonState != lastButtonStatus) {
      shortTime = millis() + DEBOUNCE_DELTA;
      longTime  = millis() + LONG_DELTA;
  }

  boolean buttonStateChange = (buttonState != lastButtonStatus);
  boolean buttonReleased = (buttonStateChange && (buttonState == HIGH));

  lastButtonStatus = buttonState;

  if (!buttonStateChange) {
       buttonOutput = STATE_NORMAL | buttonOutput;
       return;
  }

  if (timeoutLong && buttonReleased) {
      buttonOutput = STATE_LONG | buttonOutput;
      btnEnterPressed = true;
  } else if (timeoutShort && buttonReleased) {
      buttonOutput = STATE_SHORT | buttonOutput;
      btnEnterPressed = true;
  } else {
      buttonOutput = STATE_NORMAL | buttonOutput;
      btnEnterPressed = false;
  }
}

// INTRO
void DrawIntroFrame() {
  tft.fillRoundRect(FRAME_X, FRAME_Y, FRAME_W, FRAME_H, 24, GOLD1);             // Outer thick gold border
  tft.drawRoundRect(FRAME_X+5, FRAME_Y+5, FRAME_W-10, FRAME_H-10, 18, GOLD3);   // Inner shadow
  tft.drawRoundRect(FRAME_X+12, FRAME_Y+12, FRAME_W-24, FRAME_H-24, 12, GOLD4); // Inner highlight
  // Ornamental corners (simple curls)
  for (int i = 0; i < 20; i++) {
    tft.drawPixel(FRAME_X+12+i, FRAME_Y+12+sin(i*0.4)*8, GOLD2);
    tft.drawPixel(FRAME_X+FRAME_W-12-i, FRAME_Y+12+sin(i*0.4)*8, GOLD2);
    tft.drawPixel(FRAME_X+12+i, FRAME_Y+FRAME_H-12-sin(i*0.4)*8, GOLD2);
    tft.drawPixel(FRAME_X+FRAME_W-12-i, FRAME_Y+FRAME_H-12-sin(i*0.4)*8, GOLD2);
  }
  tft.fillRoundRect(FRAME_X+20, FRAME_Y+20, FRAME_W-40, FRAME_H-40, 8, ILI9341_BLACK); // Inner background
}

// INTRO
void DrawGoldenCrown(int cx, int topY) {
  // Fixed dimensions for a smaller, elegant crown
  const int w = 52;          // width of crown
  const int h = 28;          // total height (base + peaks)
  const int baseH = 8;       // height of the base band

  int x0 = cx - w / 2;        // left edge
  int y0 = topY;               // top of crown (lowest point is y0+h)

  // ------------------- BASE BAND -------------------
  tft.fillRoundRect(x0, y0 + h - baseH, w, baseH, baseH / 3, GOLD3);
  tft.drawRoundRect(x0, y0 + h - baseH, w, baseH, baseH / 3, GOLD1);
  tft.drawFastHLine(x0 + 3, y0 + h - baseH, w - 6, GOLD4);     // top highlight
  tft.drawFastHLine(x0 + 2, y0 + h - 1, w - 4, GOLD2);         // bottom shadow

  // Decorative "gems" on the base (small gold circles)
  for (int i = 0; i < 5; i++) {
    int gemX = x0 + w * (i + 1) / 6;
    int gemY = y0 + h - baseH / 2;
    tft.fillCircle(gemX, gemY, baseH / 3, GOLD1);
    tft.drawCircle(gemX, gemY, baseH / 3, GOLD4);
  }

  // ------------------- PEAKS -------------------
  int peakBaseY = y0 + h - baseH;        // where peaks start
  int leftX   = x0 + w / 4;
  int centerX = cx;
  int rightX  = x0 + 3 * w / 4;

  int leftH   = 12;      // height of left peak above base
  int centerH = 16;      // taller central peak
  int rightH  = 12;

  // Left peak (with pearl)
  tft.fillTriangle(leftX, peakBaseY,
                   leftX - 6, peakBaseY - leftH,
                   leftX + 6, peakBaseY - leftH,
                   GOLD2);
  tft.fillCircle(leftX, peakBaseY - leftH - 2, 3, GOLD4);
  tft.drawCircle(leftX, peakBaseY - leftH - 2, 3, GOLD1);

  // Right peak (with pearl)
  tft.fillTriangle(rightX, peakBaseY,
                   rightX - 6, peakBaseY - rightH,
                   rightX + 6, peakBaseY - rightH,
                   GOLD2);
  tft.fillCircle(rightX, peakBaseY - rightH - 2, 3, GOLD4);
  tft.drawCircle(rightX, peakBaseY - rightH - 2, 3, GOLD1);

  // Center peak (tallest, with a small cross)
  tft.fillTriangle(centerX, peakBaseY,
                   centerX - 8, peakBaseY - centerH,
                   centerX + 8, peakBaseY - centerH,
                   GOLD1);
  int crossX = centerX;
  int crossY = peakBaseY - centerH - 3;
  tft.drawLine(crossX, crossY - 3, crossX, crossY + 3, GOLD4);
  tft.drawLine(crossX - 3, crossY, crossX + 3, crossY, GOLD4);
  tft.fillCircle(crossX, crossY, 2, GOLD3);

  // ------------------- ARCHES -------------------
  // Left arch
  for (int i = 0; i <= 12; i++) {
    float t = i / 12.0;
    int x = leftX + t * (centerX - leftX);
    int y = peakBaseY - leftH * (1 - t) - centerH * t + 4 * sin(t * PI);
    tft.drawPixel(x, y, GOLD2);
  }
  // Right arch
  for (int i = 0; i <= 12; i++) {
    float t = i / 12.0;
    int x = centerX + t * (rightX - centerX);
    int y = peakBaseY - centerH * (1 - t) - rightH * t + 4 * sin(t * PI);
    tft.drawPixel(x, y, GOLD2);
  }

  // ------------------- EXTRA SPARKLES -------------------
  tft.drawPixel(leftX - 3, peakBaseY - leftH / 2, GOLD4);
  tft.drawPixel(rightX + 3, peakBaseY - rightH / 2, GOLD4);
  tft.drawPixel(centerX, peakBaseY - centerH + 2, GOLD4);
}

float MeasureBatteryVoltage() {
  int raw = analogRead(pinBatteryLevel);
  float voltage = (raw * referenceVoltage) / 1024.0;
  return voltage / (R2 / (R1 + R2));
}

void PlayTone(int n) {
  for (int i = 0; i < n; i++) {
    tone(pinBeepOut, PIEZO_BEEP_TONE, PEIZO_BEEP_LENGTH);
    delay(PEIZO_BEEP_LENGTH);
    noTone(pinBeepOut);
    delay(PEIZO_BEEP_PAUSE);
  }
}


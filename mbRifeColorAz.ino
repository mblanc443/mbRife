// Uses Adafruit_ILI9341 + U8g2_for_Adafruit_GFX for Cyrillic
// Arduino Mega2560 + 2.8" 320x240 TFT (ILI9341)
// Optimized partial redraw for fast scrolling, countdown and remaining time
// Added signal type indicator sin/square
#include <EEPROM.h>
#include <AD9833.h>    // https://github.com/Billwilliams1952/AD9833-Library-Arduino
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <math.h>

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

// Diagnoses - English version
//const char* diagnoses[] = {
//  "Good Sleep","Alcoholism","Angina","Stomachache","General Pain","Headaches",
//  "Infection","Acute pain","Back pain","Arthralgia","Toothache",
//  "No appetite","No taste","Motion sickness", "Hoarseness","Gastric Ulcer",
//  "Prostate ailments","Deafness","Flu","Hemorrhoids","Kidney stones",
//  "Cough","Runny nose","Hair loss","Hypertension","Low pressure",
//  "Thyroid Gland Disease","Bad breath","Herpes", "Epilepsy","Constipation",
//  "Dizziness","Accending 1","Accending 2", "H.Clark Zapper",
//  "AngelZ" // <-- AngelZ added here
//};

// Cyrillic version
const char* diagnoses[] = {
 "Хороший сон","Алкоголизм","Стенокардия","Желудочная боль","Общая боль","Головная боль",
  "Инфекция","Острая боль","Боль в спине","Артралгия","Зубная боль",
  "Нет аппетита","Нет вкуса","Морская болезнь","Охриплость","Язва желудка",
  "Недуги простаты","Глухота","Грипп","Геморой","Камни в почках",
  "Кашель","Насморк","Потеря волос","Высокое давление","Низкое давление",
  "Недуги Щитовидной","Запах изо рта","Герпес","Эпилепсия","Запоры",
  "Головокружение","Вознесение 1","Вознесение 2","H.Clark Zapper", "Angel-Z"
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
  32000,1150,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0 // <-- AngelZ frequencies (all zero)
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
byte prevSelectedItem = 1;
byte pageOffset       = 0;
char* titleLine       = (char*)"DIAGNOSES:";
char treatmentTime[3] = "20";
char* strComplete     = (char*)"";

bool isGeneratingFrequency = false;
uint16_t intFreqToGenerate = 0;
bool isSineWave            = false;   // false = Square, true = Sine

volatile bool encoderMoved    = false;
volatile bool btnEnterPressed = false;
volatile int  buttonOutput    = 0;

enum {STATE_NORMAL, STATE_SHORT, STATE_LONG};
long LONG_DELTA     = 1500UL;
long DEBOUNCE_DELTA = 30UL;

byte eepromAddress = 0;

// Frame dimensions for color horiz 320x240
const int FRAME_X =   2;
const int FRAME_Y =   2;
const int FRAME_W = 316;
const int FRAME_H = 236;

// List area constants (avoid touching top bar)
const int LIST_Y_START   = 50;
const int ITEM_HEIGHT    = 24;
const int TEXT_Y_OFFSET  = 18;

// title
const int TITLE_BAR_HIGHT = 30;

static int  prevFreqIndex        =      -1;
static char prevTimeStr[6]       = "99:99";
static char prevAngelZTimeStr[6] = "00:00";  
static bool treatmentScreenDrawn =   false;

// ==== AngelZ Sequences CLASS =====
class Sequences {
  private:
    AD9833 *m_pgen;
  public:
    Sequences() {}
    Sequences(AD9833 *pGen);
    void Execute10khzSequence();
    float GetDelaySequence(int currentPoint, double valueStart, double valueMax, double tau, int splitPoint);
};

Sequences::Sequences(AD9833 *pGen) : m_pgen(pGen) {}

// Execute10khzSequence
void Sequences::Execute10khzSequence() {
    for (int pulseNum = 0; pulseNum < 20; pulseNum++) {
        m_pgen->EnableOutput(true);
        m_pgen->ApplySignal(SQUARE_WAVE, REG0, 10000); // 10kHz
    }
}

// GetDelaySequence
float Sequences::GetDelaySequence(int currentPoint, double valueStart, double valueMax, double tau, int splitPoint) {
    if (currentPoint < splitPoint) {
        return valueStart + (valueMax - valueStart) * (1 - exp(-currentPoint / tau));
    } else {
        return valueMax * exp(-(currentPoint - splitPoint) / tau);
    }
}

#define   ANGELZ_TOTAL_POINTS       48
const int ANGELZ_SPLIT_POINT      = 24;
const int ANGELZ_NUMBER_OF_CYCLES = 59;

double angelz_valueStart[ANGELZ_NUMBER_OF_CYCLES] = {
    20.0, 15.0, 18.0, 25.0, 22.0, 30.0, 21.0, 35.0, 23.0, 40.0, 
    24.0, 45.0, 16.0, 27.0, 19.0, 31.0, 13.0, 28.0, 14.0, 34.0, 
    12.0, 39.0, 10.0, 42.0, 11.0, 37.0, 9.0, 33.0, 8.0, 36.0, 
    7.0, 29.0, 6.0, 32.0, 5.0, 38.0, 4.0, 41.0, 3.0, 44.0, 
    2.0, 43.0, 1.0, 46.0, 0.5, 48.0, 0.2, 50.0, 0.1, 52.0,
    0.0, 53.0, 12.0, 17.0, 22.0, 28.0, 34.0, 41.0, 45.0
};

double angelz_valueMax[ANGELZ_NUMBER_OF_CYCLES] = {
    52.6, 45.0, 55.0, 60.0, 50.0, 65.0, 53.0, 70.0, 56.0, 75.0,
    59.0, 80.0, 48.0, 62.0, 51.0, 64.0, 49.0, 67.0, 50.0, 71.0,
    46.0, 73.0, 47.0, 76.0, 54.0, 78.0, 57.0, 79.0, 58.0, 82.0,
    61.0, 85.0, 63.0, 88.0, 65.0, 90.0, 68.0, 92.0, 66.0, 95.0,
    69.0, 97.0, 72.0, 99.0, 74.0, 100.0, 77.0, 102.0, 80.0, 105.0,
    82.0, 110.0, 55.0, 60.0, 65.0, 70.0, 75.0, 85.0, 90.0
};

double angelz_tau[ANGELZ_NUMBER_OF_CYCLES] = {
    5.0, 7.0, 4.5, 6.0, 3.5, 5.5, 4.0, 6.5, 3.0, 5.0,
    4.3, 6.8, 3.8, 6.1, 3.2, 6.4, 4.2, 5.6, 3.9, 6.7,
    4.4, 5.9, 4.1, 6.9, 3.6, 5.7, 3.3, 6.3, 3.1, 6.2,
    4.6, 6.6, 3.7, 5.8, 4.9, 7.0, 3.4, 6.5, 4.8, 7.1,
    5.1, 7.2, 3.9, 6.0, 3.5, 6.8, 4.2, 6.3, 5.0, 7.3,
    5.2, 7.4, 4.0, 5.5, 6.0, 6.5, 5.7, 6.8, 7.1
};

// --- AngelZ Progress Bar and тиме сtate ---
//static int prevAngelZFreqBarIndex = -1;
//static char prevAngelZTimeStr[6] = "00:00";
// ==== END of AngelZ Sequences CLASS =====

// ======= SETUP ========
void setup() {
  Serial.begin(9600);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
  u8g2gfx.begin(tft);
  u8g2gfx.setFontMode(1);
  u8g2gfx.setFontDirection(0);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  pinMode(pinShutdown1, OUTPUT);
  pinMode(pinShutdown2, OUTPUT);
  digitalWrite(pinShutdown1, HIGH);
  digitalWrite(pinShutdown2, LOW);
  pinMode(pinEncoderCW,  INPUT_PULLUP);
  pinMode(pinEncoderCCW, INPUT_PULLUP);
  pinMode(pinBtnEnter,   INPUT_PULLUP);
  gen.Begin();
  gen.EnableOutput(false);
  DisplayIntroScreen(); 
  delay(2500);
  DrawTitleBar();
  DrawBattery();
  DrawList();
  attachInterrupt(digitalPinToInterrupt(pinEncoderCW),  OnScrollChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinEncoderCCW), OnScrollChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinBtnEnter),   OnButtonPress,  CHANGE);
  byte saved = EEPROM.read(eepromAddress);
  if (saved >= 1 && saved <= numberOfDiagnoses) {
    SetSelectedItem(saved);
  }
}

// ======= LOOP ========
void loop() {
  if (encoderMoved) {
    int8_t direction = AnalyzeEncoderChange();
    if (direction != 0) {
      if (!isGeneratingFrequency) {
        ScrollItem(direction > 0 ? SCROLL_UP : SCROLL_DOWN);
      }
    }
  }

  if (btnEnterPressed) {
    switch (buttonOutput) {
      case STATE_SHORT: ProcessButtonClick(); buttonOutput = STATE_NORMAL; break;
      case STATE_LONG:  Shutdown();           buttonOutput = STATE_NORMAL; break;
    }
    btnEnterPressed = false;
  }
}

// Type of signal
void UpdateSignalIndicator() {
  u8g2gfx.setFont(u8g2_font_t0_22b_tf);
  u8g2gfx.setBackgroundColor(ILI9341_BLUE);
  
  // Clear the right portion of title bar first
  tft.fillRect(260, 0, 60, TITLE_BAR_HIGHT, ILI9341_BLUE);
  
  u8g2gfx.setForegroundColor(ILI9341_WHITE);
  const char* sigStr = isSineWave ? "SIN" : "_||_";
  int sigWidth = u8g2gfx.getUTF8Width(sigStr);
  u8g2gfx.setCursor(320 - sigWidth - 4, 24);
  u8g2gfx.print(sigStr);
}

// ======= ANGELZ PROGRESS BAR & COUNTDOWN FUNCTION =======
void DisplayTreatInProgressScreenAngelZ(int freqBarIndex, unsigned long msLeft, bool forceFull = false) {
  // --- Title Bar ---
  static bool titleDrawn = false;
  if (forceFull || !titleDrawn) {
    tft.fillRect(0, 0, 320, TITLE_BAR_HIGHT, ILI9341_BLUE);
    u8g2gfx.setFont(u8g2_font_t0_22b_tf);
    u8g2gfx.setBackgroundColor(ILI9341_BLUE);
    u8g2gfx.setForegroundColor(ILI9341_YELLOW);
    int titleWidth = u8g2gfx.getUTF8Width("Angel-Z session");
    u8g2gfx.setCursor((320 - titleWidth) / 2, 24);
    u8g2gfx.print("Angel-Z session");
    titleDrawn = true;
  }

  // --- Frequency Bar (24 rectangles) ---
  const int BAR_X = 16;
  const int BAR_Y = 200;
  const int BAR_W = 288; // 12 px per rect
  const int BAR_H = 8;
  const int RECT_W = 12;
  const int RECT_H = 40;
  const int NUM_RECTS = 24;

  // Only update changed rectangles
  static int prevBarIndex = -1;
  if (forceFull || prevBarIndex != freqBarIndex) {
    int from = 0, to = NUM_RECTS - 1;
    if (!forceFull && prevBarIndex != -1) {
      from = min(prevBarIndex, freqBarIndex);
      to   = max(prevBarIndex, freqBarIndex);
    }
    for (int i = from; i <= to; i++) {
      int x = BAR_X + i * RECT_W;
      uint16_t color;
      if (i <= freqBarIndex) {
        color = (i < 18) ? ILI9341_GREEN : ILI9341_RED;
      } else {
        color = ILI9341_DARKGREY;
      }
      tft.fillRect(x, BAR_Y, RECT_W - 2, RECT_H, color); // -2 for spacing
    }
    prevBarIndex = freqBarIndex;
  }

  // --- Countdown Time in Center ---
  unsigned long secondsLeft = msLeft / 1000;
  int minLeft = secondsLeft / 60;
  int secLeft = secondsLeft % 60;
  char buf[6];
  sprintf(buf, "%02d:%02d", minLeft, secLeft);

  // Only redraw digits that change (or forceFull)
  u8g2gfx.setFont(u8g2_font_fub42_tf);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_MAGENTA);

  static bool positionsCalculated = false;
  static int  charX[5];
  static int  charW[5];
  if (!positionsCalculated) {
    positionsCalculated = true;
    int maxDigitW = 0;
    for (char c = '0'; c <= '9'; c++) {
      char tmp[2] = {c, 0};
      int w = u8g2gfx.getUTF8Width(tmp);
      if (w > maxDigitW) maxDigitW = w;
    }
    int colonW = u8g2gfx.getUTF8Width(":");
    int totalW = maxDigitW * 4 + colonW;
    int startX = 160 - totalW / 2;
    int cx = startX;
    charX[0] = cx; charW[0] = maxDigitW; cx += maxDigitW;
    charX[1] = cx; charW[1] = maxDigitW; cx += maxDigitW;
    charX[2] = cx; charW[2] = colonW;    cx += colonW;
    charX[3] = cx; charW[3] = maxDigitW; cx += maxDigitW;
    charX[4] = cx; charW[4] = maxDigitW;
  }
  for (int i = 0; i < 5; i++) {
    if (forceFull || buf[i] != prevAngelZTimeStr[i]) {
      tft.fillRect(charX[i], 96, charW[i], 45, ILI9341_BLACK);
      char tmp[2] = {buf[i], 0};
      int actualW = u8g2gfx.getUTF8Width(tmp);
      int offset = (charW[i] - actualW) / 2;
      u8g2gfx.setCursor(charX[i] + offset, 140);
      u8g2gfx.print(tmp);
    }
  }
  strcpy(prevAngelZTimeStr, buf);
}

// ======= DISPLAY FUNCTIONS =======
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
  u8g2gfx.setFont(u8g2_font_t0_22b_tf);
  u8g2gfx.setBackgroundColor(ILI9341_BLUE);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  int titleWidth = u8g2gfx.getUTF8Width(titleLine);
  u8g2gfx.setCursor((320 - titleWidth) / 2, 24);
  u8g2gfx.print(titleLine);
}

void DrawBattery() {
  float v = MeasureBatteryVoltage();
  String s = String(v, 1) + "v";
  u8g2gfx.setFont(u8g2_font_9x18B_tf);
  u8g2gfx.setBackgroundColor(ILI9341_BLUE);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  int w = u8g2gfx.getUTF8Width(s.c_str());
  u8g2gfx.setCursor(320 - w - 8, 23);
  u8g2gfx.print(s.c_str());
}

void SetSelectedItem(byte item) {
  selectedItem = item;
  pageOffset = CalulatePageOffset(item);
  prevSelectedItem = item;
  DrawList();
}

// DisplayTreatInProgressScreen
void DisplayTreatInProgressScreen( int currentFreqIndex, int selectedItem,  unsigned long msLeft, bool forceFull = false) {
  // --- Layout constants ---
  const int SCREEN_W = 320;
  const int SCREEN_H = 240;
  const int TITLE_BAR_HIGHT = 30;
  const int LEFT_W = SCREEN_W / 3;      // 106
  const int RIGHT_W = SCREEN_W - LEFT_W; // 214
  const int BAR_H = (SCREEN_H - TITLE_BAR_HIGHT) / 10; // 21
  const int BAR_W = LEFT_W - 8;         // leave a margin
  const int BAR_X = 4;
  const int BAR_Y0 = TITLE_BAR_HIGHT;   // below top bar

  // --- Draw everything ONCE at start ---
  if (forceFull || !treatmentScreenDrawn) {
    // Clear screen
    tft.fillScreen(ILI9341_BLACK);

    // Top Bar: Diagnosis
    tft.fillRect(0, 0, SCREEN_W, TITLE_BAR_HIGHT, ILI9341_BLUE);
    u8g2gfx.setFont(u8g2_font_t0_22b_tf);
    u8g2gfx.setBackgroundColor(ILI9341_BLUE);
    u8g2gfx.setForegroundColor(ILI9341_YELLOW);
    int titleWidth = u8g2gfx.getUTF8Width(titleLine);
    u8g2gfx.setCursor((SCREEN_W - titleWidth) / 2, 24);
    u8g2gfx.print(titleLine);

    // Signal type indicator - top right of title bar
    u8g2gfx.setFont(u8g2_font_t0_22b_tf);
    u8g2gfx.setBackgroundColor(ILI9341_BLUE);
    u8g2gfx.setForegroundColor(ILI9341_YELLOW);
    const char* sigStr = isSineWave ? "SIN" : "_||_";
    int sigWidth = u8g2gfx.getUTF8Width(sigStr);
    u8g2gfx.setCursor(320 - sigWidth - 4, 24);
    u8g2gfx.print(sigStr);


    // Left 1/3: All bars
    for (int i = 0; i < 10; i++) {
      int freq = frequencies[10 * (selectedItem - 1) + i];
      int y = BAR_Y0 + i * BAR_H;
      uint16_t barColor = (i == currentFreqIndex) ? ILI9341_GREENYELLOW : ILI9341_DARKGREEN;
      uint16_t borderColor = (i == currentFreqIndex) ? ILI9341_WHITE : barColor;
      tft.fillRect(BAR_X, y, BAR_W, BAR_H - 2, barColor);
      if (i == currentFreqIndex) {
        tft.drawRect(BAR_X, y, BAR_W, BAR_H - 2, borderColor);
      }
      u8g2gfx.setFont(u8g2_font_helvB14_te);
      u8g2gfx.setBackgroundColor(barColor);
      u8g2gfx.setForegroundColor(ILI9341_PINK);
      char freqStr[8];
      if (freq > 0) sprintf(freqStr, "%d", freq);
      else strcpy(freqStr, "-");
      int textWidth = u8g2gfx.getUTF8Width(freqStr);
      tft.fillRect(BAR_X + 2, y + 2, BAR_W - 4, BAR_H - 6, barColor);
      u8g2gfx.setCursor(BAR_X + (BAR_W - textWidth) / 2, y + BAR_H / 2 + 6);
      u8g2gfx.print(freqStr);
    }

    // Right 2/3: "Remaining" label
    u8g2gfx.setFont(u8g2_font_helvB24_te);
    u8g2gfx.setBackgroundColor(ILI9341_BLACK);
    u8g2gfx.setForegroundColor(ILI9341_WHITE);
    int labelWidth = u8g2gfx.getUTF8Width("Remaining");
    int labelY = 100; // halfway between top bar and countdown
    u8g2gfx.setCursor(LEFT_W + (RIGHT_W - labelWidth) / 2, labelY);
    u8g2gfx.print("Remaining");

    // Reset previous time string
    strcpy(prevTimeStr, "99:99");
    prevFreqIndex = currentFreqIndex;
    treatmentScreenDrawn = true;
  }

  // --- Only update bar highlight if changed ---
  if (prevFreqIndex != currentFreqIndex) {
    // Remove highlight from previous
    if (prevFreqIndex >= 0 && prevFreqIndex < 10) {
      int freq = frequencies[10 * (selectedItem - 1) + prevFreqIndex];
      int y = BAR_Y0 + prevFreqIndex * BAR_H;
      uint16_t barColor = ILI9341_DARKGREEN;
      tft.fillRect(BAR_X, y, BAR_W, BAR_H - 2, barColor);
      u8g2gfx.setFont(u8g2_font_helvB14_te);
      u8g2gfx.setBackgroundColor(barColor);
      u8g2gfx.setForegroundColor(ILI9341_PINK);
      char freqStr[8];
      if (freq > 0) sprintf(freqStr, "%d", freq);
      else strcpy(freqStr, "-");
      int textWidth = u8g2gfx.getUTF8Width(freqStr);
      tft.fillRect(BAR_X + 2, y + 2, BAR_W - 4, BAR_H - 6, barColor);
      u8g2gfx.setCursor(BAR_X + (BAR_W - textWidth) / 2, y + BAR_H / 2 + 6);
      u8g2gfx.print(freqStr);
    }
    // Highlight current
    if (currentFreqIndex >= 0 && currentFreqIndex < 10) {
      int freq = frequencies[10 * (selectedItem - 1) + currentFreqIndex];
      int y = BAR_Y0 + currentFreqIndex * BAR_H;
      uint16_t barColor = ILI9341_GREENYELLOW;
      tft.fillRect(BAR_X, y, BAR_W, BAR_H - 2, barColor);
      tft.drawRect(BAR_X, y, BAR_W, BAR_H - 2, ILI9341_WHITE);
      u8g2gfx.setFont(u8g2_font_helvB14_te);
      u8g2gfx.setBackgroundColor(barColor);
      u8g2gfx.setForegroundColor(ILI9341_PINK);
      char freqStr[8];
      if (freq > 0) sprintf(freqStr, "%d", freq);
      else strcpy(freqStr, "-");
      int textWidth = u8g2gfx.getUTF8Width(freqStr);
      tft.fillRect(BAR_X + 2, y + 2, BAR_W - 4, BAR_H - 6, barColor);
      u8g2gfx.setCursor(BAR_X + (BAR_W - textWidth) / 2, y + BAR_H / 2 + 6);
      u8g2gfx.print(freqStr);
    }
    prevFreqIndex = currentFreqIndex;
  }

  // --- Only update digits and colon that change ---
  unsigned long secondsLeft = msLeft / 1000;
  if ((long)msLeft < 0) secondsLeft = 0;
  int minLeft = secondsLeft / 60;
  int secLeft = secondsLeft % 60;
  char buf[6];
  sprintf(buf, "%02d:%02d", minLeft, secLeft);

  u8g2gfx.setFont(u8g2_font_fub42_tf);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_MAGENTA);

  int maxDigitW = 0;
  for (char c = '0'; c <= '9'; c++) {
    char tmp[2] = {c, 0};
    int w = u8g2gfx.getUTF8Width(tmp);
    if (w > maxDigitW) maxDigitW = w;
  }
  int colonW = u8g2gfx.getUTF8Width(":");
  int totalW = maxDigitW * 4 + colonW;
  int startX = LEFT_W + (RIGHT_W - totalW) / 2;
  int cx = startX;
  int charX[5], charW[5];
  charX[0] = cx; charW[0] = maxDigitW; cx += maxDigitW;
  charX[1] = cx; charW[1] = maxDigitW; cx += maxDigitW;
  charX[2] = cx; charW[2] = colonW;    cx += colonW;
  charX[3] = cx; charW[3] = maxDigitW; cx += maxDigitW;
  charX[4] = cx; charW[4] = maxDigitW;

  for (int i = 0; i < 5; i++) {
    if (buf[i] != prevTimeStr[i] || (i == 2)) { // Always redraw colon
      tft.fillRect(charX[i], 140, charW[i], 45, ILI9341_BLACK);
      char tmp[2] = {buf[i], 0};
      int actualW = u8g2gfx.getUTF8Width(tmp);
      int offset = (charW[i] - actualW) / 2;
      u8g2gfx.setCursor(charX[i] + offset, 180);
      u8g2gfx.print(tmp);
    }
  }
  strcpy(prevTimeStr, buf);
} 

void DrawList() {
  tft.fillRect(0, TITLE_BAR_HIGHT, 320, 240 - TITLE_BAR_HIGHT, ILI9341_BLACK); 
  u8g2gfx.setFont(u8g2_font_10x20_tf);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  int currentPosY = LIST_Y_START;
  for (byte currentItem = 0; currentItem < ITEMS_PER_PAGE; currentItem++) {
    int diagnoseIndex = pageOffset + currentItem;
    if (diagnoseIndex >= numberOfDiagnoses) break;
    bool isSelected = (diagnoseIndex == (selectedItem - 1));
    if (isSelected) {
      tft.fillRect(8, currentPosY - 19, 304, 24, ILI9341_NAVY);
      u8g2gfx.setForegroundColor(ILI9341_YELLOW);
    } else {
      u8g2gfx.setForegroundColor(ILI9341_GREEN);
    }
    u8g2gfx.setCursor(18, currentPosY);
    u8g2gfx.print(diagnoses[diagnoseIndex]);
    currentPosY += ITEM_HEIGHT;
  }
}

void RedrawSelectionOnly() {
  u8g2gfx.setFont(u8g2_font_10x20_tf);
  byte prevLocalIdx = prevSelectedItem - 1 - pageOffset;
  int prevY = LIST_Y_START + prevLocalIdx * ITEM_HEIGHT;
  tft.fillRect(8, prevY - 19, 304, 24, ILI9341_BLACK);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  u8g2gfx.setCursor(18, prevY);
  u8g2gfx.print(diagnoses[prevSelectedItem - 1]);

  byte newLocalIdx = selectedItem - 1 - pageOffset;
  int newY = LIST_Y_START + newLocalIdx * ITEM_HEIGHT;
  tft.fillRect(8, newY - 19, 304, 24, ILI9341_NAVY);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  u8g2gfx.setCursor(18, newY);
  u8g2gfx.print(diagnoses[selectedItem - 1]);
}

// === OPTIMIZED SCROLL HANDLING ====
void ScrollItem(bool direction) {
  prevSelectedItem = selectedItem;
  if (direction == SCROLL_UP) {
    selectedItem++;
    if (selectedItem > numberOfDiagnoses) selectedItem = 1;
  } else {
    selectedItem--;
    if (selectedItem < 1) selectedItem = numberOfDiagnoses;
  }
  byte newPageOffset = CalulatePageOffset(selectedItem);
  if (newPageOffset != pageOffset) {
    pageOffset = newPageOffset;
    DrawList();
  } else {
    RedrawSelectionOnly();
  }
}

byte CalulatePageOffset(byte item) {
  return ((item - 1) / ITEMS_PER_PAGE) * ITEMS_PER_PAGE;
}

// ======= ANGELZ EXECUTION FUNCTION ======
void OpenAngelZ() {
  tft.fillScreen(ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_t0_22b_tf);
  u8g2gfx.setBackgroundColor(ILI9341_BLUE);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  tft.fillRect(0, 0, 320, TITLE_BAR_HIGHT, ILI9341_BLUE);
  int titleWidth = u8g2gfx.getUTF8Width("Angel-Z session");
  u8g2gfx.setCursor((320 - titleWidth) / 2, 24);
  u8g2gfx.print("Angel-Z session");

  Sequences sequences(&gen);

  int totalSteps = ANGELZ_NUMBER_OF_CYCLES * ANGELZ_TOTAL_POINTS;
  unsigned long sessionStart = millis();
  unsigned long sessionDuration = 0;
  for (int cycleNumber = 0; cycleNumber < ANGELZ_NUMBER_OF_CYCLES; ++cycleNumber) {
    for (int pointNumber = 0; pointNumber < ANGELZ_TOTAL_POINTS; ++pointNumber) {
      sessionDuration += (unsigned long)sequences.GetDelaySequence(pointNumber, angelz_valueStart[cycleNumber], angelz_valueMax[cycleNumber], angelz_tau[cycleNumber], ANGELZ_SPLIT_POINT) + 3;
      debugln(sessionDuration);
    }
  }

  int currentStep = 0;
  for (int cycleNumber = 0; cycleNumber < ANGELZ_NUMBER_OF_CYCLES; ++cycleNumber) {
    for (int pointNumber = 0; pointNumber < ANGELZ_TOTAL_POINTS; ++pointNumber) {
      gen.EnableOutput(true);
      sequences.Execute10khzSequence();
      delay(3);
      gen.EnableOutput(false);

      int freqBarIndex = pointNumber * 24 / ANGELZ_TOTAL_POINTS;
      unsigned long elapsed = millis() - sessionStart;
      //
      DisplayTreatInProgressScreenAngelZ(freqBarIndex, elapsed, (currentStep == 0));
      float delayValue = sequences.GetDelaySequence(pointNumber, angelz_valueStart[cycleNumber], angelz_valueMax[cycleNumber], angelz_tau[cycleNumber], ANGELZ_SPLIT_POINT);
      delay((unsigned long)delayValue);

      currentStep++;
    }
  }

  // Show finished message for 3 seconds
  tft.fillScreen(ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_helvB24_te);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  int textWidth = u8g2gfx.getUTF8Width("AngelZ Finished!");
  u8g2gfx.setCursor((320 - textWidth) / 2, 120);
  u8g2gfx.print("AngelZ Finished!");
  delay(3000);

  // Restore menu
  titleLine = (char*)"DIAGNOSES:";
  DrawTitleBar();
  DrawBattery();
  DrawList();
}

// ===ProcessButtonClick() ===
void ProcessButtonClick() {
  if (!isGeneratingFrequency) {
    EEPROM.update(eepromAddress, selectedItem);
    titleLine = (char*)diagnoses[selectedItem - 1];
    isGeneratingFrequency = true;
    btnEnterPressed = false;

    // Check if AngelZ is selected by name
    if (strcmp(diagnoses[selectedItem - 1], "AngelZ") == 0) {
      OpenAngelZ();
      isGeneratingFrequency = false;
      btnEnterPressed = false;
      SetSelectedItem(selectedItem);
      titleLine = (char*)"DIAGNOSES:";
      DrawTitleBar();
      DrawBattery();
      DrawList();
      return;
    }

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
    btnEnterPressed = true;
  }
}

// --- Flicker-Free GenerateFrequency ----
bool GenerateFrequency() {
  int numFreq = 0;
  int freqIndices[10];
  for (int i = 0; i < 10; i++) {
    int freq = frequencies[10 * (selectedItem - 1) + i];
    if (freq > 0) {
      freqIndices[numFreq] = i;
      numFreq++;
    }
  }
  if (numFreq == 0) return false;

  unsigned long totalSessionMs = atoi(treatmentTime) * 60000UL;
  unsigned long sessionStart = millis();

  gen.EnableOutput(true);
  strComplete = (char*)"";
  unsigned long lastSecond = 0;
  prevFreqIndex = -1;
  treatmentScreenDrawn = false;
  //
  for (int i = 0; i < numFreq; i++) {
    unsigned long freqStart = millis();
    intFreqToGenerate = frequencies[10 * (selectedItem - 1) + freqIndices[i]];

    unsigned long elapsed = millis() - sessionStart;
    unsigned long msLeft = (elapsed < totalSessionMs) ? (totalSessionMs - elapsed) : 0;
    DisplayTreatInProgressScreen(freqIndices[i], selectedItem, msLeft, !treatmentScreenDrawn);
    treatmentScreenDrawn = true;
    UpdateSignalIndicator();
    //
    gen.ApplySignal(isSineWave ? SINE_WAVE : SQUARE_WAVE, REG0, intFreqToGenerate);
    //
    unsigned long fragmentMs = totalSessionMs / numFreq;
    while ((millis() - freqStart) < fragmentMs && isGeneratingFrequency) {
      if (btnEnterPressed) {
        gen.EnableOutput(false);
        isSineWave = false;
        return true;
      }

      // encoder handled inside while loop to update signal Type
      if (encoderMoved) {
        int8_t dir = AnalyzeEncoderChange();
        if (dir > 0 && !isSineWave) {
          isSineWave = true;
          gen.ApplySignal(SINE_WAVE, REG0, intFreqToGenerate);
          UpdateSignalIndicator();
        } else if (dir < 0 && isSineWave) {
          isSineWave = false;
          gen.ApplySignal(SQUARE_WAVE, REG0, intFreqToGenerate);
          UpdateSignalIndicator();
        }
      }
      //
      unsigned long now = millis();
      if (now - lastSecond >= 1000) {
        unsigned long elapsed = now - sessionStart;
        unsigned long msLeft = (elapsed < totalSessionMs) ? (totalSessionMs - elapsed) : 0;
        DisplayTreatInProgressScreen(freqIndices[i], selectedItem, msLeft, false);
        // Keep signal indicator visible after every countdown update
        UpdateSignalIndicator();
        lastSecond = now;
      }
    }
    prevFreqIndex = freqIndices[i];
    PlayTone(1);
  }

  gen.EnableOutput(false);
  isGeneratingFrequency = false;
  isSineWave = false;
  strComplete = (char*)"Finished!";
  PlayTone(3);

  tft.fillScreen(ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_helvB24_te);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  int textWidth = u8g2gfx.getUTF8Width("Finished!");
  u8g2gfx.setCursor((320 - textWidth) / 2, 120);
  u8g2gfx.print("Finished!");
  delay(3000);

  titleLine = (char*)"DIAGNOSES:";

  strComplete = (char*)"";
  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);

  return false;
}

void Shutdown() {
  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);
  isGeneratingFrequency = false;
  btnEnterPressed = false;
  debugln("Shutdown initiated");
}

// ===== ENCODER / BUTTON ======
void OnScrollChange() {
  encoderMoved = true;
}

int8_t AnalyzeEncoderChange() {
  encoderMoved = false;
  static uint8_t prev_AB = 0b00000011;
  static int8_t  encoderVal = 0;
  uint8_t A = digitalRead(pinEncoderCW);
  uint8_t B = digitalRead(pinEncoderCCW);
  uint8_t current_AB = (A << 1) | B;
  uint8_t transition = (prev_AB << 2) | current_AB;
  static const int8_t deltaTable[16] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
  int8_t delta = deltaTable[transition];
  encoderVal += delta;
  int8_t result = 0;
  if (encoderVal <= -4) { result = -1; encoderVal = 0; }
  if (encoderVal >=  4) { result =  1; encoderVal = 0; }
  if (encoderVal <= -2) { result = -1; encoderVal += 2; }
  if (encoderVal >=  2) { result =  1; encoderVal -= 2; }
  prev_AB = current_AB;
  if (result != 0) {
    debug("Step: ");     debug(result);
    debug("  AB: ");     debug(current_AB);
    debug("  encVal: "); debugln(encoderVal);
  }
  return result;
}

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

void DrawIntroFrame() {
  tft.fillRoundRect(FRAME_X, FRAME_Y, FRAME_W, FRAME_H, 24, GOLD1);
  tft.drawRoundRect(FRAME_X+5, FRAME_Y+5, FRAME_W-10, FRAME_H-10, 18, GOLD3);
  tft.drawRoundRect(FRAME_X+12, FRAME_Y+12, FRAME_W-24, FRAME_H-24, 12, GOLD4);
  for (int i = 0; i < 20; i++) {
    tft.drawPixel(FRAME_X+12+i, FRAME_Y+12+sin(i*0.4)*8, GOLD2);
    tft.drawPixel(FRAME_X+FRAME_W-12-i, FRAME_Y+12+sin(i*0.4)*8, GOLD2);
    tft.drawPixel(FRAME_X+12+i, FRAME_Y+FRAME_H-12-sin(i*0.4)*8, GOLD2);
    tft.drawPixel(FRAME_X+FRAME_W-12-i, FRAME_Y+FRAME_H-12-sin(i*0.4)*8, GOLD2);
  }
  tft.fillRoundRect(FRAME_X+20, FRAME_Y+20, FRAME_W-40, FRAME_H-40, 8, ILI9341_BLACK);
}

void DrawGoldenCrown(int cx, int topY) {
  const int w = 52;
  const int h = 28;
  const int baseH = 8;
  int x0 = cx - w / 2;
  int y0 = topY;
  tft.fillRoundRect(x0, y0 + h - baseH, w, baseH, baseH / 3, GOLD3);
  tft.drawRoundRect(x0, y0 + h - baseH, w, baseH, baseH / 3, GOLD1);
  tft.drawFastHLine(x0 + 3, y0 + h - baseH, w - 6, GOLD4);
  tft.drawFastHLine(x0 + 2, y0 + h - 1, w - 4, GOLD2);
  for (int i = 0; i < 5; i++) {
    int gemX = x0 + w * (i + 1) / 6;
    int gemY = y0 + h - baseH / 2;
    tft.fillCircle(gemX, gemY, baseH / 3, GOLD1);
    tft.drawCircle(gemX, gemY, baseH / 3, GOLD4);
  }
  int peakBaseY = y0 + h - baseH;
  int leftX   = x0 + w / 4;
  int centerX = cx;
  int rightX  = x0 + 3 * w / 4;
  int leftH   = 12;
  int centerH = 16;
  int rightH  = 12;
  tft.fillTriangle(leftX, peakBaseY, leftX - 6, peakBaseY - leftH, leftX + 6, peakBaseY - leftH, GOLD2);
  tft.fillCircle(leftX, peakBaseY - leftH - 2, 3, GOLD4);
  tft.drawCircle(leftX, peakBaseY - leftH - 2, 3, GOLD1);
  tft.fillTriangle(rightX, peakBaseY, rightX - 6, peakBaseY - rightH, rightX + 6, peakBaseY - rightH, GOLD2);
  tft.fillCircle(rightX, peakBaseY - rightH - 2, 3, GOLD4);
  tft.drawCircle(rightX, peakBaseY - rightH - 2, 3, GOLD1);
  tft.fillTriangle(centerX, peakBaseY, centerX - 8, peakBaseY - centerH, centerX + 8, peakBaseY - centerH, GOLD1);
  int crossX = centerX;
  int crossY = peakBaseY - centerH - 3;
  tft.drawLine(crossX, crossY - 3, crossX, crossY + 3, GOLD4);
  tft.drawLine(crossX - 3, crossY, crossX + 3, crossY, GOLD4);
  tft.fillCircle(crossX, crossY, 2, GOLD3);
  for (int i = 0; i <= 12; i++) {
    float t = i / 12.0;
    int x = leftX + t * (centerX - leftX);
    int y = peakBaseY - leftH * (1 - t) - centerH * t + 4 * sin(t * PI);
    tft.drawPixel(x, y, GOLD2);
  }
  for (int i = 0; i <= 12; i++) {
    float t = i / 12.0;
    int x = centerX + t * (rightX - centerX);
    int y = peakBaseY - centerH * (1 - t) - rightH * t + 4 * sin(t * PI);
    tft.drawPixel(x, y, GOLD2);
  }
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

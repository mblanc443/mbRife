// Rife Machine - Arduino Mega2560 + ILI9341 + AD9833 + SD card + AngelZ
// Pin 8 signal type indicator, SD card support, AngelZ unchanged
// Pin A1 connected to output which measures level of output signal during treatment
// VU-style level indicator: 20 vertical bars, 14 green + 6 red, gray when no signal
// ADC for A1 powered by internal reference voltage (commented out lines 1450-1451)
// Pin 11 used to block output to avoid of output spikes 
#include <EEPROM.h>
#include <AD9833.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include <avr/pgmspace.h>
#include <math.h>

#define DEBUG 0
#if DEBUG == 1
  #define debug(x) Serial.print(x)
  #define debugln(x) Serial.println(x)
#else
  #define debug(x)
  #define debugln(x)
#endif

// Pins
#define pinEncoderCW     2
#define pinEncoderCCW    3
#define pinBeepOut       4
#define pinShutdown2     5
#define pinShutdown1     6
#define pinSDPower       7 
#define pinSignalType    8
#define pinGenCS         9
#define SD_CS           10  // HW lib requirment as default is 53 used by ttf
#define pinOutputPause  11  // blocks output signal between frequencies - removes spikes
#define pinBtnEnter     21
#define pinBatteryLevel A0
#define pinLevelInput   A1
#define TFT_CS          53
#define TFT_DC          48
#define TFT_RST         49

// ILI9341 (Hardware SPI on Mega: SCK=52, MOSI=51)
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2gfx;

// AD9833
AD9833 gen(pinGenCS);

// Gold color palette
#define GOLD1  tft.color565(255, 215,   0)
#define GOLD2  tft.color565(218, 165,  32)
#define GOLD3  tft.color565(184, 134,  11)
#define GOLD4  tft.color565(255, 236, 139)

// Level indicator VU colors
#define LEVEL_GREEN_ACTIVE   tft.color565(0,  220,  0)
#define LEVEL_GREEN_DIM      tft.color565(0,   60,  0)
#define LEVEL_RED_ACTIVE     tft.color565(255, 30, 30)
#define LEVEL_RED_DIM        tft.color565(60,   0,  0)
#define LEVEL_GRAY_NOSIGNAL  tft.color565(80,  80, 80)

// capacity
#define WARN_DIAGNOSES_THRESHOLD 100
#define NAME_MAX_LEN              28

// Battery voltage divider
const float R1 = 32000.0; // ohm
const float R2 = 8000.0;  // ohm
const float referenceVoltage = 5.0;

// Default diagnosis names with time per frequency in seconds
const char* default_diagnoses_raw[] = {
  "Day1-Love expands:780",  //light radiates
  "Day2-Cellular and DNA repair:480",
  "Day3-Release Fear, let go:660",
  "H.Clark Zapper:120",
  "AngelZ:0"
};

// Frequencies in PROGMEM
const int frequencies[] PROGMEM = {
  465,0,0,0,0,0,0,0,0,0,
  528,0,0,0,0,0,0,0,0,0,
  417,0,0,0,0,0,0,0,0,0, 
  32000,1150,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0
};

// Dynamic runtime arrays
char     **diagnosis_names       = NULL;
int      *diagnosis_time_sec     = NULL;
int      **diagnosis_frequencies = NULL;
int      num_diagnoses           = 0;
int      capacity_diagnoses      = 0;

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

bool isGeneratingFrequency = false;
uint16_t intFreqToGenerate =     0;
bool isSineWave            =  true;   // *** CHANGED *** default SIN instead of Square

volatile bool encoderMoved    = false;
volatile bool btnEnterPressed = false;
volatile int  buttonOutput    =     0;

enum {STATE_NORMAL, STATE_SHORT, STATE_LONG};
long LONG_DELTA     = 1500UL;
long DEBOUNCE_DELTA =   30UL;

byte eepromAddress = 0;

// Frame / list constants
const int FRAME_X =   2;
const int FRAME_Y =   2;
const int FRAME_W = 316;
const int FRAME_H = 236;
const int LIST_Y_START    = 50;
const int ITEM_HEIGHT     = 24;
const int TITLE_BAR_HIGHT = 30;

// Level indicator constants - VU style with 20 vertical bars
// Indicator spans full screen width (320px), level text above bars
#define LEVEL_LABEL_Y      210
#define LEVEL_LABEL_H       18
#define LEVEL_BAR_Y        228
#define LEVEL_BAR_H         12
#define LEVEL_NUM_BARS      20
#define LEVEL_TOTAL_W      320
#define LEVEL_BAR_WIDTH    (LEVEL_TOTAL_W / LEVEL_NUM_BARS)
#define LEVEL_BAR_GAP        2
#define LEVEL_GREEN_BARS    14
#define LEVEL_RED_BARS       6

static int  prevFreqIndex        =      -1;
static char prevTimeStr[6]       = "99:99";
static char prevAngelZTimeStr[6] = "00:00";
static int  prevLevelValue       =      -1;
static int  prevLevelBars        =      -1;
static bool prevLevelNoSignal    =    true;
static bool treatmentScreenDrawn =   false;

// ==== AngelZ Constants ====
#define     ANGELZ_TOTAL_POINTS           48
const int   ANGELZ_SPLIT_POINT      =     24;
const int   ANGELZ_NUMBER_OF_CYCLES =     59;
const float ANGELZ_DELAY_SCALE      = 3.734f;

float angelz_valueStart[ANGELZ_NUMBER_OF_CYCLES] = {
  20.0, 15.0, 18.0, 25.0, 22.0, 30.0, 21.0, 35.0, 23.0, 40.0,
  24.0, 45.0, 16.0, 27.0, 19.0, 31.0, 13.0, 28.0, 14.0, 34.0,
  12.0, 39.0, 10.0, 42.0, 11.0, 37.0, 9.0, 33.0, 8.0, 36.0,
  7.0, 29.0, 6.0, 32.0, 5.0, 38.0, 4.0, 41.0, 3.0, 44.0,
  2.0, 43.0, 1.0, 46.0, 0.5, 48.0, 0.2, 50.0, 0.1, 52.0,
  0.0, 53.0, 12.0, 17.0, 22.0, 28.0, 34.0, 41.0, 45.0
};

float angelz_valueMax[ANGELZ_NUMBER_OF_CYCLES] = {
  52.6, 45.0, 55.0, 60.0, 50.0, 65.0, 53.0, 70.0, 56.0, 75.0,
  59.0, 80.0, 48.0, 62.0, 51.0, 64.0, 49.0, 67.0, 50.0, 71.0,
  46.0, 73.0, 47.0, 76.0, 54.0, 78.0, 57.0, 79.0, 58.0, 82.0,
  61.0, 85.0, 63.0, 88.0, 65.0, 90.0, 68.0, 92.0, 66.0, 95.0,
  69.0, 97.0, 72.0, 99.0, 74.0, 100.0, 77.0, 102.0, 80.0, 105.0,
  82.0, 110.0, 55.0, 60.0, 65.0, 70.0, 75.0, 85.0, 90.0
};

float angelz_tau[ANGELZ_NUMBER_OF_CYCLES] = {
  5.0, 7.0, 4.5, 6.0, 3.5, 5.5, 4.0, 6.5, 3.0, 5.0,
  4.3, 6.8, 3.8, 6.1, 3.2, 6.4, 4.2, 5.6, 3.9, 6.7,
  4.4, 5.9, 4.1, 6.9, 3.6, 5.7, 3.3, 6.3, 3.1, 6.2,
  4.6, 6.6, 3.7, 5.8, 4.9, 7.0, 3.4, 6.5, 4.8, 7.1,
  5.1, 7.2, 3.9, 6.0, 3.5, 6.8, 4.2, 6.3, 5.0, 7.3,
  5.2, 7.4, 4.0, 5.5, 6.0, 6.5, 5.7, 6.8, 7.1
};


// FORWARD DECLARATIONS
void DisplayErrorMessage(const char* message, uint16_t color = ILI9341_RED);
void DisplayTreatInProgressScreen(int currentFreqIndex, int selectedItem, unsigned long msLeft, bool forceFull = false);
void DisplayTreatInProgressScreenAngelZ(int freqBarIndex, unsigned long msLeft, bool forceFull = false);
void DisplayIntroScreen(void);
void DrawTitleBar();
void DrawBattery();
void DrawList();
void RedrawSelectionOnly();
void ScrollItem(bool direction);
byte CalulatePageOffset(byte item);
void SetSelectedItem(byte item);
void UpdateSignalIndicator();
void ProcessButtonClick();
bool GenerateFrequency();
void OpenAngelZ();
void Shutdown();
void OnScrollChange();
int8_t AnalyzeEncoderChange();
void OnButtonPress();
void DrawIntroFrame();
void DrawGoldenCrown(int cx, int topY);
float MeasureBatteryVoltage();
void PlayTone(int n);
bool AddDiagnosis(const char* nameWithTime, const int* freqs);
bool EnsureDiagnosisCapacity();
void FreeDiagnosisArrays();
void LoadDefaultDiagnoses();
void CreateDefaultSettingsFile();
void InitializeSDAndSettings();
int  ReadLevelValue();
void DrawLevelIndicator(int level, bool noSignal, bool forceFull = false);

// AngelZ Sequences Class
class Sequences {
  private:
    AD9833 *m_pgen;
  public:
    Sequences() {}
    Sequences(AD9833 *pGen);
    void Execute10khzSequence();
    float GetDelaySequence(int currentPoint, float valueStart, float valueMax, float tau, int splitPoint);
};

Sequences::Sequences(AD9833 *pGen) : m_pgen(pGen) {}

void Sequences::Execute10khzSequence() {
  for (int pulseNum = 0; pulseNum < 20; pulseNum++) {
    m_pgen->EnableOutput(true);
    m_pgen->ApplySignal(SQUARE_WAVE, REG0, 10000);
  }
}

float Sequences::GetDelaySequence(int currentPoint, float valueStart, float valueMax, float tau, int splitPoint) {
  float delayMs;
  if (currentPoint < splitPoint) {
    delayMs = valueStart + (valueMax - valueStart) * (1 - exp(-currentPoint / tau));
  } else {
    delayMs = valueMax * exp(-(currentPoint - splitPoint) / tau);
  }
  return delayMs * ANGELZ_DELAY_SCALE;
}

// DYNAMIC ARRAY MANAGEMENT
bool EnsureDiagnosisCapacity() {
  if (num_diagnoses < capacity_diagnoses) return true;

  int newCapacity = capacity_diagnoses + 10;

  char **newNames = (char**)realloc(diagnosis_names, newCapacity * sizeof(char*));
  if (!newNames) return false;
  diagnosis_names = newNames;

  for (int i = capacity_diagnoses; i < newCapacity; i++) {
    diagnosis_names[i] = (char*)malloc((NAME_MAX_LEN + 1) * sizeof(char));
    if (!diagnosis_names[i]) return false;
    diagnosis_names[i][0] = '\0';
  }

  int *newTimes = (int*)realloc(diagnosis_time_sec, newCapacity * sizeof(int));
  if (!newTimes) return false;
  diagnosis_time_sec = newTimes;
  for (int i = capacity_diagnoses; i < newCapacity; i++) {
    diagnosis_time_sec[i] = 60;
  }

  int **newFreqs = (int**)realloc(diagnosis_frequencies, newCapacity * sizeof(int*));
  if (!newFreqs) return false;
  diagnosis_frequencies = newFreqs;

  for (int i = capacity_diagnoses; i < newCapacity; i++) {
    diagnosis_frequencies[i] = (int*)malloc(10 * sizeof(int));
    if (!diagnosis_frequencies[i]) return false;
    memset(diagnosis_frequencies[i], 0, 10 * sizeof(int));
  }

  capacity_diagnoses = newCapacity;
  return true;
}

void FreeDiagnosisArrays() {
  if (diagnosis_names) {
    for (int i = 0; i < capacity_diagnoses; i++) {
      if (diagnosis_names[i]) free(diagnosis_names[i]);
    }
    free(diagnosis_names);
    diagnosis_names = NULL;
  }
  if (diagnosis_time_sec) {
    free(diagnosis_time_sec);
    diagnosis_time_sec = NULL;
  }
  if (diagnosis_frequencies) {
    for (int i = 0; i < capacity_diagnoses; i++) {
      if (diagnosis_frequencies[i]) free(diagnosis_frequencies[i]);
    }
    free(diagnosis_frequencies);
    diagnosis_frequencies = NULL;
  }
  num_diagnoses = 0;
  capacity_diagnoses = 0;
}

bool AddDiagnosis(const char* nameWithTime, const int* freqs) {
  if (!EnsureDiagnosisCapacity()) return false;
  //
  if (num_diagnoses == WARN_DIAGNOSES_THRESHOLD) {
    DisplayErrorMessage("Diagnoses count exceeded 100 - continuing...", ILI9341_YELLOW);
    delay(2000);
  }

  String s = String(nameWithTime);
  int colonPos = s.lastIndexOf(':');
  String nameStr;
  int timeSec = 60;

  if (colonPos > 0) {
    nameStr = s.substring(0, colonPos);
    String timeStr = s.substring(colonPos + 1);
    timeStr.trim();
    nameStr.trim();
    timeSec = timeStr.toInt();
    if (timeSec <= 0 && nameStr != "AngelZ") timeSec = 60;
  } else {
    nameStr = s;
    nameStr.trim();
  }

  strncpy(diagnosis_names[num_diagnoses], nameStr.c_str(), NAME_MAX_LEN);
  diagnosis_names[num_diagnoses][NAME_MAX_LEN] = '\0';
  diagnosis_time_sec[num_diagnoses] = timeSec;

  for (int j = 0; j < 10; j++) {
    diagnosis_frequencies[num_diagnoses][j] = freqs[j];
  }

  num_diagnoses++;
  return true;
}

// LEVEL INDICATOR - VU Style with 20 Vertical Bars
int ReadLevelValue() {
  long sum = 0;
  for (int i = 0; i < 4; i++) sum += analogRead(pinLevelInput);
  int avg = sum >> 2;
  int val = map(avg, 0, 1023, 0, LEVEL_NUM_BARS);
  if (val < 0) val = 0;
  if (val > LEVEL_NUM_BARS) val = LEVEL_NUM_BARS;
  return val;
}

void DrawLevelIndicator(int level, bool noSignal, bool forceFull) {
  int litBars = level;
  if (litBars < 0) litBars = 0;
  if (litBars > LEVEL_NUM_BARS) litBars = LEVEL_NUM_BARS;

  bool needFullRedraw = forceFull || (noSignal != prevLevelNoSignal);

  if (needFullRedraw) {
    for (int i = 0; i < LEVEL_NUM_BARS; i++) {
      int x = i * LEVEL_BAR_WIDTH;
      int barW = LEVEL_BAR_WIDTH - LEVEL_BAR_GAP;
      uint16_t color;

      if (noSignal) {
        color = LEVEL_GRAY_NOSIGNAL;
      } else if (i < litBars) {
        if (i < LEVEL_GREEN_BARS) {
          color = LEVEL_GREEN_ACTIVE;
        } else {
          color = LEVEL_RED_ACTIVE;
        }
      } else {
        if (i < LEVEL_GREEN_BARS) {
          color = LEVEL_GREEN_DIM;
        } else {
          color = LEVEL_RED_DIM;
        }
      }

      tft.fillRect(x, LEVEL_BAR_Y, barW, LEVEL_BAR_H, color);
    }

    // Draw label above bars
    tft.fillRect(0, LEVEL_LABEL_Y, 320, LEVEL_LABEL_H, ILI9341_BLACK);
    u8g2gfx.setFont(u8g2_font_helvB14_te);
    u8g2gfx.setBackgroundColor(ILI9341_BLACK);
    if (noSignal) {
      u8g2gfx.setForegroundColor(LEVEL_GRAY_NOSIGNAL);
      const char* noSigStr = "No Signal";
      int tw = u8g2gfx.getUTF8Width(noSigStr);
      u8g2gfx.setCursor((320 - tw) / 2, LEVEL_LABEL_Y + 15);
      u8g2gfx.print(noSigStr);
    } else {
      if (litBars >= 16) {
        u8g2gfx.setForegroundColor(LEVEL_RED_ACTIVE);
      } else {
        u8g2gfx.setForegroundColor(ILI9341_WHITE);
      }
      char digBuf[16];
      sprintf(digBuf, "Level: %d", litBars);
      int tw = u8g2gfx.getUTF8Width(digBuf);
      u8g2gfx.setCursor((320 - tw) / 2, LEVEL_LABEL_Y + 15);
      u8g2gfx.print(digBuf);
    }

    prevLevelBars = litBars;
    prevLevelValue = level;
    prevLevelNoSignal = noSignal;
    return;
  }

  // Incremental update - only redraw bars that changed state
  if (!noSignal && litBars != prevLevelBars) {
    int fromBar = min(litBars, prevLevelBars);
    int toBar = max(litBars, prevLevelBars);
    if (prevLevelBars < 0) { fromBar = 0; toBar = LEVEL_NUM_BARS; }

    for (int i = fromBar; i < toBar; i++) {
      int x = i * LEVEL_BAR_WIDTH;
      int barW = LEVEL_BAR_WIDTH - LEVEL_BAR_GAP;
      uint16_t color;

      if (i < litBars) {
        color = (i < LEVEL_GREEN_BARS) ? LEVEL_GREEN_ACTIVE : LEVEL_RED_ACTIVE;
      } else {
        color = (i < LEVEL_GREEN_BARS) ? LEVEL_GREEN_DIM : LEVEL_RED_DIM;
      }

      tft.fillRect(x, LEVEL_BAR_Y, barW, LEVEL_BAR_H, color);
    }

    prevLevelBars = litBars;
  }

  // Update label if value changed
  if (!noSignal && (level != prevLevelValue || forceFull)) {
    tft.fillRect(0, LEVEL_LABEL_Y, 320, LEVEL_LABEL_H, ILI9341_BLACK);
    u8g2gfx.setFont(u8g2_font_helvB14_te);
    u8g2gfx.setBackgroundColor(ILI9341_BLACK);
    if (litBars >= 16) {
      u8g2gfx.setForegroundColor(LEVEL_RED_ACTIVE);
    } else {
      u8g2gfx.setForegroundColor(ILI9341_WHITE);
    }
    char digBuf[16];
    sprintf(digBuf, "Level: %d", litBars);
    int tw = u8g2gfx.getUTF8Width(digBuf);
    u8g2gfx.setCursor((320 - tw) / 2, LEVEL_LABEL_Y + 15);
    u8g2gfx.print(digBuf);
    prevLevelValue = level;
  }
}


// ============================================================
// DISPLAY HELPER FUNCTIONS
void DisplayErrorMessage(const char* message, uint16_t color) {
  tft.fillRect(0, TITLE_BAR_HIGHT, 320, 240 - TITLE_BAR_HIGHT, ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_8x13_t_cyrillic);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(color);
  int w = u8g2gfx.getUTF8Width(message);
  int x = (w < 320) ? (320 - w) / 2 : 2;
  u8g2gfx.setCursor(x, 120);
  u8g2gfx.print(message);
}

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

void UpdateSignalIndicator() {
  u8g2gfx.setFont(u8g2_font_t0_22b_tf);
  u8g2gfx.setBackgroundColor(ILI9341_BLUE);
  tft.fillRect(260, 0, 60, TITLE_BAR_HIGHT, ILI9341_BLUE);
  u8g2gfx.setForegroundColor(ILI9341_WHITE);
  const char* sigStr = isSineWave ? "SIN" : "_||_";
  int sigWidth = u8g2gfx.getUTF8Width(sigStr);
  u8g2gfx.setCursor(320 - sigWidth - 4, 24);
  u8g2gfx.print(sigStr);
}

void SetSelectedItem(byte item) {
  selectedItem = item;
  pageOffset = CalulatePageOffset(item);
  prevSelectedItem = item;
  DrawList();
}


// TREATMENT IN-PROGRESS SCREEN (regular) - with VU level bars
// Title bar shows "<diagnose>:<configured_time>" (static, drawn once)
void DisplayTreatInProgressScreen(int currentFreqIndex, int selItem, unsigned long msLeft, bool forceFull) {
  const int SCREEN_W = 320;
  const int SCREEN_H = 240;
  const int LEFT_W = SCREEN_W / 3;
  const int RIGHT_W = SCREEN_W - LEFT_W;
  const int BAR_AREA_H = LEVEL_LABEL_Y - TITLE_BAR_HIGHT;
  const int BAR_H = BAR_AREA_H / 10;
  const int BAR_W = LEFT_W - 8;
  const int BAR_X = 4;
  const int BAR_Y0 = TITLE_BAR_HIGHT;

  int diagIdx = selItem - 1;

  if (forceFull || !treatmentScreenDrawn) {
    tft.fillScreen(ILI9341_BLACK);

    // Title bar: "<diagnose>:<configured_time>" - static, drawn once
    tft.fillRect(0, 0, SCREEN_W, TITLE_BAR_HIGHT, ILI9341_BLUE);
    u8g2gfx.setFont(u8g2_font_helvB14_te);
    u8g2gfx.setBackgroundColor(ILI9341_BLUE);
    u8g2gfx.setForegroundColor(ILI9341_YELLOW);
    char titleBuf[48];
    snprintf(titleBuf, sizeof(titleBuf), "%s:%d", diagnosis_names[diagIdx], diagnosis_time_sec[diagIdx]);
    int titleWidth = u8g2gfx.getUTF8Width(titleBuf);
    int titleX = (SCREEN_W - titleWidth) / 2;
    if (titleX < 2) titleX = 2;
    u8g2gfx.setCursor(titleX, 22);
    u8g2gfx.print(titleBuf);

    // Signal type indicator on title bar
    u8g2gfx.setFont(u8g2_font_10x20_t_cyrillic);
    u8g2gfx.setBackgroundColor(ILI9341_BLUE);
    u8g2gfx.setForegroundColor(ILI9341_YELLOW);
    const char* sigStr = isSineWave ? "SIN " : "_||_";
    int sigWidth = u8g2gfx.getUTF8Width(sigStr);
    u8g2gfx.setCursor(320 - sigWidth - 4, 24);
    u8g2gfx.print(sigStr);

    for (int i = 0; i < 10; i++) {
      int freq = diagnosis_frequencies[diagIdx][i];
      int y = BAR_Y0 + i * BAR_H;
      uint16_t barColor;
      if (freq == 0) {
        barColor = tft.color565(20, 20, 20);
      } else {
        barColor = (i == currentFreqIndex) ? ILI9341_GREENYELLOW : ILI9341_DARKGREEN;
      }
      uint16_t borderColor = (i == currentFreqIndex && freq > 0) ? ILI9341_WHITE : barColor;
      tft.fillRect(BAR_X, y, BAR_W, BAR_H - 1, barColor);
      if (i == currentFreqIndex && freq > 0)
        tft.drawRect(BAR_X, y, BAR_W, BAR_H - 1, borderColor);
      u8g2gfx.setFont(u8g2_font_helvB14_te);
      u8g2gfx.setBackgroundColor(barColor);
      u8g2gfx.setForegroundColor(ILI9341_PINK);
      char freqStr[8];
      if (freq > 0) sprintf(freqStr, "%d", freq);
      else strcpy(freqStr, "-");
      int textWidth = u8g2gfx.getUTF8Width(freqStr);
      tft.fillRect(BAR_X + 2, y + 2, BAR_W - 4, BAR_H - 4, barColor);
      u8g2gfx.setCursor(BAR_X + (BAR_W - textWidth) / 2, y + BAR_H / 2 + 6);
      u8g2gfx.print(freqStr);
    }

    // Draw VU level indicator (full) - initially show "no signal" gray
    DrawLevelIndicator(0, true, true);

    u8g2gfx.setFont(u8g2_font_helvB24_te);
    u8g2gfx.setBackgroundColor(ILI9341_BLACK);
    u8g2gfx.setForegroundColor(ILI9341_WHITE);
    int labelWidth = u8g2gfx.getUTF8Width("Remaining");
    int labelY = 100;
    u8g2gfx.setCursor(LEFT_W + (RIGHT_W - labelWidth) / 2, labelY);
    u8g2gfx.print("Remaining");

    strcpy(prevTimeStr, "99:99");
    prevFreqIndex = currentFreqIndex;
    treatmentScreenDrawn = true;
  }

  if (prevFreqIndex != currentFreqIndex) {
    if (prevFreqIndex >= 0 && prevFreqIndex < 10) {
      int freq = diagnosis_frequencies[diagIdx][prevFreqIndex];
      int y = BAR_Y0 + prevFreqIndex * BAR_H;
      uint16_t barColor = (freq == 0) ? tft.color565(20, 20, 20) : ILI9341_DARKGREEN;
      tft.fillRect(BAR_X, y, BAR_W, BAR_H - 1, barColor);
      u8g2gfx.setFont(u8g2_font_helvB14_te);
      u8g2gfx.setBackgroundColor(barColor);
      u8g2gfx.setForegroundColor(ILI9341_PINK);
      char freqStr[8];
      if (freq > 0) sprintf(freqStr, "%d", freq); else strcpy(freqStr, "-");
      int textWidth = u8g2gfx.getUTF8Width(freqStr);
      tft.fillRect(BAR_X + 2, y + 2, BAR_W - 4, BAR_H - 4, barColor);
      u8g2gfx.setCursor(BAR_X + (BAR_W - textWidth) / 2, y + BAR_H / 2 + 6);
      u8g2gfx.print(freqStr);
    }
    if (currentFreqIndex >= 0 && currentFreqIndex < 10) {
      int freq = diagnosis_frequencies[diagIdx][currentFreqIndex];
      int y = BAR_Y0 + currentFreqIndex * BAR_H;
      uint16_t barColor = (freq > 0) ? ILI9341_GREENYELLOW : tft.color565(20, 20, 20);
      tft.fillRect(BAR_X, y, BAR_W, BAR_H - 1, barColor);
      if (freq > 0) tft.drawRect(BAR_X, y, BAR_W, BAR_H - 1, ILI9341_WHITE);
      u8g2gfx.setFont(u8g2_font_helvB14_te);
      u8g2gfx.setBackgroundColor(barColor);
      u8g2gfx.setForegroundColor(ILI9341_PINK);
      char freqStr[8];
      if (freq > 0) sprintf(freqStr, "%d", freq); else strcpy(freqStr, "-");
      int textWidth = u8g2gfx.getUTF8Width(freqStr);
      tft.fillRect(BAR_X + 2, y + 2, BAR_W - 4, BAR_H - 4, barColor);
      u8g2gfx.setCursor(BAR_X + (BAR_W - textWidth) / 2, y + BAR_H / 2 + 6);
      u8g2gfx.print(freqStr);
    }
    prevFreqIndex = currentFreqIndex;
  }

  // Update VU level indicator (reads A1 each call) - active signal mode
  int lv = ReadLevelValue();
  DrawLevelIndicator(lv, false, false);

  // Countdown timer (big digits on right side)
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
    if (buf[i] != prevTimeStr[i] || (i == 2)) {
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


// ANGELZ TREATMENT SCREEN - no level indicator, clean screen
void DisplayTreatInProgressScreenAngelZ(int freqBarIndex, unsigned long msLeft, bool forceFull) {
  static bool titleDrawn = false;
  if (forceFull || !titleDrawn) {
    tft.fillRect(0, 0, 320, TITLE_BAR_HIGHT, ILI9341_BLUE);
    u8g2gfx.setFont(u8g2_font_t0_22b_tf);
    u8g2gfx.setBackgroundColor(ILI9341_BLUE);
    u8g2gfx.setForegroundColor(ILI9341_YELLOW);
    int titleWidth = u8g2gfx.getUTF8Width("Angel-Z SESSION");
    u8g2gfx.setCursor((320 - titleWidth) / 2, 24);
    u8g2gfx.print("Angel-Z SESSION");
    titleDrawn = true;
  }

  const int BAR_X = 16;
  const int BAR_Y = 200;
  const int RECT_W = 12;
  const int RECT_H = 40;
  const int NUM_RECTS = 24;

  static int prevBarIndex = -1;
  if (forceFull || prevBarIndex != freqBarIndex) {
    int from = 0, to = NUM_RECTS - 1;
    if (!forceFull && prevBarIndex != -1) {
      from = min(prevBarIndex, freqBarIndex);
      to   = max(prevBarIndex, freqBarIndex);
    }
    for (int i = from; i <= to; i++) {
      int x = BAR_X + i * RECT_W;
      uint16_t color = (i <= freqBarIndex) ? ((i < 18) ? ILI9341_GREEN : ILI9341_RED) : ILI9341_DARKGREY;
      tft.fillRect(x, BAR_Y, RECT_W - 2, RECT_H, color);
    }
    prevBarIndex = freqBarIndex;
  }

  unsigned long secondsLeft = msLeft / 1000;
  int minLeft = secondsLeft / 60;
  int secLeft = secondsLeft % 60;
  char buf[6];
  sprintf(buf, "%02d:%02d", minLeft, secLeft);

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

// SD CARD FUNCTIONS
void CreateDefaultSettingsFile() {
  SD.remove("settings.txt");
  File file = SD.open("settings.txt", FILE_WRITE);
  if (!file) return;

  file.println("## Diagnoses section - format: Name:seconds,freq1,freq2,...,freq10");
  file.println("## seconds = time per each non-zero frequency");
  file.println("## Zero frequencies are skipped");
  file.println("[diagnoses]");

  int n = sizeof(default_diagnoses_raw) / sizeof(default_diagnoses_raw[0]);
  for (int i = 0; i < n; i++) {
    file.print(default_diagnoses_raw[i]);
    for (int j = 0; j < 10; j++) {
      file.print(",");
      file.print(pgm_read_word(&frequencies[10 * i + j]));
    }
    file.println();
  }
  file.close();
}

void LoadDefaultDiagnoses() {
  FreeDiagnosisArrays();

  int n = sizeof(default_diagnoses_raw) / sizeof(default_diagnoses_raw[0]);
  for (int i = 0; i < n; i++) {
    int freqs[10];
    for (int j = 0; j < 10; j++) {
      freqs[j] = pgm_read_word(&frequencies[10 * i + j]);
    }
    if (!AddDiagnosis(default_diagnoses_raw[i], freqs)) {
      DisplayErrorMessage("RAM full loading defaults", ILI9341_RED);
      delay(2000);
      break;
    }
  }
}

void InitializeSDAndSettings() {
  if (!SD.begin()) {
    DisplayErrorMessage("SD card is not present");
    delay(3000);
    LoadDefaultDiagnoses();
    return;
  }

  File file = SD.open("settings.txt", FILE_READ);
  bool fileWasMissing = !file;

  if (fileWasMissing) {
    DisplayErrorMessage("settings.txt missing - creating...", ILI9341_YELLOW);
    delay(2000);
    CreateDefaultSettingsFile();
    DisplayErrorMessage("settings.txt created", ILI9341_GREEN);
    delay(2000);
    file = SD.open("settings.txt", FILE_READ);
  }

  if (!file) {
    LoadDefaultDiagnoses();
    return;
  }

  bool hasDiagnosesSection = false;
  String currentSection = "";
  FreeDiagnosisArrays();
  bool diagnosesHealthy = true;
  bool warnShown = false;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.startsWith("##") || line.length() == 0) continue;

    if (line.startsWith("[")) {
      currentSection = line;
      if (line.indexOf("[diagnoses]") != -1) hasDiagnosesSection = true;
      continue;
    }

    if (currentSection == "[diagnoses]") {
      if (num_diagnoses == WARN_DIAGNOSES_THRESHOLD && !warnShown) {
        DisplayErrorMessage("Diagnoses > 100, still loading...", ILI9341_YELLOW);
        delay(2000);
        warnShown = true;
      }

      int commaCount = 0;
      for (unsigned int i = 0; i < line.length(); i++) {
        if (line[i] == ',') commaCount++;
      }
      if (commaCount != 10) {
        diagnosesHealthy = false;
        debug("Bad comma count: "); debugln(line);
        continue;
      }

      int firstComma = line.indexOf(',');
      if (firstComma <= 0) {
        diagnosesHealthy = false;
        continue;
      }

      String nameWithTime = line.substring(0, firstComma);
      nameWithTime.trim();

      if (nameWithTime.indexOf(':') < 0) {
        nameWithTime = nameWithTime + ":60";
      }

      bool freqOk = true;
      int freqs[10];
      int pos = firstComma + 1;
      for (int f = 0; f < 10; f++) {
        int nextComma = (f < 9) ? line.indexOf(',', pos) : (int)line.length();
        if (nextComma == -1 && f < 9) { freqOk = false; break; }
        String freqStr = line.substring(pos, nextComma);
        freqStr.trim();
        if (freqStr.length() == 0) { freqOk = false; break; }
        bool isNum = true;
        for (unsigned int k = 0; k < freqStr.length(); k++) {
          if (!isdigit(freqStr[k])) { isNum = false; break; }
        }
        if (!isNum) { freqOk = false; break; }
        freqs[f] = freqStr.toInt();
        pos = nextComma + 1;
      }

      if (!freqOk) {
        diagnosesHealthy = false;
        debug("Bad freq in: "); debugln(line);
        continue;
      }

      if (!AddDiagnosis(nameWithTime.c_str(), freqs)) {
        DisplayErrorMessage("RAM exhausted, stopped loading", ILI9341_RED);
        delay(2000);
        break;
      }
    }
  }
  file.close();

  if (!hasDiagnosesSection) {
    DisplayErrorMessage("No [diagnoses] section - creating...", ILI9341_RED);
    delay(3000);
    CreateDefaultSettingsFile();
    DisplayErrorMessage("[diagnoses] section created", ILI9341_GREEN);
    delay(2000);
    LoadDefaultDiagnoses();
    return;
  }

  if (!diagnosesHealthy || num_diagnoses == 0) {
    String err = (num_diagnoses == 0)
                 ? "No diagnoses data"
                 : "Some entries had bad format";
    DisplayErrorMessage(err.c_str(), ILI9341_YELLOW);
    delay(3000);

    if (num_diagnoses == 0) {
      CreateDefaultSettingsFile();
      DisplayErrorMessage("Defaults recreated", ILI9341_GREEN);
      delay(2000);
      LoadDefaultDiagnoses();
    }
    return;
  }
}

// LIST DRAWING
void DrawList() {
  tft.fillRect(0, TITLE_BAR_HIGHT, 320, 240 - TITLE_BAR_HIGHT, ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_10x20_t_cyrillic);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  int currentPosY = LIST_Y_START;
  for (byte currentItem = 0; currentItem < ITEMS_PER_PAGE; currentItem++) {
    int diagnoseIndex = pageOffset + currentItem;
    if (diagnoseIndex >= num_diagnoses) break;
    bool isSelected = (diagnoseIndex == (selectedItem - 1));
    if (isSelected) {
      tft.fillRect(8, currentPosY - 19, 304, 24, ILI9341_NAVY);
      u8g2gfx.setForegroundColor(ILI9341_YELLOW);
    } else {
      u8g2gfx.setForegroundColor(ILI9341_GREEN);
    }
    u8g2gfx.setCursor(18, currentPosY);
    u8g2gfx.print(diagnosis_names[diagnoseIndex]);
    currentPosY += ITEM_HEIGHT;
  }
}

void RedrawSelectionOnly() {
  u8g2gfx.setFont(u8g2_font_10x20_t_cyrillic);
  byte prevLocalIdx = prevSelectedItem - 1 - pageOffset;
  int prevY = LIST_Y_START + prevLocalIdx * ITEM_HEIGHT;
  tft.fillRect(8, prevY - 19, 304, 24, ILI9341_BLACK);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  u8g2gfx.setCursor(18, prevY);
  u8g2gfx.print(diagnosis_names[prevSelectedItem - 1]);

  byte newLocalIdx = selectedItem - 1 - pageOffset;
  int newY = LIST_Y_START + newLocalIdx * ITEM_HEIGHT;
  tft.fillRect(8, newY - 19, 304, 24, ILI9341_NAVY);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  u8g2gfx.setCursor(18, newY);
  u8g2gfx.print(diagnosis_names[selectedItem - 1]);
}

void ScrollItem(bool direction) {
  prevSelectedItem = selectedItem;
  if (direction == SCROLL_UP) {
    selectedItem++;
    if (selectedItem > num_diagnoses) selectedItem = 1;
  } else {
    selectedItem--;
    if (selectedItem < 1) selectedItem = num_diagnoses;
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


// ANGELZ SESSION - clean screen, no level indicator
void OpenAngelZ() {
  tft.fillScreen(ILI9341_BLACK);

  u8g2gfx.setFont(u8g2_font_t0_22b_tf);
  u8g2gfx.setBackgroundColor(ILI9341_BLUE);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  tft.fillRect(0, 0, 320, TITLE_BAR_HIGHT, ILI9341_BLUE);
  int titleWidth = u8g2gfx.getUTF8Width("Angel-Z SESSION");
  u8g2gfx.setCursor((320 - titleWidth) / 2, 24);
  u8g2gfx.print("Angel-Z session");

  prevLevelValue = -1;
  prevLevelBars = -1;
  prevLevelNoSignal = true;

  Sequences sequences(&gen);

  int currentStep = 0;
  unsigned long sessionStart = millis();
  for (int cycleNumber = 0; cycleNumber < ANGELZ_NUMBER_OF_CYCLES; ++cycleNumber) {
    for (int pointNumber = 0; pointNumber < ANGELZ_TOTAL_POINTS; ++pointNumber) {
      gen.EnableOutput(true);
      sequences.Execute10khzSequence();
      delay(3);
      gen.EnableOutput(false);

      int freqBarIndex = pointNumber * 24 / ANGELZ_TOTAL_POINTS;
      unsigned long elapsed = millis() - sessionStart;
      DisplayTreatInProgressScreenAngelZ(freqBarIndex, elapsed, (currentStep == 0));

      float delayValue = sequences.GetDelaySequence(
        pointNumber,
        angelz_valueStart[cycleNumber],
        angelz_valueMax[cycleNumber],
        angelz_tau[cycleNumber],
        ANGELZ_SPLIT_POINT
      );
      delay((unsigned long)delayValue);
      currentStep++;
    }
  }

  tft.fillScreen(ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_helvB24_te);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  int textWidth = u8g2gfx.getUTF8Width("AngelZ Finished!");
  u8g2gfx.setCursor((320 - textWidth) / 2, 120);
  u8g2gfx.print("AngelZ Finished!");
  delay(3000);

  titleLine = (char*)"DIAGNOSES:";
  DrawTitleBar();
  DrawBattery();
  DrawList();
}

// GENERATE FREQUENCY
bool GenerateFrequency() {
  int diagIdx = selectedItem - 1;
  int numFreq = 0;
  int freqIndices[10];

  for (int i = 0; i < 10; i++) {
    if (diagnosis_frequencies[diagIdx][i] > 0) {
      freqIndices[numFreq] = i;
      numFreq++;
    }
  }
  if (numFreq == 0) return false;

  unsigned long secPerFreq = (unsigned long)diagnosis_time_sec[diagIdx];
  unsigned long fragmentMs = secPerFreq * 1000UL;
  unsigned long totalSessionMs = fragmentMs * numFreq;

  unsigned long sessionStart = millis();

  gen.EnableOutput(true);
  digitalWrite(pinSignalType, isSineWave ? LOW : HIGH);
  //digitalWrite(pinOutputPause, LOW);  // Start 

  unsigned long lastSecond = 0;
  unsigned long lastLevelUpdate = 0;
  prevFreqIndex = -1;
  prevLevelValue = -1;
  prevLevelBars = -1;
  prevLevelNoSignal = true;
  treatmentScreenDrawn = false;

  for (int i = 0; i < numFreq; i++) {

    // FREQUENCY FRAGMENT BEGINS - Set pin 11 HIGH (enable output)
    //digitalWrite(pinOutputPause, HIGH);
    gen.EnableOutput(true);

    unsigned long fragmentStartMs = millis();
    unsigned long fragmentTargetEnd = fragmentStartMs + fragmentMs;

    intFreqToGenerate = diagnosis_frequencies[diagIdx][freqIndices[i]];

    unsigned long elapsed = millis() - sessionStart;
    unsigned long msLeft = (elapsed < totalSessionMs) ? (totalSessionMs - elapsed) : 0;

    DisplayTreatInProgressScreen(freqIndices[i], selectedItem, msLeft, !treatmentScreenDrawn);
    treatmentScreenDrawn = true;
    UpdateSignalIndicator();

    gen.ApplySignal(isSineWave ? SINE_WAVE : SQUARE_WAVE, REG0, intFreqToGenerate);

    while (isGeneratingFrequency) {
      unsigned long now = millis();

      if (now >= fragmentTargetEnd) break;

      if (btnEnterPressed) {
        digitalWrite(pinOutputPause, LOW);  // Set to LOW on abort
        gen.EnableOutput(false);
        isSineWave = true;
        digitalWrite(pinSignalType, LOW);
        return true;
      }

      if (encoderMoved) {
        int8_t direction = AnalyzeEncoderChange();
        if (direction > 0 && !isSineWave) {
          isSineWave = true;
          gen.ApplySignal(SINE_WAVE, REG0, intFreqToGenerate);
          gen.SetOutputSource(REG0);
          digitalWrite(pinSignalType, LOW);
          UpdateSignalIndicator();
        } else if (direction < 0 && isSineWave) {
          isSineWave = false;
          gen.ApplySignal(SQUARE_WAVE, REG0, intFreqToGenerate);
          digitalWrite(pinSignalType, HIGH);
          UpdateSignalIndicator();
        }
      }

      if (now - lastLevelUpdate >= 50) {
        int lv = ReadLevelValue();
        DrawLevelIndicator(lv, false, false);
        lastLevelUpdate = now;
      }

      if (now - lastSecond >= 1000) {
        unsigned long elapsedTotal = now - sessionStart;
        unsigned long msLeft2 = (elapsedTotal < totalSessionMs) ? (totalSessionMs - elapsedTotal) : 0;
        DisplayTreatInProgressScreen(freqIndices[i], selectedItem, msLeft2, false);
        UpdateSignalIndicator();
        lastSecond = now;
      }
    }
    
    gen.EnableOutput(false);
    // FREQUENCY FRAGMENT ENDS - Set pin 11 LOW
    digitalWrite(pinOutputPause, HIGH);

    prevFreqIndex = freqIndices[i];
    //
    if (i < numFreq - 1) {
      PlayTone(ONE_BEEP);
    }
  }
  //
  gen.EnableOutput(false);
  digitalWrite(pinOutputPause, LOW);  // 
  //
  isGeneratingFrequency = false;
  isSineWave = true;
  digitalWrite(pinSignalType, LOW);

  tft.fillScreen(ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_helvB24_te);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  int textWidth = u8g2gfx.getUTF8Width("Finished!");
  u8g2gfx.setCursor((320 - textWidth) / 2, 120);
  u8g2gfx.print("Finished!");

  unsigned long actualElapsed = millis() - sessionStart;
  int totalMin = (actualElapsed / 1000) / 60;
  int totalSec = (actualElapsed / 1000) % 60;
  char timeBuf[20];
  sprintf(timeBuf, "Time: %02d:%02d", totalMin, totalSec);
  u8g2gfx.setFont(u8g2_font_t0_22b_tf);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  int tw = u8g2gfx.getUTF8Width(timeBuf);
  u8g2gfx.setCursor((320 - tw) / 2, 160);
  u8g2gfx.print(timeBuf);

  PlayTone(THREE_BEEPS);
  delay(3000);

  titleLine = (char*)"DIAGNOSES:";
  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);

  DrawTitleBar();
  DrawBattery();
  DrawList();

  return false;
}

/*
bool GenerateFrequency() {
  int diagIdx = selectedItem - 1;
  int numFreq = 0;
  int freqIndices[10];
  for (int i = 0; i < 10; i++) {
    if (diagnosis_frequencies[diagIdx][i] > 0) {
      freqIndices[numFreq] = i;
      numFreq++;
    }
  }
  if (numFreq == 0) return false;
  unsigned long secPerFreq = (unsigned long)diagnosis_time_sec[diagIdx];
  unsigned long fragmentMs = secPerFreq * 1000UL;
  unsigned long totalSessionMs = fragmentMs * numFreq;
  unsigned long sessionStart = millis();
  gen.EnableOutput(true);
  digitalWrite(pinSignalType, isSineWave ? LOW : HIGH);
  strComplete = (char*)"";
  unsigned long lastSecond = 0;
  unsigned long lastLevelUpdate = 0;
  prevFreqIndex = -1;
  prevLevelValue = -1;
  prevLevelBars = -1;
  prevLevelNoSignal = true;
  treatmentScreenDrawn = false;
  for (int i = 0; i < numFreq; i++) {
    unsigned long fragmentTargetEnd = (unsigned long)(i + 1) * fragmentMs;
    intFreqToGenerate = diagnosis_frequencies[diagIdx][freqIndices[i]];
    unsigned long elapsed = millis() - sessionStart;
    unsigned long msLeft = (elapsed < totalSessionMs) ? (totalSessionMs - elapsed) : 0;
    DisplayTreatInProgressScreen(freqIndices[i], selectedItem, msLeft, !treatmentScreenDrawn);
    treatmentScreenDrawn = true;
    UpdateSignalIndicator();
    gen.ApplySignal(isSineWave ? SINE_WAVE : SQUARE_WAVE, REG0, intFreqToGenerate);
    while (isGeneratingFrequency) {
      unsigned long now = millis();
      unsigned long elapsedTotal = now - sessionStart;
      if (elapsedTotal >= fragmentTargetEnd) break; 
      if (btnEnterPressed) {
        gen.EnableOutput(false);
        isSineWave = true;                     // reset to default SIN
        digitalWrite(pinSignalType, LOW);      // LOW for SIN default
        return true;
      }
      if (encoderMoved) {
        int8_t direction = AnalyzeEncoderChange();
        if (direction > 0 && !isSineWave) {
          isSineWave = true;
          gen.ApplySignal(SINE_WAVE, REG0, intFreqToGenerate);
          gen.SetOutputSource(REG0);
          digitalWrite(pinSignalType, LOW);
          UpdateSignalIndicator();
        } else if (direction < 0 && isSineWave) {
          isSineWave = false;
          gen.ApplySignal(SQUARE_WAVE, REG0, intFreqToGenerate);
          digitalWrite(pinSignalType, HIGH);
          UpdateSignalIndicator();
        }
      }
      // Update level indicator at 50ms intervals (fast refresh)
      if (now - lastLevelUpdate >= 50) {
        int lv = ReadLevelValue();
        DrawLevelIndicator(lv, false, false);
        lastLevelUpdate = now;
      }
      // Update countdown every second
      if (now - lastSecond >= 1000) {
        unsigned long msLeft2 = (elapsedTotal < totalSessionMs) ? (totalSessionMs - elapsedTotal) : 0;
        DisplayTreatInProgressScreen(freqIndices[i], selectedItem, msLeft2, false);
        UpdateSignalIndicator();
        lastSecond = now;
      }
    }
    prevFreqIndex = freqIndices[i];
    if (i < numFreq - 1) {
      PlayTone(1);
    }
  }
  gen.EnableOutput(false);
  isGeneratingFrequency = false;
  isSineWave = true;                           // reset to default SIN
  digitalWrite(pinSignalType, LOW);            // LOW for SIN default
  tft.fillScreen(ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_helvB24_te);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  int textWidth = u8g2gfx.getUTF8Width("Finished!");
  u8g2gfx.setCursor((320 - textWidth) / 2, 120);
  u8g2gfx.print("Finished!");
  unsigned long actualElapsed = millis() - sessionStart;
  int totalMin = (actualElapsed / 1000) / 60;
  int totalSec = (actualElapsed / 1000) % 60;
  char timeBuf[20];
  sprintf(timeBuf, "Time: %02d:%02d", totalMin, totalSec);
  u8g2gfx.setFont(u8g2_font_t0_22b_tf);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  int tw = u8g2gfx.getUTF8Width(timeBuf);
  u8g2gfx.setCursor((320 - tw) / 2, 160);
  u8g2gfx.print(timeBuf);
  PlayTone(3);
  delay(3000);
  strComplete = (char*)"";
  titleLine = (char*)"DIAGNOSES:";
  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);
  DrawTitleBar();
  DrawBattery();
  DrawList();
  return false;
} */

// BUTTON HANDLING
void ProcessButtonClick() {
  if (!isGeneratingFrequency) {
    EEPROM.update(eepromAddress, selectedItem);
    titleLine = diagnosis_names[selectedItem - 1];
    isGeneratingFrequency =  true;
    btnEnterPressed       = false;

    if (strcmp(diagnosis_names[selectedItem - 1], "AngelZ") == 0) {
      OpenAngelZ();
      isGeneratingFrequency = false;
      btnEnterPressed       = false;
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
      btnEnterPressed       = false;
      SetSelectedItem(selectedItem);
      titleLine = (char*)"DIAGNOSES:";
      DrawTitleBar();
      DrawBattery();
    }
  } else {
    btnEnterPressed = true;
  }
}

void Shutdown() {
  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);
  isGeneratingFrequency = false;
  btnEnterPressed = false;
  debugln("Shutdown initiated");
}


// ENCODER AND BUTTON INTERRUPTS
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

// DECORATIVE DRAWING
void DrawIntroFrame() {
  tft.fillRoundRect(FRAME_X, FRAME_Y, FRAME_W, FRAME_H, 24, GOLD1);
  tft.drawRoundRect(FRAME_X+5, FRAME_Y+5, FRAME_W-10, FRAME_H-10, 18, GOLD3);
  tft.drawRoundRect(FRAME_X+12, FRAME_Y+12, FRAME_W-24, FRAME_H-24, 12, GOLD4);
  for (int i = 0; i < 20; i++) {
    tft.drawPixel(FRAME_X+12+i, FRAME_Y+12+(int)(sin(i*0.4)*8), GOLD2);
    tft.drawPixel(FRAME_X+FRAME_W-12-i, FRAME_Y+12+(int)(sin(i*0.4)*8), GOLD2);
    tft.drawPixel(FRAME_X+12+i, FRAME_Y+FRAME_H-12-(int)(sin(i*0.4)*8), GOLD2);
    tft.drawPixel(FRAME_X+FRAME_W-12-i, FRAME_Y+FRAME_H-12-(int)(sin(i*0.4)*8), GOLD2);
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
    int x = leftX + (int)(t * (centerX - leftX));
    int y = peakBaseY - (int)(leftH * (1 - t)) - (int)(centerH * t) + (int)(4 * sin(t * PI));
    tft.drawPixel(x, y, GOLD2);
  }
  for (int i = 0; i <= 12; i++) {
    float t = i / 12.0;
    int x = centerX + (int)(t * (rightX - centerX));
    int y = peakBaseY - (int)(centerH * (1 - t)) - (int)(rightH * t) + (int)(4 * sin(t * PI));
    tft.drawPixel(x, y, GOLD2);
  }
  tft.drawPixel(leftX - 3, peakBaseY - leftH / 2, GOLD4);
  tft.drawPixel(rightX + 3, peakBaseY - rightH / 2, GOLD4);
  tft.drawPixel(centerX, peakBaseY - centerH + 2, GOLD4);
}

// UTILITY
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

// SETUP
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
  pinMode(pinShutdown1,   OUTPUT);
  pinMode(pinShutdown2,   OUTPUT);
  pinMode(pinSignalType,  OUTPUT);
  pinMode(pinOutputPause, OUTPUT);              // pauses output signl between freq. changes 
  pinMode(pinSDPower, OUTPUT);                  // SD card FET power control
  digitalWrite(pinShutdown1,  HIGH);
  digitalWrite(pinShutdown2,   LOW);
  digitalWrite(pinSignalType,  LOW);            // LOW for SIN default
  digitalWrite(pinSDPower,    HIGH);            // initially power OFF to SD card
  digitalWrite(pinOutputPause, LOW);            // default - output signal paused (LOW)
  pinMode(pinEncoderCW,  INPUT_PULLUP);
  pinMode(pinEncoderCCW, INPUT_PULLUP);
  pinMode(pinBtnEnter,   INPUT_PULLUP);
  pinMode(pinLevelInput, INPUT);

  // Use internal 1.1V reference for ADC on A1 (more sensitive for low-level signals)
  // Uncomment the following two lines to enable internal reference for pin A1 readings:
  analogReference(INTERNAL1V1);  // Set ADC reference to internal 1.1V bandgap  
  analogRead(pinLevelInput);     // Dummy read to settle ADC after ref change    

  gen.Begin();
  gen.EnableOutput(false);

  DisplayIntroScreen();
  delay(2500);

  digitalWrite(pinSDPower, LOW);                // power ON SD card before loading
  delay(100);                                   // brief delay for SD card power stabilization
  InitializeSDAndSettings();
  digitalWrite(pinSDPower, HIGH);               // power OFF SD card after loading

  DrawTitleBar();
  DrawBattery();
  DrawList();
  attachInterrupt(digitalPinToInterrupt(pinEncoderCW),  OnScrollChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinEncoderCCW), OnScrollChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinBtnEnter),   OnButtonPress,  CHANGE);
  byte saved = EEPROM.read(eepromAddress);
  if (saved >= 1 && (int)saved <= num_diagnoses) {
    SetSelectedItem(saved);
  } else {
    SetSelectedItem(1);
  }
}

// MAIN LOOP
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

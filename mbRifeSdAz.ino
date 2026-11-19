// Rife Machine - Arduino Mega2560 + ILI9341 + AD9833 + SD card + AngelZ
// Pin 8: SIN = LOW, SQUARE = HIGH
// Pin 11: HIGH = output paused, LOW = output enabled
// Pin 12: HIGH = amplifier power OFF, LOW = amplifier power ON
// Pin 13: LOW = amplifier outputs blocked, HIGH = outputs open
// Pin A1 measures the output signal and is used for short detection
//
// FIX - original encoder and button interrupt handlers are retained.
// FIX - regular treatment and AngelZ use the same startup delay:
//       pin 12 LOW -> delay -> pin 13 HIGH -> pin 11 LOW -> check A1.
// FIX - the startup delay is serviced in loop(), without a blocking wait.
// FIX - on a detected short, pin 11 goes HIGH and pin 13 goes LOW.
//       Amplifier power on pin 12 remains ON during a short retry.
// FIX - AngelZ remains pulsed when the amplifier output is retried.

#include <EEPROM.h>
#include <AD9833.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include <avr/pgmspace.h>
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG 0

#if DEBUG == 1
  #define debug(x) Serial.print(x)
  #define debugln(x) Serial.println(x)
#else
  #define debug(x)
  #define debugln(x)
#endif

// USER CONFIGURATION
#define SHRT_RETRY_SEC 4
#define SHRT_RETRY_MS  (SHRT_RETRY_SEC * 1000UL)

// NEW - shared delay for regular treatment and AngelZ.
// Timer starts immediately after pin 12 is set LOW.
#define START_TREATMENT_DELAY_SEC 2
#define START_TREATMENT_DELAY_MS  (START_TREATMENT_DELAY_SEC * 1000UL)

// NEW - countdown area during the shared startup delay.
#define START_CD_X 107
#define START_CD_Y 34
#define START_CD_W 213
#define START_CD_H 28

// Pins
#define pinEncoderCW      2
#define pinEncoderCCW     3
#define pinBeepOut        4
#define pinShutdown2      5
#define pinShutdown1      6
#define pinSDPower        7
#define pinSignalType     8
#define pinGenCS          9
#define SD_CS            10
#define pinOutputPause   11
#define pinAmpPower      12
#define pinAmpOutOff     13
#define pinBtnEnter      21
#define pinBatteryLevel  A0
#define pinLevelInput    A1
#define TFT_CS           53
#define TFT_DC           48
#define TFT_RST          49

// ILI9341 (Mega hardware SPI: SCK=52, MOSI=51)
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2gfx;

// AD9833
AD9833 gen(pinGenCS);

// Gold color palette
#define GOLD1 tft.color565(255, 215,   0)
#define GOLD2 tft.color565(218, 165,  32)
#define GOLD3 tft.color565(184, 134,  11)
#define GOLD4 tft.color565(255, 236, 139)

// Level indicator colors
#define LEVEL_GREEN_ACTIVE  tft.color565(0,   220,  0)
#define LEVEL_GREEN_DIM     tft.color565(0,    60,  0)
#define LEVEL_RED_ACTIVE    tft.color565(255,  30, 30)
#define LEVEL_RED_DIM       tft.color565(60,    0,  0)
#define LEVEL_GRAY_NOSIGNAL tft.color565(80,   80, 80)

// Capacity
#define WARN_DIAGNOSES_THRESHOLD 100
#define NAME_MAX_LEN              28

// Battery voltage divider
const float R1 = 32000.0;
const float R2 = 8000.0;
const float referenceVoltage = 1.1;
const float BATTERY_LOW_V = 3.3;

#define BATTERY_LOW_COUNT 3
#define BATTERY_CHECK_MS  1000UL

// Default diagnoses
const char* default_diagnoses_raw[] = {
  "Day1-Love expands:780",
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
char **diagnosis_names = NULL;
int *diagnosis_time_sec = NULL;
int **diagnosis_frequencies = NULL;

int num_diagnoses = 0;
int capacity_diagnoses = 0;

const int SCROLL_DOWN = 0;
const int SCROLL_UP = 1;
const int ONE_BEEP = 1;
const int THREE_BEEPS = 3;

const int PIEZO_BEEP_TONE = 2000;
const int PEIZO_BEEP_LENGTH = 1000;
const int PEIZO_BEEP_PAUSE = 500;

const byte ITEMS_PER_PAGE = 8;

// State variables
byte selectedItem = 1;
byte prevSelectedItem = 1;
byte pageOffset = 0;
byte eepromAddress = 0;

char* titleLine = (char*)"DIAGNOSES:";

bool isGeneratingFrequency = false;
uint16_t intFreqToGenerate = 0;
bool isSineWave = true;

// Original encoder and button interrupt state.
volatile bool encoderMoved = false;
volatile bool btnEnterPressed = false;
volatile int buttonOutput = 0;

enum {
  STATE_NORMAL,
  STATE_SHORT,
  STATE_LONG
};

long LONG_DELTA = 1500UL;
long DEBOUNCE_DELTA = 30UL;

// NEW - shared startup state for regular treatment and AngelZ.
bool treatmentStartPending = false;
bool treatmentStartIsAngelZ = false;
unsigned long treatmentPowerOnAt = 0;
unsigned long lastStartCountdown = 0xFFFFFFFFUL;

// Frame / list constants
const int FRAME_X = 2;
const int FRAME_Y = 2;
const int FRAME_W = 316;
const int FRAME_H = 236;

const int LIST_Y_START = 50;
const int ITEM_HEIGHT = 24;
const int TITLE_BAR_HIGHT = 30;

// Level indicator constants
#define LEVEL_LABEL_Y   210
#define LEVEL_LABEL_H    18
#define LEVEL_BAR_Y     228
#define LEVEL_BAR_H      12
#define LEVEL_NUM_BARS   20
#define LEVEL_TOTAL_W   320
#define LEVEL_BAR_WIDTH (LEVEL_TOTAL_W / LEVEL_NUM_BARS)
#define LEVEL_BAR_GAP     2
#define LEVEL_GREEN_BARS 14

// Emergency short protection
#define SHRT_THRESHOLD_MAX 1020
#define SHRT_BEEP_FREQ     1000
#define SHRT_BEEP_INTERVAL 500UL
#define SHRT_BEEP_DURATION 100
#define SHRT_CHECK_MS      100UL
#define SHRT_RECHECK_MS    500UL

static int prevFreqIndex = -1;
static char prevTimeStr[6] = "99:99";
static char prevAngelZTimeStr[6] = "00:00";
static int prevLevelValue = -1;
static int prevLevelBars = -1;
static bool prevLevelNoSignal = true;
static bool treatmentScreenDrawn = false;

// Emergency short protection state
bool emergencyShortDetected = false;
bool shortRechecking = false;

unsigned long shortBlockStartTime = 0;
unsigned long shortRecheckStartTime = 0;
unsigned long shortPausedTotal = 0;
unsigned long lastShortBeepTime = 0;
unsigned long lastShortCheckTime = 0;

// Software state of amplifier pins
bool ampOutputOpen = false;
bool ampPowerOn = false;

// Battery low protection state
bool batteryLowLatched = false;
bool batteryAlertPending = false;
byte batteryLowCount = 0;
unsigned long lastBatteryCheckTime = 0;

// AngelZ constants
#define ANGELZ_TOTAL_POINTS 48

const int ANGELZ_SPLIT_POINT = 24;
const int ANGELZ_NUMBER_OF_CYCLES = 59;
const float ANGELZ_DELAY_SCALE = 3.734f;

float angelz_valueStart[ANGELZ_NUMBER_OF_CYCLES] = {
  20.0, 15.0, 18.0, 25.0, 22.0, 30.0, 21.0, 35.0, 23.0, 40.0,
  24.0, 45.0, 16.0, 27.0, 19.0, 31.0, 13.0, 28.0, 14.0, 34.0,
  12.0, 39.0, 10.0, 42.0, 11.0, 37.0,  9.0, 33.0,  8.0, 36.0,
   7.0, 29.0,  6.0, 32.0,  5.0, 38.0,  4.0, 41.0,  3.0, 44.0,
   2.0, 43.0,  1.0, 46.0,  0.5, 48.0,  0.2, 50.0,  0.1, 52.0,
   0.0, 53.0, 12.0, 17.0, 22.0, 28.0, 34.0, 41.0, 45.0
};

float angelz_valueMax[ANGELZ_NUMBER_OF_CYCLES] = {
  52.6, 45.0,  55.0, 60.0,  50.0, 65.0,  53.0, 70.0,  56.0, 75.0,
  59.0, 80.0,  48.0, 62.0,  51.0, 64.0,  49.0, 67.0,  50.0, 71.0,
  46.0, 73.0,  47.0, 76.0,  54.0, 78.0,  57.0, 79.0,  58.0, 82.0,
  61.0, 85.0,  63.0, 88.0,  65.0, 90.0,  68.0, 92.0,  66.0, 95.0,
  69.0, 97.0,  72.0, 99.0,  74.0,100.0,  77.0,102.0,  80.0,105.0,
  82.0,110.0,  55.0, 60.0,  65.0, 70.0,  75.0, 85.0,  90.0
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
void DisplayErrorMessage(
  const char* message,
  uint16_t color = ILI9341_RED
);

void DisplayTreatInProgressScreen(
  int currentFreqIndex,
  int selectedItem,
  unsigned long msLeft,
  bool forceFull = false
);

void DisplayTreatInProgressScreenAngelZ(
  int freqBarIndex,
  unsigned long msLeft,
  bool forceFull = false
);

void DisplayIntroScreen();
void DrawTitleBar();
void DrawBattery();
void DrawList();
void RedrawSelectionOnly();
void ScrollItem(bool direction);
byte CalulatePageOffset(byte item);
void SetSelectedItem(byte item);
void UpdateSignalIndicator();

void ProcessButtonClick();
void ServicePendingTreatment();
bool GenerateFrequency();
void OpenAngelZ();
void Shutdown();

void OnScrollChange();
int8_t AnalyzeEncoderChange();
void OnButtonPress();

void DrawIntroFrame();
void DrawGoldenCrown(int cx, int topY);
void DrawStartCountdown(unsigned long secondsLeft);
void ClearStartCountdown();

float MeasureBatteryVoltage();
void PlayTone(int n);
void WaitBatterySafe(unsigned long ms);

bool AddDiagnosis(const char* nameWithTime, const int* freqs);
bool EnsureDiagnosisCapacity();
void FreeDiagnosisArrays();
void LoadDefaultDiagnoses();
void CreateDefaultSettingsFile();
void InitializeSDAndSettings();

int ReadLevelValue();
int ReadShortLevel();
void DrawLevelIndicator(
  int level,
  bool noSignal,
  bool forceFull = false
);

void CheckAndHandleAmplifierShort();
void BlockAmpOutput();
void UnblockAmpOutput();
void ResetShortProtection();
unsigned long TreatmentClock();
void DisplayAmpBlockedScreen();

bool CheckBatteryLow();
void ShowBatteryLowAlert();
void SetAmpOutput(bool open);
void SetAmpPower(bool on);
void SetSignalType(bool sine);
void AbortTreatment();

bool TakeButtonPress(int& action);
void ClearButtonPress();
bool WaitForAngelZShortRetry();

// ANGELZ SEQUENCES
class Sequences {
  private:
    AD9833* m_pgen;

  public:
    Sequences(AD9833* pGen);

    void Execute10khzSequence();

    float GetDelaySequence(
      int currentPoint,
      float valueStart,
      float valueMax,
      float tau,
      int splitPoint
    );
};

Sequences::Sequences(AD9833* pGen) : m_pgen(pGen) {}

void Sequences::Execute10khzSequence() {
  // Original AngelZ sequence is retained.
  for (int pulseNum = 0; pulseNum < 20; pulseNum++) {
    m_pgen->EnableOutput(true);
    m_pgen->ApplySignal(SQUARE_WAVE, REG0, 10000);
  }
}

float Sequences::GetDelaySequence(
  int currentPoint,
  float valueStart,
  float valueMax,
  float tau,
  int splitPoint
) {
  float delayMs;

  if (currentPoint < splitPoint) {
    delayMs = valueStart +
      (valueMax - valueStart) *
      (1 - exp(-currentPoint / tau));
  } else {
    delayMs = valueMax *
      exp(-(currentPoint - splitPoint) / tau);
  }

  return delayMs * ANGELZ_DELAY_SCALE;
}

// DYNAMIC ARRAY MANAGEMENT
bool EnsureDiagnosisCapacity() {
  if (num_diagnoses < capacity_diagnoses) return true;

  int newCapacity = capacity_diagnoses + 10;

  char** newNames =
    (char**)realloc(
      diagnosis_names,
      newCapacity * sizeof(char*)
    );

  if (!newNames) return false;
  diagnosis_names = newNames;

  for (int i = capacity_diagnoses; i < newCapacity; i++) {
    diagnosis_names[i] =
      (char*)malloc((NAME_MAX_LEN + 1) * sizeof(char));

    if (!diagnosis_names[i]) return false;
    diagnosis_names[i][0] = '\0';
  }

  int* newTimes =
    (int*)realloc(
      diagnosis_time_sec,
      newCapacity * sizeof(int)
    );

  if (!newTimes) return false;
  diagnosis_time_sec = newTimes;

  for (int i = capacity_diagnoses; i < newCapacity; i++) {
    diagnosis_time_sec[i] = 60;
  }

  int** newFreqs =
    (int**)realloc(
      diagnosis_frequencies,
      newCapacity * sizeof(int*)
    );

  if (!newFreqs) return false;
  diagnosis_frequencies = newFreqs;

  for (int i = capacity_diagnoses; i < newCapacity; i++) {
    diagnosis_frequencies[i] =
      (int*)malloc(10 * sizeof(int));

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
      if (diagnosis_frequencies[i]) {
        free(diagnosis_frequencies[i]);
      }
    }

    free(diagnosis_frequencies);
    diagnosis_frequencies = NULL;
  }

  num_diagnoses = 0;
  capacity_diagnoses = 0;
}

bool AddDiagnosis(const char* nameWithTime, const int* freqs) {
  if (!EnsureDiagnosisCapacity()) return false;

  if (num_diagnoses == WARN_DIAGNOSES_THRESHOLD) {
    DisplayErrorMessage(
      "Diagnoses count exceeded 100 - continuing...",
      ILI9341_YELLOW
    );
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

    if (timeSec <= 0 && nameStr != "AngelZ") {
      timeSec = 60;
    }
  } else {
    nameStr = s;
    nameStr.trim();
  }

  strncpy(
    diagnosis_names[num_diagnoses],
    nameStr.c_str(),
    NAME_MAX_LEN
  );

  diagnosis_names[num_diagnoses][NAME_MAX_LEN] = '\0';
  diagnosis_time_sec[num_diagnoses] = timeSec;

  for (int j = 0; j < 10; j++) {
    diagnosis_frequencies[num_diagnoses][j] = freqs[j];
  }

  num_diagnoses++;
  return true;
}

// LEVEL INDICATOR
int ReadLevelValue() {
  long sum = 0;

  for (int i = 0; i < 4; i++) {
    sum += analogRead(pinLevelInput);
  }

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

  bool needFullRedraw =
    forceFull || (noSignal != prevLevelNoSignal);

  if (needFullRedraw) {
    for (int i = 0; i < LEVEL_NUM_BARS; i++) {
      int x = i * LEVEL_BAR_WIDTH;
      int barW = LEVEL_BAR_WIDTH - LEVEL_BAR_GAP;
      uint16_t color;

      if (noSignal) {
        color = LEVEL_GRAY_NOSIGNAL;
      } else if (i < litBars) {
        color = (i < LEVEL_GREEN_BARS)
          ? LEVEL_GREEN_ACTIVE
          : LEVEL_RED_ACTIVE;
      } else {
        color = (i < LEVEL_GREEN_BARS)
          ? LEVEL_GREEN_DIM
          : LEVEL_RED_DIM;
      }

      tft.fillRect(x, LEVEL_BAR_Y, barW, LEVEL_BAR_H, color);
    }

    tft.fillRect(
      0,
      LEVEL_LABEL_Y,
      320,
      LEVEL_LABEL_H,
      ILI9341_BLACK
    );

    u8g2gfx.setFont(u8g2_font_helvB14_te);
    u8g2gfx.setBackgroundColor(ILI9341_BLACK);

    if (noSignal) {
      u8g2gfx.setForegroundColor(LEVEL_GRAY_NOSIGNAL);

      const char* noSigStr = "No Signal";
      int tw = u8g2gfx.getUTF8Width(noSigStr);

      u8g2gfx.setCursor((320 - tw) / 2, LEVEL_LABEL_Y + 15);
      u8g2gfx.print(noSigStr);
    } else {
      u8g2gfx.setForegroundColor(
        (litBars >= 16) ? LEVEL_RED_ACTIVE : ILI9341_WHITE
      );

      char digBuf[16];
      snprintf(digBuf, sizeof(digBuf), "Level: %d", litBars);

      int tw = u8g2gfx.getUTF8Width(digBuf);
      u8g2gfx.setCursor((320 - tw) / 2, LEVEL_LABEL_Y + 15);
      u8g2gfx.print(digBuf);
    }

    prevLevelBars = litBars;
    prevLevelValue = level;
    prevLevelNoSignal = noSignal;
    return;
  }

  // Incremental update - redraw only changed bars.
  if (!noSignal && litBars != prevLevelBars) {
    int fromBar = min(litBars, prevLevelBars);
    int toBar = max(litBars, prevLevelBars);

    if (prevLevelBars < 0) {
      fromBar = 0;
      toBar = LEVEL_NUM_BARS;
    }

    for (int i = fromBar; i < toBar; i++) {
      int x = i * LEVEL_BAR_WIDTH;
      int barW = LEVEL_BAR_WIDTH - LEVEL_BAR_GAP;
      uint16_t color;

      if (i < litBars) {
        color = (i < LEVEL_GREEN_BARS)
          ? LEVEL_GREEN_ACTIVE
          : LEVEL_RED_ACTIVE;
      } else {
        color = (i < LEVEL_GREEN_BARS)
          ? LEVEL_GREEN_DIM
          : LEVEL_RED_DIM;
      }

      tft.fillRect(x, LEVEL_BAR_Y, barW, LEVEL_BAR_H, color);
    }

    prevLevelBars = litBars;
  }

  if (!noSignal && level != prevLevelValue) {
    tft.fillRect(
      0,
      LEVEL_LABEL_Y,
      320,
      LEVEL_LABEL_H,
      ILI9341_BLACK
    );

    u8g2gfx.setFont(u8g2_font_helvB14_te);
    u8g2gfx.setBackgroundColor(ILI9341_BLACK);
    u8g2gfx.setForegroundColor(
      (litBars >= 16) ? LEVEL_RED_ACTIVE : ILI9341_WHITE
    );

    char digBuf[16];
    snprintf(digBuf, sizeof(digBuf), "Level: %d", litBars);

    int tw = u8g2gfx.getUTF8Width(digBuf);
    u8g2gfx.setCursor((320 - tw) / 2, LEVEL_LABEL_Y + 15);
    u8g2gfx.print(digBuf);

    prevLevelValue = level;
  }
}

// DISPLAY HELPERS
void DisplayErrorMessage(const char* message, uint16_t color) {
  tft.fillRect(
    0,
    TITLE_BAR_HIGHT,
    320,
    240 - TITLE_BAR_HIGHT,
    ILI9341_BLACK
  );

  u8g2gfx.setFont(u8g2_font_8x13_t_cyrillic);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(color);

  int w = u8g2gfx.getUTF8Width(message);
  int x = (w < 320) ? (320 - w) / 2 : 2;

  u8g2gfx.setCursor(x, 120);
  u8g2gfx.print(message);
}

void DisplayAmpBlockedScreen() {
  tft.fillRect(
    0,
    TITLE_BAR_HIGHT,
    320,
    240 - TITLE_BAR_HIGHT,
    ILI9341_BLACK
  );

  u8g2gfx.setFont(u8g2_font_helvB14_te);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_RED);

  const char* line1 = "AMPLIFIER SHORT!";
  int w1 = u8g2gfx.getUTF8Width(line1);

  u8g2gfx.setCursor((320 - w1) / 2, 100);
  u8g2gfx.print(line1);

  u8g2gfx.setForegroundColor(ILI9341_YELLOW);

  char line2[32];
  snprintf(
    line2,
    sizeof(line2),
    "Output blocked, retry %us",
    (unsigned int)SHRT_RETRY_SEC
  );

  int w2 = u8g2gfx.getUTF8Width(line2);
  u8g2gfx.setCursor((320 - w2) / 2, 140);
  u8g2gfx.print(line2);
}

void ShowBatteryLowAlert() {
  batteryAlertPending = false;

  tft.fillScreen(ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_helvB14_te);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_RED);

  const char* alertStr = "Battery Low - Charge it";
  int tw = u8g2gfx.getUTF8Width(alertStr);

  u8g2gfx.setCursor((320 - tw) / 2, 110);
  u8g2gfx.print(alertStr);

  u8g2gfx.setForegroundColor(ILI9341_YELLOW);

  const char* hintStr = "Power OFF to exit";
  tw = u8g2gfx.getUTF8Width(hintStr);

  u8g2gfx.setCursor((320 - tw) / 2, 150);
  u8g2gfx.print(hintStr);
}

void DisplayIntroScreen() {
  // Original intro drawing is retained.
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

// REGULAR TREATMENT SCREEN
void DisplayTreatInProgressScreen(
  int currentFreqIndex,
  int selItem,
  unsigned long msLeft,
  bool forceFull
) {
  const int SCREEN_W = 320;
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

    tft.fillRect(
      0,
      0,
      SCREEN_W,
      TITLE_BAR_HIGHT,
      ILI9341_BLUE
    );

    u8g2gfx.setFont(u8g2_font_helvB14_te);
    u8g2gfx.setBackgroundColor(ILI9341_BLUE);
    u8g2gfx.setForegroundColor(ILI9341_YELLOW);

    char titleBuf[48];
    snprintf(
      titleBuf,
      sizeof(titleBuf),
      "%s:%d",
      diagnosis_names[diagIdx],
      diagnosis_time_sec[diagIdx]
    );

    int titleWidth = u8g2gfx.getUTF8Width(titleBuf);
    int titleX = (SCREEN_W - titleWidth) / 2;

    if (titleX < 2) titleX = 2;

    u8g2gfx.setCursor(titleX, 22);
    u8g2gfx.print(titleBuf);

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
        barColor = (i == currentFreqIndex)
          ? ILI9341_GREENYELLOW
          : ILI9341_DARKGREEN;
      }

      tft.fillRect(BAR_X, y, BAR_W, BAR_H - 1, barColor);

      if (i == currentFreqIndex && freq > 0) {
        tft.drawRect(
          BAR_X,
          y,
          BAR_W,
          BAR_H - 1,
          ILI9341_WHITE
        );
      }

      u8g2gfx.setFont(u8g2_font_helvB14_te);
      u8g2gfx.setBackgroundColor(barColor);
      u8g2gfx.setForegroundColor(ILI9341_PINK);

      char freqStr[8];

      if (freq > 0) {
        snprintf(freqStr, sizeof(freqStr), "%d", freq);
      } else {
        strcpy(freqStr, "-");
      }

      int textWidth = u8g2gfx.getUTF8Width(freqStr);

      tft.fillRect(
        BAR_X + 2,
        y + 2,
        BAR_W - 4,
        BAR_H - 4,
        barColor
      );

      u8g2gfx.setCursor(
        BAR_X + (BAR_W - textWidth) / 2,
        y + BAR_H / 2 + 6
      );

      u8g2gfx.print(freqStr);
    }

    DrawLevelIndicator(0, true, true);

    u8g2gfx.setFont(u8g2_font_helvB24_te);
    u8g2gfx.setBackgroundColor(ILI9341_BLACK);
    u8g2gfx.setForegroundColor(ILI9341_WHITE);

    int labelWidth = u8g2gfx.getUTF8Width("Remaining");
    u8g2gfx.setCursor(
      LEFT_W + (RIGHT_W - labelWidth) / 2,
      100
    );
    u8g2gfx.print("Remaining");

    strcpy(prevTimeStr, "99:99");
    prevFreqIndex = currentFreqIndex;
    treatmentScreenDrawn = true;
  }

  if (prevFreqIndex != currentFreqIndex) {
    if (prevFreqIndex >= 0 && prevFreqIndex < 10) {
      int freq = diagnosis_frequencies[diagIdx][prevFreqIndex];
      int y = BAR_Y0 + prevFreqIndex * BAR_H;

      uint16_t barColor = (freq == 0)
        ? tft.color565(20, 20, 20)
        : ILI9341_DARKGREEN;

      tft.fillRect(BAR_X, y, BAR_W, BAR_H - 1, barColor);

      u8g2gfx.setFont(u8g2_font_helvB14_te);
      u8g2gfx.setBackgroundColor(barColor);
      u8g2gfx.setForegroundColor(ILI9341_PINK);

      char freqStr[8];

      if (freq > 0) {
        snprintf(freqStr, sizeof(freqStr), "%d", freq);
      } else {
        strcpy(freqStr, "-");
      }

      int textWidth = u8g2gfx.getUTF8Width(freqStr);

      tft.fillRect(
        BAR_X + 2,
        y + 2,
        BAR_W - 4,
        BAR_H - 4,
        barColor
      );

      u8g2gfx.setCursor(
        BAR_X + (BAR_W - textWidth) / 2,
        y + BAR_H / 2 + 6
      );

      u8g2gfx.print(freqStr);
    }

    if (currentFreqIndex >= 0 && currentFreqIndex < 10) {
      int freq = diagnosis_frequencies[diagIdx][currentFreqIndex];
      int y = BAR_Y0 + currentFreqIndex * BAR_H;

      uint16_t barColor = (freq > 0)
        ? ILI9341_GREENYELLOW
        : tft.color565(20, 20, 20);

      tft.fillRect(BAR_X, y, BAR_W, BAR_H - 1, barColor);

      if (freq > 0) {
        tft.drawRect(
          BAR_X,
          y,
          BAR_W,
          BAR_H - 1,
          ILI9341_WHITE
        );
      }

      u8g2gfx.setFont(u8g2_font_helvB14_te);
      u8g2gfx.setBackgroundColor(barColor);
      u8g2gfx.setForegroundColor(ILI9341_PINK);

      char freqStr[8];

      if (freq > 0) {
        snprintf(freqStr, sizeof(freqStr), "%d", freq);
      } else {
        strcpy(freqStr, "-");
      }

      int textWidth = u8g2gfx.getUTF8Width(freqStr);

      tft.fillRect(
        BAR_X + 2,
        y + 2,
        BAR_W - 4,
        BAR_H - 4,
        barColor
      );

      u8g2gfx.setCursor(
        BAR_X + (BAR_W - textWidth) / 2,
        y + BAR_H / 2 + 6
      );

      u8g2gfx.print(freqStr);
    }

    prevFreqIndex = currentFreqIndex;
  }

  int lv = ReadLevelValue();
  DrawLevelIndicator(lv, false, false);

  unsigned long secondsLeft = msLeft / 1000UL;

  // FIX - keep the existing five-character timer within its buffer.
  if (secondsLeft > 5999UL) secondsLeft = 5999UL;

  int minLeft = (int)(secondsLeft / 60UL);
  int secLeft = (int)(secondsLeft % 60UL);

  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", minLeft, secLeft);

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

  int charX[5];
  int charW[5];

  charX[0] = cx; charW[0] = maxDigitW; cx += maxDigitW;
  charX[1] = cx; charW[1] = maxDigitW; cx += maxDigitW;
  charX[2] = cx; charW[2] = colonW;    cx += colonW;
  charX[3] = cx; charW[3] = maxDigitW; cx += maxDigitW;
  charX[4] = cx; charW[4] = maxDigitW;

  for (int i = 0; i < 5; i++) {
    if (buf[i] != prevTimeStr[i] || i == 2) {
      tft.fillRect(
        charX[i],
        140,
        charW[i],
        45,
        ILI9341_BLACK
      );

      char tmp[2] = {buf[i], 0};
      int actualW = u8g2gfx.getUTF8Width(tmp);
      int offset = (charW[i] - actualW) / 2;

      u8g2gfx.setCursor(charX[i] + offset, 180);
      u8g2gfx.print(tmp);
    }
  }

  strcpy(prevTimeStr, buf);
}

// ANGELZ TREATMENT SCREEN
void DisplayTreatInProgressScreenAngelZ(
  int freqBarIndex,
  unsigned long msLeft,
  bool forceFull
) {
  static bool titleDrawn = false;

  if (forceFull || !titleDrawn) {
    tft.fillRect(
      0,
      0,
      320,
      TITLE_BAR_HIGHT,
      ILI9341_BLUE
    );

    u8g2gfx.setFont(u8g2_font_t0_22b_tf);
    u8g2gfx.setBackgroundColor(ILI9341_BLUE);
    u8g2gfx.setForegroundColor(ILI9341_YELLOW);

    int titleWidth =
      u8g2gfx.getUTF8Width("Angel-Z SESSION");

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
    int from = 0;
    int to = NUM_RECTS - 1;

    if (!forceFull && prevBarIndex != -1) {
      from = min(prevBarIndex, freqBarIndex);
      to = max(prevBarIndex, freqBarIndex);
    }

    for (int i = from; i <= to; i++) {
      int x = BAR_X + i * RECT_W;

      uint16_t color = (i <= freqBarIndex)
        ? ((i < 18) ? ILI9341_GREEN : ILI9341_RED)
        : ILI9341_DARKGREY;

      tft.fillRect(x, BAR_Y, RECT_W - 2, RECT_H, color);
    }

    prevBarIndex = freqBarIndex;
  }

  unsigned long secondsLeft = msLeft / 1000UL;

  // FIX - keep the existing five-character timer within its buffer.
  if (secondsLeft > 5999UL) secondsLeft = 5999UL;

  int minLeft = (int)(secondsLeft / 60UL);
  int secLeft = (int)(secondsLeft % 60UL);

  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", minLeft, secLeft);

  u8g2gfx.setFont(u8g2_font_fub42_tf);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_MAGENTA);

  static bool positionsCalculated = false;
  static int charX[5];
  static int charW[5];

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
      tft.fillRect(
        charX[i],
        96,
        charW[i],
        45,
        ILI9341_BLACK
      );

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

  file.println(
    "## Diagnoses section - format: "
    "Name:seconds,freq1,freq2,...,freq10"
  );

  file.println("## seconds = time per each non-zero frequency");
  file.println("## Zero frequencies are skipped");
  file.println("[diagnoses]");

  int n = sizeof(default_diagnoses_raw) /
          sizeof(default_diagnoses_raw[0]);

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

  int n = sizeof(default_diagnoses_raw) /
          sizeof(default_diagnoses_raw[0]);

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
  // FIX - standard Arduino SD library uses SD.begin(csPin).
  if (!SD.begin(SD_CS)) {
    DisplayErrorMessage("SD card is not present");
    delay(3000);
    LoadDefaultDiagnoses();
    return;
  }

  File file = SD.open("settings.txt", FILE_READ);

  if (!file) {
    DisplayErrorMessage(
      "settings.txt missing - creating...",
      ILI9341_YELLOW
    );

    delay(2000);
    CreateDefaultSettingsFile();

    DisplayErrorMessage(
      "settings.txt created",
      ILI9341_GREEN
    );

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

    if (line.startsWith("##") || line.length() == 0) {
      continue;
    }

    if (line.startsWith("[")) {
      currentSection = line;

      if (line.indexOf("[diagnoses]") != -1) {
        hasDiagnosesSection = true;
      }

      continue;
    }

    if (currentSection == "[diagnoses]") {
      if (num_diagnoses == WARN_DIAGNOSES_THRESHOLD &&
          !warnShown) {
        DisplayErrorMessage(
          "Diagnoses > 100, still loading...",
          ILI9341_YELLOW
        );

        delay(2000);
        warnShown = true;
      }

      int commaCount = 0;

      for (unsigned int i = 0; i < line.length(); i++) {
        if (line[i] == ',') commaCount++;
      }

      if (commaCount != 10) {
        diagnosesHealthy = false;
        debug("Bad comma count: ");
        debugln(line);
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
        int nextComma = (f < 9)
          ? line.indexOf(',', pos)
          : (int)line.length();

        if (nextComma == -1 && f < 9) {
          freqOk = false;
          break;
        }

        String freqStr = line.substring(pos, nextComma);
        freqStr.trim();

        if (freqStr.length() == 0) {
          freqOk = false;
          break;
        }

        bool isNum = true;

        for (unsigned int k = 0; k < freqStr.length(); k++) {
          if (!isdigit((unsigned char)freqStr[k])) {
            isNum = false;
            break;
          }
        }

        if (!isNum) {
          freqOk = false;
          break;
        }

        freqs[f] = freqStr.toInt();
        pos = nextComma + 1;
      }

      if (!freqOk) {
        diagnosesHealthy = false;
        debug("Bad freq in: ");
        debugln(line);
        continue;
      }

      if (!AddDiagnosis(nameWithTime.c_str(), freqs)) {
        DisplayErrorMessage(
          "RAM exhausted, stopped loading",
          ILI9341_RED
        );

        delay(2000);
        break;
      }
    }
  }

  file.close();

  if (!hasDiagnosesSection) {
    DisplayErrorMessage(
      "No [diagnoses] section - creating...",
      ILI9341_RED
    );

    delay(3000);
    CreateDefaultSettingsFile();

    DisplayErrorMessage(
      "[diagnoses] section created",
      ILI9341_GREEN
    );

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
  }
}

// LIST DRAWING
void DrawList() {
  tft.fillRect(
    0,
    TITLE_BAR_HIGHT,
    320,
    240 - TITLE_BAR_HIGHT,
    ILI9341_BLACK
  );

  u8g2gfx.setFont(u8g2_font_10x20_t_cyrillic);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);

  int currentPosY = LIST_Y_START;

  for (byte currentItem = 0;
       currentItem < ITEMS_PER_PAGE;
       currentItem++) {
    int diagnoseIndex = pageOffset + currentItem;

    if (diagnoseIndex >= num_diagnoses) break;

    bool isSelected =
      (diagnoseIndex == (selectedItem - 1));

    if (isSelected) {
      tft.fillRect(
        8,
        currentPosY - 19,
        304,
        24,
        ILI9341_NAVY
      );
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

// AMPLIFIER AND SIGNAL CONTROL
void SetSignalType(bool sine) {
  isSineWave = sine;

  // FIX - SIN = LOW, SQUARE = HIGH.
  digitalWrite(pinSignalType, sine ? LOW : HIGH);
}

void SetAmpOutput(bool open) {
  ampOutputOpen = open;
  digitalWrite(pinAmpOutOff, open ? HIGH : LOW);

  debugln(
    open
      ? "AMP OUTPUT -> OPEN (HIGH)"
      : "AMP OUTPUT -> BLOCKED (LOW)"
  );
}

void SetAmpPower(bool on) {
  ampPowerOn = on;
  digitalWrite(pinAmpPower, on ? LOW : HIGH);

  debugln(
    on
      ? "AMP POWER -> ON (LOW)"
      : "AMP POWER -> OFF (HIGH)"
  );
}

int ReadShortLevel() {
  long sum = 0;

  for (int i = 0; i < 4; i++) {
    sum += analogRead(pinLevelInput);
  }

  return (int)(sum >> 2);
}

// SHORT PROTECTION
void CheckAndHandleAmplifierShort() {
  unsigned long now = millis();

  // FIX - while blocked, only service the retry timer and beeper.
  if (emergencyShortDetected) {
    if (now - shortBlockStartTime >= SHRT_RETRY_MS) {
      UnblockAmpOutput();
      shortRechecking = true;
      shortRecheckStartTime = millis();

      debugln("EMERGENCY: Re-checking short condition.");
    }
  } else if (ampPowerOn && ampOutputOpen) {
    // FIX - A1 is evaluated only after the amplifier output is open.
    int rawValue = ReadShortLevel();

    if (rawValue >= SHRT_THRESHOLD_MAX) {
      // FIX - block the output before any diagnostic display or logging.
      BlockAmpOutput();

      debug("EMERGENCY: A1 raw=");
      debugln(rawValue);
      return;
    }

    #if DEBUG == 1
      static unsigned long lastDbgTime = 0;

      if (now - lastDbgTime >= 500UL) {
        lastDbgTime = now;
        debug("A1 raw: ");
        debugln(rawValue);
      }
    #endif

    if (shortRechecking &&
        now - shortRecheckStartTime >= SHRT_RECHECK_MS) {
      shortRechecking = false;
      treatmentScreenDrawn = false;

      // FIX - do not repaint while an AngelZ pulse is active.
      if (!treatmentStartIsAngelZ) {
        DisplayErrorMessage("OUTPUT RESTORED", ILI9341_GREEN);
      }

      debugln("EMERGENCY: Short condition cleared.");
    }
  }

  if (emergencyShortDetected) {
    if (now - lastShortBeepTime >= SHRT_BEEP_INTERVAL) {
      lastShortBeepTime = now;
      tone(pinBeepOut, SHRT_BEEP_FREQ, SHRT_BEEP_DURATION);
    }
  } else {
    noTone(pinBeepOut);
  }
}

void BlockAmpOutput() {
  emergencyShortDetected = true;
  shortRechecking = false;
  shortBlockStartTime = millis();
  lastShortBeepTime = shortBlockStartTime;

  // FIX - immediately inhibit the signal and amplifier outputs.
  digitalWrite(pinOutputPause, HIGH);
  SetAmpOutput(false);
  gen.EnableOutput(false);

  treatmentScreenDrawn = false;

  tone(pinBeepOut, SHRT_BEEP_FREQ, SHRT_BEEP_DURATION);
  DisplayAmpBlockedScreen();
}

void UnblockAmpOutput() {
  if (emergencyShortDetected) {
    shortPausedTotal += millis() - shortBlockStartTime;
  }

  emergencyShortDetected = false;
  noTone(pinBeepOut);

  // FIX - regular treatment resumes its continuous signal.
  // AngelZ leaves the generator muted until its next pulse.
  if (isGeneratingFrequency && !treatmentStartIsAngelZ) {
    gen.EnableOutput(true);
  }

  SetAmpOutput(true);
  digitalWrite(pinOutputPause, LOW);
}

void ResetShortProtection() {
  emergencyShortDetected = false;
  shortRechecking = false;
  shortPausedTotal = 0;
  noTone(pinBeepOut);
}

unsigned long TreatmentClock() {
  unsigned long nowMs = millis();
  unsigned long t = nowMs - shortPausedTotal;

  if (emergencyShortDetected) {
    t -= nowMs - shortBlockStartTime;
  }

  return t;
}

// BATTERY PROTECTION
bool CheckBatteryLow() {
  if (batteryLowLatched) return true;

  if (millis() - lastBatteryCheckTime < BATTERY_CHECK_MS) {
    return false;
  }

  lastBatteryCheckTime = millis();

  if (MeasureBatteryVoltage() <= BATTERY_LOW_V) {
    batteryLowCount++;
  } else {
    batteryLowCount = 0;
  }

  if (batteryLowCount >= BATTERY_LOW_COUNT) {
    batteryLowLatched = true;
    batteryAlertPending = true;

    // FIX - battery shutdown is immediate and does not use the start delay.
    digitalWrite(pinOutputPause, HIGH);
    SetAmpOutput(false);
    gen.EnableOutput(false);
    SetAmpPower(false);
    SetSignalType(true);
    noTone(pinBeepOut);

    debugln("BATTERY LOW: outputs blocked, amps OFF.");
  }

  return batteryLowLatched;
}

void AbortTreatment() {
  digitalWrite(pinOutputPause, HIGH);
  SetAmpOutput(false);
  gen.EnableOutput(false);
  SetAmpPower(false);
  ResetShortProtection();
  SetSignalType(true);
}

void WaitBatterySafe(unsigned long ms) {
  unsigned long start = millis();

  while (millis() - start < ms) {
    CheckBatteryLow();

    if (batteryLowLatched) return;
  }
}

// SHARED STARTUP COUNTDOWN
void DrawStartCountdown(unsigned long secondsLeft) {
  tft.fillRect(
    START_CD_X,
    START_CD_Y,
    START_CD_W,
    START_CD_H,
    ILI9341_BLACK
  );

  u8g2gfx.setFont(u8g2_font_helvB14_te);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);

  char cdBuf[28];

  snprintf(
    cdBuf,
    sizeof(cdBuf),
    "Starting in %lu s",
    secondsLeft
  );

  int tw = u8g2gfx.getUTF8Width(cdBuf);

  u8g2gfx.setCursor(
    START_CD_X + (START_CD_W - tw) / 2,
    START_CD_Y + 20
  );

  u8g2gfx.print(cdBuf);
}

void ClearStartCountdown() {
  tft.fillRect(
    START_CD_X,
    START_CD_Y,
    START_CD_W,
    START_CD_H,
    ILI9341_BLACK
  );
}

// NEW - copy and clear the button event atomically on the Mega.
// The original button ISR itself is unchanged.
bool TakeButtonPress(int& action) {
  bool pressed;

  noInterrupts();

  pressed = btnEnterPressed;

  if (pressed) {
    action = buttonOutput;
    btnEnterPressed = false;
    buttonOutput = STATE_NORMAL;
  }

  interrupts();
  return pressed;
}

void ClearButtonPress() {
  noInterrupts();
  btnEnterPressed = false;
  buttonOutput = STATE_NORMAL;
  interrupts();
}

// BUTTON HANDLING AND STARTUP
void ProcessButtonClick() {
  if (isGeneratingFrequency) {
    btnEnterPressed = true;
    return;
  }

  // FIX - do not dereference an empty or invalid selection.
  if (num_diagnoses == 0 ||
      selectedItem < 1 ||
      selectedItem > num_diagnoses) {
    return;
  }

  int diagIdx = selectedItem - 1;

  treatmentStartIsAngelZ =
    (strcmp(diagnosis_names[diagIdx], "AngelZ") == 0);

  // FIX - do not power the amplifiers for an empty regular diagnosis.
  if (!treatmentStartIsAngelZ) {
    bool hasFrequency = false;

    for (int i = 0; i < 10; i++) {
      if (diagnosis_frequencies[diagIdx][i] > 0) {
        hasFrequency = true;
        break;
      }
    }

    if (!hasFrequency) {
      DisplayErrorMessage(
        "No frequencies configured",
        ILI9341_RED
      );
      return;
    }
  }

  EEPROM.update(eepromAddress, selectedItem);
  titleLine = diagnosis_names[diagIdx];

  isGeneratingFrequency = true;

  // FIX - both output paths stay blocked before and during the delay.
  digitalWrite(pinOutputPause, HIGH);
  SetAmpOutput(false);
  gen.EnableOutput(false);
  ResetShortProtection();

  if (treatmentStartIsAngelZ) {
    SetSignalType(false);
  } else {
    SetSignalType(isSineWave);
  }

  // FIX - pin 12 LOW comes BEFORE the shared startup delay.
  SetAmpPower(true);

  // FIX - measure the delay from the pin 12 power-on operation.
  treatmentPowerOnAt = millis();
  treatmentStartPending = true;
  lastStartCountdown = 0xFFFFFFFFUL;

  DrawTitleBar();
}

void ServicePendingTreatment() {
  unsigned long elapsed = millis() - treatmentPowerOnAt;

  if (elapsed < START_TREATMENT_DELAY_MS) {
    unsigned long remaining =
      START_TREATMENT_DELAY_MS - elapsed;

    unsigned long secondsLeft =
      (remaining + 999UL) / 1000UL;

    if (secondsLeft != lastStartCountdown) {
      lastStartCountdown = secondsLeft;
      DrawStartCountdown(secondsLeft);
    }

    return;
  }

  // FIX - do not start while a possible cancel press is held.
  if (btnEnterPressed ||
      digitalRead(pinBtnEnter) == LOW) {
    return;
  }

  CheckBatteryLow();

  if (batteryLowLatched || btnEnterPressed) return;

  ClearStartCountdown();

  // FIX - check again after the display operation.
  CheckBatteryLow();

  if (batteryLowLatched ||
      btnEnterPressed ||
      digitalRead(pinBtnEnter) == LOW) {
    return;
  }

  treatmentStartPending = false;

  // FIX - do not apply rotation made during the startup delay.
  encoderMoved = false;

  if (treatmentStartIsAngelZ) {
    OpenAngelZ();

    isGeneratingFrequency = false;

    if (batteryLowLatched) return;

    ClearButtonPress();

    titleLine = (char*)"DIAGNOSES:";
    DrawTitleBar();
    DrawBattery();
    SetSelectedItem(selectedItem);
    return;
  }

  bool aborted = GenerateFrequency();
  isGeneratingFrequency = false;

  if (batteryLowLatched) return;

  ClearButtonPress();

  if (aborted) {
    titleLine = (char*)"DIAGNOSES:";
    DrawTitleBar();
    DrawBattery();
    SetSelectedItem(selectedItem);
  }
}

// ANGELZ SHORT RETRY
bool WaitForAngelZShortRetry() {
  while (emergencyShortDetected) {
    CheckBatteryLow();

    if (btnEnterPressed || batteryLowLatched) {
      AbortTreatment();
      return false;
    }

    unsigned long now = millis();

    if (now - lastShortCheckTime >= SHRT_CHECK_MS) {
      CheckAndHandleAmplifierShort();
      lastShortCheckTime = millis();
    }

    // FIX - allow interrupts to continue while the retry is pending.
    delay(1);
  }

  return true;
}

// ANGELZ SESSION
void OpenAngelZ() {
  // FIX - pin 12 has already been LOW for the shared delay.
  if (btnEnterPressed || batteryLowLatched) {
    AbortTreatment();
    return;
  }

  SetSignalType(false);

  unsigned long sessionStart = TreatmentClock();

  // FIX - prepare the AngelZ test signal while pin 11 is still HIGH.
  gen.ApplySignal(SQUARE_WAVE, REG0, 10000);
  gen.EnableOutput(true);

  CheckBatteryLow();

  if (btnEnterPressed || batteryLowLatched) {
    AbortTreatment();
    return;
  }

  // FIX - open pin 13, then set pin 11 LOW, then check A1.
  SetAmpOutput(true);
  digitalWrite(pinOutputPause, LOW);
  CheckAndHandleAmplifierShort();
  lastShortCheckTime = millis();

  // FIX - AngelZ remains pulsed after the initial A1 test.
  gen.EnableOutput(false);

  Sequences sequences(&gen);
  int currentStep = 0;
  bool forceAngelScreen = true;
  bool clearAngelScreen = true;

  for (int cycleNumber = 0;
       cycleNumber < ANGELZ_NUMBER_OF_CYCLES;
       ++cycleNumber) {
    for (int pointNumber = 0;
         pointNumber < ANGELZ_TOTAL_POINTS;
         ++pointNumber) {

      bool pointComplete = false;

      // FIX - retry the same point if a short interrupts its pulse.
      while (!pointComplete) {
        CheckBatteryLow();

        if (btnEnterPressed || batteryLowLatched) {
          AbortTreatment();
          return;
        }

        if (emergencyShortDetected) {
          forceAngelScreen = true;
          clearAngelScreen = true;

          if (!WaitForAngelZShortRetry()) return;

          // FIX - generator stays muted until the next AngelZ pulse.
          gen.EnableOutput(false);
        }

        CheckBatteryLow();

        if (btnEnterPressed || batteryLowLatched) {
          AbortTreatment();
          return;
        }

        // Original AngelZ pulse sequence.
        gen.EnableOutput(true);
        sequences.Execute10khzSequence();

        // FIX - evaluate A1 while the AngelZ pulse is active.
        CheckAndHandleAmplifierShort();
        lastShortCheckTime = millis();

        if (emergencyShortDetected) {
          gen.EnableOutput(false);
          continue;
        }

        delay(3);
        gen.EnableOutput(false);
        pointComplete = true;
      }

      if (clearAngelScreen) {
        tft.fillScreen(ILI9341_BLACK);
        clearAngelScreen = false;
      }

      int freqBarIndex =
        pointNumber * 24 / ANGELZ_TOTAL_POINTS;

      unsigned long elapsed =
        TreatmentClock() - sessionStart;

      DisplayTreatInProgressScreenAngelZ(
        freqBarIndex,
        elapsed,
        forceAngelScreen || currentStep == 0
      );

      forceAngelScreen = false;

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

  // FIX - close outputs and power off amplifiers after AngelZ.
  AbortTreatment();

  if (!batteryLowLatched) {
    tft.fillScreen(ILI9341_BLACK);

    u8g2gfx.setFont(u8g2_font_helvB24_te);
    u8g2gfx.setForegroundColor(ILI9341_GREEN);

    int textWidth =
      u8g2gfx.getUTF8Width("AngelZ Finished!");

    u8g2gfx.setCursor((320 - textWidth) / 2, 120);
    u8g2gfx.print("AngelZ Finished!");

    WaitBatterySafe(3000);
  }
}

// REGULAR TREATMENT
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

  if (numFreq == 0) {
    AbortTreatment();
    return true;
  }

  unsigned long secPerFreq =
    (unsigned long)diagnosis_time_sec[diagIdx];

  unsigned long fragmentMs = secPerFreq * 1000UL;
  unsigned long totalSessionMs = fragmentMs * numFreq;

  // FIX - pin 12 has already been LOW for the shared delay.
  if (btnEnterPressed || batteryLowLatched) {
    AbortTreatment();
    return true;
  }

  lastShortCheckTime = 0;
  lastShortBeepTime = 0;

  prevFreqIndex = -1;
  prevLevelValue = -1;
  prevLevelBars = -1;
  prevLevelNoSignal = true;
  treatmentScreenDrawn = false;

  unsigned long sessionStart = TreatmentClock();

  // FIX - prepare the first frequency while pin 11 is still HIGH.
  intFreqToGenerate =
    diagnosis_frequencies[diagIdx][freqIndices[0]];

  gen.ApplySignal(
    isSineWave ? SINE_WAVE : SQUARE_WAVE,
    REG0,
    intFreqToGenerate
  );

  gen.EnableOutput(true);

  CheckBatteryLow();

  if (btnEnterPressed || batteryLowLatched) {
    AbortTreatment();
    return true;
  }

  // FIX - open pin 13, then set pin 11 LOW, then check A1.
  SetAmpOutput(true);
  digitalWrite(pinOutputPause, LOW);
  CheckAndHandleAmplifierShort();
  lastShortCheckTime = millis();

  unsigned long lastSecond = 0;
  unsigned long lastLevelUpdate = 0;

  for (int i = 0; i < numFreq; i++) {
    if (btnEnterPressed || batteryLowLatched) {
      AbortTreatment();
      return true;
    }

    intFreqToGenerate =
      diagnosis_frequencies[diagIdx][freqIndices[i]];

    if (i > 0) {
      // FIX - frequency changes occur while pin 11 is HIGH.
      gen.ApplySignal(
        isSineWave ? SINE_WAVE : SQUARE_WAVE,
        REG0,
        intFreqToGenerate
      );

      CheckBatteryLow();

      if (btnEnterPressed || batteryLowLatched) {
        AbortTreatment();
        return true;
      }

      digitalWrite(pinOutputPause, LOW);

      // FIX - check A1 immediately after enabling the new frequency.
      CheckAndHandleAmplifierShort();
      lastShortCheckTime = millis();
    }

    unsigned long fragmentStartMs = TreatmentClock();

    unsigned long elapsed = TreatmentClock() - sessionStart;
    unsigned long msLeft = (elapsed < totalSessionMs)
      ? (totalSessionMs - elapsed)
      : 0;

    if (!emergencyShortDetected) {
      DisplayTreatInProgressScreen(
        freqIndices[i],
        selectedItem,
        msLeft,
        !treatmentScreenDrawn
      );

      UpdateSignalIndicator();
    }

    while (isGeneratingFrequency) {
      unsigned long now = millis();

      if (now - lastShortCheckTime >= SHRT_CHECK_MS) {
        CheckAndHandleAmplifierShort();
        lastShortCheckTime = millis();
      }

      CheckBatteryLow();

      if (btnEnterPressed || batteryLowLatched) {
        AbortTreatment();
        return true;
      }

      if (emergencyShortDetected) continue;

      unsigned long treatNow = TreatmentClock();

      // FIX - subtraction also works across millis() rollover.
      if (treatNow - fragmentStartMs >= fragmentMs) break;

      // Original encoder waveform-selection logic.
      if (encoderMoved) {
        int8_t direction = AnalyzeEncoderChange();

        if (direction > 0 && !isSineWave) {
          SetSignalType(true);
          gen.ApplySignal(SINE_WAVE, REG0, intFreqToGenerate);
          gen.SetOutputSource(REG0);
          UpdateSignalIndicator();
        } else if (direction < 0 && isSineWave) {
          SetSignalType(false);
          gen.ApplySignal(SQUARE_WAVE, REG0, intFreqToGenerate);
          UpdateSignalIndicator();
        }
      }

      if (now - lastLevelUpdate >= 50UL) {
        int lv = ReadLevelValue();
        DrawLevelIndicator(lv, false, false);
        lastLevelUpdate = now;
      }

      if (now - lastSecond >= 1000UL) {
        unsigned long elapsedTotal =
          treatNow - sessionStart;

        unsigned long msLeft2 =
          (elapsedTotal < totalSessionMs)
            ? (totalSessionMs - elapsedTotal)
            : 0;

        DisplayTreatInProgressScreen(
          freqIndices[i],
          selectedItem,
          msLeft2,
          false
        );

        UpdateSignalIndicator();
        lastSecond = now;
      }
    }

    digitalWrite(pinOutputPause, HIGH);
    prevFreqIndex = freqIndices[i];

    if (i < numFreq - 1) {
      PlayTone(ONE_BEEP);
    }
  }

  gen.EnableOutput(false);
  digitalWrite(pinOutputPause, HIGH);
  SetAmpOutput(false);
  SetAmpPower(false);

  unsigned long actualElapsed =
    TreatmentClock() - sessionStart;

  ResetShortProtection();
  isGeneratingFrequency = false;
  SetSignalType(true);

  tft.fillScreen(ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_helvB24_te);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);

  int textWidth = u8g2gfx.getUTF8Width("Finished!");
  u8g2gfx.setCursor((320 - textWidth) / 2, 120);
  u8g2gfx.print("Finished!");

  unsigned long totalSeconds = actualElapsed / 1000UL;
  unsigned long totalMin = totalSeconds / 60UL;
  unsigned long totalSec = totalSeconds % 60UL;

  char timeBuf[24];
  snprintf(
    timeBuf,
    sizeof(timeBuf),
    "Time: %02lu:%02lu",
    totalMin,
    totalSec
  );

  u8g2gfx.setFont(u8g2_font_t0_22b_tf);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);

  int tw = u8g2gfx.getUTF8Width(timeBuf);
  u8g2gfx.setCursor((320 - tw) / 2, 160);
  u8g2gfx.print(timeBuf);

  PlayTone(THREE_BEEPS);
  WaitBatterySafe(3000);

  // FIX - do not draw the menu after a battery-low latch.
  if (batteryLowLatched) return true;

  titleLine = (char*)"DIAGNOSES:";

  // Original end-of-treatment shutdown-pin behavior is retained.
  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);

  DrawTitleBar();
  DrawBattery();
  DrawList();

  return false;
}

void Shutdown() {
  // FIX - keep outputs inhibited before changing shutdown pins.
  treatmentStartPending = false;
  AbortTreatment();

  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);

  isGeneratingFrequency = false;
  btnEnterPressed = false;

  debugln("Shutdown initiated");
}

// ORIGINAL ENCODER AND BUTTON INTERRUPTS
void OnScrollChange() {
  encoderMoved = true;
}

int8_t AnalyzeEncoderChange() {
  encoderMoved = false;

  static uint8_t prev_AB = 0b00000011;
  static int8_t encoderVal = 0;

  uint8_t A = digitalRead(pinEncoderCW);
  uint8_t B = digitalRead(pinEncoderCCW);
  uint8_t current_AB = (A << 1) | B;
  uint8_t transition = (prev_AB << 2) | current_AB;

  static const int8_t deltaTable[16] = {
     0,-1, 1, 0,
     1, 0, 0,-1,
    -1, 0, 0, 1,
     0, 1,-1, 0
  };

  int8_t delta = deltaTable[transition];
  encoderVal += delta;

  int8_t result = 0;

  if (encoderVal <= -4) {
    result = -1;
    encoderVal = 0;
  }

  if (encoderVal >= 4) {
    result = 1;
    encoderVal = 0;
  }

  if (encoderVal <= -2) {
    result = -1;
    encoderVal += 2;
  }

  if (encoderVal >= 2) {
    result = 1;
    encoderVal -= 2;
  }

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
  boolean timeoutLong = (millis() > longTime);

  if (buttonState != lastButtonStatus) {
    shortTime = millis() + DEBOUNCE_DELTA;
    longTime = millis() + LONG_DELTA;
  }

  boolean buttonStateChange =
    (buttonState != lastButtonStatus);

  boolean buttonReleased =
    (buttonStateChange && (buttonState == HIGH));

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
  tft.fillRoundRect(
    FRAME_X,
    FRAME_Y,
    FRAME_W,
    FRAME_H,
    24,
    GOLD1
  );

  tft.drawRoundRect(
    FRAME_X + 5,
    FRAME_Y + 5,
    FRAME_W - 10,
    FRAME_H - 10,
    18,
    GOLD3
  );

  tft.drawRoundRect(
    FRAME_X + 12,
    FRAME_Y + 12,
    FRAME_W - 24,
    FRAME_H - 24,
    12,
    GOLD4
  );

  for (int i = 0; i < 20; i++) {
    tft.drawPixel(
      FRAME_X + 12 + i,
      FRAME_Y + 12 + (int)(sin(i * 0.4) * 8),
      GOLD2
    );

    tft.drawPixel(
      FRAME_X + FRAME_W - 12 - i,
      FRAME_Y + 12 + (int)(sin(i * 0.4) * 8),
      GOLD2
    );

    tft.drawPixel(
      FRAME_X + 12 + i,
      FRAME_Y + FRAME_H - 12 - (int)(sin(i * 0.4) * 8),
      GOLD2
    );

    tft.drawPixel(
      FRAME_X + FRAME_W - 12 - i,
      FRAME_Y + FRAME_H - 12 - (int)(sin(i * 0.4) * 8),
      GOLD2
    );
  }

  tft.fillRoundRect(
    FRAME_X + 20,
    FRAME_Y + 20,
    FRAME_W - 40,
    FRAME_H - 40,
    8,
    ILI9341_BLACK
  );
}

void DrawGoldenCrown(int cx, int topY) {
  const int w = 52;
  const int h = 28;
  const int baseH = 8;

  int x0 = cx - w / 2;
  int y0 = topY;

  tft.fillRoundRect(
    x0, y0 + h - baseH, w, baseH, baseH / 3, GOLD3
  );

  tft.drawRoundRect(
    x0, y0 + h - baseH, w, baseH, baseH / 3, GOLD1
  );

  tft.drawFastHLine(x0 + 3, y0 + h - baseH, w - 6, GOLD4);
  tft.drawFastHLine(x0 + 2, y0 + h - 1, w - 4, GOLD2);

  for (int i = 0; i < 5; i++) {
    int gemX = x0 + w * (i + 1) / 6;
    int gemY = y0 + h - baseH / 2;

    tft.fillCircle(gemX, gemY, baseH / 3, GOLD1);
    tft.drawCircle(gemX, gemY, baseH / 3, GOLD4);
  }

  int peakBaseY = y0 + h - baseH;
  int leftX = x0 + w / 4;
  int centerX = cx;
  int rightX = x0 + 3 * w / 4;

  int leftH = 12;
  int centerH = 16;
  int rightH = 12;

  tft.fillTriangle(
    leftX, peakBaseY,
    leftX - 6, peakBaseY - leftH,
    leftX + 6, peakBaseY - leftH,
    GOLD2
  );

  tft.fillCircle(leftX, peakBaseY - leftH - 2, 3, GOLD4);
  tft.drawCircle(leftX, peakBaseY - leftH - 2, 3, GOLD1);

  tft.fillTriangle(
    rightX, peakBaseY,
    rightX - 6, peakBaseY - rightH,
    rightX + 6, peakBaseY - rightH,
    GOLD2
  );

  tft.fillCircle(rightX, peakBaseY - rightH - 2, 3, GOLD4);
  tft.drawCircle(rightX, peakBaseY - rightH - 2, 3, GOLD1);

  tft.fillTriangle(
    centerX, peakBaseY,
    centerX - 8, peakBaseY - centerH,
    centerX + 8, peakBaseY - centerH,
    GOLD1
  );

  int crossX = centerX;
  int crossY = peakBaseY - centerH - 3;

  tft.drawLine(crossX, crossY - 3, crossX, crossY + 3, GOLD4);
  tft.drawLine(crossX - 3, crossY, crossX + 3, crossY, GOLD4);
  tft.fillCircle(crossX, crossY, 2, GOLD3);

  for (int i = 0; i <= 12; i++) {
    float t = i / 12.0;

    int x = leftX + (int)(t * (centerX - leftX));
    int y = peakBaseY -
            (int)(leftH * (1 - t)) -
            (int)(centerH * t) +
            (int)(4 * sin(t * PI));

    tft.drawPixel(x, y, GOLD2);
  }

  for (int i = 0; i <= 12; i++) {
    float t = i / 12.0;

    int x = centerX + (int)(t * (rightX - centerX));
    int y = peakBaseY -
            (int)(centerH * (1 - t)) -
            (int)(rightH * t) +
            (int)(4 * sin(t * PI));

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
    if (batteryLowLatched) break;

    tone(pinBeepOut, PIEZO_BEEP_TONE, PEIZO_BEEP_LENGTH);
    WaitBatterySafe(PEIZO_BEEP_LENGTH);

    noTone(pinBeepOut);
    WaitBatterySafe(PEIZO_BEEP_PAUSE);
  }
}

// SETUP
void setup() {
  Serial.begin(9600);

  // FIX - preload safe output levels before enabling output drivers.
  digitalWrite(pinOutputPause, HIGH);
  digitalWrite(pinAmpPower, HIGH);
  digitalWrite(pinAmpOutOff, LOW);
  digitalWrite(pinSignalType, LOW);

  pinMode(pinShutdown1, OUTPUT);
  pinMode(pinShutdown2, OUTPUT);
  pinMode(pinSignalType, OUTPUT);
  pinMode(pinSDPower, OUTPUT);
  pinMode(pinOutputPause, OUTPUT);
  pinMode(pinAmpPower, OUTPUT);
  pinMode(pinAmpOutOff, OUTPUT);

  digitalWrite(pinShutdown1, HIGH);
  digitalWrite(pinShutdown2, LOW);
  digitalWrite(pinSDPower, HIGH);

  SetSignalType(true);
  SetAmpPower(false);
  SetAmpOutput(false);
  digitalWrite(pinOutputPause, HIGH);

  pinMode(pinEncoderCW, INPUT_PULLUP);
  pinMode(pinEncoderCCW, INPUT_PULLUP);
  pinMode(pinBtnEnter, INPUT_PULLUP);
  pinMode(pinLevelInput, INPUT);

  tft.begin();
  tft.setRotation(1);

  u8g2gfx.begin(tft);
  u8g2gfx.setFontMode(1);
  u8g2gfx.setFontDirection(0);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);

  analogReference(INTERNAL1V1);
  analogRead(pinLevelInput);

  gen.Begin();
  gen.EnableOutput(false);

  DisplayIntroScreen();
  delay(2500);

  digitalWrite(pinSDPower, LOW);
  delay(100);

  InitializeSDAndSettings();

  digitalWrite(pinSDPower, HIGH);

  DrawTitleBar();
  DrawBattery();

  byte saved = EEPROM.read(eepromAddress);

  if (saved >= 1 && (int)saved <= num_diagnoses) {
    SetSelectedItem(saved);
  } else {
    SetSelectedItem(1);
  }

  // Original interrupt assignments are retained.
  attachInterrupt(
    digitalPinToInterrupt(pinEncoderCW),
    OnScrollChange,
    CHANGE
  );

  attachInterrupt(
    digitalPinToInterrupt(pinEncoderCCW),
    OnScrollChange,
    CHANGE
  );

  attachInterrupt(
    digitalPinToInterrupt(pinBtnEnter),
    OnButtonPress,
    CHANGE
  );
}

// MAIN LOOP
void loop() {
  CheckBatteryLow();

  if (batteryLowLatched) {
    // FIX - a battery latch cancels any pending treatment start.
    treatmentStartPending = false;
    isGeneratingFrequency = false;

    if (ampOutputOpen) SetAmpOutput(false);
    if (ampPowerOn) SetAmpPower(false);

    if (batteryAlertPending) ShowBatteryLowAlert();

    int action = STATE_NORMAL;

    if (TakeButtonPress(action) && action == STATE_LONG) {
      Shutdown();
    }

    return;
  }

  if (treatmentStartPending) {
    int action = STATE_NORMAL;

    if (TakeButtonPress(action)) {
      // FIX - another click cancels startup; a long click shuts down.
      treatmentStartPending = false;
      isGeneratingFrequency = false;
      AbortTreatment();

      if (action == STATE_LONG) {
        Shutdown();
      } else {
        titleLine = (char*)"DIAGNOSES:";
        DrawTitleBar();
        DrawBattery();
        SetSelectedItem(selectedItem);
      }

      return;
    }

    // FIX - both regular treatment and AngelZ use the same timer.
    ServicePendingTreatment();
    return;
  }

  if (millis() - lastShortCheckTime >= 200UL) {
    CheckAndHandleAmplifierShort();
    lastShortCheckTime = millis();
  }

  // Original menu encoder processing is retained.
  if (encoderMoved) {
    int8_t direction = AnalyzeEncoderChange();

    if (direction != 0 &&
        !isGeneratingFrequency &&
        num_diagnoses > 0) {
      ScrollItem(direction > 0 ? SCROLL_UP : SCROLL_DOWN);
    }
  }

  int action = STATE_NORMAL;

  if (TakeButtonPress(action)) {
    switch (action) {
      case STATE_SHORT:
        ProcessButtonClick();
        break;

      case STATE_LONG:
        Shutdown();
        break;
    }
  }
}

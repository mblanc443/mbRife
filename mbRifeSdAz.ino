// Rife Machine - Arduino Mega2560 + ILI9341 + AD9833 + SD card + AngelZ
// Pin 8: SIN = LOW, SQUARE = HIGH
// Pin 11: HIGH = output paused, LOW = output enabled
// Pin 12: HIGH = amplifier power OFF, LOW = amplifier power ON
// Pin 13: LOW = amplifier outputs blocked, HIGH = outputs open
// A1: output level and short detection
//
// FIX - regular treatment and AngelZ use the same start sequence:
// pin 12 LOW -> start delay -> pin 11 LOW -> pin 13 HIGH -> check A1.
// FIX - start delay is serviced in loop(), without delay() or a waiting loop.
// FIX - a short immediately pauses pin 11, blocks pin 13 and mutes AD9833.
// FIX - short retry is fixed at SHRT_RETRY_SEC = 4; no range check.

#include <EEPROM.h>
#include <AD9833.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include <avr/pgmspace.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DEBUG 0
#if DEBUG
  #define debug(x)   Serial.print(x)
  #define debugln(x) Serial.println(x)
#else
  #define debug(x)
  #define debugln(x)
#endif

// USER CONFIGURATION
#define SHRT_RETRY_SEC             4
#define SHRT_RETRY_MS              (SHRT_RETRY_SEC * 1000UL)
#define START_TREATMENT_DELAY_SEC  2
#define START_TREATMENT_DELAY_MS   (START_TREATMENT_DELAY_SEC * 1000UL)

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

// Display and generator
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2gfx;
AD9833 gen(pinGenCS);

// Gold color palette
#define GOLD1 tft.color565(255, 215, 0)
#define GOLD2 tft.color565(218, 165, 32)
#define GOLD3 tft.color565(184, 134, 11)
#define GOLD4 tft.color565(255, 236, 139)

// Level indicator colors
#define LEVEL_GREEN_ACTIVE  tft.color565(0, 220, 0)
#define LEVEL_GREEN_DIM     tft.color565(0, 60, 0)
#define LEVEL_RED_ACTIVE    tft.color565(255, 30, 30)
#define LEVEL_RED_DIM       tft.color565(60, 0, 0)
#define LEVEL_GRAY          tft.color565(80, 80, 80)

// Treatment and protection constants
#define NAME_MAX_LEN       28
#define FREQS_PER_DIAG     10
#define ITEMS_PER_PAGE      8
#define SHRT_THRESHOLD_MAX 1020
#define SHRT_CHECK_MS      100UL
#define SHRT_RECHECK_MS    500UL
#define SHRT_BEEP_FREQ     1000
#define SHRT_BEEP_MS       500UL
#define SHRT_BEEP_DURATION 100

#define BATTERY_LOW_COUNT  3
#define BATTERY_CHECK_MS   1000UL

#define LEVEL_NUM_BARS     20
#define LEVEL_GREEN_BARS   14
#define LEVEL_BAR_WIDTH    16
#define LEVEL_BAR_GAP       2
#define LEVEL_BAR_Y       228
#define LEVEL_BAR_H        12
#define LEVEL_LABEL_Y     210

#define ANGELZ_TOTAL_POINTS 48
#define ANGELZ_CYCLES       59
#define ANGELZ_SPLIT_POINT  24

const float R1 = 32000.0f;
const float R2 = 8000.0f;
const float referenceVoltage = 1.1f;
const float BATTERY_LOW_V = 3.3f;
const float ANGELZ_DELAY_SCALE = 3.734f;

// Default diagnoses
const char* default_diagnoses_raw[] = {
  "Day1-Love expands:780",
  "Day2-Cellular and DNA repair:480",
  "Day3-Release Fear, let go:660",
  "H.Clark Zapper:120",
  "AngelZ:0"
};

const int default_frequencies[] PROGMEM = {
  465,0,0,0,0,0,0,0,0,0,
  528,0,0,0,0,0,0,0,0,0,
  417,0,0,0,0,0,0,0,0,0,
  32000,1150,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0
};

// Dynamic diagnosis storage
struct Diagnosis {
  char name[NAME_MAX_LEN + 1];
  uint16_t seconds;
  uint16_t freq[FREQS_PER_DIAG];
};

Diagnosis* diagnoses = NULL;
uint8_t numDiagnoses = 0;
uint8_t diagnosisCapacity = 0;

// UI and operating state
enum RunState {
  RUN_IDLE,
  RUN_STARTING,
  RUN_REGULAR,
  RUN_ANGELZ,
  RUN_BATTERY,
  RUN_SHUTDOWN
};

RunState runState = RUN_IDLE;
bool startingAngelZ = false;
unsigned long startRequestedAt = 0;
int startCountdownShown = -1;

uint8_t selectedItem = 1;
uint8_t previousItem = 1;
uint8_t pageOffset = 0;
bool isSineWave = true;

bool ampPowerOn = false;
bool ampOutputOpen = false;
bool batteryLowLatched = false;
bool batteryAlertPending = false;
uint8_t batteryLowCount = 0;
unsigned long lastBatteryCheckTime = 0;

bool emergencyShortDetected = false;
bool shortRechecking = false;
unsigned long shortBlockStartTime = 0;
unsigned long shortRecheckStartTime = 0;
unsigned long shortPausedTotal = 0;
unsigned long lastShortCheckTime = 0;
unsigned long lastShortBeepTime = 0;

volatile bool encoderMoved = false;
volatile uint8_t buttonEvent = 0;        // 1 = short press, 2 = long press
volatile unsigned long buttonPressedAt = 0;
volatile unsigned long buttonLastEdge = 0;

int8_t previousFrequencyBar = -1;
char previousTimeText[6] = "";
int previousLevelBars = -1;
bool previousLevelNoSignal = true;
bool treatmentScreenDrawn = false;

// AngelZ sequences
float angelz_valueStart[ANGELZ_CYCLES] = {
  20.0,15.0,18.0,25.0,22.0,30.0,21.0,35.0,23.0,40.0,
  24.0,45.0,16.0,27.0,19.0,31.0,13.0,28.0,14.0,34.0,
  12.0,39.0,10.0,42.0,11.0,37.0,9.0,33.0,8.0,36.0,
  7.0,29.0,6.0,32.0,5.0,38.0,4.0,41.0,3.0,44.0,
  2.0,43.0,1.0,46.0,0.5,48.0,0.2,50.0,0.1,52.0,
  0.0,53.0,12.0,17.0,22.0,28.0,34.0,41.0,45.0
};

float angelz_valueMax[ANGELZ_CYCLES] = {
  52.6,45.0,55.0,60.0,50.0,65.0,53.0,70.0,56.0,75.0,
  59.0,80.0,48.0,62.0,51.0,64.0,49.0,67.0,50.0,71.0,
  46.0,73.0,47.0,76.0,54.0,78.0,57.0,79.0,58.0,82.0,
  61.0,85.0,63.0,88.0,65.0,90.0,68.0,92.0,66.0,95.0,
  69.0,97.0,72.0,99.0,74.0,100.0,77.0,102.0,80.0,105.0,
  82.0,110.0,55.0,60.0,65.0,70.0,75.0,85.0,90.0
};

float angelz_tau[ANGELZ_CYCLES] = {
  5.0,7.0,4.5,6.0,3.5,5.5,4.0,6.5,3.0,5.0,
  4.3,6.8,3.8,6.1,3.2,6.4,4.2,5.6,3.9,6.7,
  4.4,5.9,4.1,6.9,3.6,5.7,3.3,6.3,3.1,6.2,
  4.6,6.6,3.7,5.8,4.9,7.0,3.4,6.5,4.8,7.1,
  5.1,7.2,3.9,6.0,3.5,6.8,4.2,6.3,5.0,7.3,
  5.2,7.4,4.0,5.5,6.0,6.5,5.7,6.8,7.1
};

// FORWARD DECLARATIONS
void SetSignalType(bool sine);
void SetAmpPower(bool on);
void SetAmpOutput(bool open);
void AbortTreatment();
void Shutdown();
bool CheckBatteryLow();
void ShowBatteryLowAlert();
void CheckAndHandleAmplifierShort();
void ResetShortProtection();
unsigned long TreatmentClock();
void DrawMenu();
void DrawRegularScreen(uint8_t freqIndex, unsigned long msLeft,
                       bool full);
void DrawAngelZScreen(uint8_t barIndex, unsigned long elapsedMs,
                      bool full);
void RunRegularTreatment();
void RunAngelZTreatment();
void StartSelectedTreatment();
void ServiceStarting();
uint8_t TakeButtonEvent();
void OnButtonChange();
void OnEncoderChange();

// HARDWARE HELPERS

// FIX - keep pin 8 and the waveform-selection state synchronized.
void SetSignalType(bool sine) {
  isSineWave = sine;
  digitalWrite(pinSignalType, sine ? LOW : HIGH);
}

// FIX - pin 12 is driven from one function only.
void SetAmpPower(bool on) {
  ampPowerOn = on;
  digitalWrite(pinAmpPower, on ? LOW : HIGH);
  debugln(on ? "AMP POWER ON" : "AMP POWER OFF");
}

// FIX - pin 13 is driven from one function only.
void SetAmpOutput(bool open) {
  ampOutputOpen = open;
  digitalWrite(pinAmpOutOff, open ? HIGH : LOW);
  debugln(open ? "AMP OUTPUT OPEN" : "AMP OUTPUT BLOCKED");
}

float MeasureBatteryVoltage() {
  int raw = analogRead(pinBatteryLevel);
  float adcVoltage = raw * referenceVoltage / 1024.0f;
  return adcVoltage * (R1 + R2) / R2;
}

int ReadShortLevel() {
  long total = 0;
  for (uint8_t i = 0; i < 4; i++) {
    total += analogRead(pinLevelInput);
  }
  return (int)(total / 4);
}

int ReadLevelBars() {
  int raw = ReadShortLevel();
  int bars = map(raw, 0, 1023, 0, LEVEL_NUM_BARS);
  return constrain(bars, 0, LEVEL_NUM_BARS);
}

// BATTERY AND SHORT PROTECTION

bool CheckBatteryLow() {
  if (batteryLowLatched) return true;

  unsigned long now = millis();
  if (now - lastBatteryCheckTime < BATTERY_CHECK_MS) return false;
  lastBatteryCheckTime = now;

  if (MeasureBatteryVoltage() <= BATTERY_LOW_V) {
    if (batteryLowCount < BATTERY_LOW_COUNT) batteryLowCount++;
  } else {
    batteryLowCount = 0;
  }

  if (batteryLowCount >= BATTERY_LOW_COUNT) {
    batteryLowLatched = true;
    batteryAlertPending = true;

    // FIX - low battery immediately inhibits every output.
    digitalWrite(pinOutputPause, HIGH);
    SetAmpOutput(false);
    gen.EnableOutput(false);
    SetAmpPower(false);
    SetSignalType(true);
    noTone(pinBeepOut);

    debugln("BATTERY LOW - LATCHED");
  }
  return batteryLowLatched;
}

void ShowBatteryLowAlert() {
  batteryAlertPending = false;
  tft.fillScreen(ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_helvB14_te);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_RED);
  u8g2gfx.setCursor(35, 110);
  u8g2gfx.print("Battery Low - Charge it");
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  u8g2gfx.setCursor(60, 150);
  u8g2gfx.print("Power OFF to exit");
}

void ResetShortProtection() {
  emergencyShortDetected = false;
  shortRechecking = false;
  shortPausedTotal = 0;
  noTone(pinBeepOut);
}

unsigned long TreatmentClock() {
  unsigned long now = millis();
  unsigned long result = now - shortPausedTotal;
  if (emergencyShortDetected) {
    result -= now - shortBlockStartTime;
  }
  return result;
}

void DrawShortAlert() {
  tft.fillRect(0, 30, 320, 210, ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_helvB14_te);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_RED);
  u8g2gfx.setCursor(45, 100);
  u8g2gfx.print("AMPLIFIER SHORT!");
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  u8g2gfx.setCursor(35, 140);
  u8g2gfx.print("Outputs blocked, retry 4 s");
}

void BlockAmpOutput() {
  emergencyShortDetected = true;
  shortRechecking = false;
  shortBlockStartTime = millis();
  lastShortBeepTime = shortBlockStartTime;

  // FIX - protection is immediate; only the retry uses SHRT_RETRY_MS.
  digitalWrite(pinOutputPause, HIGH);
  SetAmpOutput(false);
  gen.EnableOutput(false);

  tone(pinBeepOut, SHRT_BEEP_FREQ, SHRT_BEEP_DURATION);
  treatmentScreenDrawn = false;
  DrawShortAlert();
}

void UnblockAmpOutput() {
  if (emergencyShortDetected) {
    shortPausedTotal += millis() - shortBlockStartTime;
  }
  emergencyShortDetected = false;
  shortRechecking = true;
  shortRecheckStartTime = millis();
  noTone(pinBeepOut);

  // FIX - re-check uses the active waveform, including AngelZ square wave.
  gen.EnableOutput(true);
  digitalWrite(pinOutputPause, LOW);
  SetAmpOutput(true);
}

void CheckAndHandleAmplifierShort() {
  if (!ampPowerOn || batteryLowLatched) return;

  unsigned long now = millis();

  if (emergencyShortDetected) {
    if (now - shortBlockStartTime >= SHRT_RETRY_MS) {
      UnblockAmpOutput();
    } else if (now - lastShortBeepTime >= SHRT_BEEP_MS) {
      lastShortBeepTime = now;
      tone(pinBeepOut, SHRT_BEEP_FREQ, SHRT_BEEP_DURATION);
    }
    return;
  }

  // FIX - A1 is checked only after pin 11 has gone LOW and pin 13 has opened.
  if (ampOutputOpen &&
      digitalRead(pinOutputPause) == LOW) {
    int raw = ReadShortLevel();
    debug("A1 raw: ");
    debugln(raw);

    if (raw >= SHRT_THRESHOLD_MAX) {
      BlockAmpOutput();
      return;
    }
  }

  if (shortRechecking &&
      now - shortRecheckStartTime >= SHRT_RECHECK_MS) {
    shortRechecking = false;
    treatmentScreenDrawn = false;
    debugln("SHORT CLEARED");
  }
}

void AbortTreatment() {
  // FIX - clear output before removing amplifier power.
  digitalWrite(pinOutputPause, HIGH);
  SetAmpOutput(false);
  gen.EnableOutput(false);
  SetAmpPower(false);
  ResetShortProtection();
  SetSignalType(true);
}

void Shutdown() {
  AbortTreatment();
  runState = RUN_SHUTDOWN;
  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);
}

// INPUT

void OnEncoderChange() {
  encoderMoved = true;
}

// FIX - generate one event per released press; ignore contact bounce.
void OnButtonChange() {
  unsigned long now = millis();
  bool pressed = (digitalRead(pinBtnEnter) == LOW);

  if (now - buttonLastEdge < 30UL) return;
  buttonLastEdge = now;

  if (pressed) {
    buttonPressedAt = now;
  } else {
    unsigned long held = now - buttonPressedAt;
    if (held >= 30UL && buttonEvent == 0) {
      buttonEvent = (held >= 1500UL) ? 2 : 1;
    }
  }
}

uint8_t TakeButtonEvent() {
  noInterrupts();
  uint8_t result = buttonEvent;
  buttonEvent = 0;
  interrupts();
  return result;
}

int8_t AnalyzeEncoderChange() {
  encoderMoved = false;

  static uint8_t previousAB = 3;
  static int8_t accumulator = 0;
  static const int8_t deltaTable[16] = {
     0,-1, 1, 0,
     1, 0, 0,-1,
    -1, 0, 0, 1,
     0, 1,-1, 0
  };

  uint8_t a = digitalRead(pinEncoderCW);
  uint8_t b = digitalRead(pinEncoderCCW);
  uint8_t currentAB = (a << 1) | b;
  uint8_t index = (previousAB << 2) | currentAB;

  accumulator += deltaTable[index];
  previousAB = currentAB;

  if (accumulator >= 2) {
    accumulator -= 2;
    return 1;
  }
  if (accumulator <= -2) {
    accumulator += 2;
    return -1;
  }
  return 0;
}

// DIAGNOSIS STORAGE AND SD

bool EnsureDiagnosisCapacity() {
  if (numDiagnoses < diagnosisCapacity) return true;

  // FIX - one realloc keeps diagnosis data together and avoids partial arrays.
  uint8_t nextCapacity = diagnosisCapacity + 8;
  Diagnosis* next =
    (Diagnosis*)realloc(diagnoses,
                        (size_t)nextCapacity * sizeof(Diagnosis));
  if (!next) return false;

  diagnoses = next;
  diagnosisCapacity = nextCapacity;
  return true;
}

bool AddDiagnosis(const char* nameWithTime, const uint16_t* freqs) {
  if (!EnsureDiagnosisCapacity()) return false;

  Diagnosis& entry = diagnoses[numDiagnoses];
  char work[NAME_MAX_LEN + 16];
  strncpy(work, nameWithTime, sizeof(work) - 1);
  work[sizeof(work) - 1] = '\0';

  char* colon = strrchr(work, ':');
  long seconds = 60;
  if (colon) {
    *colon++ = '\0';
    seconds = atol(colon);
    if (seconds < 1 && strcmp(work, "AngelZ") != 0) {
      seconds = 60;
    }
  }

  if (seconds > 65535L) seconds = 65535L;
  strncpy(entry.name, work, NAME_MAX_LEN);
  entry.name[NAME_MAX_LEN] = '\0';
  entry.seconds = (uint16_t)seconds;

  for (uint8_t i = 0; i < FREQS_PER_DIAG; i++) {
    entry.freq[i] = freqs[i];
  }

  numDiagnoses++;
  return true;
}

void LoadDefaultDiagnoses() {
  numDiagnoses = 0;
  const uint8_t count =
    sizeof(default_diagnoses_raw) /
    sizeof(default_diagnoses_raw[0]);

  for (uint8_t i = 0; i < count; i++) {
    uint16_t f[FREQS_PER_DIAG];

    for (uint8_t j = 0; j < FREQS_PER_DIAG; j++) {
      f[j] = pgm_read_word(
        &default_frequencies[i * FREQS_PER_DIAG + j]
      );
    }

    if (!AddDiagnosis(default_diagnoses_raw[i], f)) break;
  }
}

void CreateDefaultSettingsFile() {
  SD.remove("settings.txt");
  File file = SD.open("settings.txt", FILE_WRITE);
  if (!file) return;

  file.println("## Name:seconds,freq1,...,freq10");
  file.println("[diagnoses]");

  const uint8_t count =
    sizeof(default_diagnoses_raw) /
    sizeof(default_diagnoses_raw[0]);

  for (uint8_t i = 0; i < count; i++) {
    file.print(default_diagnoses_raw[i]);
    for (uint8_t j = 0; j < FREQS_PER_DIAG; j++) {
      file.print(',');
      file.print(pgm_read_word(
        &default_frequencies[i * FREQS_PER_DIAG + j]
      ));
    }
    file.println();
  }
  file.close();
}

bool ParseDiagnosisLine(char* line) {
  char* comma = strchr(line, ',');
  if (!comma) return false;
  *comma++ = '\0';

  uint16_t freqs[FREQS_PER_DIAG];

  for (uint8_t i = 0; i < FREQS_PER_DIAG; i++) {
    char* end = comma;
    while (isdigit((unsigned char)*end)) end++;
    if (end == comma) return false;

    unsigned long value = strtoul(comma, NULL, 10);
    if (value > 65535UL) return false;
    freqs[i] = (uint16_t)value;

    if (i < FREQS_PER_DIAG - 1) {
      if (*end != ',') return false;
      comma = end + 1;
    } else {
      if (*end != '\0') return false;
    }
  }

  return AddDiagnosis(line, freqs);
}

void InitializeSDAndSettings() {
  // FIX - standard Arduino SD library uses SD.begin(csPin).
  if (!SD.begin(SD_CS)) {
    LoadDefaultDiagnoses();
    return;
  }

  File file = SD.open("settings.txt", FILE_READ);
  if (!file) {
    CreateDefaultSettingsFile();
    file = SD.open("settings.txt", FILE_READ);
  }

  if (!file) {
    LoadDefaultDiagnoses();
    return;
  }

  numDiagnoses = 0;
  bool inDiagnoses = false;
  char line[160];

  while (file.available()) {
    size_t length = 0;
    bool overflow = false;

    while (file.available()) {
      int c = file.read();
      if (c == '\n') break;
      if (c == '\r') continue;

      if (length < sizeof(line) - 1) {
        line[length++] = (char)c;
      } else {
        overflow = true;
      }
    }

    line[length] = '\0';
    if (overflow || length == 0 || line[0] == '#') continue;

    if (line[0] == '[') {
      inDiagnoses = (strcmp(line, "[diagnoses]") == 0);
      continue;
    }

    if (inDiagnoses) {
      // Invalid entries are skipped; valid entries remain available.
      ParseDiagnosisLine(line);
    }
  }

  file.close();
  if (numDiagnoses == 0) LoadDefaultDiagnoses();
}

// DISPLAY

void DrawTitleBar(const char* title) {
  tft.fillRect(0, 0, 320, 30, ILI9341_BLUE);
  u8g2gfx.setFont(u8g2_font_t0_22b_tf);
  u8g2gfx.setBackgroundColor(ILI9341_BLUE);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);

  int width = u8g2gfx.getUTF8Width(title);
  u8g2gfx.setCursor(max(2, (320 - width) / 2), 24);
  u8g2gfx.print(title);
}

void DrawBattery() {
  float voltage = MeasureBatteryVoltage();
  char text[12];
  dtostrf(voltage, 3, 1, text);
  strcat(text, "v");

  u8g2gfx.setFont(u8g2_font_9x18B_tf);
  u8g2gfx.setBackgroundColor(ILI9341_BLUE);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);

  int width = u8g2gfx.getUTF8Width(text);
  u8g2gfx.setCursor(320 - width - 8, 23);
  u8g2gfx.print(text);
}

void DrawMenu() {
  DrawTitleBar("DIAGNOSES:");
  DrawBattery();

  tft.fillRect(0, 30, 320, 210, ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_10x20_t_cyrillic);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);

  if (numDiagnoses == 0) {
    u8g2gfx.setForegroundColor(ILI9341_RED);
    u8g2gfx.setCursor(20, 90);
    u8g2gfx.print("No diagnoses");
    return;
  }

  pageOffset =
    ((selectedItem - 1) / ITEMS_PER_PAGE) * ITEMS_PER_PAGE;

  for (uint8_t row = 0; row < ITEMS_PER_PAGE; row++) {
    uint8_t index = pageOffset + row;
    if (index >= numDiagnoses) break;

    int y = 50 + row * 24;
    bool selected = (index == selectedItem - 1);
    if (selected) {
      tft.fillRect(8, y - 19, 304, 24, ILI9341_NAVY);
    }

    u8g2gfx.setForegroundColor(
      selected ? ILI9341_YELLOW : ILI9341_GREEN
    );
    u8g2gfx.setCursor(18, y);
    u8g2gfx.print(diagnoses[index].name);
  }
}

// FIX - draw intro background by scanline, without an initial black clear.
void DrawIntroScreen() {
  for (int y = 0; y < 240; y++) {
    if (y < 2 || y >= 238) {
      tft.drawFastHLine(0, y, 320, ILI9341_BLUE);
    } else if (y < 22 || y >= 218) {
      tft.drawFastHLine(0, y, 2, ILI9341_BLUE);
      tft.drawFastHLine(2, y, 316, GOLD1);
      tft.drawFastHLine(318, y, 2, ILI9341_BLUE);
    } else {
      tft.drawFastHLine(0, y, 2, ILI9341_BLUE);
      tft.drawFastHLine(2, y, 20, GOLD1);
      tft.drawFastHLine(22, y, 276, ILI9341_BLACK);
      tft.drawFastHLine(298, y, 20, GOLD1);
      tft.drawFastHLine(318, y, 2, ILI9341_BLUE);
    }
  }

  // Decorative outlines overlay the completed background once.
  tft.drawRoundRect(7, 7, 306, 226, 18, GOLD3);
  tft.drawRoundRect(14, 14, 292, 212, 12, GOLD4);

  u8g2gfx.setFont(u8g2_font_helvB24_te);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(GOLD2);

  const char* line1 = "Dr. Royal Rife";
  const char* line2 = "Healing Machine";
  u8g2gfx.setCursor(
    (320 - u8g2gfx.getUTF8Width(line1)) / 2, 80
  );
  u8g2gfx.print(line1);
  u8g2gfx.setCursor(
    (320 - u8g2gfx.getUTF8Width(line2)) / 2, 120
  );
  u8g2gfx.print(line2);

  // Crown
  tft.fillTriangle(140, 158, 146, 139, 154, 158, GOLD2);
  tft.fillTriangle(151, 158, 160, 131, 169, 158, GOLD1);
  tft.fillTriangle(166, 158, 174, 139, 180, 158, GOLD2);
  tft.fillRoundRect(134, 155, 52, 9, 2, GOLD3);
  tft.drawFastHLine(137, 156, 46, GOLD4);

  u8g2gfx.setFont(u8g2_font_7x14_tr);
  u8g2gfx.setForegroundColor(ILI9341_MAGENTA);
  u8g2gfx.setCursor(102, 190);
  u8g2gfx.print("by kd2cmo 2026");
  u8g2gfx.setCursor(138, 210);
  u8g2gfx.print("Wait...");
}

void DrawSignalIndicator() {
  tft.fillRect(259, 0, 61, 30, ILI9341_BLUE);
  u8g2gfx.setFont(u8g2_font_10x20_t_cyrillic);
  u8g2gfx.setBackgroundColor(ILI9341_BLUE);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);

  const char* text = isSineWave ? "SIN" : "_||_";
  int width = u8g2gfx.getUTF8Width(text);
  u8g2gfx.setCursor(316 - width, 24);
  u8g2gfx.print(text);
}

void DrawLevelIndicator(int bars, bool noSignal, bool full) {
  bars = constrain(bars, 0, LEVEL_NUM_BARS);

  if (full || previousLevelNoSignal != noSignal ||
      previousLevelBars != bars) {
    for (int i = 0; i < LEVEL_NUM_BARS; i++) {
      uint16_t color;

      if (noSignal) {
        color = LEVEL_GRAY;
      } else if (i < bars) {
        color = (i < LEVEL_GREEN_BARS) ?
          LEVEL_GREEN_ACTIVE : LEVEL_RED_ACTIVE;
      } else {
        color = (i < LEVEL_GREEN_BARS) ?
          LEVEL_GREEN_DIM : LEVEL_RED_DIM;
      }

      tft.fillRect(i * LEVEL_BAR_WIDTH, LEVEL_BAR_Y,
                   LEVEL_BAR_WIDTH - LEVEL_BAR_GAP,
                   LEVEL_BAR_H, color);
    }

    tft.fillRect(0, LEVEL_LABEL_Y, 320, 18, ILI9341_BLACK);
    u8g2gfx.setFont(u8g2_font_helvB14_te);
    u8g2gfx.setBackgroundColor(ILI9341_BLACK);
    u8g2gfx.setForegroundColor(
      noSignal ? LEVEL_GRAY : ILI9341_WHITE
    );

    char label[18];
    if (noSignal) {
      strcpy(label, "No Signal");
    } else {
      snprintf(label, sizeof(label), "Level: %d", bars);
    }

    u8g2gfx.setCursor(
      (320 - u8g2gfx.getUTF8Width(label)) / 2,
      LEVEL_LABEL_Y + 15
    );
    u8g2gfx.print(label);
  }

  previousLevelBars = bars;
  previousLevelNoSignal = noSignal;
}

void DrawRegularScreen(uint8_t freqIndex,
                       unsigned long msLeft, bool full) {
  Diagnosis& diagnosis = diagnoses[selectedItem - 1];

  if (full || !treatmentScreenDrawn) {
    tft.fillScreen(ILI9341_BLACK);

    char title[48];
    snprintf(title, sizeof(title), "%s:%u",
             diagnosis.name, diagnosis.seconds);
    DrawTitleBar(title);
    DrawSignalIndicator();

    for (uint8_t i = 0; i < FREQS_PER_DIAG; i++) {
      int y = 30 + i * 18;
      uint16_t color =
        (diagnosis.freq[i] == 0) ?
        tft.color565(20, 20, 20) :
        ((i == freqIndex) ?
         ILI9341_GREENYELLOW : ILI9341_DARKGREEN);

      tft.fillRect(4, y, 98, 17, color);

      char freqText[9];
      if (diagnosis.freq[i]) {
        snprintf(freqText, sizeof(freqText), "%u",
                 diagnosis.freq[i]);
      } else {
        strcpy(freqText, "-");
      }

      u8g2gfx.setFont(u8g2_font_helvB14_te);
      u8g2gfx.setBackgroundColor(color);
      u8g2gfx.setForegroundColor(ILI9341_PINK);
      int w = u8g2gfx.getUTF8Width(freqText);
      u8g2gfx.setCursor(4 + (98 - w) / 2, y + 15);
      u8g2gfx.print(freqText);
    }

    u8g2gfx.setFont(u8g2_font_helvB24_te);
    u8g2gfx.setBackgroundColor(ILI9341_BLACK);
    u8g2gfx.setForegroundColor(ILI9341_WHITE);
    u8g2gfx.setCursor(125, 100);
    u8g2gfx.print("Remaining");

    // FIX - do not sample A1 merely to draw the start screen.
    DrawLevelIndicator(0, true, true);
    previousFrequencyBar = freqIndex;
    previousTimeText[0] = '\0';
    treatmentScreenDrawn = true;
  }

  if (previousFrequencyBar != (int8_t)freqIndex) {
    // FIX - frequency change redraws only the narrow frequency area.
    for (uint8_t n = 0; n < 2; n++) {
      uint8_t i = (n == 0) ?
        (uint8_t)previousFrequencyBar : freqIndex;
      if (i >= FREQS_PER_DIAG) continue;

      int y = 30 + i * 18;
      uint16_t color =
        (i == freqIndex) ?
        ILI9341_GREENYELLOW : ILI9341_DARKGREEN;
      tft.fillRect(4, y, 98, 17, color);

      char text[9];
      snprintf(text, sizeof(text), "%u", diagnosis.freq[i]);
      u8g2gfx.setFont(u8g2_font_helvB14_te);
      u8g2gfx.setBackgroundColor(color);
      u8g2gfx.setForegroundColor(ILI9341_PINK);
      u8g2gfx.setCursor(
        4 + (98 - u8g2gfx.getUTF8Width(text)) / 2,
        y + 15
      );
      u8g2gfx.print(text);
    }
    previousFrequencyBar = freqIndex;
  }

  unsigned long secondsLeft = msLeft / 1000UL;
  char timeText[12];
  snprintf(timeText, sizeof(timeText), "%02lu:%02lu",
           secondsLeft / 60UL, secondsLeft % 60UL);

  if (strcmp(timeText, previousTimeText) != 0) {
    tft.fillRect(110, 133, 210, 57, ILI9341_BLACK);
    u8g2gfx.setFont(u8g2_font_fub42_tf);
    u8g2gfx.setBackgroundColor(ILI9341_BLACK);
    u8g2gfx.setForegroundColor(ILI9341_MAGENTA);
    int w = u8g2gfx.getUTF8Width(timeText);
    u8g2gfx.setCursor(110 + (210 - w) / 2, 180);
    u8g2gfx.print(timeText);

    strncpy(previousTimeText, timeText,
            sizeof(previousTimeText) - 1);
    previousTimeText[sizeof(previousTimeText) - 1] = '\0';
  }
}

void DrawAngelZScreen(uint8_t barIndex,
                      unsigned long elapsedMs, bool full) {
  if (full || !treatmentScreenDrawn) {
    tft.fillScreen(ILI9341_BLACK);
    DrawTitleBar("Angel-Z SESSION");
    previousTimeText[0] = '\0';
    treatmentScreenDrawn = true;
  }

  for (uint8_t i = 0; i < 24; i++) {
    uint16_t color =
      (i <= barIndex) ?
      ((i < 18) ? ILI9341_GREEN : ILI9341_RED) :
      ILI9341_DARKGREY;
    tft.fillRect(16 + i * 12, 200, 10, 40, color);
  }

  unsigned long seconds = elapsedMs / 1000UL;
  char text[12];
  snprintf(text, sizeof(text), "%02lu:%02lu",
           seconds / 60UL, seconds % 60UL);

  if (strcmp(text, previousTimeText) != 0) {
    tft.fillRect(25, 92, 270, 57, ILI9341_BLACK);
    u8g2gfx.setFont(u8g2_font_fub42_tf);
    u8g2gfx.setBackgroundColor(ILI9341_BLACK);
    u8g2gfx.setForegroundColor(ILI9341_MAGENTA);
    int width = u8g2gfx.getUTF8Width(text);
    u8g2gfx.setCursor((320 - width) / 2, 140);
    u8g2gfx.print(text);

    strncpy(previousTimeText, text,
            sizeof(previousTimeText) - 1);
    previousTimeText[sizeof(previousTimeText) - 1] = '\0';
  }
}

// SHARED NON-BLOCKING STARTUP

void DrawStartCountdown(unsigned long msLeft) {
  unsigned long secondsLeft = (msLeft + 999UL) / 1000UL;
  if ((int)secondsLeft == startCountdownShown) return;

  startCountdownShown = (int)secondsLeft;
  tft.fillRect(106, 34, 214, 28, ILI9341_BLACK);

  u8g2gfx.setFont(u8g2_font_helvB14_te);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);

  char text[28];
  snprintf(text, sizeof(text), "Starting in %lu s",
           secondsLeft);
  int width = u8g2gfx.getUTF8Width(text);
  u8g2gfx.setCursor(106 + (214 - width) / 2, 54);
  u8g2gfx.print(text);
}

void StartSelectedTreatment() {
  if (numDiagnoses == 0) return;

  EEPROM.update(0, selectedItem);
  startingAngelZ =
    strcmp(diagnoses[selectedItem - 1].name, "AngelZ") == 0;

  // FIX - keep signal and amplifier outputs blocked during preparation.
  digitalWrite(pinOutputPause, HIGH);
  SetAmpOutput(false);
  gen.EnableOutput(false);
  ResetShortProtection();

  SetSignalType(!startingAngelZ);
  treatmentScreenDrawn = false;
  previousTimeText[0] = '\0';

  if (startingAngelZ) {
    DrawAngelZScreen(0, 0, true);
  } else {
    Diagnosis& d = diagnoses[selectedItem - 1];
    uint8_t first = 0;
    while (first < FREQS_PER_DIAG && d.freq[first] == 0) {
      first++;
    }

    if (first == FREQS_PER_DIAG) {
      DrawMenu();
      return;
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < FREQS_PER_DIAG; i++) {
      if (d.freq[i] != 0) count++;
    }

    DrawRegularScreen(
      first, (unsigned long)d.seconds * count * 1000UL, true
    );
  }

  // FIX - pin 12 goes LOW BEFORE the shared delay starts.
  SetAmpPower(true);
  startRequestedAt = millis();
  startCountdownShown = -1;
  runState = RUN_STARTING;

  DrawStartCountdown(START_TREATMENT_DELAY_MS);
}

void ServiceStarting() {
  // FIX - battery check occurs in loop() before this function.
  if (batteryLowLatched) return;

  unsigned long elapsed = millis() - startRequestedAt;
  if (elapsed < START_TREATMENT_DELAY_MS) {
    DrawStartCountdown(START_TREATMENT_DELAY_MS - elapsed);
    return;
  }

  tft.fillRect(106, 34, 214, 28, ILI9341_BLACK);

  if (startingAngelZ) {
    // FIX - prepare AngelZ test signal while outputs are still blocked.
    gen.ApplySignal(SQUARE_WAVE, REG0, 10000);
  } else {
    Diagnosis& d = diagnoses[selectedItem - 1];
    uint8_t first = 0;
    while (first < FREQS_PER_DIAG && d.freq[first] == 0) {
      first++;
    }
    gen.ApplySignal(
      isSineWave ? SINE_WAVE : SQUARE_WAVE,
      REG0, d.freq[first]
    );
  }

  gen.EnableOutput(true);

  // FIX - identical pin order for regular treatment and AngelZ:
  // pin 12 was LOW during delay; now pin 11 LOW, then pin 13 HIGH.
  digitalWrite(pinOutputPause, LOW);
  SetAmpOutput(true);

  // FIX - first A1 short test is AFTER pin 11 LOW and pin 13 HIGH.
  CheckAndHandleAmplifierShort();
  lastShortCheckTime = millis();

  if (startingAngelZ) {
    runState = RUN_ANGELZ;
    RunAngelZTreatment();
  } else {
    runState = RUN_REGULAR;
    RunRegularTreatment();
  }

  if (!batteryLowLatched && runState != RUN_SHUTDOWN) {
    runState = RUN_IDLE;
    DrawMenu();
  }
}

// ACTIVE-TREATMENT HELPERS

bool StopRequested() {
  CheckBatteryLow();
  if (batteryLowLatched) return true;

  uint8_t event = TakeButtonEvent();
  if (event == 0) return false;

  if (event == 2) {
    Shutdown();
  } else {
    AbortTreatment();
  }
  return true;
}

bool WaitBatteryAndButton(unsigned long durationMs) {
  unsigned long started = millis();

  while (millis() - started < durationMs) {
    if (StopRequested()) return false;
    delay(1);
  }
  return true;
}

bool WaitForShortClear() {
  // FIX - both treatment types pause while outputs are blocked or rechecked.
  while (emergencyShortDetected || shortRechecking) {
    if (StopRequested()) return false;

    unsigned long now = millis();
    if (now - lastShortCheckTime >= SHRT_CHECK_MS) {
      CheckAndHandleAmplifierShort();
      lastShortCheckTime = now;
    }
    delay(1);
  }
  return true;
}

void ShowFinished(const char* message, unsigned long elapsedMs) {
  tft.fillScreen(ILI9341_BLACK);
  u8g2gfx.setFont(u8g2_font_helvB24_te);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);
  int width = u8g2gfx.getUTF8Width(message);
  u8g2gfx.setCursor((320 - width) / 2, 120);
  u8g2gfx.print(message);

  unsigned long seconds = elapsedMs / 1000UL;
  char timeText[24];
  snprintf(timeText, sizeof(timeText), "Time: %02lu:%02lu",
           seconds / 60UL, seconds % 60UL);

  u8g2gfx.setFont(u8g2_font_t0_22b_tf);
  u8g2gfx.setForegroundColor(ILI9341_YELLOW);
  width = u8g2gfx.getUTF8Width(timeText);
  u8g2gfx.setCursor((320 - width) / 2, 160);
  u8g2gfx.print(timeText);
}

void RunRegularTreatment() {
  Diagnosis& d = diagnoses[selectedItem - 1];
  uint8_t indices[FREQS_PER_DIAG];
  uint8_t count = 0;

  for (uint8_t i = 0; i < FREQS_PER_DIAG; i++) {
    if (d.freq[i] != 0) indices[count++] = i;
  }

  if (count == 0) {
    AbortTreatment();
    return;
  }

  unsigned long fragmentMs =
    (unsigned long)d.seconds * 1000UL;
  unsigned long totalMs = fragmentMs * count;
  unsigned long sessionStart = TreatmentClock();
  unsigned long lastDisplay = 0;

  for (uint8_t n = 0; n < count; n++) {
    if (StopRequested()) return;

    // FIX - never overwrite the short alert or reopen a blocked output.
    if (!WaitForShortClear()) return;

    uint8_t index = indices[n];
    digitalWrite(pinOutputPause, HIGH);
    gen.ApplySignal(
      isSineWave ? SINE_WAVE : SQUARE_WAVE,
      REG0, d.freq[index]
    );

    if (StopRequested()) return;
    digitalWrite(pinOutputPause, LOW);

    // FIX - check after each new frequency is enabled.
    CheckAndHandleAmplifierShort();
    lastShortCheckTime = millis();

    unsigned long fragmentStart = TreatmentClock();

    while (true) {
      if (StopRequested()) return;

      unsigned long now = millis();
      if (now - lastShortCheckTime >= SHRT_CHECK_MS) {
        CheckAndHandleAmplifierShort();
        lastShortCheckTime = now;
      }

      if (emergencyShortDetected || shortRechecking) {
        if (!WaitForShortClear()) return;
        treatmentScreenDrawn = false;
      }

      unsigned long treatmentNow = TreatmentClock();
      if (treatmentNow - fragmentStart >= fragmentMs) break;

      // Waveform selection remains available during regular treatment.
      if (encoderMoved) {
        int8_t direction = AnalyzeEncoderChange();

        if (direction > 0 && !isSineWave) {
          SetSignalType(true);
          gen.ApplySignal(SINE_WAVE, REG0, d.freq[index]);
          gen.SetOutputSource(REG0);
          DrawSignalIndicator();
        } else if (direction < 0 && isSineWave) {
          SetSignalType(false);
          gen.ApplySignal(SQUARE_WAVE, REG0, d.freq[index]);
          DrawSignalIndicator();
        }
      }

      if (now - lastDisplay >= 100UL ||
          !treatmentScreenDrawn) {
        unsigned long elapsed =
          treatmentNow - sessionStart;
        unsigned long left =
          elapsed < totalMs ? totalMs - elapsed : 0;

        DrawRegularScreen(
          index, left, !treatmentScreenDrawn
        );
        DrawLevelIndicator(ReadLevelBars(), false, false);
        lastDisplay = now;
      }

      delay(1);
    }

    digitalWrite(pinOutputPause, HIGH);

    if (n + 1 < count) {
      tone(pinBeepOut, 2000, 200);
      if (!WaitBatteryAndButton(250)) return;
      noTone(pinBeepOut);
    }
  }

  unsigned long elapsed = TreatmentClock() - sessionStart;
  AbortTreatment();

  ShowFinished("Finished!", elapsed);
  WaitBatteryAndButton(3000);

  digitalWrite(pinShutdown1, LOW);
  digitalWrite(pinShutdown2, HIGH);
}

float AngelZPointDelay(uint8_t cycle, uint8_t point) {
  float value;

  if (point < ANGELZ_SPLIT_POINT) {
    value = angelz_valueStart[cycle] +
      (angelz_valueMax[cycle] - angelz_valueStart[cycle]) *
      (1.0f - exp(-point / angelz_tau[cycle]));
  } else {
    value = angelz_valueMax[cycle] *
      exp(-(point - ANGELZ_SPLIT_POINT) /
          angelz_tau[cycle]);
  }

  return value * ANGELZ_DELAY_SCALE;
}

void RunAngelZTreatment() {
  unsigned long sessionStart = TreatmentClock();
  int currentStep = 0;

  // FIX - A1 was tested in ServiceStarting() before AngelZ enters here.
  if (!WaitForShortClear()) return;

  // FIX - AngelZ is pulsed; keep pin 11 HIGH between individual pulses.
  digitalWrite(pinOutputPause, HIGH);
  gen.EnableOutput(false);

  for (uint8_t cycle = 0; cycle < ANGELZ_CYCLES; cycle++) {
    for (uint8_t point = 0;
         point < ANGELZ_TOTAL_POINTS; point++) {
      if (StopRequested()) return;
      if (!WaitForShortClear()) return;

      // FIX - pulse only after the common startup delay and first A1 test.
      gen.ApplySignal(SQUARE_WAVE, REG0, 10000);
      gen.EnableOutput(true);
      digitalWrite(pinOutputPause, LOW);

      // FIX - test A1 while this AngelZ pulse is actually enabled.
      CheckAndHandleAmplifierShort();
      lastShortCheckTime = millis();

      if (!emergencyShortDetected) {
        if (!WaitBatteryAndButton(3)) return;
        CheckAndHandleAmplifierShort();
        lastShortCheckTime = millis();
      }

      digitalWrite(pinOutputPause, HIGH);
      gen.EnableOutput(false);

      if (emergencyShortDetected) {
        if (!WaitForShortClear()) return;
        treatmentScreenDrawn = false;
        digitalWrite(pinOutputPause, HIGH);
        gen.EnableOutput(false);
      }

      uint8_t barIndex =
        ((unsigned int)point * 24U) /
        ANGELZ_TOTAL_POINTS;

      DrawAngelZScreen(
        barIndex,
        TreatmentClock() - sessionStart,
        !treatmentScreenDrawn || currentStep == 0
      );

      unsigned long pointDelay =
        (unsigned long)AngelZPointDelay(cycle, point);
      if (!WaitBatteryAndButton(pointDelay)) return;

      currentStep++;
    }
  }

  unsigned long elapsed = TreatmentClock() - sessionStart;
  AbortTreatment();

  ShowFinished("AngelZ Finished!", elapsed);
  WaitBatteryAndButton(3000);
}

// SETUP AND MAIN LOOP

void setup() {
  Serial.begin(9600);

  // FIX - establish inhibited output states before starting peripherals.
  digitalWrite(pinOutputPause, HIGH);
  digitalWrite(pinAmpOutOff, LOW);
  digitalWrite(pinAmpPower, HIGH);
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

  pinMode(pinEncoderCW, INPUT_PULLUP);
  pinMode(pinEncoderCCW, INPUT_PULLUP);
  pinMode(pinBtnEnter, INPUT_PULLUP);
  pinMode(pinLevelInput, INPUT);

  tft.begin();
  tft.setRotation(1);
  u8g2gfx.begin(tft);
  u8g2gfx.setFontMode(1);
  u8g2gfx.setFontDirection(0);
  u8g2gfx.setBackgroundColor(ILI9341_BLACK);
  u8g2gfx.setForegroundColor(ILI9341_GREEN);

  analogReference(INTERNAL1V1);
  analogRead(pinLevelInput);       // ADC settling read

  gen.Begin();
  gen.EnableOutput(false);

  DrawIntroScreen();
  delay(2500);

  digitalWrite(pinSDPower, LOW);
  delay(100);
  InitializeSDAndSettings();
  digitalWrite(pinSDPower, HIGH);

  uint8_t saved = EEPROM.read(0);
  if (saved >= 1 && saved <= numDiagnoses) {
    selectedItem = saved;
  }

  DrawMenu();

  attachInterrupt(digitalPinToInterrupt(pinEncoderCW),
                  OnEncoderChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinEncoderCCW),
                  OnEncoderChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinBtnEnter),
                  OnButtonChange, CHANGE);
}

void loop() {
  CheckBatteryLow();

  if (batteryLowLatched) {
    runState = RUN_BATTERY;
    if (batteryAlertPending) ShowBatteryLowAlert();

    if (TakeButtonEvent() == 2) Shutdown();
    return;
  }

  if (runState == RUN_SHUTDOWN) return;

  uint8_t event = TakeButtonEvent();

  if (runState == RUN_STARTING) {
    // FIX - second click cancels the start delay; long press shuts down.
    if (event != 0) {
      if (event == 2) {
        Shutdown();
      } else {
        AbortTreatment();
        runState = RUN_IDLE;
        DrawMenu();
      }
      return;
    }

    // FIX - both regular and AngelZ use this SAME delay timer.
    ServiceStarting();
    return;
  }

  if (runState != RUN_IDLE) return;

  if (encoderMoved && numDiagnoses != 0) {
    int8_t direction = AnalyzeEncoderChange();

    if (direction != 0) {
      previousItem = selectedItem;
      if (direction > 0) {
        selectedItem =
          (selectedItem >= numDiagnoses) ?
          1 : selectedItem + 1;
      } else {
        selectedItem =
          (selectedItem <= 1) ?
          numDiagnoses : selectedItem - 1;
      }

      if (selectedItem != previousItem) DrawMenu();
    }
  }

  if (event == 2) {
    Shutdown();
  } else if (event == 1) {
    StartSelectedTreatment();
  }
}

/*
**Проверка последовательности на стенде** — желательно логическим анализатором на пинах 11, 12 и 13:

| Событие                  | Пин 12  | Пин 11  | Пин 13 |
|--------------------------|---------|---------|--------|
| Ожидание в меню          | HIGH    | HIGH    | LOW    |
| Нажатие: начало задержки | **LOW** | HIGH    | LOW    |
| Все 2 секунды задержки   | LOW     | HIGH    | LOW    |
| После задержки           | LOW     | *LOW*   | *HIGH* |
| Затем                    | LOW     | LOW,нет КЗ | HIGH,нет КЗ |
| Обнаружено КЗ на A1      | LOW     | *HIGH** | *LOW*  |

Проверьте эту таблицу **отдельно для обычного диагноза и AngelZ**. 
Затем установите `START_TREATMENT_DELAY_SEC` в `8`: интервал между переходом пина 12 в LOW и переходом пина 11 в LOW 
должен стать примерно восемь секунд **в обоих режимах**. 
Проверка A1 начинается только после этих переходов.
*/

// Arduino Mega2560 & Mega2560-Pro Rife Machine generator 
// - logically bounce protected encoder
// - UTF8 cyrillic support
// - Supports LEDs: 20pins ST7920 12864 &
//                  13pins GMG12864-06D ST7565 v2.x displays
#include <EEPROM.h>
#include <AD9833.h>   // https://github.com/Billwilliams1952/AD9833-Library-Arduino
#include <U8g2lib.h>

U8G2_ST7920_128X64_1_SW_SPI u8g2(U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* CS=*/ 10, /* reset=*/ 8);
// uncomment for GMG12864-06D ST7565 v2.x display while above line to be commented out
//U8G2_ST7565_ERC12864_F_4W_SW_SPI u8g2 (U8G2_R0, /* clock*/ 13, /* data*/ 11, /*CS*/ 10, /*dc*/ 7, /*reset*/ 8); 

// GMG12864-06D (powered directly from Mega2560 +3.3v as ST7565 is 2.1v controller)
// below 5 signals resolved by 1k-2k resistor deviders between Mega2560 & GMG12864-06D
// Mega2560[pin# 13] -> GMG12864-06D[SCL pin# 4]
//         pin# [11] ->              SI  pin# [5]
//         pin# [10] ->              CS  pin# [1]
//         pin# [7]  ->              RS  pin# [3] 
//         pin# [8]  ->              RSE pin# [2]

#define numberOfDiagnoses 35 //the number of diagnoses in the indexOfIllness[] array  //const int
// english
/*
const char* diagnoses[numberOfDiagnoses] = {
  "Good Sleep","Alcoholism","Angina","Stomachache","General Pain","Headaches",
  "Infection","Acute pain","Back pain","Arthralgia","Toothache",
  "No appetite","No taste","Motion sickness", "Hoarseness","Gastric Ulcer",
  "Prostate ailments","Deafness","Flu","Hemorrhoids","Kidney stones", 
  "Cough","Runny nose","Hair loss","Hypertension","Low pressure", 
  "Thyroid Gland Disease","Bad breath","Herpes", "Epilepsy","Constipation",
  "Dizziness","Accending 1","Accending 2", "H.Clark Zapper"
  };
  */
// uncomment for russian list while disable english above
const char* diagnoses[numberOfDiagnoses] = {
  "Хороший сон","Алкоголизм","Стенокардия","Желудочная боль","Общая боль","Головная боль",
  "Инфекция","Острая боль","Боль в спине","Артралгия","Зубная боль",
  "Нет аппетита","Нет вкуса","Морская болезнь","Охриплость","Язва желудка",
  "Недуги простаты", "Глухота","Грипп","Геморой","Камни в почках", 
  "Кашель","Насморк","Потеря волос","Высокое давление","Низкое давление", 
  "Недуги Щитовидной","Запах изо рта","Герпес","Эпилепсия","Запоры",
  "Головокружение" ,"Вознесение 1","Вознесение 2", "H.Clark Zapper"
};

const int frequencies[numberOfDiagnoses * 10] = { 
  4,5,6,0,0,0,0,0,0,0,                            //"Good Sleep"
  10000,0,0,0,0,0,0,0,0,0,                        //"Alcoholism"
  787,776,727,690,465,428,660,0,0,0,              //"Angina"
  10000,3000,95,0,0,0,0,0,0,0,                    //"Stomachache"
  3000,2720,95,666,80,40,0,0,0,0,                 //"General Pain"
  10000,144,160,520,304,0,0,0,0,0,                //"Headache"
  3000,95,880,1550,802,787,776,727,0,0,           //"Infection"
  3000,95,10000,1550,802,880,787,727,690,666,     //"Acute pain"
  787,784,776,728,727,465,432,0,0,0,              //"Back pain"
  160,500,1600,5000,324,528,0,0,0,0,              //"Arthralgia"
  5170,3000,2720,2489,1800,1600,1550,880,832,666, //"Toothache" 
  10000,465,444,1865,125,95,72,880,787,727,       //"No appetite"
  10000,20,0,0,0,0,0,0,0,0,                       //"No taste"
  10000,5000,648,624,600,465,440,648,444,1865,    //"Motion sickness"
  880,760,727,0,0,0,0,0,0,0,                      //"Hoarseness"
  10000,1550,802,880,832,787,727,465,0,0,         //"Gastric Ulcer"
  2050,880,1550,802,787,727,465,20,0,0,           //"Bladder and prostate ailments"
  10000,1550,880,802,787,727,20,0,0,0,            //"Deafness"
  954,889,841,787,763,753,742,523,513,482,        //"Flu"
  4474,6117,774,1550,447,880,802,727,0,0,         //"Hemorrhoids"
  10000,444,727,787,880,6000,3000,1552,0,0,       //"Kidney stones"
  7760,7344,3702,3672,1550,1500,1234,776,766,728, //"Cough"
  1800,1713,1550,802,800,880,787,727,444,20,      //"runny nose"
  10000,5000,2720,2170,1552,880,800,787,727,465,  //"Hair loss"
  10000,3176,2112,95,324,528,880,787,727,304,     //"Hypertension"
  727,787,880,0,0,0,0,0,0,0,                      //"Low pressure"
  16000,10000,160,80,35,0,0,0,0,0,                //"Thyroid Gland disease"
  1550,802,880,787,727,0,0,0,0,0,                 //"Bad breath"
  2950,1900,1577,1550,1489,1488,629,464,450,383,  //"General herpes"
  10000,880,802,787,727,700,650,600,210,125,      //"Epilepsy"
  3176,1550,880,832,802,787,776,727,444,422,      //"Constipation"
  1550,880,802,784,787,786,766,522,727,72,        //"Dizziness"
  432,528,0,0,0,0,0,0,0,0,                        //"Accending 1"
  174,285,396,417,639,741,852,963,528,528,        //"Accending 2"
  32000,0,0,0,0,0,0,0,0,0                         //"Halda Clark Zapper"
};

#define pinEncoderCW         2 // encoder 
#define pinEncoderCCW        3 // encoder
#define pinBtnEnter         21 // encoder ENTER    
#define pinLcdBacklight     13 // on/off lcd backlight
#define pinGenCS             9 // CS for AD9833
#define pinBeepOut           4 // beep at each frequency and 3 beeps at the end
#define pinCustomCtrl        5 // Custom request
#define pinBatteryLevel     A0 // request to measure battery voltage - 40kohm in sum of two resistors devider. Middle connected to pin A0, top to Vcc and bottom to GND
 
AD9833 gen(pinGenCS);  //connect FSYNC/CS to D9 of UNO or Nano
//
const int SCROLL_DOWN=0;
const int SCROLL_UP=1;
const int ONE_BEEP=1;
const int THREE_BEEPS=3;
const int PIEZO_BEEP_TONE = 2000; // Adjust to your loudwdest piezo beeper 1000 to 4000hz
const int PEIZO_BEEP_LENGTH = 1000;
const int PEIZO_BEEP_PAUSE = 500;
// voltage sensor
const float R1 = 32000.0; // 30 k
const float R2 = 8000.0;  // 7.5k
float referenceVoltage = 5.0; // Float for Reference Voltage
int voltageAnalogPoints = 0;  // Integer for ADC value
float relativeVoltage = 0.0;
float voltageOutput = 0.0;
//
byte selectedItem; //currenly selected diagnose
byte pageOffset;   //offset from the top of the current page
unsigned long  timeStart, timeEndEnterButton; 
char* titleLine = "Diagnose:";      //name of the selected diagnosis
// values to display as strings                        
char treatmentTime[3] = {"20"};     // 20 minutes
char charFreqSequentialNumber[3];   
char charFrequency[5];
uint16_t intFreqToGenerate;         
char* strComplete;                      // the end of the session message
int rotationCounter=0;                  // encoder turn counter (negative -> CCW)
volatile bool encoderMoved = false;     // Flag from interrupt routine (moved = true)
volatile bool btnEnterPressed = false;  // Flag from Btn Enter interrupt routine
// eeprom related
byte eepromAddress = 0;
int itemToSelect;
// Variables to track state and timing
unsigned long frequencyStartTime = 0;
bool isGeneratingFrequency = false;


// runs once
void setup(void) {
  Serial.begin(9600);
  u8g2.begin();
  //u8g2.setContrast(20); // uncomment only for GMG12864-06D display
  u8g2.enableUTF8Print();
  pinMode(pinLcdBacklight, OUTPUT);
  digitalWrite (pinLcdBacklight, LOW);  // turning ON the LCD backlight
  digitalWrite  (pinCustomCtrl, LOW);;  // optional, for future feature 
  pinMode(pinEncoderCW, INPUT_PULLUP);  // Encoder CW The module already has pullup resistors on board
  pinMode(pinEncoderCCW, INPUT_PULLUP); // Encoder CCW
  pinMode(pinBtnEnter, INPUT_PULLUP);   // Encoder button
  // set up external generator AD0933
  gen.Begin();  
  gen.EnableOutput(false);

  selectedItem = 1; // highlight the first item
  pageOffset = 0;   // offset from the beginning of the array to the current page displayed
  // display intro 
  u8g2.firstPage();
  do { DisplayIntroScreen(); } while ( u8g2.nextPage() );
  // wait 
  delay(2000);
  // display first screen 
  u8g2.firstPage();
  do {
      DisplayMainMenu(0); 
      HighlightSelectedItem(selectedItem, pageOffset); 
  } while ( u8g2.nextPage() );
  //
  attachInterrupt(digitalPinToInterrupt(pinEncoderCW), OnScrollChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinEncoderCCW), OnScrollChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinBtnEnter), OnEnterBtnChange, RISING);

  // read EEPROM and update UI if memorized
  itemToSelect = EEPROM.read(eepromAddress);  
  //
  if (itemToSelect > 0) { 
      //Serial.print ("Reading from EEPROM: "); Serial.println(itemToSelect);
      SetSelectedItem(itemToSelect); 
      selectedItem = itemToSelect;
  }

  //
  //Serial.println("Battery [V]: " + String(MeasureBatteryVoltage()));
}

// main loop
void loop() {
    //
    if (encoderMoved) {
        // valid movement only if it's not zero
        int8_t isValidRotationValue = AnalyzeEncoderChange();
        if (isValidRotationValue != 0) {
          rotationCounter += isValidRotationValue;
          //
          if (isValidRotationValue < 1) {
            ScrollItem(SCROLL_DOWN); // rotate encoder CCW
          } else {
            ScrollItem(SCROLL_UP);   // rotate encoder CW
          }
        }
    }
    //
    if (btnEnterPressed) {
        ProcessPressEnter();
    }
}

//
float MeasureBatteryVoltage() {
   voltageAnalogPoints = analogRead(pinBatteryLevel);
   relativeVoltage  = (voltageAnalogPoints * referenceVoltage) / 1024.0; 
   voltageOutput = relativeVoltage / (R2/(R1+R2)); 
   return voltageOutput;
}

// sets previously used item after unit turned on 
void SetSelectedItem(byte itemToSelect) {
    int pgOffset = CalulatePageOffset(itemToSelect);
    u8g2.firstPage();
    do {
        DisplayMainMenu(pgOffset); 
        HighlightSelectedItem(itemToSelect, pgOffset); 
    } while ( u8g2.nextPage() );
}

//
void OnEnterBtnChange() {
    btnEnterPressed = true;
}

//
void OnScrollChange() { // IRQ routine sets a flag when rotation is detected
    encoderMoved = true;
}

// intro screen
void DisplayIntroScreen(void) { 
   u8g2.setFont(u8g2_font_helvB12_te);           // large font
   u8g2.drawStr( 5, 20,  "Dr. Royal Rife");
   u8g2.drawStr( 20, 37,  "Machine");
   u8g2.setFont(u8g2_font_6x12_te);
   u8g2.drawStr( 15, 52,  "by kd2cmo 2024");
   u8g2.drawStr( 30, 62,  "wait...");
}

// list of diagnoses screen displayed after intro
void DisplayMainMenu(int pgOffset) {
     // frame
    DrawTitleFrame();
    u8g2.setDrawColor(2);                // inverse the color
    u8g2.setFontMode(1);                 // is transparent
    u8g2.setFont(u8g2_font_3x5im_tr);    // or u8g2_font_tiny_simon_mr github.com/olikraus/u8g2/wiki/fntgrpbitfontmaker2 and /olikraus/u8g2/wiki/fntlist8#3-pixel-height
    String strVoltage;
    float fVoltage = MeasureBatteryVoltage();
    strVoltage = String(String(fVoltage) + "v");
    int intVoltageLength = strVoltage.length()+1;
    // allocate buffer
    char* batteryVoltage = new char[intVoltageLength]; 
    strVoltage.toCharArray(batteryVoltage, intVoltageLength);
    u8g2.drawStr(128-strVoltage.length()*4, 8, batteryVoltage);    
    // fill in diagnoses list
    for (int counter = pgOffset; counter < 6 + pgOffset; counter++) {
        if (counter <= numberOfDiagnoses - 1) {
        u8g2.setFont(u8g2_font_6x12_t_cyrillic);
        u8g2.setCursor( 10, 20 + 10 * (counter-pgOffset) );  
        u8g2.print( diagnoses[counter] );
    }        
  }
}

// Draw main window w/ diagnose on top
void DrawTitleFrame(void) {
    u8g2.drawFrame( 0, 0 , 128, 64);  
    u8g2.drawBox(0, 0, 128, 10);      
    u8g2.setDrawColor(2);             //inverse the color
    u8g2.setFontMode(1);              
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.setCursor(CalculatePositionX(titleLine), 8);
    u8g2.print(titleLine);
    u8g2.setDrawColor(1);             //reset to normal black on white
}

// position calculated for utf8 chars with mixed and averaged with regular fonts
int CalculatePositionX(char * title) {
    //return (128/2-(strlen(title)*(5+1)/2)); // perfect centering works for english only
    return (2); // TO DO   
}

// screen displayed during the treatment
void DisplayTreatInProgressScreen(String frequency, String frequencySquence) {
    // concatenate all details into
    String strStatus = String("Seq:" + frequencySquence + " Freq:" + frequency + "Hz ");
    int intStatusLength = strStatus.length();
    // allocate buffer for converter to char pointer
    char* status = new char[intStatusLength+1]; //char * cstr = new char [str.length()+1];
    strStatus.toCharArray(status, intStatusLength+1);
    //
    String strVoltage;
    float fVoltage = MeasureBatteryVoltage();
    strVoltage = String(String(fVoltage) + "v");
    int intVoltageLength = strVoltage.length()+1;
    // allocate buffer
    char* batteryVoltage = new char[intVoltageLength]; 
    strVoltage.toCharArray(batteryVoltage, intVoltageLength);
    //
    //Serial.println(String("String Voltage: " + strVoltage));
    //  
    u8g2.firstPage();
    do {
      DrawTitleFrame();
      u8g2.setDrawColor(2);                            // inverse the color
      u8g2.setFontMode(1);                             // is transparent
      u8g2.setFont(u8g2_font_3x5im_tr);                // or u8g2_font_tiny_simon_mr github.com/olikraus/u8g2/wiki/fntgrpbitfontmaker2 and /olikraus/u8g2/wiki/fntlist8#3-pixel-height
      u8g2.drawStr(128-strVoltage.length()*4, 8, batteryVoltage);            // 105
      u8g2.setFont(u8g2_font_6x12_te);                 // choosing small fonts
      u8g2.drawStr(37, 25, "Therapy");
      u8g2.drawStr(18, 38, "Time:");
      u8g2.drawStr(48, 38, treatmentTime);
      u8g2.drawStr(65, 38, "minutes");                 //
      u8g2.setFont(u8g2_font_helvB14_te);              // choosing large fonts
      if (strComplete == "") {
          u8g2.setFont(u8g2_font_6x12_te);             // choosing small fonts
          if (frequency.length() > 0 ) { u8g2.drawStr(10, 58, status); };
      }
      if (strComplete != "") u8g2.drawStr(15, 58, strComplete);
    } while ( u8g2.nextPage() );
}

// encoder button pressed  - invoked by IRQ
void ProcessPressEnter() {
  byte pinOutput = HIGH;
  byte pinOutputNext;
    
    //ENTER button - "Therapy" mode
    pinOutput = digitalRead(pinBtnEnter);            //reading state of button Enter
    pinOutputNext = digitalRead(pinBtnEnter);
    if (pinOutput == LOW and pinOutputNext == LOW) {   
        timeEndEnterButton = micros() - timeStart;
    } 
    if (timeEndEnterButton > 50 and pinOutputNext == HIGH) {
        //
        if (!isGeneratingFrequency) { // btn pressed when already in use 
            // set flags
            isGeneratingFrequency = true;
            btnEnterPressed = false;
            // save selected item into EEPROM
            EEPROM.update(eepromAddress, selectedItem);  
            // display title on top
            titleLine = diagnoses[selectedItem-1];
            //            
            // prepare and invoke external generator
            bool isAborted = GenerateFrequency();
            if (isAborted) {
                // 
                SetSelectedItem(selectedItem);
            }
            timeStart = 0; timeEndEnterButton = 0;
        }
    }
} 

// actual frequency formed and passed to ext AD9833
bool GenerateFrequency(void) {
 int freqValue = 0;
 float fragmentTime;
 float numberOfFreqInSet;
 String strFreqToGenerate;
 String strSeqNumber;

  numberOfFreqInSet = 0; 
  intFreqToGenerate = 0;
  strComplete = "";
  
  //determine number of f in the diagnoze array
  for (int counter=0; counter < 10; counter++) {                    
     freqValue = frequencies[10*(selectedItem-1) + counter];
     if (freqValue > 0) numberOfFreqInSet++;   // increment number of frequencies found in array
  }
  
  fragmentTime = atoi (treatmentTime) / numberOfFreqInSet * 60000; // time splitted between frequences equaly in milliseconds 60000ms = 1min
  //
  gen.EnableOutput(true);

  for (int intFreqSeqNumber=0; intFreqSeqNumber < numberOfFreqInSet; intFreqSeqNumber++) {
    // responce to button enter pressed anytime
    unsigned long currentTime = millis();
    unsigned long elapsedTime = 0;

      frequencyStartTime = millis();    
      intFreqToGenerate = frequencies[10*(selectedItem-1) + intFreqSeqNumber];
      //
      strFreqToGenerate = String(intFreqToGenerate, DEC);
      strSeqNumber = String(intFreqSeqNumber+1, DEC);
      DisplayTreatInProgressScreen(strFreqToGenerate, strSeqNumber);
      //
      gen.ApplySignal(SQUARE_WAVE, REG0, intFreqToGenerate); 

      //Serial.println("GenerateFrequency() - btnEnterPressed:" + String(btnEnterPressed) + " isGeneratingFrequency:" + String(isGeneratingFrequency));
      // Non-blocking delay loop 
      while (elapsedTime < fragmentTime && isGeneratingFrequency) {
         // Check for encoder button press to interrupt
         if (btnEnterPressed) {
            // Stop generating frequency
            isGeneratingFrequency = false; 
            // disable output and leave 
            gen.EnableOutput(false); 
            // abort loop
            return true; // aborted
         }
        // Update elapsed time
        currentTime = millis();
        elapsedTime = currentTime - frequencyStartTime;
     }
    
      // beep with one beep after each frequency change
      PlayTone(ONE_BEEP);
  }
  gen.EnableOutput(false); // disable output after each frequency block ends
  isGeneratingFrequency = false; 
  //
  strComplete = "Finished!";
  //
  PlayTone(THREE_BEEPS);
  DisplayTreatInProgressScreen("", "");
  delay(3000); // 3sec
  // go to previously selected page
  SetSelectedItem(selectedItem); 
  //
  strComplete = "";
  digitalWrite (pinCustomCtrl, HIGH); // Custom request set - it will shut off power here if auto power off impllemented
  return false; // normal exit
} 

// for encoder
void ScrollItem(bool direction) {
    direction==SCROLL_UP?selectedItem++:selectedItem--;
    if (selectedItem < 1) {selectedItem = 1;}
    if (selectedItem > numberOfDiagnoses) { selectedItem = 1; }
    pageOffset = CalulatePageOffset(selectedItem);
    //highlight item based on (P1-offset)
    HighlightSelectedItem(selectedItem, pageOffset);
}

//calculate first item on the selected page
byte CalulatePageOffset(byte currentItem) {
    return ((currentItem-1) - ((currentItem-1) % 5));
}

// draw square box around the selection or highlight
void HighlightSelectedItem(byte selectItem, byte offset){                        //displays text and cursor
  u8g2.firstPage();
  do {
     u8g2.drawHLine(5, 11 + (selectItem-offset)*10, 118);
     u8g2.drawHLine(5, 2 + (selectItem-offset)*10, 118);
     u8g2.drawVLine(5, 2 + (selectItem-offset)*10, 10);
     u8g2.drawVLine(122, 2 + (selectItem-offset)*10, 10);     
     DisplayMainMenu(offset);
  } while ( u8g2.nextPage() );
}

// signals between frequency switch and at the end of the session
void PlayTone(int numberOfBeeps) {
  for (int count = 1;   count <= numberOfBeeps; count++ ) {
      tone(pinBeepOut, PIEZO_BEEP_TONE, PEIZO_BEEP_LENGTH); 
      delay(PEIZO_BEEP_LENGTH);
      noTone(pinBeepOut);
      delay(PEIZO_BEEP_PAUSE);
  } 
}

// See https://www.pinteric.com/rotary.html
int8_t AnalyzeEncoderChange() {
    encoderMoved = false; // Reset the flag that brought us here (from ISR) 
    static uint8_t lrmem = 3;
    static int lrsum = 0;
    static int8_t TRANS[] = {0, -1, 1, 14, 1, 0, 14, -1, -1, 14, 0, 1, 14, 1, -1, 0};
    // Read BOTH pin states to deterimine validity of rotation (ie not just switch bounce)
    int8_t moveLeft = digitalRead(pinEncoderCW);
    int8_t moveRight = digitalRead(pinEncoderCCW);
    // move previous value 2 bits to the left and add in our new values
    lrmem = ((lrmem & 0x03) << 2) + 2 * moveLeft + moveRight;
    // convert the bit pattern to a movement indicator (14 = impossible, ie switch bounce)
    lrsum += TRANS[lrmem];
    // encoder not in the neutral (detent) state
    if (lrsum % 4 != 0) { return 0; }
    // encoder in the neutral state - CW rotation
    if (lrsum == 4) { lrsum = 0; return 1; }
    // encoder in the neutral state - CCW rotation
    if (lrsum == -4) { lrsum = 0; return -1; }
    // An impossible rotation has been detected - ignore the movement
    lrsum = 0;
    return 0;
}

//attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), checkButton, CHANGE);
#define HALT while(true);
#define STATE_NORMAL 0 // no button activity
#define STATE_SHORT  1
#define STATE_LONG   2 
#define BUTTON_PIN   2 
volatile int  resultButton = 0; // global value set by checkButton()

void OnButtonPress() {
  const unsigned long LONG_DELTA = 1500ul;          // hold seconds for a long press
  const unsigned long DEBOUNCE_DELTA = 30ul;        // debounce time
  static int lastButtonStatus = HIGH;               // HIGH indicates the button is NOT pressed
  int buttonStatus;                                 // button atate Pressed/LOW; Open/HIGH
  static unsigned long longTime = 0ul, shortTime = 0ul; // future times to determine is button has been poressed a short or long time
  boolean Released = true, Transition = false;          // various button states
  boolean timeoutShort = false, timeoutLong = false;    // flags for the state of the presses

  buttonStatus = digitalRead(BUTTON_PIN);  // read the button state on the pin "BUTTON_PIN"
  timeoutShort = (millis() > shortTime);   // calculate the current time states for the button presses
  timeoutLong = (millis() > longTime);

  if (buttonStatus != lastButtonStatus) {               // reset the timeouts if the button state changed
      shortTime = millis() + DEBOUNCE_DELTA;
      longTime = millis() + LONG_DELTA;
  }

  Transition = (buttonStatus != lastButtonStatus);      // has the button changed state
  Released = (Transition && (buttonStatus == HIGH));    // for input pullup circuit

  lastButtonStatus = buttonStatus;                      // save the button status

  if ( !Transition) {                                  //without a transition, there's no change in input
       // if there has not been a transition, don't change the previous result
       resultButton =  STATE_NORMAL | resultButton;
       return;
  }

  if (timeoutLong && Released) {                  // long timeout has occurred and the button was just released
       resultButton = STATE_LONG | resultButton;  // ensure the button result reflects a long press
  } else if (timeoutShort && Released) {          // short timeout has occurred (and not long timeout) and button was just released
      resultButton = STATE_SHORT | resultButton;  // ensure the button result reflects a short press
  } else {                                        // else there is no change in status, return the normal state
      resultButton = STATE_NORMAL | resultButton; // with no change in status, ensure no change in button status
  }
}


/*
void loop() {
  int longButton=0;
  int count=0;
 
    while (true) {
        switch (resultButton) 
        case STATE_NORMAL: {
            //  Serial.print("."); count++; count = count % 10; if (count==0) Serial.println("");
            break;
        }
        case STATE_SHORT: {
            Serial.println("Short press has been detected");
            resultButton=STATE_NORMAL;
            break;
        }
        case STATE_LONG: {
            Serial.println("Button was pressed for long time");
            longButton++;
            resultButton=STATE_NORMAL;
            break;
        }
    }
    if (longButton==5) {
        Serial.println("Halting");
        HALT;
    }
}
*/

// Arduino Mega2560 & Mega2560-Pro Rife Machine generator 
// - logically bounce protected encoder
// - UTF8 cyrillic support
// - Supports LEDs: 20pins ST7920 12864 &
//                  13pins GMG12864-06D ST7565 v2.x displays
#include <EEPROM.h>
#include <AD9833.h>   // https://github.com/Billwilliams1952/AD9833-Library-Arduino
#include <U8g2lib.h>

//U8G2_ST7920_128X64_1_SW_SPI u8g2(U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* CS=*/ 10, /* reset=*/ 8);
// uncomment for GMG12864-06D ST7565 v2.x display while above line to be commented out
U8G2_ST7565_ERC12864_F_4W_SW_SPI u8g2 (U8G2_R0, /* clock*/ 13, /* data*/ 11, /*CS*/ 10, /*dc*/ 7, /*reset*/ 8); 

// GMG12864-06D (powered directly from Mega2560 +3.3v as ST7565 is 2.1v controller)
// below 5 signals resolved by 1k-2k resistor deviders between Mega2560 & GMG12864-06D
// Mega2560[pin# 13] -> GMG12864-06D[SCL pin# 4]
//         pin# [11] ->              SI  pin# [5]
//         pin# [10] ->              CS  pin# [1]
//         pin# [7]  ->              RS  pin# [3] 
//         pin# [8]  ->              RSE pin# [2]

#define numberOfDiagnoses 33 //the number of diagnoses in the indexOfIllness[] array  //const int
// english
/*
const char* diagnoses[numberOfDiagnoses] = {
  "Alcoholism","Angina","Stomachache","General Pain","Headaches",
  "Infection","Acute pain","Back pain","Arthralgia","Toothache",
  "No appetite","No taste","Motion sickness", "Hoarseness","Dolegl. gastric",
  "Prostate ailments","Deafness","Flu","Hemorrhoids","Kidney stones", 
  "Cough","Runny nose","Hair loss","Hypertension","Low pressure", 
  "Thyroid Gland Disease","Bad breath","Herpes", "Epilepsy","Constipation",
  "Dizziness","Accending 1","Accending 2"
  };
  */
// uncomment for russian list while disable english above
const char* diagnoses[numberOfDiagnoses] = {
  "Алкоголизм","Стенокардия","Желудочная боль","Общая боль","Головная боль",
  "Инфекция","Острая боль","Боль в спине 2","Артралгия","Зубная боль",
  "Нет аппетита","Нет вкуса","Морская болезнь","Охриплость","Долегл. желудочный",
  "Недуги простаты", "Глухота","Грипп","Геморой","Камни в почках", 
  "Кашель","Насморк","Потеря волос","Высокое давление","Низкое давление", 
  "Недуги Щитовидной","Запах изо рта","Герпес","Эпилепсия","Запоры",
  "Головокружение" ,"Вознесение 1","вознесение 2"
};

const int frequencies[numberOfDiagnoses * 10] = { 
  10000,0,0,0,0,0,0,0,0,0, //"Alcoholism"
  787,776,727,690,465,428,660,0,0,0, //"Angina"
  10000,3000,95,0,0,0,0,0,0,0, //"Stomachache"
  3000,2720,95,666,80,40,0,0,0,0, //"Pain in general"
  10000,144,160,520,304,0,0,0,0,0, //"Headache"
  3000,95,880,1550,802,787,776,727,0,0, //"Infection"
  3000,95,10000,1550,802,880,787,727,690,666, //"Acute pain"
  787,784,776,728,727,465,432,0,0,0, //"Back pain"
  160,500,1600,5000,324,528,0,0,0,0, //"Arthralgia"
  5170,3000,2720,2489,1800,1600,1550,880,832,666, //"Toothache" 
  10000,465,444,1865,125,95,72,880,787,727, //"No appetite"
  10000,20,0,0,0,0,0,0,0,0, //"No taste"
  10000,5000,648,624,600,465,440,648,444,1865, //"Motion sickness"
  880,760,727,0,0,0,0,0,0,0, //"Hoarseness"
  10000,1550,802,880,832,787,727,465,0,0, //"Dolegl. gastric",
  2050,880,1550,802,787,727,465,20,0,0, //"Bladder and prostate ailments",
  10000,1550,880,802,787,727,20,0,0,0, //"Deafness",
  954,889,841,787,763,753,742,523,513,482, //"Flu",
  4474,6117,774,1550,447,880,802,727,0,0, //"Hemorrhoids",
  10000,444,727,787,880,6000,3000,1552,0,0, //"Kidney stones",
  7760,7344,3702,3672,1550,1500,1234,776,766,728, //"Cough",
  1800,1713,1550,802,800,880,787,727,444,20, //"runny nose",
  10000,5000,2720,2170,1552,880,800,787,727,465, //"Hair loss",
  10000,3176,2112,95,324,528,880,787,727,304, //"Hypertension",
  727,787,880,0,0,0,0,0,0,0, //"Low pressure",
  16000,10000,160,80,35,0,0,0,0,0, //"Disease. thyroid gland"
  1550,802,880,787,727,0,0,0,0,0, //"Bad breath",
  2950,1900,1577,1550,1489,1488,629,464,450,383, //"General herpes",
  10000,880,802,787,727,700,650,600,210,125, //"Epilepsy"'
  3176,1550,880,832,802,787,776,727,444,422, //"Constipation",
  1550,880,802,784,787,786,766,522,727,72,       //"Dizziness",
  432,528,0,0,0,0,0,0,0,0,
  174,285,396,417,639,741,852,963,528,528
};

#define pinEncoderCW     2  // encoder 
#define pinEncoderCCW    3  // encoder
#define pinBtnEnter     21  // encoder ENTER    
#define pinLcdBacklight 13
#define pinGenCS         9  // CS for AD9833

AD9833 gen(pinGenCS);  //connect FSYNC/CS to D9 of UNO or Nano

const int SCROLL_DOWN=0;
const int SCROLL_UP=1;

byte selectedItem; //currenly selected diagnose
byte pageOffset;   //offset from the top of the current page

unsigned long  timeStart, timeEndEnterButton; //, timeEndPressButton, timeEndDownButton;

char* titleLine = "Diagnose:";      //name of the selected sickness
byte numberOfFreqInSet;  

// values to display as strings                        
char treatmentTime[3] = {"10"};  
byte fragmentTime;            
char charFreqSequentialNumber[3];   
char charFrequency[5];
uint16_t intFreqToGenerate;         
char* strComplete;                  //the end of the session message


int rotationCounter=0; // encoder turn counter (negative -> CCW)
volatile bool encoderMoved = false;     // Flag from interrupt routine (moved = true)
volatile bool btnEnterPressed = false;  // Flag from Btn Enter interrupt routine

// eeprom related
byte eepromAddress = 0;
int itemToSelect;

volatile bool inProgress = false;

void setup(void) {
  Serial.begin(9600);
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setContrast(20); // uncomment for GMG12864-06D display
  //	
  pinMode(pinLcdBacklight, OUTPUT);         
  digitalWrite (pinLcdBacklight, LOW);  // turning ON the LCD backlight
  pinMode(pinEncoderCW, INPUT_PULLUP);  // Encoder CW The module already has pullup resistors on board
  pinMode(pinEncoderCCW, INPUT_PULLUP); // Encoder CCW
  pinMode(pinBtnEnter, INPUT_PULLUP);   // Encoder button
  // set up external generator
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
      DisplayMainMenu(); 
      highlightItem(selectedItem, pageOffset); 
  } while ( u8g2.nextPage() );
  //
  attachInterrupt(digitalPinToInterrupt(pinEncoderCW), OnScrollChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinEncoderCCW), OnScrollChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinBtnEnter), OnEnterBtnChange, CHANGE);

  // read EEPROM and update UI if memorized
  itemToSelect = EEPROM.read(eepromAddress);  
  //
  if (itemToSelect > 0) { 
      Serial.print ("EEPROM Item Memorized: "); Serial.println(itemToSelect);
      SetSelectedItem(itemToSelect); 
  }
}


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
        ProcessPressExecute(); 
    }
}

// TO DO
void SetSelectedItem(byte itemToSelect) {
    pageOffset = CalulatePageOffset(itemToSelect);
    //highlight item based on (P1-offset)
    highlightItem(itemToSelect, pageOffset);
}

void OnEnterBtnChange() {
    btnEnterPressed = true;
}

//
void OnScrollChange() { //IRAM_ATTR // Interrupt routine just sets a flag when rotation is detected
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

void DisplayMainMenu(void) {
    // frame
    DrawTitleFrame();
    // fill in diagnoses list
    for (int counter = pageOffset; counter < 6 + pageOffset; counter++) {
        if (counter <= numberOfDiagnoses - 1) {
        u8g2.setFont(u8g2_font_6x12_t_cyrillic);
        u8g2.setCursor( 10, 20 + 10 * (counter-pageOffset) );  
        u8g2.print( diagnoses[counter] );
    }        
  }
}

//
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
    return (64-(strlen(title)*2));
}

void DisplayTimerScreen() {
    u8g2.firstPage();
    do {
      u8g2.setFont(u8g2_font_6x12_te);                // choosing small fonts
      DrawTitleFrame();
      u8g2.drawStr(37, 25, "Therapy");
      u8g2.drawStr(18, 38, "Time:");
      u8g2.drawStr(48, 38, treatmentTime);
      u8g2.drawStr(65, 38, "minutes");
      u8g2.setFont(u8g2_font_helvB14_te);             // choosing large fonts
      if (strComplete == "") {
          u8g2.setFont(u8g2_font_6x12_te);            // choosing small fonts
          u8g2.drawStr(10, 58, "Sequence Number:");
          //u8g2.drawStr(42, 58, charFrequency);       // show frequncy sequence number
          u8g2.drawStr(110, 58, charFreqSequentialNumber); // show frequncy sequence number
      }
      if (strComplete != "") u8g2.drawStr(15, 58, strComplete);
    } while ( u8g2.nextPage() );
}

void ProcessPressExecute() {
  byte pinOutput = HIGH;
  byte pinOutputNext;
    
    //ENTER button - "Therapy" mode
    pinOutput = digitalRead(pinBtnEnter);            //reading state of button Enter
    pinOutputNext = digitalRead(pinBtnEnter);
    if (pinOutput == LOW and pinOutputNext == LOW) {   
        timeEndEnterButton = micros() - timeStart;
    } 
    if (timeEndEnterButton > 20 and pinOutputNext == HIGH) {
        if (inProgress == true) {
            inProgress = false;            
            // display first screen 
            u8g2.firstPage();
            do {
                DisplayMainMenu(); 
               // highlightItem(0,0); 
            } while ( u8g2.nextPage() );            
        } else {
            //Serial.println("Button Enter pressed while NOT in progress!");
            inProgress = true;
            // save selected itme into EEPROM
            EEPROM.update(eepromAddress, selectedItem);  

            titleLine = diagnoses[selectedItem-1];
            //
            GenerateFrequency();
            timeStart = 0; timeEndEnterButton = 0;
        }
    }
}

// for encoder
void ScrollItem(bool direction) {
    direction==SCROLL_UP?selectedItem++:selectedItem--;
    if (selectedItem < 1) {selectedItem = 1;}
    if (selectedItem > numberOfDiagnoses) { selectedItem = 1; }
    pageOffset = CalulatePageOffset(selectedItem);
    //highlight item based on (P1-offset)
    highlightItem(selectedItem, pageOffset);
}

//calculate first item on the selected page
byte CalulatePageOffset(byte currentItem) {
    return ((currentItem-1) - ((currentItem-1) % 5));
}

void highlightItem(byte selectItem, byte offset){                        //displays text and cursor
  u8g2.firstPage();
  do {
     u8g2.drawHLine( 5, 11 + (selectItem-offset)*10, 118);
     u8g2.drawHLine( 5, 2 + (selectItem-offset)*10, 118);
     u8g2.drawVLine( 5, 2 + (selectItem-offset)*10, 10);
     u8g2.drawVLine( 122, 2 + (selectItem-offset)*10, 10);     
     DisplayMainMenu();
  } while ( u8g2.nextPage() );
}

void GenerateFrequency(void) {
  int freqValue = 0;
  numberOfFreqInSet = 0; 
  intFreqToGenerate = 0;
  strComplete = "";
  
  //determine number of f in the ilness array
  for (int counter=0; counter < 10; counter++) {                    
     freqValue = frequencies[10*(selectedItem-1) + counter];
     if (freqValue > 0) numberOfFreqInSet++;   // increment number of frequencies found in array
  }
  //
  fragmentTime = 10/numberOfFreqInSet*60000; // time splitted between existing frequences proportionally
  //
  gen.EnableOutput(true);
  for (int intFreqSeqNumber=0; intFreqSeqNumber < numberOfFreqInSet; intFreqSeqNumber++) {
      intFreqToGenerate = frequencies[10*(selectedItem-1) + intFreqSeqNumber];
      //Convert f seq# to a 2-digit string 
      strcpy(charFreqSequentialNumber,u8x8_u8toa(intFreqSeqNumber+1,2));  //Convert to a 2-digit string
      //     
      DisplayTimerScreen();
      //
      gen.ApplySignal(SQUARE_WAVE, REG0, intFreqToGenerate);        
      delay(fragmentTime);
  }
  gen.EnableOutput(false);
  strComplete = "Finished!";
  DisplayTimerScreen();
  strComplete = "";
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

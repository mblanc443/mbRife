 // ArduinoMega Rife bioresonance generator

#include <U8g2lib.h>

U8G2_ST7920_128X64_1_SW_SPI u8g2(U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* CS=*/ 10, /* reset=*/ 8);

#define numberOfDiagnoses 31 //the number of diagnoses in the indexOfIllness[] array  //const int

// russian list
const char* diagnoses[numberOfDiagnoses] = {
  "Алкоголизм","Стенокардия","Желудочная боль","Общая боль","Головная боль",
  "Инфекция","Острая боль","Боль в спине 2","Артралгия","Зубная боль",
  "Нет аппетита","Нет вкуса","Морская болезнь","Охриплость","Долегл. желудочный",
  "Недуги простаты", "Глухота","Грипп","Геморой","Камни в почках", 
  "Кашель","Насморк","Потеря волос","Высокое давление","Низкое давление", 
  "Недуги Щитовидной","Запах изо рта","Герпес","Эпилепсия","Запоры",
  "Головокружение"
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
  1550,880,802,784,787,786,766,522,727,72       //"Dizziness",
};

#define pinEncoderCW  2  //encoder pin 2
#define pinEncoderCCW 3
#define pinBtnEnter 5     //encoder ENTER    
#define pinFrequencyOut 4 //frequency out
#define pinLcdBacklight 13

const int SCROLL_DOWN=0;
const int SCROLL_UP=1;

byte selectedItem; //currenly selected diagnose
byte pageOffset;   //offset from the top of the current page
byte timeFragment;

unsigned long  timeStart, timeEndEnterButton; //, timeEndPressButton, timeEndDownButton;

char* titleLine = "Diagnose:";      //name of the selected sickness
byte numberOfFreqInSet;  

// values to display as strings                        
char treatmentTime[3];              
char charFreqSequentialNumber[3];   
char charFrequency[5];
uint16_t intFreqToGenerate;         
char* strComplete;                  //the end of the session message


int rotationCounter=0; // encoder turn counter (negative -> CCW)
volatile bool encoderMoved = false;     // Flag from interrupt routine (moved = true)
//
void OnChange() { //IRAM_ATTR // Interrupt routine just sets a flag when rotation is detected
    encoderMoved = true;
}

void setup(void) {
  u8g2.begin();
  u8g2.enableUTF8Print();
  //	
  Serial.begin(9600);
  pinMode(pinLcdBacklight, OUTPUT);         
  digitalWrite (pinLcdBacklight, LOW); //turning on the LCD backlight
  pinMode(pinEncoderCW, INPUT);        // The module already has pullup resistors on board
  pinMode(pinEncoderCCW, INPUT); 
  pinMode(pinBtnEnter, INPUT_PULLUP);
  
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
      highlightItem(); 
  } while ( u8g2.nextPage() );
  //
  attachInterrupt(digitalPinToInterrupt(pinEncoderCW), OnChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinEncoderCCW), OnChange, CHANGE);
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
    ProcessPressExecute(); 
}

// intro screen
void DisplayIntroScreen(void) { 
   u8g2.setFont(u8g2_font_helvB12_te);           // large font
   u8g2.drawStr( 5, 20,  "Dr. Royal Rife");
   u8g2.drawStr( 20, 37,  "Machine");
   u8g2.setFont(u8g2_font_6x12_te);
   u8g2.drawStr( 15, 52,  "by kd2cmo 2023");
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
        titleLine = diagnoses[selectedItem-1];
        //
        GenerateFrequency();
        timeStart = 0; timeEndEnterButton = 0;
    }
}

// for encoder
void ScrollItem(bool direction) {
    direction==SCROLL_UP?selectedItem++:selectedItem--;
    if (selectedItem < 1) {selectedItem = 1;}
    if (selectedItem >= numberOfDiagnoses) { selectedItem = numberOfDiagnoses; }
    pageOffset = CalulatePageOffset(selectedItem);
    //highlight item based on (P1-offset)
    highlightItem();
}

//calculate first item on the selected page
byte CalulatePageOffset(byte currentItem) {
    return ((currentItem-1) - ((currentItem-1) % 5));
}

void highlightItem(void){                        //displays text and cursor
  u8g2.firstPage();
  do {
     u8g2.drawHLine( 5, 11 + (selectedItem-pageOffset)*10, 118);
     u8g2.drawHLine( 5, 2 + (selectedItem-pageOffset)*10, 118);
     u8g2.drawVLine( 5, 2 + (selectedItem-pageOffset)*10, 10);
     u8g2.drawVLine( 122, 2 + (selectedItem-pageOffset)*10, 10);     
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
  // make up treatment time
  if (numberOfFreqInSet > 4) {strcpy(treatmentTime, u8x8_u8toa(numberOfFreqInSet, 2)); timeFragment = 1;}
  if (numberOfFreqInSet == 4){strcpy(treatmentTime, u8x8_u8toa(8, 2)); timeFragment = 2;}
  if (numberOfFreqInSet == 3){strcpy(treatmentTime, u8x8_u8toa(6, 2)); timeFragment = 2;}
  if (numberOfFreqInSet == 2){strcpy(treatmentTime, u8x8_u8toa(6, 2)); timeFragment = 3;}
  if (numberOfFreqInSet == 1){strcpy(treatmentTime, u8x8_u8toa(5, 2)); timeFragment = 5;}  
  //
  for (int intFreqSeqNumber=0; intFreqSeqNumber < numberOfFreqInSet; intFreqSeqNumber++) {
      intFreqToGenerate = frequencies[10*(selectedItem-1) + intFreqSeqNumber];
     
      //Convert f seq# to a 2-digit string 
      //itoa((intFreqSeqNumber+1), charFreqSequentialNumber, 2);
      strcpy(charFreqSequentialNumber,u8x8_u8toa(intFreqSeqNumber+1,2));  //Convert to a 2-digit string
      //itoa(intFreqToGenerate,charFrequency,DEC);
      //strcpy(charFreqSequentialNumber,u8x8_u16toa(intFreqToGenerate+1,5));

      //Serial.print("intFreqSeqNumber+1: "); Serial.print((intFreqSeqNumber+1)); Serial.print(" strFreqSequentialNumber: "); Serial.println(charFreqSequentialNumber);  //Serial.print(" strFreqSequentialNumberTmp: "); Serial.println(strFreqSequentialNumberTmp);
      //Serial.print("intFreqToGenerate: "); Serial.print(intFreqToGenerate); Serial.print(" stringFrequency: "); Serial.println(charFrequency);
      //     
      DisplayTimerScreen();
      //generating 1 minute (6 * 10sec)
      for (int counterA=0; counterA < timeFragment * 6; counterA++){                    
          PlayFrequency(intFreqToGenerate, 10000); // healing Freq, in 10sec fragments: duration=10000msec=10sec
      }   
     delay (1000); // 1sec 
  }
  strComplete = "Finished!";
  DisplayTimerScreen();
  strComplete = "";
}

/*
void GenerateFrequency(void) {
  int freqValue = 0, 
  int b9;
  ilosc_f = 0; 
  strComplete = "";
  
  //determine number of f in the ilness array
  for (i=0; i < 10; i++) {                    
     freqValue = frequencies[10*(selectedItem-1) + i];
     if (freqValue > 0) ilosc_f ++;  // increment number of frequencies found in array
  }
  if (ilosc_f > 4) {strcpy(f_str, u8x8_u8toa(ilosc_f, 2)); b = 1;}
  if (ilosc_f == 4){strcpy(f_str, u8x8_u8toa(8, 2)); b = 2;}
  if (ilosc_f == 3){strcpy(f_str, u8x8_u8toa(6, 2)); b = 2;}
  if (ilosc_f == 2){strcpy(f_str, u8x8_u8toa(6, 2)); b = 3;}
  if (ilosc_f == 1){strcpy(f_str, u8x8_u8toa(5, 2)); b = 5;}  
  for (i=0; i < ilosc_f; i++) {
      f_gen = frequencies[10*(selectedItem-1) + i];
      //Convert to a 2-digit string
      strcpy(i_str, u8x8_u8toa(i+1, 2));       
      DisplayTimerScreen();
      //generating 1 minute (6 * 10sec)
      for (a=0; a < b*6; a++){                    
          beep(f_gen, 10000); // healing Freq, duration=10000msec=10sec
      }   
     delay (1000); // 1sec 
  }
  strComplete = "Complete!";
  DisplayTimerScreen();
  strComplete = "";
}
*/

void PlayFrequency(int healingFrequency, int duration){
  tone(pinFrequencyOut, healingFrequency, duration);
  delay(duration);
  noTone(pinFrequencyOut);
  delay(30);
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

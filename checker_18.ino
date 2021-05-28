/*
    Lithium Ion Cell Capacity Checker - Version 1
    Release Date 28/04/2018.
    Written By Antony Cartwright BSc (Hons).
    www.electronicsandcode.co.uk
    www.youtube.com/antonycartwright

    If you decide to remake this, you'll need to calibrate it properly.
    Pay extra attention to the contacts of the cell holder as these have a somewhat high resistance. Under load, these can cause a voltage drop.
    Choose whichever load resistors you find most appropriate for your cells. Try to discharge cells at 1C.
    So for example, if you think your cells are probably 1000mah, try to keep discharge amperage at around 1A.
    Doing very heavy discharges can damage your cells.

    The capacitor may not be 100% necessary. I added it because it seemed to stabilise the internal reference calcs.

    NOTE: The device seems to need 30-60 seconds to warm up on start, otherwise the ADC can be a little bit out.
*/

#include <SPI.h>
#include <Adafruit_SSD1306.h>

#define OLED_RESET 9
#define OLED_CS    10
#define OLED_MOSI   11
#define OLED_DC    12
#define OLED_CLK   13

#define buzzerPin 2         // Triggers the buzzer.
#define switchPin 3         // Pin to detect Uuser input.
#define drainingLedPin 4    // Triggers the red 'draining' LED.
#define completeLedPin 5    // Triggers the green 'complete' LED.
#define standbyLedPin 6     // Triggers the blue 'standby' LED.
#define mosfetPin 7         // Triggers the load MOSFET.
#define batteryVoltage A0   // Pin for reading battery voltage.
#define preLoadVoltage A1   // Pin for reading pre-load voltage.
#define postLoadVoltage A2  // Pin for reading post-load voltage.

Adafruit_SSD1306 display(OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

const float loadResistance = 1.65;      //Your load resistance here.
const float terminalResistance = 0.011; //I explain this later on.
const int internalReferenceAdjust = 20; //You'll need to calibrate your Arduino. I'll probably explain in a video.
const float groundDiff = 0.003;

float mvpc;                             //Millivolts per ADC count - this is refreshed continuously.
float sumAmperage;                      //Stores the ongoing sum of drainage.
byte currentMenu = 1;                   //Start on menu 1.
bool previousSwitchState = HIGH;        //Stores the previous switch state so that it can detect a change.

unsigned long drainStartMillis = 0;     //Time the sampling started.
unsigned long drainStopMillis = 0;      //Time the sampling ended.

unsigned long lastDrainSample = 0;      //Last drain sample.
const int drainSampleInterval = 1000;   //Drain sample interval. Don't change.

unsigned long lastVCCSet = 0;           //Time the mvpc var was last updated.
const int updateVCCInterval = 1000;     //mvpc update interval.

unsigned long currentTime;              //Stores the current time since Arduino started.

void setup() {

  Serial.begin(115200);

  Serial.println("Started!");

  display.begin(SSD1306_SWITCHCAPVCC);

  display.clearDisplay();

  pinMode(mosfetPin, OUTPUT);
  pinMode(completeLedPin, OUTPUT);
  pinMode(drainingLedPin, OUTPUT);
  pinMode(standbyLedPin, OUTPUT);
  pinMode(switchPin, INPUT_PULLUP);
  pinMode(batteryVoltage, INPUT);
  pinMode(preLoadVoltage, INPUT);
  pinMode(postLoadVoltage, INPUT);
  pinMode(buzzerPin, OUTPUT);

  //Turn the blue light on.
  standbyPinOn(true);

  display.setTextSize(2);
  display.setTextColor(WHITE);

  //Make the welcome message appear on screen.
  printHello("Hello.");
  printHello("Hello..");
  printHello("Hello...");

  displayMenuOne();
}

void printHello(String message) {
  display.setCursor(10, 24);
  display.println(message);
  display.display();
  delay(1000);
}

void standbyPinOn(bool on) {
  digitalWrite(standbyLedPin, on);

  if (on) {
    //If this LED is on, the others should be off.
    drainingPinOn(false);
    completedPinOn(false);
  }
}

void drainingPinOn(bool on) {
  digitalWrite(drainingLedPin, on);

  if (on) {
    //If this LED is on, the others should be off.
    standbyPinOn(false);
    completedPinOn(false);
  }
}

void completedPinOn(bool on) {
  digitalWrite(completeLedPin, on);

  if (on) {
    //If this LED is on, the others should be off.
    standbyPinOn(false);
    drainingPinOn(false);
  }
}

void turnLoadOn(bool on) {

  //The function which controls the driving of the mosfet.

  if (on)
  {
    drainStartMillis = millis();
  } else {
    drainStopMillis = millis();
  }

  drainingPinOn(on);
  digitalWrite(mosfetPin, on);
}

void displayMenuOne() {

  //This function draws the static content of menu 1.
  //Menu 1 contains only static content.
  //This is the main screen.

  display.clearDisplay();

  currentMenu = 1;

  display.setTextSize(1);
  display.setCursor(10, 12);
  display.println(F("Insert Cell Now!"));
  display.setCursor(10, 28);
  display.println(F("(Press When Ready)"));
  display.display ();
}

void displayMenuTwo() {

  //This function draws the static content of menu 2.
  //Menu 2 contains only static content.
  //This shows the initial battery voltage.

  display.clearDisplay();

  currentMenu = 2;

  display.setTextSize(1);
  display.setCursor(10, 12);
  display.println("Voltage: " + String(analogRead(batteryVoltage) * mvpc / 1000));
  display.setCursor(10, 28);
  display.println(F("(Press To Start)"));
  display.display();
}

void displayMenuThree() {

  //This function draws the static content of menu 3.
  //Menu 3 contains only static content.
  //This shows a message saying 'cell under voltage'.

  display.clearDisplay();

  currentMenu = 3;

  display.setTextSize(1);
  display.setCursor(10, 12);
  display.println(F("Cell Under Voltage!"));
  display.setCursor(10, 28);
  display.println(F("(Press To Return)"));
  display.display();
}

void displayMenuFour() {

  //This function draws the static content of menu 4.
  //Menu 4 contains static and non-static content.
  //This is what I call the drain screen. It gets updated every second by the loop.

  display.clearDisplay();
  display.display();

  currentMenu = 4;

  sumAmperage = 0;

  turnLoadOn(true);

  display.setCursor(16, 12);
  display.println(F("Voltage: "));

  display.setCursor(16, 28);
  display.println(F("Amperage: "));

  display.setCursor(16, 44);
  display.println(F("Duration: "));
}

void displayMenuFive() {

  //This function draws the static content of menu 5.
  //Menu 5 contains only static content.
  //This shows the summary of the cell.

  display.clearDisplay();

  currentMenu = 5;

  turnLoadOn(false);

  unsigned long durationMillis = drainStopMillis - drainStartMillis;                        //Amount fo time the test run for.
  unsigned int durationTotalSeconds = durationMillis / 1000;                                //Same as above but now in seconds.
  unsigned int durationMinutes = durationTotalSeconds / 60;                                 //Same as above but in minutes.
  unsigned int durationSeconds = durationTotalSeconds - (durationMinutes * 60);             //Amount of seconds remaining after calculating total minutes.

  completedPinOn(true);

  float avgAmperage = sumAmperage / (float)durationTotalSeconds;                            //Find the mean drain amperage. (1 sample per second).

  display.setCursor(16, 12);
  display.println("Avg: " + String(int(avgAmperage * 1000)) + " MA");                       //Display average amperage throughout test.

  display.setCursor(16, 28);
  display.println(String(durationMinutes) + " min " + String(durationSeconds) + " sec");    //Display user friendly minutes and seconds.

  display.setCursor(16, 44);

  float durationHours = (float)durationTotalSeconds / (float)3600;                          //MAh means milliamp hour, so we need to work with hours. 3600 seconds in an hour.
  unsigned int mah = durationHours * avgAmperage * 1000;                                    //The actual MAh calculation, x 1000 to convert A into MA.

  display.println(String(mah) + " MAh");

  display.display();
}

void loop() {

  currentTime = millis();

  if (currentTime > lastVCCSet + updateVCCInterval) {
    //If a VCC check is required, then do it now.
    setMvpc();
    lastVCCSet = currentTime;
  }

  //Check for user input here.
  bool currentSwitchState = digitalRead(switchPin);
  if (currentSwitchState == LOW && previousSwitchState == HIGH) {
    //A button has been pressed, but which menu was it pressed in?

    switch (currentMenu) {

      case 1:
        //Menu 1 - Insert cell
        //A press here indicates 'move to next menu'.
        if (analogRead(batteryVoltage) * mvpc >= 3)
        {
          //Battery over 3v, then proceed.
          displayMenuTwo();
        } else {
          //Not over 3v? Deny proceed - Go to menu 3 instead.
          displayMenuThree();
        }
        break;

      case 2:
        //Menu 2 - Voltage is xxx
        //If we get input here, it's when the user wants to proceed. We've check the voltage, and it's over 3v.
        //Proceed to the drain screen.
        displayMenuFour();
        break;

      case 3:
        //Menu 3 - Cell no good.
        //When user inputs here, go back to menu 1.
        displayMenuOne();
        break;

      case 4:
        //Menu 4 - Main Drain Screen.
        //This is the main drain screen - input here means 'abandon' task and go to the summary.
        //Proceed with the abandonment.
        displayMenuFive();
        break;

      case 5:
        //Menu 5 - Summary.
        //Input here means return to the main screen.
        //Proceed.
        standbyPinOn(true);
        displayMenuOne();
        break;
    }
  }
  previousSwitchState = currentSwitchState;

  //If we're on screen 4, then there is automatic sampling to do.
  if (currentMenu == 4) {
    if (currentTime > lastDrainSample + drainSampleInterval) {
      //A sample is due.
      DrainScreenSample(); //Take a sample.
      lastDrainSample = currentTime;
    }
  }

  delay(5);
}

void DrainScreenSample() {

  //Draw a big black rectangle over the old details. Lazy or what? Lol.
  display.fillRect(92, 0, 32, 64, BLACK);

  display.display();

  //Do inital battery voltage calculation. This will be redone in a sec.
  float bv = (analogRead(batteryVoltage) * mvpc) / 1000;
  Serial.println("BV: " + String(bv));

  //Get the pre-load volatge.
  float prlv = (analogRead(preLoadVoltage) * mvpc) / 1000;
  Serial.println("PrLV: " + String(prlv));

  //Get the post-load volatge.
  float polv = (analogRead(postLoadVoltage) * mvpc) / 1000;
  Serial.println("PoLV: " + String(polv));

  //Now we have the pre and post, we can calculate the voltage drop over the load.
  float vDrop = prlv - polv;
  Serial.println("VDrop: " + String(vDrop));

  //Now we know the voltage drop AND the resistance of the load, we can detect amperage over the drain loop.
  float amperage = vDrop / loadResistance;
  Serial.println("Amperage: " + String(amperage));

  //When under heavy loads, the terminals of the cell holders have a large voltage drop due to their resistance (poor quality).
  //Due to the way in which they've been designed, when we want to detect voltage, we have to also experience the voltage drop because it's being used by the load too.
  //This affects our measurements a little bit. I've worked out that the resistance of each terminal is 0.011Ω for my particular cell holder. Yours might be different.
  //The voltage drop across the terminals will be variable. It depends on the amount of amperage flowing across them. I calculate the drop below:

  float terminalVDrop = amperage * terminalResistance;
  Serial.println("Terminal VDrop: " + String(terminalVDrop));

  //In my design, there is also a difference between the cell cathode and Arduino ground. I've not investigated why this is the case, but it's approx 3mv.

  Serial.println("Ground Difference: " + String(groundDiff));

  //So now we have the details, let's adjust the cell voltage by adding on the extra voltage drops which the polling can't detect.

  bv += (terminalVDrop * 2) + groundDiff;
  Serial.println("BV: " + String(bv));

  Serial.println("- - - - - - - - - -");

  //To calculate the mean amperage, we just add all the samples together to start with.
  //We then divide by the amount of samples later on in the summary screen.

  sumAmperage += amperage;

  display.setTextColor(WHITE);

  display.setCursor(92, 12);
  display.println(String(bv));

  display.setCursor(92, 28);
  display.println(String(amperage));

  int currentTime2 = currentTime / 1000;                      //Details in seconds, not milliseconds - it's for a user to see.
  int drainStartMillis2 = drainStartMillis / 1000;            //Same as above.

  display.setCursor(92, 44);
  display.println(String(currentTime2 - drainStartMillis2));  //Print the time taken so far, in seconds.

  display.display();

  if (prlv <= 3) {
    //If the battery has drained (<=3v), then end the drain, and move to the next screen.
    displayMenuFive();
    buzz();
  }
}

void buzz() {
  //Do three beeps!
  for (int i = 0; i < 3; i++) {
    digitalWrite(buzzerPin, HIGH);
    delay(500);
    digitalWrite(buzzerPin, LOW);
    delay(500);
  }
}

void setMvpc() {
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
  ADMUX = _BV(MUX5) | _BV(MUX0);
#elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
  ADMUX = _BV(MUX3) | _BV(MUX2);
#else
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#endif

  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA, ADSC)); // measuring

  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH
  uint8_t high = ADCH; // unlocks both

  long result = (high << 8) | low;

  result = (1125300L / result) + internalReferenceAdjust; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000

  Serial.println("VCC: " + String(result) + "mV");

  float deviceVoltage = (float)result / (float)1000;
  Serial.println("VCC: " + String(deviceVoltage, 3) + "V");

  //We need to know the amount of millivolts per ADC count, so that we can read in meaningful voltages accurately.

  mvpc = (float)result / (float)1024;
  Serial.println("MVPC: " + String(mvpc, 3) + "mV");

  Serial.println("----------");
}

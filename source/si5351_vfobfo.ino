
/*
  Program to control the "Adafruit Si5351A Clock Generator" or similar via Arduino.
  This program control the frequencies os two clocks (CLK) output of the Si5351A
  The CLK 0 can be used as a VFO (2MHz to 150MHz)
  The CLK 1 can be used as a BFO (440KHz to 460KHz)
  See on https://github.com/etherkit/Si5351Arduino  and know how to calibrate your Si5351
  See also the example Etherkit/si5251_calibration
  Author: Ricardo Lima Caratti (PU2CLR) -   2019/03/07
*/

// #include <SPI.h>
#include <Encoder.h>
#include <si5351.h>
#include <Wire.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"

// Enconder constants
#define ENCONDER_PIN_A 8 // Arduino  D8
#define ENCONDER_PIN_B 9 // Arduino  D9

// OLED Diaplay constants
// 0X3C+SA0 - 0x3C or 0x3D
#define I2C_ADDRESS 0x3C
// Define proper RST_PIN if required.
#define RST_PIN -1

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
SSD1306AsciiAvrI2c display;

// Local constants
#define CORRECTION_FACTOR 80231 // See how to calibrate your si5351 (0 if you do not want)

#define BUTTON_STEP 0    // Control the frequency increment and decrement
#define BUTTON_BAND 1    // Controls the band
#define BUTTON_VFO_BFO 7 // Switch VFO to BFO

#define STATUS_LED 10 // Arduino status LED
#define STATUSLED(ON_OFF) digitalWrite(STATUS_LED, ON_OFF)
#define MIN_ELAPSED_TIME 300

// I'm using in this project the Adafruit Si5351A
Si5351 si5351;

Encoder rotaryEncoder(ENCONDER_PIN_A, ENCONDER_PIN_B);

// Structure for Bands database
typedef struct
{
  char *name;
  uint64_t minFreq; // Frequency min value for the band (unit 0.01Hz)
  uint64_t maxFreq; // Frequency max value for the band (unit 0.01Hz)
} Band;

// Band database:  More information see  https://en.wikipedia.org/wiki/Radio_spectrum
Band band[] = {
    {"AM   ", 53500000LLU, 170000000LLU}, // 535KHz to 1700KHz
    {"SW1  ", 170000001LLU, 350000000LLU},
    {"SW2  ", 350000000LLU, 400000001LLU},
    {"SW3  ", 400000001LLU, 700000000LLU},
    {"SW4A ", 700000000LLU, 730000000LLU},   // 7MHz to 7.3 MHz  (Amateur 40m)
    {"SW5  ", 730000000LLU, 900000001LLU},   // 41m
    {"SW6  ", 900000000LLU, 1000000000LLU},  // 31m
    {"SW7A ", 1000000000LLU, 1100000000LLU}, // 10 MHz to 11 MHz (Amateur 30m)
    {"SW8  ", 1100000000LLU, 1400000001LLU}, // 25 and 22 meters
    {"SW9A ", 1400000000LLU, 1500000000LLU}, // 14MHz to 15Mhz (Amateur 20m)
    {"SW10 ", 1500000000LLU, 1700000000LLU}, // 19m
    {"SW11 ", 1700000000LLU, 1800000000LLU}, // 16m
    {"SW12A", 1800000000LLU, 2000000000LLU}, // 18MHz to 20Mhz (Amateur and comercial 15m)
    {"SW13A", 2000000000LLU, 2135000000LLU}, // 20MHz to 22Mhz (Amateur and comercial 15m/13m)
    {"SW14 ", 2135000000LLU, 2200000000LLU},
    {"SW15 ", 2235000000LLU, 2498000000LLU},
    {"SW16A", 2488000000LLU, 2499000000LLU}, // 24.88MHz to 24.99MHz (Amateur 12m)
    {"SW17 ", 2499000000LLU, 2600000000LLU},
    {"SW18C", 2600000000LLU, 2800000000LLU},
    {"SW19A", 2800000000LLU, 3000000000LLU}, // 28MHz to 30MHz (Amateur 10M)
    {"VHF1 ", 3000000001LLU, 5000000000LLU},
    {"VHF2A", 5000000001LLU, 5400000000LLU},
    {"VHF3 ", 5400000001LLU, 8600000000LLU},
    {"FM   ", 8600000000LLU, 10800000000LLU},  // Comercial FM
    {"VHF4 ", 10800000000LLU, 16000000000LLU}, // 108MHz to 160MHz
};

// Last element position of the array band
volatile int lastBand = 24;   //sizeof band / sizeof(Band);
volatile int currentBand = 0; // AM is the current band

// Struct for step database
typedef struct
{
  char *name; // step name: 100Hz, 500Hz, 1KHz, 5KHz, 10KHz and 500KHz
  long value; // Frequency value (unit 0.01Hz See documentation) to increment or decrement
} Step;

Step step[] = {
    {"100Hz ", 10000},
    {"500Hz ", 50000},
    {"1KHz  ", 100000},
    {"2.5KHz", 250000},
    {"5KHz  ", 500000},
    {"10KHz ", 1000000},
    {"100KHz", 10000000},
    {"500KHz", 50000000}};

volatile int lastStepVFO = 7;
volatile int lastStepBFO = 3; // Max increment or decrement for BFO is 2.5KHz
volatile long currentStep = 0;

volatile boolean isFreqChanged = false;
volatile boolean clearDisplay = false;

// AM is the default band
volatile uint64_t vfoFreq = band[currentBand].minFreq; // VFO starts on AM
volatile uint64_t bfoFreq = 45500000LU;                // 455 KHz -  BFO

volatile int currentClock = 0; // If 0, the clock 0 (VFO) will be controlled else the clock 1 (BFO) will be

long volatile elapsedTimeInterrupt = millis(); // will control the minimum time to process an interrupt action
long elapsedTimeEncoder = millis();

int enconderCurrentPosition = 0;
int enconderPosition = 0;

void setup()
{
  // LED Pin
  pinMode(STATUS_LED, OUTPUT);
  // Encoder pins
  pinMode(ENCONDER_PIN_A, INPUT);
  pinMode(ENCONDER_PIN_B, INPUT);
  // Si5351 contrtolers pins
  pinMode(BUTTON_BAND, INPUT);
  pinMode(BUTTON_STEP, INPUT);
  pinMode(BUTTON_VFO_BFO, INPUT);
  // The sistem is alive
  blinkLed(STATUS_LED, 100);
  STATUSLED(LOW);
  // Initiating the OLED Display
  display.begin(&Adafruit128x64, I2C_ADDRESS);
  display.setFont(Adafruit5x7);
  display.set2X();
  display.clear();
  display.print("\n\n   PU2CLR");
  // Initiating the Signal Generator (si5351)
  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
  // Adjusting the frequency (see how to calibrate the Si5351 - example si5351_calibration.ino)
  si5351.set_correction(CORRECTION_FACTOR, SI5351_PLL_INPUT_XO);
  si5351.set_pll(SI5351_PLL_FIXED, SI5351_PLLA);
  si5351.set_freq(vfoFreq, SI5351_CLK0);          // Start CLK0 (VFO)
  si5351.set_freq(bfoFreq, SI5351_CLK1);          // Start CLK1 (BFO)
  si5351.update_status();
  // Show the initial system information
  display.clear();
  displayDial();
  // Will stop what Arduino is doing and call changeStep(), changeBand() or switchVFOBFO 
  attachInterrupt(digitalPinToInterrupt(BUTTON_STEP), changeStep, RISING);      // whenever the BUTTON_STEP goes from LOW to HIGH
  attachInterrupt(digitalPinToInterrupt(BUTTON_BAND), changeBand, RISING);      // whenever the BUTTON_BAND goes from LOW to HIGH
  attachInterrupt(digitalPinToInterrupt(BUTTON_VFO_BFO), switchVFOBFO, RISING); // whenever the BUTTON_VFO_BFO goes from LOW to HIGH
  // wait for 4 seconds and the system will be ready.
  delay(4000);
}

// Blink the STATUS LED
void blinkLed(int pinLed, int blinkDelay)
{
  for (int i = 0; i < 5; i++)
  {
    STATUSLED(HIGH);
    delay(blinkDelay);
    STATUSLED(LOW);
    delay(blinkDelay);
  }
}

// Considere utilizar este código para limpar o OLED mais rápido
// ref: https://github.com/greiman/SSD1306Ascii/issues/38
void fastClear()
{
  for (uint8_t r = 0; r < 8; r++)
  {
    // set row to clear
    display.setCursor(0, r);
    // Wire has 32 byte buffer so send 8 packets of 17 bytes.
    for (uint8_t b = 0; b < 8; b++)
    {
      Wire.beginTransmission(I2C_ADDRESS);
      Wire.write(0X40);
      for (uint8_t i = 0; i < 16; i++)
      {
        Wire.write(0);
      }
      Wire.endTransmission();
    }
  }
  display.setCursor(0, 0);
}

// Show Signal Generator Information
// Verificar setCursor() em https://github.com/greiman/SSD1306Ascii/issues/53
void displayDial()
{
  double vfo = vfoFreq / 100000.0;
  double bfo = bfoFreq / 100000.0;

  // display.setCursor(0,0)
  // display.clear();
  display.set2X();
  display.setCursor(0, 0);
  display.print(" ");
  display.print(vfo);
  display.print(" ");

  display.set1X();
  display.print("\n\n\nBFO.: ");
  display.print(bfo);

  display.print("\nBand: ");
  display.print(band[currentBand].name);

  display.print("\nStep: ");
  display.print(step[currentStep].name);

  display.print("\n\nCtrl: ");
  display.print((currentClock) ? "BFO" : "VFO");
}

// Change the frequency (increment or decrement)
// direction parameter is 1 (clockwise) or -1 (counter-clockwise)
void changeFreq(int direction)
{
  if (currentClock == 0)
  { // Check who will be chenged (VFO or BFO)
    vfoFreq += step[currentStep].value * direction;
    // Check the VFO limits
    if (vfoFreq > band[currentBand].maxFreq) // Max. VFO frequency for the current band
      vfoFreq = band[currentBand].minFreq;
    else if (vfoFreq < band[currentBand].minFreq) // Min. VFO frequency for the band
      vfoFreq = band[currentBand].maxFreq;
  }
  else
  {
    bfoFreq += step[currentStep].value * direction; // currentStep * direction;

    // Check the BFO limits
    if (bfoFreq > 56000000LU) // Max. BFO: 560KHz
      bfoFreq = 35000000LU;
    else if (bfoFreq < 35000000LU) // Min. BFO: 350KHz
      bfoFreq = 56000000LU;
  }
  isFreqChanged = true;
}

// Change frequency increment rate
void changeStep()
{
  if ((millis() - elapsedTimeInterrupt) < MIN_ELAPSED_TIME)
    return;                                                            // nothing to do if the time less than MIN_ELAPSED_TIME milisecounds
  noInterrupts();                                                      //// disable global interrupts:
  if (currentClock == 0)                                               // Is VFO
    currentStep = (currentStep < lastStepVFO) ? (currentStep + 1) : 0; // Increment the step or go back to the first
  else                                                                 // Is BFO
    currentStep = (currentStep < lastStepBFO) ? (currentStep + 1) : 0;
  isFreqChanged = true;
  clearDisplay = true;
  elapsedTimeInterrupt = millis();
  interrupts(); // enable interrupts
}

// Change band
void changeBand()
{
  if ((millis() - elapsedTimeInterrupt) < MIN_ELAPSED_TIME)
    return;                                                       // nothing to do if the time less than 11 milisecounds
  noInterrupts();                                                 //  disable global interrupts:
  currentBand = (currentBand < lastBand) ? (currentBand + 1) : 0; // Is the last band? If so, go to the first band (AM). Else. Else, next band.
  vfoFreq = band[currentBand].minFreq;
  isFreqChanged = true;
  elapsedTimeInterrupt = millis();
  interrupts(); // enable interrupts
}

// Switch the Encoder control from VFO to BFO and virse versa.
void switchVFOBFO()
{
  if ((millis() - elapsedTimeInterrupt) < MIN_ELAPSED_TIME)
    return;       // nothing to do if the time less than 11 milisecounds
  noInterrupts(); //  disable global interrupts:
  currentClock = !currentClock;
  currentStep = 0; // go back to first Step (100Hz)
  clearDisplay = true;
  elapsedTimeInterrupt = millis();
  interrupts(); // enable interrupts
}

// main loop
void loop()
{
  // Read the Encoder
  // Next Enconder action can be processed after 5 milisecounds

  if ((millis() - elapsedTimeEncoder) > 10)
  {
    enconderCurrentPosition = rotaryEncoder.read();
    noInterrupts();
    if (enconderCurrentPosition != enconderPosition)
    {
      // change the frequency - clockwise (1) or counter-clockwise (-1)
      changeFreq((enconderCurrentPosition < enconderPosition) ? -1 : 1);
    }
    enconderPosition = enconderCurrentPosition;
    elapsedTimeEncoder = millis(); // keep elapsedTimeEncoder updated
    interrupts();
  }
  // check if some action changed the frequency
  if (isFreqChanged)
  {
    if (currentClock == 0)
    {
      si5351.set_freq(vfoFreq, SI5351_CLK0);
    }
    else
    {
      si5351.set_freq(bfoFreq, SI5351_CLK1);
    }
    isFreqChanged = false;
    displayDial();
  }
  else if (clearDisplay)
  {
    display.clear();
    displayDial();
    clearDisplay = false;
  }
}
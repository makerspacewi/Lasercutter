/* DESCRIPTION
  ====================
  Code for machine control over RFID
  written by: Dieter Haude
  reading IDENT from xBee, retrait sending ...POR until time responds
  add current control

  Commands from RFID4Lasercutter
  'lacu,sd' - Ident started?
  'lacu;ok' - Ident on?
  'lacu;em' - Ident error message?
  'lsdis'   - Laser disable
  'lsena'   - Laser enable
  'ok'      - Handshake return

  Commands to RFID4Lasercutter
  'lacu'      - LAserCUtter =Ident
  'por'       - machine power on reset (LCIdent;por)
  'err'       - bus error ocurse
  'sdv;00.0'  - Send Display Temperatur Vorlauf °C
  'sdr;00.0'  - Send Display Temperatur Rücklauf °C
  'sdf;00.0'  - Send Display Flowmeter value l/min
  'msl;x'     - message Laser enabled 1 = active (displayed as las.ena / las.dis)
  'mse;x'     - message Emergency stop 1 = active
  'msc;x'     - message Cover positon 1 = open
  'msp;x'     - message power on 1 = switch on and voltage ok

  'er0;Vorlauf? DS18B20' - no sensor avilable
  'er1;Rueckl.? DS18B20' - no sensor avilable

  last change: 11.11.2020 by Michael Muehl
  changed: communication lasercutter with Serial,
  switch lasercutter on and off, incert emergency stop
*/
#define Version "7.0.0" // (Test =7.0.x ==> 7.0.1)

#include <Arduino.h>
#include <TaskScheduler.h>

// Lasercutter --------
#include <OneWire.h>
#include <DallasTemperature.h>
#define TEMPERATURE_PRECISION 9

// PIN Assignments
// RFID Control -------
// Lasercutter --------
#define FLOWMETER    2  // input Flow Meter [INT0]
#define COVER        3  //~ input lasercutter cover open / closed [int1]
#define FANSPEED     5  //~ PWM - this pin will drive the FET for the cooling fan
#define OWB          6  //~ 1-Wire Bus
#define EMERGENCY    7  // input emergency stop (motors)
#define LCPOWERUP    9  // input switch POWER on (HIGH) [from RFID]
#define SSR_Machine A2  // SSR Machine on / off  (Laser)
#define POWERV      A3  // SSR Machine on / off  (Laser)
// Machine control (Laser Control)
#define ENALaser    A0  // Enable Laser (Optokoppler)
#define SECURITY    A1  // Security enable LaserNT (Optokoppler)

#define BUSError     8  // Bus error
#define signIP      13  // Debug signal (ok)

// Lasercutter --------------
// TempSensoren
#define indexVL 0       // Vorlauf Temp.
#define indexRL 1       // Rücklauf Temp.

// CREATE OBJECTS
Scheduler runner;

// Lasercutter ---------
OneWire  ds(OWB); // on pin 10 (a 4.7K resistor is necessary)
DallasTemperature sensors(&ds);
// arrays to hold device addresses
DeviceAddress tempSensV, tempSensR;

// Callback methods prototypes
void ControlLaser();         // Task for Mainfunction
void BlinkCallback();        // Task to let LED blink - added by D. Haude 08.03.2017

// Lasercutter ---------
void FlowCallback();         // Task to calculate Flow Rate - added by D. Haude 08.03.2017
void TempCallback();         // Task to read temps
void Send2DispCallback();    // Task to send dislpay values
void Send4MesaCallback();    // Task to send messages

void dispPARAM();

// TASKS
Task tM(TASK_SECOND / 10 +2, TASK_FOREVER, &ControlLaser); // 100ms main task
Task tB(TASK_SECOND * 5, TASK_FOREVER, &BlinkCallback);    // 5000ms added M. Muehl

// Lasercutter --------------
Task tFL(TASK_SECOND, TASK_FOREVER, &FlowCallback);
Task tTP(1, TASK_ONCE, &TempCallback);
Task tSD(1, TASK_ONCE, &Send2DispCallback);
Task tSM(1, TASK_ONCE, &Send4MesaCallback);

// VARIABLES
// Lasercutter ------------------------
String IDENT = "LACU";  // Machine identifier for LAserCUtercontroller

// POWER voltage check -
unsigned long val;
int valPOW =  0;        // value POWER
int powOK = 475;        // power voltage > 11.5V

// Sensors -------------
int numberOfDevices;         // Number of temperature devices found
boolean sensorsERR = true;   // Sensor for temperature or flow has wrong values

// Temperature Sensors -
float tempMV;           // Temperature Measure Value
float tempV;            // Temperature Vorlauf (V)
float prevtV;           // letzte Temperature Vorlauf (V)
float tempR;            // Temperature Rücklauf (R)
float prevtR;           // letzte Temperature Rücklauf (R)
const float ttar = 25;  // Target Temperatur (Room temperature)
const float tmax = 40;  // Max. Temperatur (40.0) -> turn relais off if exceeded
const int slope = 25;   // slope for cooling fan speed calculation
bool tempSenV = true;
bool tempSenR = true;

bool tempSensErr = false;  // Temperature sensor error
bool prevTmpSErr;         // letzter Temperature sensor error
bool recOK = false;       // send to display ok
bool firstOK = false;     // first display is send

// Flow Sensor --------------
float flowRate;              // represents Liter/minute
float prevflow;              // prevue represents Liter/minute
byte sensorInterrupt = 0;    // 0 = digital pin 2
int flowcnt = 0;             // Flowmeter Counter
const float flowMin = 3.0;   // value changed by D. Haude on 25.01.2017
// The hall-effect flow sensor outputs approximately 4.5 pulses per second per litre/minute of flow.
const float calibrationFactor = 4.5;

// Messages -----------------
byte coverInterrupt = 1;     // 1 = digital pin 3

boolean LaserCuReady = false; // Lasercutter is ready
boolean EnableLaser = false;  // Laser is ready to burn
boolean laserEnab = false;    // laser can be enabled
boolean prevLaser = true;     // previous laser

boolean emergSTOP;      // emergency stop = true (opto = no current)
boolean prevEmerg;      // previous emergency
boolean coverPOSC;      // cover position closed = false (opto = current)
boolean prevCover;      // previous cover position (changed?)
boolean powerLCON;      // lasercutter is powered on
boolean prevPowON;      // previous power on
boolean powerLCUP;      // power lasercutter up

// Serial
String inStr = "";      // a string to hold incoming data

// ======>  SET UP AREA <=====
void setup() {
  // initialize:
  Serial.begin(9600);   // Serial
  inStr.reserve(40);    // reserve for instr serial input

  sensors.begin();      // Sensoren (Temp)

  // PIN MODES
  pinMode(BUSError, OUTPUT);
  pinMode(SSR_Machine, OUTPUT);

  // Lasercutter ---------
  pinMode(FLOWMETER, INPUT_PULLUP);
  pinMode(COVER, INPUT_PULLUP);
  pinMode(EMERGENCY, INPUT_PULLUP);
  pinMode(FANSPEED, OUTPUT);
  pinMode(SECURITY, OUTPUT);
  pinMode(ENALaser, OUTPUT);

  digitalWrite(SECURITY, HIGH); // Attention! Reversed Logic High = NOT SAVE, LOW = SAVE
  digitalWrite(ENALaser, HIGH); // Attention! Reversed Logic High = NOT SAVE, LOW = SAVE
  analogWrite(FANSPEED, 0);     // set initial fan speed to 0%

  // Set default values
  digitalWrite(BUSError, HIGH);	// turn the LED ON (init start)
  digitalWrite(SSR_Machine, LOW);

  runner.init();
  runner.addTask(tM);
  runner.addTask(tB);

  // Lasercutter ---------
  runner.addTask(tFL);
  runner.addTask(tTP);
  runner.addTask(tSD);
  runner.addTask(tSM);

  // Check 1Wire sensors
  numberOfDevices = sensors.getDeviceCount();

  // set the resolution to 9 bit per device
  sensors.setResolution(tempSensV, TEMPERATURE_PRECISION);
  sensors.setResolution(tempSensR, TEMPERATURE_PRECISION);

  attachInterrupt(digitalPinToInterrupt(FLOWMETER), pulseCounter, FALLING);
  attachInterrupt(digitalPinToInterrupt(COVER), posCover, CHANGE);

  // set all values for power up
  // method 1: by index
  if (!sensors.getAddress(tempSensV, indexVL))
  {
    tempSenV = false;
    tempSensErr = true;
  }
  if (!sensors.getAddress(tempSensR, indexRL))
  {
    tempSenR = false;
    tempSensErr = true;
  }

  emergSTOP = digitalRead(EMERGENCY);

  coverPOSC = digitalRead(COVER);

  powerLCUP = digitalRead(LCPOWERUP);

  valPOW = analogRead(POWERV);

  if (valPOW > powOK && powerLCUP) powerLCON = true;
  else powerLCON = false;

  tFL.enable();
  tM.enable();
}

// FUNCTIONS (Tasks) ----------------------------
void ControlLaser() {   // 500ms Tick
  if (LaserCuReady) LaserControl();
}

void BlinkCallback() {
  // --Blink if BUS Error
  digitalWrite(BUSError, !digitalRead(BUSError));
}

// Lasercutter ---------
void FlowCallback() {
  sensors.requestTemperatures();  // start temperature measurement
  flowRate = flowcnt / calibrationFactor; // result in l/min
  flowcnt = 0;
  tTP.restartDelayed(50);
}

void TempCallback() {
  // --Temperaturmessung T1 + T2
  tempMV = sensors.getTempC(tempSensV);
  if (tempMV == -127.00) {
    tempV = 99.9;
    tempSensErr = true;
  } else {
    tempV = tempMV;
    tempSensErr = false;
  }
  tempMV = sensors.getTempC(tempSensR);
  if (tempMV == -127.00) {
    tempR = 99.9;
    tempSensErr = true;
  } else {
    tempR = tempMV;
    if (tempV < 99.9) tempSensErr = false;  // tempR + tempV <99.9 --> no error
  }

  // Check temperature and flow -------
  if (max(tempR, tempV) >= tmax || flowRate < flowMin || tempSensErr)
  {
    sensorsERR = true;
  }
  else
  {
    sensorsERR = false;
    analogWrite(FANSPEED, speed()); // set appropriate fan speed
  }

  if (recOK) {
    tSD.restartDelayed(50);
  }
}

void Send4MesaCallback()
{
  // Serial.println("Bool: " + String(tempSensErr) + String(sensorsERR) + String(emergSTOP) + String(coverPOSC) + String(powerLCON) + String(powerLCUP) + String(EnableLaser)); // show first display
  if (firstOK)
  {
    if (powerLCON != prevPowON)
    {
      Serial.println("msp;" + String(powerLCON));
      prevPowON = powerLCON;
    }

    // -- send messages for display -----
    if (laserEnab != prevLaser)
    {
      Serial.println("msl;" + String(laserEnab));
      prevLaser = laserEnab;
    }

    if (coverPOSC != prevCover)
    {
      Serial.println("msc;" + String(coverPOSC));
      prevCover = coverPOSC;
    }

    if (emergSTOP != prevEmerg)
    {
      if (powerLCON)
      {
        Serial.println("mss;" + String(emergSTOP));
      }
      else
      {
        Serial.println("mss;0"); // simulate no emergency if no power
      }
      prevEmerg = emergSTOP;
    }

    // -- send messages for temp. sen. error -----
    if (tempSensErr != prevTmpSErr)
    {
      Serial.println("mse;" + String(tempSensErr));
      prevTmpSErr = tempSensErr;
    }
  }
}

void Send2DispCallback()
{
  // -- send values to display --------
  if (tempV != prevtV)
  {
    Serial.println("sdv;" + String(tempV, 1));
    prevtV = tempV;
  }

  if (tempR != prevtR)
  {
    Serial.println("sdr;" + String(tempR, 1));
    prevtR = tempR;
  }

  if (flowRate != prevflow)
  {
    if (flowRate > 99.90)
      flowRate = 99.9;
    String txtVal = String(flowRate, 1);
    if (txtVal.length() < 4)
      txtVal = " " + txtVal;
    Serial.println("sdf;" + txtVal);
    prevflow = flowRate;
  }
  recOK = false;
}
// END OF TASKS ---------------------------------

// FUNCTIONS ------------------------------------
// Lasercutter ------------------------
void LaserControl()
{
  // POWER value ----------------------
  valPOW = analogRead(POWERV);
  // Lasercutter powered UP -----------
  powerLCUP = digitalRead(LCPOWERUP);
  // Lasercutter powered ON -----------
  if (powerLCUP)
  {
    digitalWrite(SSR_Machine, HIGH);
    if (valPOW > powOK) powerLCON = true;
    else powerLCON = false;
  }
  else
  {
    digitalWrite(SSR_Machine, LOW);
    powerLCON = false;
  }
  // emergency stop -------------------
  emergSTOP = digitalRead(EMERGENCY);

  // Lasercut enabled? ----------------
  if (!tempSensErr && !sensorsERR && !emergSTOP && !coverPOSC && powerLCON && EnableLaser)
  {
     if (digitalRead(SECURITY))
       digitalWrite(SECURITY, LOW);
     if (digitalRead(ENALaser))
     {
       digitalWrite(ENALaser, LOW); // turn LASER ENABELE ON
     }
     laserEnab = HIGH;
  }
  else
  {
    // disable lasercut
    if (!digitalRead(SECURITY) || !digitalRead(ENALaser))
    {
      digitalWrite(SECURITY, HIGH); // Output off
      digitalWrite(ENALaser, HIGH); // turn off SECURITY Relais
    }
    laserEnab = LOW;
  }
  tSM.restartDelayed(10);
}

void posCover()
{
  // cover position -------------------
  coverPOSC = digitalRead(COVER);
  LaserControl();
}

void pulseCounter()
{
  flowcnt++;
}

int speed()
{
  float dif = tempR - ttar;
  if (dif > 0)
  {
    return constrain((dif)*slope, 10, 255); // (temperature to tube - target temp) * slope - limited to 10 - 255
  }
  else
  {
    return 0;
  }
}
// End Funktions --------------------------------

// Funktions Serial Input (Event) ---------------
void evalSerialData() {
  inStr.toUpperCase();

  delay(TASK_SECOND / 5);
  if (inStr.startsWith(IDENT))
  {
    if (inStr.endsWith("SD")) // StarteD
    {
      LaserCuReady = false;
      prevPowON = !powerLCON;
      prevTmpSErr = !tempSensErr;
      prevEmerg = !emergSTOP;
      prevCover = !coverPOSC;
      prevtV = 00.0;
      prevtR = 00.0;
      prevflow = 00.0;
      firstOK = false;
      if (numberOfDevices == 2)
      {
        Serial.println("LACU;POR");
      }
      else
      {
        Serial.println("LACU;ERR");
      }
    }
    else if (inStr.endsWith("EM"))
    {
      LaserCuReady = false;
      if (!tempSenV)
      {
        Serial.println("ER0;Vorlauf?     DS18B20");
      }
      if (!tempSenR)
      {
        Serial.println("ER1;Ruecklauf.?  DS18B20");
      }
    }
    else if (inStr.endsWith("OK"))
    {
      LaserCuReady = true;
      digitalWrite(BUSError, LOW); // turn the LED OFF
    }
  }

  if (inStr.equals("LSENA")) EnableLaser = true;

  if (inStr.equals("LSDIS")) EnableLaser = false;

  if (inStr.equals("OK")) {
    recOK = true;
    firstOK = true;
  }
}

/* SerialEvent occurs whenever a new data comes in the
  hardware serial RX.  This routine is run between each
  time loop() runs, so using delay inside loop can delay
  response.  Multiple bytes of data may be available.
*/
void serialEvent() {
  char inChar = (char)Serial.read();
  if (inChar == '\x0d') {
    evalSerialData();
    inStr = "";
  } else if (inChar != '\x0a') {
    inStr += inChar;
  }
}
// End Funktions Serial Input -------------------

// PROGRAM LOOP AREA ----------------------------
void loop() {
  runner.execute();
}

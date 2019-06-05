/* DESCRIPTION
  ====================
  Code for machine control over RFID
  written by: Dieter Haude
  reading IDENT from xBee, retrait sending ...POR until time responds
  add current control

  Commands to RFID4Lasercutter
  'laser;por' - Lasercutter power on reset
  's2l;12.0;34.0;67.0;xxx890x'
  's2d'     - Send 2 Display
  '00.0'    - Temperatur Vorlauf °C
  '00.0'    - Temperatur Rücklauf °C
  '00.0'    - Flowmeter value l/min
  'xxx890x' =:
  'las.dis' - Laser disabled
  'las.ena' - Laser enabled
  'dtt00.0' - Disabled Temperatur Tube to high, Temp

  Commands from RFID4Lasercutter
  'laser;ok'- Lasercutter on?
  'lsdis'   - Laser disable
  'lsena'   - Laser enable
  'vLsOn'   - Visual Laser on
  'vLsOf'   - Visual Laser off
  'ok'      - Handshake return

  last change: 27.09.2018 by Michael Muehl
  changed: communication lasercutter with RFID started,
  added LED13 as indikator for OK-handsake 
*/
#define Version "6.2"

#include <TaskScheduler.h>

// Lasercutter --------
#include <Servo.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#define TEMPERATURE_PRECISION 9

// PIN Assignments
// RFID Control -------
// Lasercutter --------
#define FLOWMETER    2  // input Flow Meter
#define SERVO        3  //~ Servo Motor for visible laser
#define FANSPEED     5  //~ PWM - this pin will drive the FET for the cooling fan
#define OWB          6  //~ 1-Wire Bus
#define VLASER       7  // Visual Laser control
// Machine control (Laser Control)
#define ENALaser    A0  // Enable Laser (Optokoppler)
#define SECURITY    A1  // Security enable LaserNT (Optokoppler)

#define BUSError     8  // Bus error
#define LCControl    9  // SSR Machine on / off  (Machine) [not used, only serial]
#define waitOK      13  // wait for answer: OK

// Lasercutter --------------
// TempSensoren
#define indexVL 0         // Vorlauf Temp.
#define indexRL 1         // Rücklauf Temp.
#define indexLT 2         // LaserTube Temp.

#define vLaserMaxOn 15000 // ms (Wartezeit zum Visuallaser abschalten =vLaserMaxOn/butTime)

// Servo Pos
#define laserONpos   1800 // 1330 Servo home position    changed by E.Terelle on 29.1
#define laserOFFpos  1330 // 1720 Servo end position     changed by E.Terelle on 29.1

// DEFINES
#define butTime       500 // ms Tastenabfragezeit
#define SECONDS      1000 // multiplier for second
#define servoOfftime  750 // ms wait before servo is deatched

// CREATE OBJECTS
Scheduler runner;

// Lasercutter ---------
OneWire  ds(OWB); // on pin 10 (a 4.7K resistor is necessary)
Servo myservo;    // create servo object to control a servo
DallasTemperature sensors(&ds);
// arrays to hold device addresses
DeviceAddress tempSensV, tempSensR, tempSensK;

// Callback methods prototypes
void ControlLaser();       // Task for Mainfunction
void BlinkCallback();   // Task to let LED blink - added by D. Haude 08.03.2017

// Lasercutter ---------
void FlowCallback();  // Task to calculate Flow Rate - added by D. Haude 08.03.2017
void TempCallback();  // Task to read temps - added by M. Muehl 15.03.2017
void SvOfCallback();  // Task to deattach servo - added by M. Muehl 08.03.2017
void DispCallback();  // Task to dislpay values - added by M. Muehl 08.03.2017

void servo2pos(int);
void dispPARAM();

// TASKS
Task tM(butTime, TASK_FOREVER, &ControlLaser);
Task tB(5000, TASK_FOREVER, &BlinkCallback); //added M. Muehl

// Lasercutter --------------
Task tF(1000, TASK_FOREVER, &FlowCallback);
Task tT(1,    TASK_ONCE,    &TempCallback);
Task tS(1,    TASK_ONCE,    &SvOfCallback);
Task tD(1,    TASK_ONCE,    &Send2DispCallback);

// VARIABLES
unsigned long val;
unsigned int timer = 0;
boolean onTime = false;
int minutes = 0;
bool toggle = false;

// Lasercutter ---------
// Temperature Control
float tempMV;             // Temperature Measure Value
float tempV;              // Temperature Vorlauf (V)
float tempR;              // Temperature Rücklauf (R)
float tempK = 15.0;       // Temperature Kathode (K) - added by D.Haude on 27.01.2017
float tdif = 0;           // Temperature Difference VL - RL
const float ttar = 25.0;  // Target Temperatur
const float tmax = 40.0;  // Max. Temperatur (40.0) -> turn relais off if exceeded
const float ttub = 40.0;  // Max. Temperatur (40.0) for Tube (Kathode)
const int  slope = 25;    // slope for cooling fan speed calculation

// Flow Control
float flowRate; // represents Liter/minute
byte sensorInterrupt = 0; // 0 = digital pin 2
int flowcnt = 0;          // Flowmeter Counter
const float flowMin =3.0; // value changed by D. Haude on 25.01.2017
// The hall-effect flow sensor outputs approximately 4.5 pulses per second per litre/minute of flow.
const float calibrationFactor = 4.5;

int numberOfDevices;   // Number of temperature devices found
int vLaserTime = 0;    // Visual Laser Taster time
boolean tubeTempErr  = true; // Tube Temp. überschritten
boolean controlErr   = true; // Control of temp or flow are wrong

int InterSens = 5000;     // Intervallzeit Sensors
unsigned long prevSens;   // jetziger Zeitstempel Sensors

// Handshake with RFID4Lasercutter:
boolean recOK        = false; // received OK
boolean LaserPowerOn = false; // Power On Reset set
boolean EnableLaser  = false; // Laser is ready to burn
boolean VisualLaser  = false; // Visual Laser is active? (True)

// Serial
String inStr = "";  // a string to hold incoming data

// ======>  SET UP AREA <=====
void setup() {
  // initialize:
  Serial.begin(9600);  // Serial
  inStr.reserve(30);    // reserve for instr serial input

  sensors.begin();      // Sensoren (Temp)

  // PIN MODES
  pinMode(BUSError, OUTPUT);

// Lasercutter ---------
  pinMode(FLOWMETER, INPUT_PULLUP);
  pinMode(VLASER, OUTPUT);
  pinMode(FANSPEED, OUTPUT);
  pinMode(SECURITY, OUTPUT);
  pinMode(SERVO, OUTPUT);
  pinMode(ENALaser, OUTPUT);

  // Set default values
  digitalWrite(BUSError, HIGH);	// turn the LED ON (init start)
  digitalWrite(waitOK, HIGH);	  // turn the LED ON (init start)

// Lasercutter ---------
  digitalWrite(SECURITY, HIGH); // Attention! Reversed Logic High = NOT SAVE, LOW = SAVE
  digitalWrite(ENALaser, HIGH); // Attention! Reversed Logic High = NOT SAVE, LOW = SAVE
  analogWrite(FANSPEED, 0);	    // set initial fan speed to 0%
  digitalWrite(VLASER, LOW);	  // set VLaser off

  runner.init();
  runner.addTask(tM);
  runner.addTask(tB);

// Lasercutter ---------
  runner.addTask(tF);
  runner.addTask(tT);
  runner.addTask(tD);
  runner.addTask(tS);

  numberOfDevices = sensors.getDeviceCount();
  // 1Wire mit slave vorhanden
  if (numberOfDevices >=2) {
    // method 1: by index
    if (!sensors.getAddress(tempSensV, indexVL)) Serial.println("No Device V");
    if (!sensors.getAddress(tempSensR, indexRL)) Serial.println("No Device R");
    if (!sensors.getAddress(tempSensK, indexLT) && numberOfDevices ==3) Serial.println("Unable to find address for Device K");

    // set the resolution to 9 bit per device
    sensors.setResolution(tempSensV, TEMPERATURE_PRECISION);
    sensors.setResolution(tempSensR, TEMPERATURE_PRECISION);
    if (numberOfDevices ==3) sensors.setResolution(tempSensK, TEMPERATURE_PRECISION);

    servo2pos(laserONpos);

    attachInterrupt(sensorInterrupt, pulseCounter, FALLING);

    tF.enable();
    delay(2000);  // wait for power on
    digitalWrite(waitOK, LOW);	  // turn the LED ON (init start)
    Serial.println("LASER;POR");
    tM.enable();
  } else {
    tB.enable();  // enable Task Error blinking
    tB.setInterval(SECONDS);
  }
}

// FUNCTIONS (Tasks) ----------------------------
void ControlLaser() {   // 500ms Tick
  if (LaserPowerOn) LaserControl();
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
  tT.restartDelayed(50);
}

void TempCallback() {
  // --Temperaturmessung T1 + T2 + T3
  tempMV = sensors.getTempC(tempSensV);
  if (tempMV != -127.00) tempV = tempMV;
  tempMV = sensors.getTempC(tempSensR);
  if (tempMV != -127.00) tempR = tempMV;
  tdif = tempR - tempV;
  if (numberOfDevices ==3) {
    tempK = sensors.getTempC(tempSensK);
  }
  // Check Errors ------
  if (max(tempR,tempV) >= tmax || flowRate < flowMin) controlErr = true;
  else controlErr = false;
  if (tempK > ttub) tubeTempErr = true;
  else tubeTempErr = false;
  // Set Fan speed
  analogWrite(FANSPEED, speed());   // set appropriate fan speed

  if (recOK) tD.restartDelayed(50);
}

void Send2DispCallback() {
  // --Display all
  digitalWrite(waitOK, HIGH);
  Serial.print("s2d");
  Serial.print(";" + String(tempV,1));
  Serial.print(";" + String(tempR,1));
  if (flowRate > 99.90) flowRate = 99.9;
  String txtVal = String(flowRate,1);
  if (txtVal.length() < 4) txtVal =  " " + txtVal;
  Serial.print(";" + txtVal);
  if(!digitalRead(SECURITY) && !digitalRead(ENALaser) && (tubeTempErr == false))
  {
    Serial.println(";" + String("Las.ENA"));
  } else if (tubeTempErr == false) {
    Serial.println(";" + String("Las.DIS"));
  } else {
    Serial.println(";" + String(tempK,1));
  }
  recOK = false;
}

void SvOfCallback() {
  // --Servo off
  if (myservo.attached() == true) {
    myservo.detach(); //added M. Muehl detach after longer time on 15.2.2017
    digitalWrite(VLASER, VisualLaser); // turn on vl
  }
}
// END OF TASKS ---------------------------------

// FUNCTIONS ------------------------------------
// Lasercutter ------------------------
void LaserControl() {
  // check no error -------------------
  if (!controlErr && EnableLaser) {
    if (digitalRead(SECURITY)) digitalWrite(SECURITY, LOW);
    if (VisualLaser) {
      if(!digitalRead(ENALaser)) {
        digitalWrite(ENALaser, HIGH);  // turn LASER ENABELE OFF
        servo2pos(laserONpos);
      }
      if(vLaserTime == vLaserMaxOn/butTime) {
        digitalWrite(VLASER, LOW);
      } else {
        vLaserTime++;
      }
    } else {
      if(digitalRead(ENALaser)) {
        digitalWrite(ENALaser, LOW); // turn LASER ENABELE ON
        servo2pos(laserOFFpos);
      }
      vLaserTime = 0;
    }
  } else {
    shutdownLaser();
  }
}

void shutdownLaser() {
  // reset all values for lasercutter
  if(!digitalRead(SECURITY) || !digitalRead(ENALaser)) {
    digitalWrite(SECURITY, HIGH); // Output off
    digitalWrite(ENALaser, HIGH); // turn off SECURITY Relais
    servo2pos(laserONpos);
    digitalWrite(VLASER, LOW);
    VisualLaser = LOW;
  }
}

void pulseCounter() {
  flowcnt++;
}

int speed() {
  float dif=tempV-ttar;
  if(dif > 0) {
    return constrain((dif) * slope, 10, 255);  // (temperature to tube - target temp) * slope - limited to 10 - 255
  } else {
    return 0;
  }
}

void servo2pos(int val) {
//  Servo set Pos
  digitalWrite(VLASER, LOW); // turn off vl
  if (myservo.attached() == false) myservo.attach(SERVO, 1000, 2000);  //changed D. Haude and M. Muehl
  myservo.writeMicroseconds(val);  // tell servo to go to position in variable 'pos'changed by E.Terelle on 29.1
  tS.restartDelayed(servoOfftime);
}
// End Funktions --------------------------------

// Funktions Serial Input (Event) ---------------
void evalSerialData() {
  inStr.toUpperCase();

  if (inStr.startsWith("LASER")) {
    if (inStr.substring(6) == "OK") {
      LaserPowerOn = true;
      digitalWrite(BUSError, LOW);	// turn the LED OFF
      digitalWrite(waitOK, HIGH);	  // wait for ok
    }
  }

  if (inStr.equals("LSENA")) EnableLaser = true;

  if (inStr.equals("LSDIS")) EnableLaser = false;

  if (inStr.equals("VLSON")) VisualLaser = true;

  if (inStr.equals("VLSOF")) VisualLaser = false;

  if (inStr.equals("OK")) {
    recOK = true;
    digitalWrite(waitOK, LOW);
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

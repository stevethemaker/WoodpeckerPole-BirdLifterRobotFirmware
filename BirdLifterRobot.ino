/*
 * BirdLifter Robot Firmware
 * 
 * Copyright (c) 2017 Steve Knodl 
 * 
 * App in support of "Possibly the World's tallest Woodpecker Toy Pole"
 * Exhibit at Austin Maker Faire, May 13-14, 2017
 *  
 * MIT License, check LICENSE for more information
 * 
 */

#include <Arduino.h>
#include <SPI.h>
#if not defined (_VARIANT_ARDUINO_DUE_X_) && not defined (_VARIANT_ARDUINO_ZERO_)
#include <SoftwareSerial.h>
#endif

#include "Adafruit_BLE.h"
#include "Adafruit_BluefruitLE_SPI.h"
#include "Adafruit_BluefruitLE_UART.h"
#include "Adafruit_BLEGatt.h"
#include "BluefruitConfig.h"

#include <Wire.h>
#include <Servo.h>

#include "MotorUtility.h"

//#define DEBUG

#ifdef DEBUG 
#define Sprintln(a) (Serial.println(a))
#define Sprint(a) (Serial.print(a))
#else
#define Sprintln(a)
#define Sprint(a)
#endif

/*
 * FACTORYRESET_ENABLE     Perform a factory reset when running this sketch
 *                         Enabling this will put your Bluefruit LE module
 *                         in a 'known good' state and clear any config
 *                         data set in previous sketches or projects, so
 *                         running this at least once is a good idea.
 */

#define FACTORYRESET_ENABLE        1

/*
 * Pin constants
 */

const int bleIRQ_D_Input_pin = 1;  // Must physically wire DFU pin to interruptable pin

const int latchServo_D_Output_pin = 5;
const int launcherServo_D_Output_pin = 6;

const int processorVoltage_A_Input_pin = 9;
const int motorVoltage_A_Input_pin = 15;

const int redLED_D_Output_pin = 13;

const int limitSwitchTop_D_Input_pin = 18;
const int limitSwitchBottom_D_Input_pin = 19;

const int motorEnable_D_Output_pin = 10;
const int motorDirA_D_Output_pin = 11;
const int motorDirB_D_Output_pin = 12;

/*
 * BLE UUIDs
 */

uint8_t robotControlServiceIdentifer[16] = {0xC7, 0x00, 0x60, 0x4B, 0x27, 0x57, 0x49, 0xA0, 0xB0, 0x2A, 0x6A, 0x8C, 0x06, 0x1B, 0xBC, 0x1E};
uint8_t batteryVoltage[16] = {0x33, 0xDF, 0xD5, 0x73, 0xAB, 0xF9, 0x47, 0x07, 0x9E, 0x34, 0x29, 0xE2, 0x01, 0x1C, 0x23, 0x1E};
uint8_t motorControl[16] = {0xC8, 0x2B, 0x47, 0x53, 0x2D, 0x94, 0x47, 0xDF, 0xB2, 0xFF, 0x09, 0x09, 0x9F, 0x2B, 0x0E, 0x39};
uint8_t robotPosition[16] = {0xAD, 0x12, 0x37, 0x65, 0xA4, 0x21, 0x4F, 0x80, 0xBE, 0x1B, 0x62, 0xDE, 0xEB, 0x85, 0x41, 0x41};
uint8_t latchPosition[16] = {0x67, 0x5C, 0xF6, 0x27, 0x6C, 0xC0, 0x48, 0x10, 0xAF, 0xDB, 0xB6, 0xAA, 0x3F, 0x71, 0x83, 0xC5};
uint8_t launcherPosition[16] = {0x43, 0x4D, 0x07, 0x8D, 0x79, 0x03, 0x44, 0xD0, 0xA3, 0xB5, 0x7E, 0x87, 0x88, 0x4B, 0x80, 0xED};

/*
 * Globals 
 */

Adafruit_BluefruitLE_SPI ble(BLUEFRUIT_SPI_CS, BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST);
Adafruit_BLEGatt gatt(ble);

MotorController triMotorController = MotorController(motorEnable_D_Output_pin, motorDirA_D_Output_pin, motorDirB_D_Output_pin);

Servo latchServo;
Servo launcherServo;

int32_t charid_batteryVoltage;
int32_t charid_motorControl;
int32_t charid_robotPosition;
int32_t charid_latchPosition;
int32_t charid_launcherPosition;

volatile boolean irq_event_available = false; // use boolean variable to signal loop() to call ble.update()

unsigned int irq_ovf_count = 0;

/*
 * Pin state helpers
 */

bool isRobotAtUpperLimit() {
  return  (digitalRead(limitSwitchTop_D_Input_pin) == LOW);
}

bool isRobotAtLowerLimit() {
   return  (digitalRead(limitSwitchBottom_D_Input_pin) == LOW);
}

/*
 * Helper - Print error to console and hang
 */

void error(const __FlashStringHelper*err) {
  Sprintln(err);
  while (1);
}

/*
 * BLE ISR
 */

void DfuIrqHandle(void)
{
  // signal loop() to handle event
  irq_event_available = true;
}

/*
 * BLE Callbacks
 */

void connected(void)
{
  irq_event_available = false;
  Sprintln( F("Connected") );
}

void disconnected(void)
{
  irq_event_available = false;
  Sprintln( F("Disconnected") );
}

void BleGattRX(int32_t chars_id, uint8_t data[], uint16_t len)
{
  irq_event_available = false;
  Sprint( F("[BLE GATT RX] (" ) );
  Sprint(chars_id);
  Sprint(") - ");
  //Sprintln();
  uint16_t i;
  for (i = 0; i < len; i++) {
    Sprint(data[i]);
    Sprint( F("-") );
  }
  Sprintln();

  // Ensure password bytes match or ignore
  if (!weakSecurityCheckPassed(data,len)) {
    Sprintln("Bad Password");
    return;
  }

  if (chars_id == charid_motorControl) {
    MotorPosition motorCommand = newMotorPosition(data, len);
    triMotorController.executeCommand(motorCommand);
    Sprint("Motor Control - ");
    Sprint(motorCommand.speed); 
    Sprint("  ");
    Sprint(motorCommand.direction);
    Sprint("\n");
  } else if (chars_id == charid_latchPosition) {
    ServoPosition newPosition = newServoPosition(data, len);
    latchServo.write(newPosition.desiredPosition);
    Sprintln("Latch Control");
  } else if (chars_id == charid_launcherPosition) {
    ServoPosition newPosition = newServoPosition(data, len);
    launcherServo.write(newPosition.desiredPosition);
    Sprintln("Launcher Control");
  }  
}

/*
 * Set up the HW an the BLE module
 */

void setup(void)
{

#ifdef DEBUG
  while (!Serial);  // required for Flora & Micro
  delay(500);
  Serial.begin(115200);
#endif

  delay(500);
  
  Sprintln(F("Steve the Maker Woodpecker Pole Robot Controller"));
  Sprintln(F("------------------------------------------------"));

  pinMode(bleIRQ_D_Input_pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(bleIRQ_D_Input_pin), DfuIrqHandle, FALLING);

  /* Initialise the module */
  Sprint(F("Initialising the Bluefruit LE module: "));

#ifdef DEBUG
#define VERBOSE_MODE_LOCAL true
#else 
#define VERBOSE_MODE_LOCAL false
#endif

  if ( !ble.begin(VERBOSE_MODE_LOCAL) )
  {
    error(F("Couldn't find Bluefruit, make sure it's in CoMmanD mode & check wiring?"));
  }
  Sprintln( F("OK!") );

#ifdef DEBUG
  Sprintln(F("Existing GATT"));
  ble.sendCommandCheckOK( F("AT+GATTLIST") );
#endif 

  if ( FACTORYRESET_ENABLE )
  {
    /* Perform a factory reset to make sure everything is in a known state */
    Sprintln(F("Performing a factory reset: "));
    if ( ! ble.factoryReset() ) {
      error(F("Couldn't factory reset"));
    }
  
  
  Sprintln( F("Adding Robot Control Service with five characteristics") );

  gatt.addService(robotControlServiceIdentifer);
  charid_batteryVoltage = gatt.addCharacteristic(batteryVoltage, GATT_CHARS_PROPERTIES_READ, 4, 4, BLE_DATATYPE_BYTEARRAY, "battery voltage");
  charid_robotPosition = gatt.addCharacteristic(robotPosition, GATT_CHARS_PROPERTIES_READ, 2, 2, BLE_DATATYPE_BYTEARRAY, "robot position");
  charid_motorControl = gatt.addCharacteristic(motorControl, GATT_CHARS_PROPERTIES_WRITE | GATT_CHARS_PROPERTIES_READ, 5, 5, BLE_DATATYPE_BYTEARRAY, "motor control");
  charid_latchPosition = gatt.addCharacteristic(latchPosition, GATT_CHARS_PROPERTIES_WRITE | GATT_CHARS_PROPERTIES_READ, 3, 3, BLE_DATATYPE_BYTEARRAY, "latch position");
  charid_launcherPosition = gatt.addCharacteristic(launcherPosition, GATT_CHARS_PROPERTIES_WRITE | GATT_CHARS_PROPERTIES_READ, 3, 3, BLE_DATATYPE_BYTEARRAY, "launcher position");

  }
  /* Set Robot Service advertising packet */
  ble.sendCommandCheckOK( F("AT+GAPSETADVDATA=02-01-06-11-06-1E-BC-1B-06-8C-6A-2A-B0-A0-49-57-27-4B-60-00-C7") );

  
  /* Reset the device for the new service setting changes to take effect */
  Sprint(F("Performing a SW reset (service changes require a reset): "));
  ble.reset();

#ifdef DEBUG
  Sprintln(F("Existing GATT"));
  ble.sendCommandCheckOK( F("AT+GATTLIST") );
#endif 

  /* Disable command echo from Bluefruit */
  ble.echo(VERBOSE_MODE_LOCAL);

#ifdef DEBUG
  Sprintln("Requesting Bluefruit info:");
  /* Print Bluefruit information */
  ble.info();
#endif

  /* Change DFU Pin to IRQ mode */
  Sprintln( F("Change DFU Pin to IRQ Mode") );
  ble.sendCommandCheckOK( F("AT+DFUIRQ=on") );

  /* Set callbacks */
  ble.setConnectCallback(connected);
  ble.setDisconnectCallback(disconnected);

  /* Only one BLE GATT function should be set, it is possible to set it
    multiple times for multiple Chars ID  */

  // Set callbacks for all writeable characteristics
  //  
  ble.setBleGattRxCallback(charid_motorControl, BleGattRX);
  ble.setBleGattRxCallback(charid_latchPosition, BleGattRX);
  ble.setBleGattRxCallback(charid_launcherPosition, BleGattRX);

/*
 * Limit Switches
 */

  pinMode(limitSwitchTop_D_Input_pin, INPUT_PULLUP);
  pinMode(limitSwitchBottom_D_Input_pin, INPUT_PULLUP);

/*
 * LED output
 */

  pinMode(redLED_D_Output_pin, OUTPUT);

/*
 * Motor Init - Zero speed, disconnected
 */

  pinMode(motorEnable_D_Output_pin, OUTPUT);
  pinMode(motorDirA_D_Output_pin, OUTPUT);
  pinMode(motorDirB_D_Output_pin, OUTPUT);

  triMotorController.stop();
   
/*
 * Servo setup
 */
 
  latchServo.write(90);
  latchServo.attach(latchServo_D_Output_pin);
  launcherServo.write(90);
  launcherServo.attach(launcherServo_D_Output_pin);

/*
 * Analog Battery Monitors
 */

 pinMode(processorVoltage_A_Input_pin, INPUT);
 pinMode(motorVoltage_A_Input_pin, INPUT);

/*
 * Timers for multitasking/timing
 */

 
/**
 * @author Markus Bader
 * @brief this program shows how to use the TCC timer with interrupts on an Arduino Zero board
 * @email markus.bader@tuwien.ac.at
 */
  if (true) {
  // Enable clock for TC 
  REG_GCLK_CLKCTRL = (uint16_t) (GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID_TCC0_TCC1) ;
  while ( GCLK->STATUS.bit.SYNCBUSY == 1 ); // wait for sync 


  // The type cast must fit with the selected timer 
  Tcc* TC = (Tcc*) TCC0; // get timer struct
  
  TC->CTRLA.reg &= ~TCC_CTRLA_ENABLE;   // Disable TC
  while (TC->SYNCBUSY.bit.ENABLE == 1); // wait for sync 


  TC->CTRLA.reg |= TCC_CTRLA_PRESCALER_DIV256;   // Set perscaler


  TC->WAVE.reg |= TCC_WAVE_WAVEGEN_NFRQ;   // Set wave form configuration 
  while (TC->SYNCBUSY.bit.WAVE == 1); // wait for sync 

  TC->PER.reg = 0x07AE;              // Set counter Top using the PER register  (~10ms)
  while (TC->SYNCBUSY.bit.PER == 1); // wait for sync 

  TC->CC[0].reg = 0x0000;
  while (TC->SYNCBUSY.bit.CC0 == 1); // wait for sync 
  
  // Interrupts 
  TC->INTENSET.reg = 0;                 // disable all interrupts
  TC->INTENSET.bit.OVF = 1;          // enable overfollow
  //TC->INTENSET.bit.MC0 = 1;          // enable compare match to CC0

  // Enable InterruptVector
  NVIC_EnableIRQ(TCC0_IRQn);

  // Enable TC
  TC->CTRLA.reg |= TCC_CTRLA_ENABLE ;
  while (TC->SYNCBUSY.bit.ENABLE == 1); // wait for sync 
  }

}

/*
 * TIMER ISR
 */

void TCC0_Handler() {
  Tcc* TC = (Tcc*) TCC0;       // get timer struct
  if (TC->INTFLAG.bit.OVF == 1) {  // A overflow caused the interrupt
  //  digitalWrite(pin_ovf_led, irq_ovf_count % 2); // for debug leds
  //  digitalWrite(redLED_D_Output_pin, HIGH); // for debug leds

  // Call Timer based stuff here
    triMotorController.update();

    TC->INTFLAG.bit.OVF = 1;    // writing a one clears the flag ovf flag
    irq_ovf_count++;                 // for debug leds
  }
  
//  if (TC->INTFLAG.bit.MC0 == 1) {  // A compare to cc0 caused the interrupt
//    digitalWrite(redLED_D_Output_pin, LOW);  // for debug leds
//    TC->INTFLAG.bit.MC0 = 1;    // writing a one clears the flag ovf flag
//  }
}

/*
 * 
 * Main Event Loop
 * 
 */

void loop(void)
{
  /*
   * BLE IRQ checks
   */
   
  if (irq_event_available) {
    // Registered callbacks for the event will be fired accordingly
    // Sprintln("Dfu");
    ble.handleDfuIrq();
  }

  /*
   * Robot carriage limit checks
   */

  if (isRobotAtUpperLimit()) {
    Sprintln("Upper Limit Stop");
  }
  triMotorController.setAtUpperLimit(isRobotAtUpperLimit()); 
  
  if (isRobotAtLowerLimit()) {
    Sprintln("Upper Limit Stop");
  }
  triMotorController.setAtLowerLimit(isRobotAtLowerLimit()); 

  int processorVoltage = analogRead(processorVoltage_A_Input_pin);
  int motorVoltage = analogRead(motorVoltage_A_Input_pin);
  uint32_t battery = (uint16_t)motorVoltage << 16 | (uint16_t)processorVoltage;
  gatt.setChar(charid_batteryVoltage,battery);
  
}

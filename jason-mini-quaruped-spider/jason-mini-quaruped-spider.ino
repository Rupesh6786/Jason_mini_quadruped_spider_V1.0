/*
 * =====================================================================================
 *  Project       : ESP32 QuadRobot Spider Firmware (Driverless Direct PWM)
 *  Author        : Rupesh Thakur (https://github.com/Rupesh6786)
 *  Repository    : https://github.com/Rupesh6786/esp_quadruped_robot
 *  Target MCU    : ESP32 (WROOM-32 / ESP32-DevKit v1)
 *  Language      : C++ / Arduino Framework
 * =====================================================================================
 * 
 *  [OVERVIEW]
 *  Firmware for an 8-DOF Quadruped Spider Robot driven directly via ESP32 PWM pins 
 *  without external hardware servo drivers (e.g., PCA9685). Features low-power 
 *  optimization strategies including software deadband filtering, micro-staggered 
 *  PWM pulses, CPU clock scaling, and RF transmission power reduction to limit peak 
 *  current consumption under 3A on a 2S Li-ion battery setup.
 * 
 *  [ROBOT TOPOLOGY & PINOUT (TOP VIEW)]
 *
 *               FRONT
 *      -----               -----
 *     |  1  |             |  5  |   [1] FR Foot (GPIO 32)   | [5] FL Foot (GPIO 2)
 *     | P32 |             | P2  |   [2] FR Leg  (GPIO 33)   | [6] FL Leg  (GPIO 4)
 *      ----- -----   ----- -----
 *           |  2  | |  6  |
 *           | P33 | |  P4 |
 *            -----   -----
 *           |  3  | |  7  |         [3] RR Leg  (GPIO 27)   | [7] RL Leg  (GPIO 18)
 *           | P27 | | D18 |         [4] RR Foot (GPIO 12)   | [8] RL Foot (GPIO 19)
 *      ----- -----   ----- -----
 *     |  4  |             |  8  |
 *     | P12 |             | D19 |
 *      -----               -----
 *               REAR
 *
 * =====================================================================================
 */


//++ Include lib to perform operation with BLE
#include "BluetoothSerial.h"
#include "esp_bt.h"

//++ Include lib for handling the EROM storage
#include <EEPROM.h>

//++ Including the lib for performing the animation on the robot
#include "animations.h"

//++ Include the lib for servo control operation
#include <ESP32Servo.h>

BluetoothSerial SerialBT;

const uint8_t FRONT_RIGHT_FOOT = 32;
const uint8_t FRONT_RIGHT_LEG = 33;
const uint8_t REAR_RIGHT_LEG = 27;
const uint8_t REAR_RIGHT_FOOT = 12;

const uint8_t FRONT_LEFT_FOOT = 2;
const uint8_t FRONT_LEFT_LEG = 4;
const uint8_t REAR_LEFT_LEG = 18;
const uint8_t REAR_LEFT_FOOT = 19;

const int numberOfServos = 8;  // Number of servos

//++ variable to handle continuos animation handling via non-blocking animation method
bool isAnimationRunning = false;

//calibration legs FRF, FRL, RRL, RRF, FLF, FLL, RLL, RLF
int servoCal[] = { 0, 0, 0, 0, 0, 0, 0, 0 };                    // Servo calibration data
int servoPos[] = { 135, 45, 135, 45, 45, 135, 45, 135 };        // Servo current position
int lastWrittenPos[] = { 135, 45, 135, 45, 45, 135, 45, 135 };  // Track actual PWM states to prevent cumulative read errors
const int servoPrgPeriod = 20;                                  // 20 ms
String dataIn = "";

//++ Intialize the servo object
Servo servo[numberOfServos];

// EEPROM Clear (For debug only)
// void eepromClear()
// {
//   for (int i = 0; i < EEPROM.length(); i++) {
//     EEPROM.write(i, 0);
//   }
// }

// Get servoCal from EEPROM
void getServoCal() {
  int eeAddress = 0;
  for (int i = 0; i < numberOfServos; i++) {
    EEPROM.get(eeAddress, servoCal[i]);
    eeAddress += sizeof(servoCal[i]);
  }
}

// Put servoCal to EEPROM
void putServoCal() {
  int eeAddress = 0;
  for (int i = 0; i < numberOfServos; i++) {
    EEPROM.put(eeAddress, servoCal[i]);
    eeAddress += sizeof(servoCal[i]);
  }
}

// Clear Servo calibration data
void clearCal() {
  for (int i = 0; i < numberOfServos; i++) {
    servoCal[i] = 0;
  }
  putServoCal();                            // Put servoCal to EEPROM
  runServoPrg(servoPrg00, servoPrg00step);  // zero position
}

// Calibration
// void calibration(int i, int change)
// {
//   servoCal[i] = servoCal[i] + change;
//   servo[i].write(servoAct00[i] + servoCal[i]);
//   putServoCal(); // Put servoCal to EEPROM
//   delay(400);
// }

void runServoPrg(int servoPrg[][numberOfACE], int step) {
  for (int i = 0; i < step; i++) {  // Loop for step

    int totalTime = servoPrg[i][numberOfACE - 1];  // Total time of this step
    int stepsInTime = totalTime / servoPrgPeriod;
    if (stepsInTime < 1) stepsInTime = 1;

    // Get servo start position from last tracked PWM positions
    for (int s = 0; s < numberOfServos; s++) {
      servoPos[s] = lastWrittenPos[s] - servoCal[s];
    }

    // Optimized runServoPrg loop block with deadband filter and micro-staggering
    for (int j = 0; j <= stepsInTime; j++) {
      for (int k = 0; k < numberOfServos; k++) {
        int targetPos = (map(j, 0, stepsInTime, servoPos[k], 180 - servoPrg[i][k])) + servoCal[k];

        // Deadband filter: only update PWM if target changes by > 1 degree (or at step end)
        if (abs(lastWrittenPos[k] - targetPos) > 1 || j == stepsInTime) {
          servo[k].write(targetPos);
          lastWrittenPos[k] = targetPos;

          // 0.4ms micro-stagger between servos flattens simultaneous peak current spikes
          delayMicroseconds(400);
        }
      }
      delay(servoPrgPeriod);
    }
  }
}

//++ Function to handle the input commands via bluetooth based android app
void runAnimationFromInput(String inputValue) {

  if (isAnimationRunning) {
    Serial.println("Animation is still running. Command ignored.");
    return;
  }

  isAnimationRunning = true;  // BLOCK new commands
  Serial.printf("Got value as %s %d\n", inputValue.c_str(), inputValue.toInt());

  switch (inputValue.toInt()) {  //++ Switch Case between different animation
    case 0:
      Serial.println("Executing the zero state Animation");
      runServoPrg(servoPrg00, servoPrg00step);
      break;
    case 1:
      Serial.println("Executing the Stand-By Animation");
      runServoPrg(servoPrg01, servoPrg01step);
      break;
    case 2:
      Serial.println("Executing the Move-Foward Animation");
      runServoPrg(servoPrg02, servoPrg02step);
      break;
    case 3:
      Serial.println("Executing the Move-Backward Animation");
      runServoPrg(servoPrg03, servoPrg03step);
      break;
    case 4:
      runServoPrg(servoPrg04, servoPrg04step);
      break;
    case 5:
      runServoPrg(servoPrg05, servoPrg05step);
      break;
    case 6:
      runServoPrg(servoPrg06, servoPrg06step);
      break;
    case 7:
      runServoPrg(servoPrg07, servoPrg07step);
      break;
    case 8:
      runServoPrg(servoPrg08, servoPrg08step);
      break;
    case 9:
      runServoPrg(servoPrg09, servoPrg09step);
      break;
    case 10:
      runServoPrg(servoPrg10, servoPrg10step);
      break;
    case 11:
      runServoPrg(servoPrg11, servoPrg11step);
      break;
    case 12:
      runServoPrg(servoPrg12, servoPrg12step);
      break;
    case 13:
      runServoPrg(servoPrg13, servoPrg13step);
      break;
    case 14:
      runServoPrg(servoPrg14, servoPrg14step);
      break;
    case 15:
      runServoPrg(servoPrg15, servoPrg15step);
      break;
    default:
      Serial.println("Invalid input for animation.");
      break;
  }
  isAnimationRunning = false;  // RELEASE robot after animation completes
}

void setup() {
  setCpuFrequencyMhz(80);  // Lower CPU clock to reduce core draw
  Serial.begin(115200);
  Serial.println("BLUETOOTH CONTROL QuadRobot Spider!");
  SerialBT.begin("QuadRobot-Model-TEST-ONE");

  // Set Bluetooth Classic power level correctly using ESP-IDF API
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_N3);

  //++ setting up the all servos objects
  servo[0].attach(FRONT_RIGHT_FOOT);
  servo[1].attach(FRONT_RIGHT_LEG);
  servo[2].attach(REAR_RIGHT_LEG);
  servo[3].attach(REAR_RIGHT_FOOT);
  servo[4].attach(FRONT_LEFT_FOOT);
  servo[5].attach(FRONT_LEFT_LEG);
  servo[6].attach(REAR_LEFT_LEG);
  servo[7].attach(REAR_LEFT_FOOT);

  // EEPROM can be used later to store calibration data if needed
  EEPROM.begin(64);
  delay(300);
  Serial.println("Setting up the all servo to the default postion");
  runServoPrg(servoPrg01, servoPrg01step);  // standby position at setup
}

void loop() {
  if (SerialBT.hasClient()) {
    if (SerialBT.available() > 0) {
      
      // Read incoming data from Bluetooth buffer
      dataIn = SerialBT.readString();
      dataIn.trim();

      if (dataIn.length() > 0) {
        // Case A: It's a continuous movement stream ending with 'S' (e.g., "22222S")
        if (dataIn.endsWith("S") || dataIn.length() > 2) {
          // Extract just the unique repeating movement digit (e.g., the first character)
          char cmdCode = dataIn.charAt(0);
          dataIn = String(cmdCode);
        }
        // Case B: It's a direct action/pose command (e.g., "12", "0", "8") -> keep it as is!

        // If it's a valid command and robot isn't busy
        if (!isAnimationRunning) {
          Serial.println("Processed Command: " + dataIn);
          runAnimationFromInput(dataIn);
        }
      }
    }
  }
}
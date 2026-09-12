#ifndef IMU_MANAGER_H
#define IMU_MANAGER_H

#include "../config.h"
#include <MPU6050_light.h>
#include <Wire.h>

class IMUManager {
public:
  IMUManager();
  void begin();
  void update();
  void calibrate();

  float getAngleX() { return _angleX; }
  float getAngleY() { return _angleY; }
  float getAngleZ() { return _angleZ; }
  float getLeanAngle() { return _angleX; } // Alias for Roll
  float getMaxLeanLeft() { return _maxLeanLeft; }
  float getMaxLeanRight() { return _maxLeanRight; }
  void resetMaxLean() {
    _maxLeanLeft = 0;
    _maxLeanRight = 0;
  }

  void requestActivity(bool active) { _requestActive = active; }

  float getAccX() { return _accX; }
  float getAccY() { return _accY; }
  float getAccZ() { return _accZ; }

  bool isConnected() { return _isConnected; }

  bool isEnabled() { return _isEnabled; }
  void setEnabled(bool enabled) { _isEnabled = enabled; }

  void setRollOffset(float offset) { _rollOffset = offset; }
  float getRollOffset() { return _rollOffset; }
  void setPitchOffset(float offset) { _pitchOffset = offset; }
  float getPitchOffset() { return _pitchOffset; }

  void calibrateLevel(); // Set current orientation as 0,0
  void saveSettings();

private:
  MPU6050 *_mpu;
  bool _isConnected;
  bool _isEnabled;
  bool _dataReady;

  float _angleX, _angleY, _angleZ;
  float _accX, _accY, _accZ;
  float _rollOffset, _pitchOffset;
  float _accXOffset, _accYOffset, _accZOffset;
  float _maxLeanLeft = 0;
  float _maxLeanRight = 0;

  unsigned long _lastUpdate;
  int _consecutiveErrors;
  bool _requestActive = false;
};

extern IMUManager imuManager;

#endif

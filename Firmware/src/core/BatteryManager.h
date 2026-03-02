#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Arduino.h>

class BatteryManager {
public:
  static BatteryManager &getInstance();

  void begin();
  void update();

  float getVoltage() const;
  int getPercentage() const;
  bool isCharging() const;
  bool isUsbConnected();
  void checkDuringBoot();

private:
  BatteryManager();
  // Delete copy constructor and assignment operator to enforce singleton
  BatteryManager(const BatteryManager &) = delete;
  BatteryManager &operator=(const BatteryManager &) = delete;

  float _voltage;
  int _percentage;
  bool _isCharging;

  // Trend Detection
  static const int HISTORY_SIZE =
      6; // 10 seconds interval * 6 = 1 minute history
  float _voltageHistory[HISTORY_SIZE];
  int _historyIndex;
  unsigned long _lastTrendCheck;

  // Smoothing
  static const int SAMPLE_SIZE = 10;
  float _samples[SAMPLE_SIZE];
  int _sampleIndex;
  unsigned long _lastUpdate;
  bool _firstRead;
};

#endif // BATTERY_MANAGER_H

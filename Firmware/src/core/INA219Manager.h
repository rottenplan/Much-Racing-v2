#ifndef INA219_MANAGER_H
#define INA219_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <INA219.h>

class INA219Manager {
public:
  static INA219Manager &getInstance();

  void begin();
  void update();

  bool isPresent() const;
  float getVoltage() const;
  float getCurrent() const;
  float getPower() const;
  float getMaxCurrent() const;

private:
  INA219Manager();
  INA219Manager(const INA219Manager &) = delete;
  INA219Manager &operator=(const INA219Manager &) = delete;

  TwoWire _wire;
  INA219 *_ina;
  bool _present;
  float _voltage;
  float _current;
  float _power;
  unsigned long _lastRead;
};

#endif // INA219_MANAGER_H
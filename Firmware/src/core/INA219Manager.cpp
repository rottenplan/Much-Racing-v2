#include "INA219Manager.h"
#include "../config.h"

INA219Manager &INA219Manager::getInstance() {
  static INA219Manager instance;
  return instance;
}

INA219Manager::INA219Manager()
    : _wire(1), _ina(nullptr), _present(false), _voltage(0.0f), _current(0.0f),
      _power(0.0f), _lastRead(0) {}

void INA219Manager::begin() {
#ifdef PIN_INA219_SDA
  _wire.begin(PIN_INA219_SDA, PIN_INA219_SCL);
  _wire.setClock(100000);
  _wire.setTimeOut(50);

  _ina = new INA219(0x40, &_wire);
  if (!_ina->begin()) {
    DEBUG_PRINTLN("INA219: not found at 0x40, trying 0x41...");
    delete _ina;
    _ina = new INA219(0x41, &_wire);
    if (!_ina->begin()) {
      DEBUG_PRINTLN("INA219: not connected on SDA=8/SCL=9");
      delete _ina;
      _ina = nullptr;
      return;
    }
  }

  _ina->setBusVoltageRange(16); // Default range (up to 16V, module rated 26V)
  _ina->setMaxCurrentShunt(INA219_MAX_CURRENT_A, INA219_SHUNT_OHM);

  _present = true;
  DEBUG_PRINTF("INA219: OK addr=0x%02X maxA=%.1f shunt=%.3fR\n",
               _ina->getAddress(), _ina->getMaxCurrent(), _ina->getShunt());
#endif
}

void INA219Manager::update() {
  if (!_present || _ina == nullptr)
    return;
#ifdef PIN_INA219_SDA
  if (millis() - _lastRead < 100) // 10 Hz cukup untuk volt/arus
    return;
  _lastRead = millis();

  float v = _ina->getBusVoltage();
  if (v < 0.0f) // math overflow -> pertahankan nilai terakhir
    v = _voltage;

  float vScaled = v * INA219_VOLTAGE_SCALE;
  _voltage = vScaled;
  _current = _ina->getCurrent();
  _power = vScaled * _current;
#endif
}

bool INA219Manager::isPresent() const { return _present; }

float INA219Manager::getVoltage() const { return _voltage; }

float INA219Manager::getCurrent() const { return _current; }

float INA219Manager::getPower() const { return _power; }

float INA219Manager::getMaxCurrent() const {
  return _ina ? _ina->getMaxCurrent() : 0.0f;
}
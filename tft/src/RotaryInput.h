#pragma once

#include <Arduino.h>

class RotaryInput {
public:
  RotaryInput(int pinA, int pinB, int pinButton);

  void begin();
  void loop();

  int consumeTurnSteps();
  bool consumeShortPress();
  bool consumeLongPress();

private:
  int pinA_;
  int pinB_;
  int pinButton_;

  uint8_t lastAB_ = 0;
  uint8_t detentAB_ = 0;
  int8_t quarterSteps_ = 0;
  int turnSteps_ = 0;

  bool rawButtonPressed_ = false;
  bool stableButtonPressed_ = false;
  unsigned long rawButtonChangeMs_ = 0;
  unsigned long buttonPressedMs_ = 0;
  bool longPressReported_ = false;

  bool shortPressPending_ = false;
  bool longPressPending_ = false;
};

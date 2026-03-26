#include "RotaryInput.h"

namespace {
static const int8_t kTransitionTable[16] = {
  0, -1, 1, 0,
  1, 0, 0, -1,
  -1, 0, 0, 1,
  0, 1, -1, 0
};

constexpr unsigned long kButtonDebounceMs = 20;
constexpr unsigned long kMinShortPressMs = 35;
constexpr unsigned long kLongPressMs = 700;
// EC11: 1 "klik" mechaniczny to pelny cykl 4 przejsc A/B.
constexpr int8_t kStepThreshold = 4;
}  // namespace

RotaryInput::RotaryInput(int pinA, int pinB, int pinButton)
  : pinA_(pinA), pinB_(pinB), pinButton_(pinButton) {}

void RotaryInput::begin() {
  pinMode(pinA_, INPUT_PULLUP);
  pinMode(pinB_, INPUT_PULLUP);
  pinMode(pinButton_, INPUT_PULLUP);

  const uint8_t a = digitalRead(pinA_) ? 1 : 0;
  const uint8_t b = digitalRead(pinB_) ? 1 : 0;
  lastAB_ = (uint8_t)((a << 1) | b);
  detentAB_ = lastAB_;

  rawButtonPressed_ = (digitalRead(pinButton_) == LOW);
  stableButtonPressed_ = rawButtonPressed_;
  rawButtonChangeMs_ = millis();
  buttonPressedMs_ = millis();
  longPressReported_ = false;
}

void RotaryInput::loop() {
  const uint8_t a = digitalRead(pinA_) ? 1 : 0;
  const uint8_t b = digitalRead(pinB_) ? 1 : 0;
  const uint8_t ab = (uint8_t)((a << 1) | b);

  if (ab != lastAB_) {
    const uint8_t idx = (uint8_t)(((lastAB_ << 2) | ab) & 0x0F);
    quarterSteps_ += kTransitionTable[idx];
    if (quarterSteps_ > 8) quarterSteps_ = 8;
    if (quarterSteps_ < -8) quarterSteps_ = -8;
    lastAB_ = ab;

    // Krok zatwierdzamy tylko po powrocie na pozycje "detent",
    // dzieki czemu przypadkowe dotkniecia i drgania stykow nie przeskakuja menu.
    if (ab == detentAB_) {
      if (quarterSteps_ >= kStepThreshold) {
        turnSteps_++;
      } else if (quarterSteps_ <= -kStepThreshold) {
        turnSteps_--;
      }
      quarterSteps_ = 0;
    }
  }

  const unsigned long now = millis();
  const bool rawPressedNow = (digitalRead(pinButton_) == LOW);

  if (rawPressedNow != rawButtonPressed_) {
    rawButtonPressed_ = rawPressedNow;
    rawButtonChangeMs_ = now;
  }

  if (now - rawButtonChangeMs_ >= kButtonDebounceMs &&
      stableButtonPressed_ != rawButtonPressed_) {
    stableButtonPressed_ = rawButtonPressed_;
    if (stableButtonPressed_) {
      buttonPressedMs_ = now;
      longPressReported_ = false;
    } else {
      if (!longPressReported_ && (now - buttonPressedMs_ >= kMinShortPressMs)) {
        shortPressPending_ = true;
      }
    }
  }

  if (stableButtonPressed_ && !longPressReported_ &&
      now - buttonPressedMs_ >= kLongPressMs) {
    longPressPending_ = true;
    longPressReported_ = true;
  }
}

int RotaryInput::consumeTurnSteps() {
  const int v = turnSteps_;
  turnSteps_ = 0;
  return v;
}

bool RotaryInput::consumeShortPress() {
  const bool v = shortPressPending_;
  shortPressPending_ = false;
  return v;
}

bool RotaryInput::consumeLongPress() {
  const bool v = longPressPending_;
  longPressPending_ = false;
  return v;
}

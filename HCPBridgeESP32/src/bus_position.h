#pragma once
#include <stdint.h>
namespace BusPosition {
struct Sample { uint8_t current, target; bool currentValid, targetValid; };
inline Sample decode(uint16_t raw) { Sample s{uint8_t(raw & 255),uint8_t(raw >> 8),false,false}; s.currentValid=s.current<=200;s.targetValid=s.target<=200;return s; }
inline float fraction(uint8_t raw) { return float(raw)/200.0f; }
}

#pragma once
struct TwoWire { void begin() {} void beginTransmission(int) {} int endTransmission() { return 1; } };
static TwoWire Wire;

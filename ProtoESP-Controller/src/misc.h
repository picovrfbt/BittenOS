#include <math.h>
#include <Arduino.h>

bool isApproxEqual(const float ax, const float ay, const float az, const float bx, const float by, const float bz, const float tiltTol);
//How far the head has leaned along a taught direction, as a fraction of the way to it:
//0 at neutral, 1 exactly where the position was taught, and more than 1 leaning further.
float tiltFraction(const float ax, const float ay, const float az,
                   const float px, const float py, const float pz,
                   const float nx, const float ny, const float nz);
//How closely the lean matches a taught direction, -1 to 1, 1 being straight along it. Which way
//the head is leaning; tiltFraction says how far. Needed because a fraction alone can be large for
//a short taught direction when the head leans a long way in a roughly similar one.
float tiltAlign(const float ax, const float ay, const float az,
                const float px, const float py, const float pz,
                const float nx, const float ny, const float nz);
uint64_t speakMatrix(uint64_t input, uint8_t columns, bool inverted);
uint64_t speakMatrixGrow(uint64_t input, uint8_t columns, bool inverted, uint8_t grow);
float mapfloat(float x, float in_min, float in_max, float out_min, float out_max);
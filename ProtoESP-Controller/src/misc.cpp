#include "misc.h"

bool isApproxEqual(const float ax, const float ay, const float az, const float bx, const float by, const float bz, const float tiltTol) {
  return (fabs(ax-bx) < tiltTol && fabs(ay-by) < tiltTol && fabs(az-bz) < tiltTol);
}

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

//--------------------------------//Dynamic speak anim, opened by more than one pixel
//speakMatrix() opens a column by a single pixel: one above the topmost lit pixel, one below the
//bottom-most. That is the whole of the mouth movement, so every volume looks the same. This does
//the same thing `grow` times over, so the mouth can open wide for a shout and barely part for a
//mumble. grow = 1 is exactly speakMatrix().
uint64_t speakMatrixGrow(uint64_t input, uint8_t columns, bool inverted, uint8_t grow) {
  int darray[8][8];

  for (int i = 0; i < 8; i++) {
    uint8_t row = (input >> (i * 8)) & 0xFF;
    for (int j = 0; j < 8; j++) {
      darray[i][j] = bitRead(row, j);
    }
  }

  if (columns > 8) columns = 8;
  if (grow < 1) grow = 1;
  if (grow > 4) grow = 4;

  for (uint8_t g = 0; g < grow; g++) {
    for (int c = 0; c < columns; c++) {
      int i = inverted ? (7 - c) : c;

      for (int j = 0; j < 8; j++) {          //one pixel above the top of what is lit
        if (darray[j][i] == 1 && j != 0) {
          darray[j-1][i] = 1;
          break;
        }
      }

      for (int j = 7; j > -1; j--) {         //and one below the bottom
        if (darray[j][i] == 1 && j != 7) {
          darray[j+1][i] = 1;
          break;
        }
      }
    }
  }

  uint64_t out = 0;
  for (int i = 0; i < 8; i++) {
    uint8_t row = 0;
    for (int j = 0; j < 8; j++) {
      bitWrite(row, j, darray[i][j]);
    }
    out |= (uint64_t)row << (i * 8);
  }

  return out;
}

//--------------------------------//Dynamic speak anim
uint64_t speakMatrix(uint64_t input, uint8_t columns, bool inverted) {
  int darray[8][8];

  // convert uint64_t to 2D array
  for (int i = 0; i < 8; i++) {
    uint8_t row = (input >> (i * 8)) & 0xFF;
    for (int j = 0; j < 8; j++) {
      darray[i][j] = bitRead(row, j);
    }
  }

  // clamp columns to valid range
  if (columns > 8) columns = 8;

  // process only selected columns
  for (int c = 0; c < columns; c++) {
    int i = inverted ? (7 - c) : c;

    // --- ORIGINAL LOGIC (unchanged) ---
    for (int j = 0; j < 8; j++) {
      if (darray[j][i] == 1 && j != 0) {
        darray[j-1][i] = 1;
        break;
      }
    }

    for (int j = 7; j > -1; j--) {
      if (darray[j][i] == 1 && j != 7) {
        darray[j+1][i] = 1;
        break;
      }
    }
    // ----------------------------------
  }

  // convert 2D array back to uint64_t
  uint64_t out = 0;
  for (int i = 0; i < 8; i++) {
    uint8_t row = 0;
    for (int j = 0; j < 8; j++) {
      bitWrite(row, j, darray[i][j]);
    }
    out |= (uint64_t)row << (i * 8);
  }

  return out;
}
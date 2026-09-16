#include "oled.h"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0,/* reset=*/ U8X8_PIN_NONE);

//--------------------------------//OLED Brightness
//Five levels, 0 dimmest to 4 brightest. What actually sets an SSD1306's brightness is its contrast
//register; the original code only changed the precharge and VCOMH, which barely moves it. VCOMH is
//raised alongside for the brighter steps, and the precharge is lengthened at the top so the pixels
//have time to reach full brightness.
void SSDOLED::oledBright(int level) const {
    if(level < 0) level = 0;
    if(level > 4) level = 4;
    static const uint8_t contrast[5] = {0x01, 0x28, 0x64, 0xB4, 0xFF};
    static const uint8_t vcomh[5]    = {0x00, 0x20, 0x20, 0x30, 0x40};
    static const uint8_t precharge[5] = {0xF1, 0xF1, 0xF1, 0xF1, 0x22};
    u8g2.sendF("ca", 0x0D9, precharge[level]);
    u8g2.sendF("ca", 0x0DB, vcomh[level]);
    u8g2.setContrast(contrast[level]);
}

//--------------------------------//Screen the other way up (whoever calls this redraws after)
void SSDOLED::setFlip(bool upsideDown) const {
  u8g2.setFlipMode(upsideDown ? 1 : 0);
}

//--------------------------------//Whole-screen draw, for layouts from screen.json
void SSDOLED::blit(const uint8_t *page_buffer) const {
  memcpy(u8g2.getBufferPtr(), page_buffer, 128 * 8);
  u8g2.sendBuffer();
}

//--------------------------------//OLED off (switched off from the web page); init() wakes it again
void SSDOLED::off() const {
  u8g2.clearDisplay();
  u8g2.setPowerSave(1);
}

//--------------------------------//OLED Init
bool SSDOLED::init(uint8_t oledAddr, int brightness, bool INA) {
  INAavail = INA;
  Wire.beginTransmission(oledAddr); //check for oled on address 0x3c
  byte error = Wire.endTransmission();
  if(error == 0) {
    u8g2.setI2CAddress(oledAddr*2);
    u8g2.begin();
    u8g2.setFlipMode(0);                        //the caller applies the saved Upside down setting
    oledBright(brightness);
    return true;
  } else {
    return false;
  }
}

//--------------------------------//Animation name
int SSDOLED::writeAnim(String anim) const {
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, 0, 128, 35);
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_logisoso28_tr);
  int width = u8g2.getStrWidth(anim.c_str());
  if(width>127) {
    u8g2.drawStr(0, 28, anim.c_str());
  } else {
    u8g2.drawStr((128 - width) / 2, 28, anim.c_str());
  }
  u8g2.updateDisplayArea(0, 0, 16, 5);
  return width;
}

//--------------------------------//INA Voltage & Current
void SSDOLED::writeINA(float volt, float amp) const {
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, 32, 128, 13); //0-128 ; 32-45
  u8g2.setDrawColor(1);
  String toShow = String(volt,2)+"V "+String(amp/1000,2)+"A";
  float width = u8g2.getStrWidth(toShow.c_str());
  u8g2.setFont(u8g2_font_t0_22b_tr);
  u8g2.drawStr((128 - width) / 2, 45, toShow.c_str());
  u8g2.updateDisplayArea(0, 4, 16, 2);
}

//--------------------------------//Speaking symbol
void SSDOLED::speak(bool show) const {
  if(INAavail) {
    if(show) {
      u8g2.drawXBM(45, 50, spk_width, spk_height, spkONsml_bits);
    } else {
      u8g2.drawXBM(45, 50, spk_width, spk_height, spkOFFsml_bits);
    }
    u8g2.updateDisplayArea(5, 6, 3, 2);
  } else {
    if(show) {
      u8g2.drawXBM(45, 36, spk_width*2, spk_height*2, spkONlrg_bits);
    } else {
      u8g2.drawXBM(45, 36, spk_width*2, spk_height*2, spkOFFlrg_bits);
    }
    u8g2.updateDisplayArea(5, 4, 5, 4);
  }
}

//--------------------------------//Remote symbol
void SSDOLED::remote(bool show) const {
  if(INAavail) {
    if(show) {
      u8g2.drawXBM(8, 49, rem_width, rem_height, remONsml_bits);
    } else {
      u8g2.drawXBM(8, 49, rem_width, rem_height, remOFFsml_bits);
    }
    u8g2.updateDisplayArea(1, 6, 2, 2);
  } else {
    if(show) {
      u8g2.drawXBM(2, 36, rem_width*2, rem_height*2, remONlrg_bits);
    } else {
      u8g2.drawXBM(2, 36, rem_width*2, rem_height*2, remOFFlrg_bits);
    }
    u8g2.updateDisplayArea(0, 4, 4, 4);
  }
}

//--------------------------------//Remote set number
void SSDOLED::writeSet(int setNum) const {
  if(INAavail) {
    u8g2.setDrawColor(0);
    u8g2.drawBox(20, 50, 10, 13); //20-30 ; 50-63
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_t0_22b_tf);
    u8g2.drawStr(19, 63, String(setNum).c_str());
    u8g2.updateDisplayArea(2, 6, 2, 2);
  } else {
    u8g2.setDrawColor(0);
    u8g2.drawBox(25, 40, 13, 21); //25-38 ; 40-61
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_helvB18_tn);
    u8g2.drawStr(26, 59, String(setNum).c_str());
    u8g2.updateDisplayArea(3, 5, 2, 3);
  }
}

//--------------------------------//RGB acronym status
void SSDOLED::writeRGB(String name) const {
  u8g2.setFont(u8g2_font_t0_22b_tr);
  if(INAavail) {
    u8g2.setDrawColor(0);
    u8g2.drawBox(69, 49, 53, 15); //69-122 ; 49-64
    u8g2.setDrawColor(1);
    u8g2.drawStr(70, 63, name.c_str());
    u8g2.updateDisplayArea(8, 6, 8, 2);
  } else {
    u8g2.setDrawColor(0);
    u8g2.drawBox(75, 41, 53, 20); //75-128 ; 41-61
    u8g2.setDrawColor(1);
    u8g2.drawStr(76, 55, name.c_str());
    u8g2.updateDisplayArea(9, 5, 7, 3);
  }
}
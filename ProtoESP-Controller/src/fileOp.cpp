#include "fileOp.h"

//--------------------------------//CRC checksum class
CrcWriter::CrcWriter() {
  _hash = _hasher.crc32(NULL, 0);
}

size_t CrcWriter::write(uint8_t c) {
  _hash = _hasher.crc32_upd(&c, 1);
  return 1;
}

size_t CrcWriter::write(const uint8_t *buffer, size_t length) {
  _hash = _hasher.crc32_upd(buffer, length);
  return length;
}

uint32_t CrcWriter::hash() const {
  return _hash;
}

//--------------------------------//Config variables store/save/load/default
void Config::setDefault() {
  boopEna = true;
  speechEna = true;
  tiltEna = false;
  bleEna = false;
  oledEna = false;
  bEar = 60;
  bVisor = 5;
  bOled = 2;
  rbSpeed = 15;
  rbWidth = 8;
  spMin = 40;      //the room's noise floor, peak to peak - measured, not guessed
  spMax = 500;
  spTrig = 10;
  aTilt = "confused.json";
  aUp = "sad.json";
  aBoop = "boop.json",
  neutralX = 0.3;
  neutralY = 0.92;
  neutralZ = 0.33;
  tiltX = 0.05;
  tiltY = 0.7;
  tiltZ = 0.73;
  upX = -0.17;
  upY = 0.99;
  upZ = 0.11;
  tiltTol = 0.1;
  visColorStr = "#ff0000";
  visColor = strtol(visColorStr.c_str()+1, NULL, 16);
  wifiName = "ProtoWiFi";
  wifiPass = "Proto1234";
  fanDuty = 255;
  boopThresh = 15;
  sleepEna = false;
  sleepMins = 5;
  aSleep = "sleep.json";
  transEna = true;
  bobEna = false;
  boopCountEna = true;
  boopCount = 0;
  boopBase = 0;    //resting proximity, i.e. the visor's own reflection
  boopLightEna = true; //a boop also has to shade the light sensor, when there's light to shade
  boopShade = 30;  //percent darker than normal that counts as a shadow
  mouthMode = "shapes";  //how the mouth moves with your voice: flap (the old way) or shapes
  blinkMode = "face";    //blinks: face (the face's own), blink (the mask blinks it), still (neither)
  //The button keeps doing what it always did until you change it
  btnShort = "nextFace";
  btnDouble = "none";
  btnHold = "displays";
  btnShortFace = "default.json";
  btnDoubleFace = "default.json";
  btnHoldFace = "default.json";
  btnHoldMs = 1500;
  btnShortPhrase = 0;
  btnDoublePhrase = 0;
  btnHoldPhrase = 0;
  for(int i = 0; i < 6; i++) {
    phrase[i] = "";
    phraseSpeed[i] = 55;   //the page's "Normal" speed
    phraseOnce[i] = false;
  }
  sleepDeepEna = false;  //off by default: the panels going dark surprises people
  sleepDeepMins = 20;    //asleep this long and the panels go off entirely
  wakeFlickEna = true;   //coming back from deep sleep, the panels flicker on one at a time
  wakeFlickBoot = true;  //and again when the mask is switched on, like an old device starting up
  wakeFlickStyle = "crt";
  wakeFlickMs = 1800;    //how long the whole warm-up takes
  wakeFlickGlitch = 1;   //0 none, 1 a little tearing, 2 a lot
  boopMilestone = 100;   //every this many boops plays a special face; 0 turns it off
  aMilestone = "heart.json";
  oledFlip = false;      //the screen mounted upside down
  autoBriEna = false;
  autoBriMode = "blank"; //blank, live or hybrid
  autoBriSecs = 60;      //how often it looks at the light
  autoBriMin = 1;        //brightness clamps, 0-15
  autoBriMax = 8;
  autoBriDark = 5;       //light-sensor readings that map to the clamps
  autoBriBright = 400;
  transMs = 80;    //one direction of the dip, split across the brightness steps
  sleepTol = 0.2;  //g of movement before the mask counts as awake
  save();
}

bool Config::save() {
  JsonDocument doc;
  doc["boopEna"] = boopEna;
  doc["speechEna"] = speechEna;
  doc["tiltEna"] = tiltEna;
  doc["bleEna"] = bleEna;
  doc["oledEna"] = oledEna;
  doc["bEar"] = bEar;
  doc["bVisor"] = bVisor;
  doc["bOled"] = bOled;
  doc["rbSpeed"] = rbSpeed;
  doc["rbWidth"] = rbWidth;
  doc["spMin"] = spMin;
  doc["spMax"] = spMax;
  doc["spTrig"] = spTrig;
  doc["aTilt"] = aTilt;
  doc["aUp"] = aUp;
  doc["aBoop"] = aBoop;
  doc["neutralX"] = neutralX;
  doc["neutralY"] = neutralY;
  doc["neutralZ"] = neutralZ;
  doc["tiltX"] = tiltX;
  doc["tiltY"] = tiltY;
  doc["tiltZ"] = tiltZ;
  doc["upX"] = upX;
  doc["upY"] = upY;
  doc["upZ"] = upZ;
  doc["tiltTol"] = tiltTol;
  doc["visColor"] = visColorStr;
  doc["wifiName"] = wifiName;
  doc["wifiPass"] = wifiPass;
  doc["fanDuty"] = fanDuty;
  doc["boopThresh"] = boopThresh;
  doc["sleepEna"] = sleepEna;
  doc["sleepMins"] = sleepMins;
  doc["aSleep"] = aSleep;
  doc["transEna"] = transEna;
  doc["bobEna"] = bobEna;
  doc["boopCountEna"] = boopCountEna;
  doc["boopCount"] = boopCount;
  doc["boopBase"] = boopBase;
  doc["boopLightEna"] = boopLightEna;
  doc["boopShade"] = boopShade;
  doc["mouthMode"] = mouthMode;
  doc["blinkMode"] = blinkMode;
  doc["btnShort"] = btnShort;
  doc["btnDouble"] = btnDouble;
  doc["btnHold"] = btnHold;
  doc["btnShortFace"] = btnShortFace;
  doc["btnDoubleFace"] = btnDoubleFace;
  doc["btnHoldFace"] = btnHoldFace;
  doc["btnHoldMs"] = btnHoldMs;
  doc["btnShortPhrase"] = btnShortPhrase;
  doc["btnDoublePhrase"] = btnDoublePhrase;
  doc["btnHoldPhrase"] = btnHoldPhrase;
  for(int i = 0; i < 6; i++) {
    doc["phrase"][i] = phrase[i];
    doc["phraseSpeed"][i] = phraseSpeed[i];
    doc["phraseOnce"][i] = phraseOnce[i];
  }
  doc["sleepDeepEna"] = sleepDeepEna;
  doc["sleepDeepMins"] = sleepDeepMins;
  doc["wakeFlickEna"] = wakeFlickEna;
  doc["wakeFlickBoot"] = wakeFlickBoot;
  doc["wakeFlickStyle"] = wakeFlickStyle;
  doc["wakeFlickMs"] = wakeFlickMs;
  doc["wakeFlickGlitch"] = wakeFlickGlitch;
  doc["boopMilestone"] = boopMilestone;
  doc["aMilestone"] = aMilestone;
  doc["oledFlip"] = oledFlip;
  doc["autoBriEna"] = autoBriEna;
  doc["autoBriMode"] = autoBriMode;
  doc["autoBriSecs"] = autoBriSecs;
  doc["autoBriMin"] = autoBriMin;
  doc["autoBriMax"] = autoBriMax;
  doc["autoBriDark"] = autoBriDark;
  doc["autoBriBright"] = autoBriBright;
  doc["transMs"] = transMs;
  doc["sleepTol"] = sleepTol;
  
  CrcWriter CRCchk;
  serializeJson(doc, CRCchk);
  File fileCRC = LittleFS.open("/configCRC.txt", "w");
  if(!fileCRC) {
    Serial.println(F("[E] There was an error opening the configs CRC file!"));
    fileCRC.close();
  } else {
    fileCRC.print(String(CRCchk.hash()));
    fileCRC.close();
    Serial.println("[I] CRC saved as: "+ String(CRCchk.hash()));
  }

  File file = LittleFS.open("/config.json", "w");
  if (!file) {
    Serial.println(F("[E] There was an error opening the config file!"));
    file.close();
    return false;
  }
  WriteBufferingStream bufferedFile2(file, 64);
  if(serializeJson(doc, bufferedFile2) == 0) {
    Serial.println(F("[E] Failed to deserialize the config file!"));
    return false;
  }
  bufferedFile2.flush();
  file.close();
  Serial.println(F("[I] Config file saved!"));

  return true;
}

bool Config::load() {
  JsonDocument doc;

  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    Serial.println(F("[E] There was an error opening the config file, using default config"));
    //using default
    return false;
  }
  Serial.println(F("[I] Config file opened"));

  if(deserializeJson(doc, file)){
    Serial.println(F("[E] Failed to deserialize the config file, using default config"));
    //using default
    return false;
  }
  file.close();
  Serial.println(F("[I] Deserialized JSON"));
  
  CrcWriter CRCchk;
  serializeJson(doc, CRCchk);
  File fileCRC = LittleFS.open("/configCRC.txt", "r");
  if(!fileCRC) {
    Serial.println(F("[E] There was an error opening the configs CRC file, using default config"));
    fileCRC.close();
    return false;
  } else {
    String jsonCRC;
    while(fileCRC.available()) {
      jsonCRC+=char(fileCRC.read());
    }
    fileCRC.close();
    if(jsonCRC == String(CRCchk.hash())) {
      Serial.println("[I] CRC check OK; is: " + String(CRCchk.hash()));
    } else {
      Serial.println("[E] CRC check FAILED; is: " + String(CRCchk.hash()) + " and should be: " + jsonCRC + "; using default config");
      return false;
    }
  }
  
  boopEna = doc["boopEna"].as<bool>();
  speechEna = doc["speechEna"].as<bool>();
  tiltEna = doc["tiltEna"].as<bool>();
  bleEna = doc["bleEna"].as<bool>();
  oledEna = doc["oledEna"].as<bool>();
  bEar = doc["bEar"].as<int>();
  bVisor = doc["bVisor"].as<int>();
  bOled = doc["bOled"].as<int>();
  rbSpeed = doc["rbSpeed"].as<int>();
  rbWidth = doc["rbWidth"].as<int>();
  spMin = doc["spMin"].as<int>();
  spMax = doc["spMax"].as<int>();
  spTrig = doc["spTrig"].as<int>();
  aTilt = doc["aTilt"].as<String>();
  aUp = doc["aUp"].as<String>();
  aBoop = doc["aBoop"].as<String>();
  neutralX = doc["neutralX"].as<float>();
  neutralY = doc["neutralY"].as<float>();
  neutralZ = doc["neutralZ"].as<float>();
  tiltX = doc["tiltX"].as<float>();
  tiltY = doc["tiltY"].as<float>();
  tiltZ = doc["tiltZ"].as<float>();
  upX = doc["upX"].as<float>();
  upY = doc["upY"].as<float>();
  upZ = doc["upZ"].as<float>();
  tiltTol = doc["tiltTol"].as<float>();
  visColorStr = doc["visColor"].as<String>();
  visColor = strtol(visColorStr.c_str()+1, NULL, 16);
  wifiName = doc["wifiName"].as<String>();
  wifiPass = doc["wifiPass"].as<String>();
  fanDuty = doc["fanDuty"].as<int>();
  boopThresh = doc["boopThresh"].as<int>();
  sleepEna = doc["sleepEna"] | false;
  sleepMins = doc["sleepMins"] | 5;
  aSleep = doc["aSleep"] | "sleep.json";
  transEna = doc["transEna"] | true;
  bobEna = doc["bobEna"] | false;
  boopCountEna = doc["boopCountEna"] | true;
  boopCount = doc["boopCount"] | 0;
  boopBase = doc["boopBase"] | 0;
  boopLightEna = doc["boopLightEna"] | true;
  boopShade = doc["boopShade"] | 30;
  mouthMode = doc["mouthMode"] | "shapes";
  blinkMode = doc["blinkMode"] | "face";
  btnShort = doc["btnShort"] | "nextFace";
  btnDouble = doc["btnDouble"] | "none";
  btnHold = doc["btnHold"] | "displays";
  btnShortFace = doc["btnShortFace"] | "default.json";
  btnDoubleFace = doc["btnDoubleFace"] | "default.json";
  btnHoldFace = doc["btnHoldFace"] | "default.json";
  btnHoldMs = doc["btnHoldMs"] | 1500;
  btnShortPhrase = doc["btnShortPhrase"] | 0;
  btnDoublePhrase = doc["btnDoublePhrase"] | 0;
  btnHoldPhrase = doc["btnHoldPhrase"] | 0;
  for(int i = 0; i < 6; i++) {
    phrase[i] = doc["phrase"][i] | "";
    phraseSpeed[i] = doc["phraseSpeed"][i] | 55;
    phraseOnce[i] = doc["phraseOnce"][i] | false;
  }
  sleepDeepEna = doc["sleepDeepEna"] | false;
  sleepDeepMins = doc["sleepDeepMins"] | 20;
  wakeFlickEna = doc["wakeFlickEna"] | true;
  wakeFlickBoot = doc["wakeFlickBoot"] | true;
  wakeFlickStyle = doc["wakeFlickStyle"] | "crt";
  wakeFlickMs = doc["wakeFlickMs"] | 1800;
  wakeFlickGlitch = doc["wakeFlickGlitch"] | 1;
  boopMilestone = doc["boopMilestone"] | 100;
  aMilestone = doc["aMilestone"] | "heart.json";
  oledFlip = doc["oledFlip"] | false;
  autoBriEna = doc["autoBriEna"] | false;
  autoBriMode = doc["autoBriMode"] | "blank";
  autoBriSecs = doc["autoBriSecs"] | 60;
  autoBriMin = doc["autoBriMin"] | 1;
  autoBriMax = doc["autoBriMax"] | 8;
  autoBriDark = doc["autoBriDark"] | 5;
  autoBriBright = doc["autoBriBright"] | 400;
  transMs = doc["transMs"] | 80;
  sleepTol = doc["sleepTol"] | 0.2f;

  return true;
}

bool Config::getBool(AsyncWebServerRequest *req, const char *name, bool &out) {
    if (!req->hasParam(name)) return false;
    String v = req->getParam(name)->value();
    out = (v == "true" || v == "1" || v == "on");
    return true;
}

bool Config::getInt(AsyncWebServerRequest *req, const char *name, int &out) {
    if (!req->hasParam(name)) return false;
    out = req->getParam(name)->value().toInt();
    return true;
}

bool Config::getFloat(AsyncWebServerRequest *req, const char *name, float &out) {
    if (!req->hasParam(name)) return false;
    out = req->getParam(name)->value().toFloat();
    return true;
}

bool Config::getString(AsyncWebServerRequest *req, const char *name, String &out) {
    if (!req->hasParam(name)) return false;
    out = req->getParam(name)->value();
    return true;
}
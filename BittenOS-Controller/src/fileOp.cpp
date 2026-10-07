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
  aTilt2 = "surprise.json";   //tilting the other way
  aBoop = "boop.json",
  neutralX = 0.3;
  neutralY = 0.92;
  neutralZ = 0.33;
  tiltX = 0.05;
  tilt2X = 0.05;   //taught by hand: the mirror of a tilt is not the same reading
  tilt2Y = 0.7;
  tilt2Z = -0.73;
  tiltY = 0.7;
  tiltZ = 0.73;
  tiltTrig = 0.6;  //lean 60% of the way to a taught position and it counts
  visColorStr = "#ff0000";
  visColor = strtol(visColorStr.c_str()+1, NULL, 16);
  wifiName = "ProtoWiFi";
  wifiPass = "Proto1234";
  boopThresh = 15;
  boopConfirm = 5;   //readings in a row before a boop counts
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
  blinkDoublePct = 25;   //a quarter of the mask's blinks come as a quick double
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
  staEna = false;        //off until you give it a network to join; the access point always runs
  staName = "";
  staPass = "";
  mdnsName = "protogen"; //reachable as protogen.local once it has joined
  wakeFlickEna = true;   //coming back from deep sleep, the panels flicker on one at a time
  wakeFlickBoot = true;  //and again when the mask is switched on, like an old device starting up
  bootBarEna = true;     //a loading bar fills first, then the warm-up reveals the face
  bootBarMs = 1500;      //how long the bar takes, on its own clock
  dinoBest = 0;          //nobody has run yet
  eggPresses = 6;        //button presses in quick succession to start an easter egg
  eggAction = "slots";   //slots, dino or none
  slotWins = 0; slotSpins = 0;
  rmtMac = "";           //nothing paired until you pair one
  //Defaults worth having out of the box, and a showcase for the newer actions
  rmtAct[0] = "nextFace";  rmtFace[0] = "";             rmtPhrase[0] = 0;
  rmtHAct[0] = "displays"; rmtHFace[0] = "";            rmtHPhrase[0] = 0;
  rmtAct[1] = "prevFace";  rmtFace[1] = "";             rmtPhrase[1] = 0;
  rmtHAct[1] = "sleep";    rmtHFace[1] = "";            rmtHPhrase[1] = 0;
  rmtAct[2] = "slots";     rmtFace[2] = "";             rmtPhrase[2] = 0;
  rmtHAct[2] = "dino";     rmtHFace[2] = "";            rmtHPhrase[2] = 0;
  //Double tap off to begin with. It is not only a spare action: a button with one set makes the
  //remote wait out the double-tap window before a single press counts, so leaving it off keeps
  //every button as sharp as it was until it is asked for.
  for(int i = 0; i < 3; i++) { rmtDAct[i] = "none"; rmtDFace[i] = ""; rmtDPhrase[i] = 0; }
  wakeFlickStyle = "crt";
  wakeFlickMs = 1800;    //how long the whole warm-up takes
  wakeFlickGlitch = 1;   //0 none, 1 a little tearing, 2 a lot
  boopMilestone = 100;   //every this many boops plays a special face; 0 turns it off
  //Cheek strip. Twenty fitted, ten a side; the second side runs back the other way in one chain.
  stripEna = true;
  stripRevA = false;
  stripRevB = true;
  stripBlushEna = true;  //a boop brings the blush up
  stripIdleEna = false;  //a soft blush the rest of the time, off by default
  stripRaveEna = true;   //the DJ meter while rave mode runs
  bStrip = 77;           //0-255, about 30%. Not a cap - set it wherever you like.
  stripNum = 20;
  stripSplit = 10;       //how many of those are on the first side
  stripBlushNum = 6;     //LEDs a side in the blush, counting back from the ear end
  stripBlushMs = 2500;
  stripBlushStr = "#ff2d96";
  stripBlushCol = strtol(stripBlushStr.c_str() + 1, NULL, 16);
  stripBlushMap = "";    //nothing picked yet: the per-side guess stands until the diagram is used
  sassBoops = 5;         //this many boops inside sassSecs pulls the sass face; 0 turns it off
  sassSecs = 10;
  aSass = "upset.json";
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
  doc["aTilt2"] = aTilt2;
  doc["aBoop"] = aBoop;
  doc["neutralX"] = neutralX;
  doc["neutralY"] = neutralY;
  doc["neutralZ"] = neutralZ;
  doc["tiltX"] = tiltX;
  doc["tilt2X"] = tilt2X;
  doc["tilt2Y"] = tilt2Y;
  doc["tilt2Z"] = tilt2Z;
  doc["tiltY"] = tiltY;
  doc["tiltZ"] = tiltZ;
  doc["tiltTrig"] = tiltTrig;
  doc["visColor"] = visColorStr;
  doc["wifiName"] = wifiName;
  doc["wifiPass"] = wifiPass;
  doc["boopThresh"] = boopThresh;
  doc["boopConfirm"] = boopConfirm;
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
  doc["blinkDoublePct"] = blinkDoublePct;
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
  doc["staEna"] = staEna;
  doc["staName"] = staName;
  doc["staPass"] = staPass;
  doc["mdnsName"] = mdnsName;
  doc["wakeFlickEna"] = wakeFlickEna;
  doc["wakeFlickBoot"] = wakeFlickBoot;
  doc["bootBarEna"] = bootBarEna;
  doc["bootBarMs"] = bootBarMs;
  doc["dinoBest"] = dinoBest;
  doc["eggPresses"] = eggPresses;
  doc["eggAction"] = eggAction;
  doc["slotWins"] = slotWins;
  doc["slotSpins"] = slotSpins;
  doc["rmtMac"] = rmtMac;
  for(int i = 0; i < 3; i++) {
    String k = "rmt" + String(i);
    doc[k] = rmtAct[i];
    doc[k + "Face"] = rmtFace[i];
    doc[k + "Phrase"] = rmtPhrase[i];
    doc[k + "H"] = rmtHAct[i];
    doc[k + "HFace"] = rmtHFace[i];
    doc[k + "HPhrase"] = rmtHPhrase[i];
    doc[k + "D"] = rmtDAct[i];
    doc[k + "DFace"] = rmtDFace[i];
    doc[k + "DPhrase"] = rmtDPhrase[i];
  }
  doc["wakeFlickStyle"] = wakeFlickStyle;
  doc["wakeFlickMs"] = wakeFlickMs;
  doc["wakeFlickGlitch"] = wakeFlickGlitch;
  doc["boopMilestone"] = boopMilestone;
  doc["stripEna"] = stripEna;
  doc["stripRevA"] = stripRevA;
  doc["stripRevB"] = stripRevB;
  doc["stripBlushEna"] = stripBlushEna;
  doc["stripIdleEna"] = stripIdleEna;
  doc["stripRaveEna"] = stripRaveEna;
  doc["bStrip"] = bStrip;
  doc["stripNum"] = stripNum;
  doc["stripSplit"] = stripSplit;
  doc["stripBlushNum"] = stripBlushNum;
  doc["stripBlushMs"] = stripBlushMs;
  doc["stripBlush"] = stripBlushStr;
  doc["stripBlushMap"] = stripBlushMap;
  doc["sassBoops"] = sassBoops;
  doc["sassSecs"] = sassSecs;
  doc["aSass"] = aSass;
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
  aTilt2 = doc["aTilt2"] | "surprise.json";
  aBoop = doc["aBoop"].as<String>();
  neutralX = doc["neutralX"].as<float>();
  neutralY = doc["neutralY"].as<float>();
  neutralZ = doc["neutralZ"].as<float>();
  tiltX = doc["tiltX"].as<float>();
  tilt2X = doc["tilt2X"] | 0.05f;
  tilt2Y = doc["tilt2Y"] | 0.7f;
  tilt2Z = doc["tilt2Z"] | -0.73f;
  tiltY = doc["tiltY"].as<float>();
  tiltZ = doc["tiltZ"].as<float>();
  tiltTrig = doc["tiltTrig"] | 0.6f;
  visColorStr = doc["visColor"].as<String>();
  visColor = strtol(visColorStr.c_str()+1, NULL, 16);
  wifiName = doc["wifiName"].as<String>();
  wifiPass = doc["wifiPass"].as<String>();
  boopThresh = doc["boopThresh"].as<int>();
  //This has to be read here, not just set in setDefault(): setDefault() only runs when load()
  //fails, so any field without a line in load() comes up zero-initialised. Zero is the worst
  //possible value for this one - min(n, 0) is always 0 and 0 >= 0 is always true, so the count of
  //readings never builds and the proximity check is bypassed, booping on any shadow at all.
  boopConfirm = doc["boopConfirm"] | 5;
  if(boopConfirm < 2 || boopConfirm > 10) boopConfirm = 5;
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
  blinkDoublePct = doc["blinkDoublePct"] | 25;
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
  staEna = doc["staEna"] | false;
  staName = doc["staName"] | "";
  staPass = doc["staPass"] | "";
  mdnsName = doc["mdnsName"] | "protogen";
  wakeFlickEna = doc["wakeFlickEna"] | true;
  wakeFlickBoot = doc["wakeFlickBoot"] | true;
  bootBarEna = doc["bootBarEna"] | true;
  bootBarMs = doc["bootBarMs"] | 1500;
  dinoBest = doc["dinoBest"] | 0;
  eggPresses = doc["eggPresses"] | 6;
  eggAction = doc["eggAction"] | "slots";
  slotWins = doc["slotWins"] | 0;
  slotSpins = doc["slotSpins"] | 0;
  rmtMac = doc["rmtMac"] | "";
  for(int i = 0; i < 3; i++) {
    String k = "rmt" + String(i);
    rmtAct[i] = doc[k] | (i == 0 ? "nextFace" : i == 1 ? "prevFace" : "slots");
    rmtFace[i] = doc[k + "Face"] | "";
    rmtPhrase[i] = doc[k + "Phrase"] | 0;
    rmtHAct[i] = doc[k + "H"] | (i == 0 ? "displays" : i == 1 ? "sleep" : "dino");
    rmtHFace[i] = doc[k + "HFace"] | "";
    rmtHPhrase[i] = doc[k + "HPhrase"] | 0;
    rmtDAct[i] = doc[k + "D"] | "none";
    rmtDFace[i] = doc[k + "DFace"] | "";
    rmtDPhrase[i] = doc[k + "DPhrase"] | 0;
  }
  wakeFlickStyle = doc["wakeFlickStyle"] | "crt";
  wakeFlickMs = doc["wakeFlickMs"] | 1800;
  wakeFlickGlitch = doc["wakeFlickGlitch"] | 1;
  boopMilestone = doc["boopMilestone"] | 100;
  stripEna = doc["stripEna"] | true;
  stripRevA = doc["stripRevA"] | false;
  stripRevB = doc["stripRevB"] | true;
  stripBlushEna = doc["stripBlushEna"] | true;
  stripIdleEna = doc["stripIdleEna"] | false;
  stripRaveEna = doc["stripRaveEna"] | true;
  bStrip = doc["bStrip"] | 77;
  stripNum = doc["stripNum"] | 20;
  stripSplit = doc["stripSplit"] | 10;
  stripBlushNum = doc["stripBlushNum"] | 6;
  stripBlushMs = doc["stripBlushMs"] | 2500;
  stripBlushMap = doc["stripBlushMap"] | "";
  stripBlushStr = doc["stripBlush"] | "#ff2d96";
  stripBlushCol = strtol(stripBlushStr.c_str() + 1, NULL, 16);
  sassBoops = doc["sassBoops"] | 5;
  sassSecs = doc["sassSecs"] | 10;
  aSass = doc["aSass"] | "upset.json";
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
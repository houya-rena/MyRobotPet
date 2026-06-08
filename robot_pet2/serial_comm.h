#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include "eye_animation.h"

// ── シリアルコマンド定義 ───────────────────────────
#define CMD_WEATHER_RAIN   "W_RAIN"
#define CMD_WEATHER_SUN    "W_SUN"
#define CMD_WEATHER_CLOUD  "W_CLOUD"
#define CMD_TEMP_HOT       "T_HOT"
#define CMD_TEMP_NORMAL    "T_NORMAL"
#define CMD_HUM_HIGH       "H_HIGH"
#define CMD_EMOTION_HAPPY  "E_HAPPY"
#define CMD_EMOTION_SAD    "E_SAD"
#define CMD_EMOTION_NORMAL "E_NORMAL"

// setup()はrobot_pet.inoのSerial.begin()完了後に呼ぶ
void serialCommInit();
// 毎呼び出しでバッファを読み進める。変化があればEmotionType、なければEMOTION_COUNTを返す
EmotionType serialCommUpdate();

#endif // SERIAL_COMM_H

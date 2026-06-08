#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include "eye_animation.h"

// ── シリアルコマンド定義 ───────────────────────────
// PCや温湿度センサーモジュールから受け取るコマンド文字列
#define CMD_WEATHER_RAIN   "W_RAIN"    // 雨
#define CMD_WEATHER_SUN    "W_SUN"     // 晴れ
#define CMD_WEATHER_CLOUD  "W_CLOUD"   // 曇り
#define CMD_TEMP_HOT       "T_HOT"     // 高温 (例: 30℃以上)
#define CMD_TEMP_NORMAL    "T_NORMAL"  // 通常温度
#define CMD_HUM_HIGH       "H_HIGH"    // 高湿度
#define CMD_EMOTION_HAPPY  "E_HAPPY"   // 手動: 嬉しい
#define CMD_EMOTION_SAD    "E_SAD"     // 手動: 悲しい
#define CMD_EMOTION_NORMAL "E_NORMAL"  // 手動: 通常

// ── 関数プロトタイプ ───────────────────────────────
void serialCommInit(unsigned long baudRate);
EmotionType serialCommUpdate();  // 毎ループ呼ぶ。新しい感情があれば返す

#endif // SERIAL_COMM_H

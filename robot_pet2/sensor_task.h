#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include "eye_animation.h"

// ── センサーピン設定 ───────────────────────────────
#define DHT_PIN  7          // DHTセンサー接続ピン
#define DHT_TYPE DHT22      // DHT11 の場合は DHT11 に変更

// ── 閾値設定 ───────────────────────────────────────
#define TEMP_HOT_THRESHOLD   30.0f   // ℃ 以上で EMOTION_HOT
#define TEMP_COLD_THRESHOLD  10.0f   // ℃ 以下で EMOTION_SAD
#define HUM_HIGH_THRESHOLD   75.0f   // % 以上で EMOTION_SAD

// ── 公開関数 ───────────────────────────────────────
void        sensorInit();
EmotionType sensorUpdate();   // 最新センサー値から感情を判定して返す

// デバッグ用：最後の計測値を取得
float sensorGetTemp();
float sensorGetHum();

#endif // SENSOR_TASK_H

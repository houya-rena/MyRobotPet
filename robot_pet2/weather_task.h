#ifndef WEATHER_TASK_H
#define WEATHER_TASK_H

#include "eye_animation.h"

// ── 取得間隔 ───────────────────────────────────────
#define WEATHER_INTERVAL_MS  600000UL   // 10分ごと（OWM無料枠: 60回/分）
#define WEATHER_RETRY_MS      30000UL   // 失敗時リトライ間隔

// ── OpenWeatherMap 天気コード → 感情マッピング ───────
// weather.id 参照: https://openweathermap.org/weather-conditions
//   2xx: 雷雨    → ANGRY
//   3xx: 霧雨    → SAD
//   5xx: 雨      → SAD
//   6xx: 雪      → SAD
//   7xx: 霧など  → NORMAL
//   800: 快晴    → HAPPY
//   80x: 曇り    → NORMAL
//
// 気温による上書き:
//   temp >= 33℃  → HOT（天気より優先）
//   temp <= 5℃   → SAD（天気より優先）

// ── 公開関数 ───────────────────────────────────────
void        weatherTaskInit();
EmotionType weatherTaskUpdate();   // TaskWeather から呼ぶ

// デバッグ用：最後の取得値
int         weatherGetId();
float       weatherGetTemp();
const char* weatherGetDesc();

#endif // WEATHER_TASK_H

#ifndef WEATHER_TASK_H
#define WEATHER_TASK_H

#include <Arduino.h>
#include "eye_animation.h"

#define WEATHER_INTERVAL_MS  600000UL   // 10分ごと
#define WEATHER_RETRY_MS      30000UL   // 失敗時リトライ間隔

void        weatherTaskInit();
EmotionType weatherTaskUpdate();

float       weatherGetTemp();
const char* weatherGetDesc();

#endif // WEATHER_TASK_H

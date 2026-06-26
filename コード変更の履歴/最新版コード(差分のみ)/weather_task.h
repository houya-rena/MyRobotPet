#ifndef WEATHER_TASK_H
#define WEATHER_TASK_H
 
#include <Arduino.h>
#include "eye_animation.h"
 
#define WEATHER_INTERVAL_MS  600000UL   // 10分ごと
#define WEATHER_RETRY_MS      30000UL   // 失敗時リトライ間隔

// ★ここに追加：この変数は他のファイル（robot_pet.ino）に実体があることを伝える
extern volatile int g_weatherId;
extern volatile float g_weatherTemp;

void        weatherTaskInit();
EmotionType weatherTaskUpdate();
 
int         weatherGetId();
float       weatherGetTemp();
const char* weatherGetDesc();
 
#endif // WEATHER_TASK_H
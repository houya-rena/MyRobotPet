#ifndef WEATHER_DISPLAY_H
#define WEATHER_DISPLAY_H
 
#include <Adafruit_SSD1306.h>
#include "eye_animation.h"   // EmotionType
 
// 天気表示画面を1フレーム描画する
// 天気ID・気温・それに対応する感情を受け取って描画
void weatherDraw(Adafruit_SSD1306 &display,
                 int weatherId,
                 float tempC,
                 const char *desc,
                 EmotionType emotion);
 
#endif // WEATHER_DISPLAY_H
 
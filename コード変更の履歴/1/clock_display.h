#ifndef CLOCK_DISPLAY_H
#define CLOCK_DISPLAY_H

#include <Adafruit_SSD1306.h>
#include <RTC.h>   // UNO R4 内蔵RTC（ボードパッケージ標準内蔵）

// 時刻表示画面を1フレーム描画する
// display: OLEDオブジェクト
void clockDraw(Adafruit_SSD1306 &display);

#endif // CLOCK_DISPLAY_H

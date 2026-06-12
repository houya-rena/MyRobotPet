// 時刻表示機能のヘッダーファイル（設計図）
// 

#ifndef CLOCK_DISPLAY_H
#define CLOCK_DISPLAY_H

#include <Adafruit_SSD1306.h>
#include <RTC.h>   // UNO R4 内蔵RTC（ボードパッケージ標準内蔵）

// 関数のプロトタイプ宣言
// 時刻表示画面を1フレーム描画する
// display: OLEDオブジェクト
void clockDraw(Adafruit_SSD1306 &display);

#endif // CLOCK_DISPLAY_H

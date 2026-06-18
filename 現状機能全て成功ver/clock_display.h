// 「ヘッダーファイル（.h）」
// 「時計表示機能」には何が入っていてどう使えばいいかの説明書

#ifndef CLOCK_DISPLAY_H
#define CLOCK_DISPLAY_H

// 依存関係の明示
#include <Adafruit_SSD1306.h>
#include <RTC.h>                // UNO R4 内蔵RTC（ボードパッケージ標準内蔵）

// 時刻表示画面を1フレーム描画する
// display: OLEDオブジェクト
// プロトタイプ宣言
void clockDraw(Adafruit_SSD1306 &display);

#endif // CLOCK_DISPLAY_H

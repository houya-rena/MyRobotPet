#ifndef EYE_DRAW_H  // ① まだ定義されていなければ…
#define EYE_DRAW_H  // ② この名前を定義する

#include "eye_animation.h"

// ── 関数宣言 ──
void drawOneEye(Adafruit_SSD1306 &dsp, int cx, int cy, int r,
                int8_t gx, int8_t gy, float lidClose, float lidCut,
                bool isLeft, EmotionType emotion);

void drawNose(Adafruit_SSD1306 &dsp);
void drawMouth(Adafruit_SSD1306 &dsp, EmotionType emotion, int frame);
void drawCheek(Adafruit_SSD1306 &dsp, int cx, int cy, int r);

void drawFoodItem(Adafruit_SSD1306 &dsp, int frame);
void drawSnackItem(Adafruit_SSD1306 &dsp, int frame);
void drawSleepZzz(Adafruit_SSD1306 &dsp, int frame);
void drawWaterLine(Adafruit_SSD1306 &dsp, int frame);
void drawHeadTowel(Adafruit_SSD1306 &dsp);

#endif // EYE_DRAW_H // ③ ここまでがガード範囲
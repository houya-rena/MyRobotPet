/**
 * weather_display.cpp
 * ボタン2を押した時に表示する天気画面
 *
 * 【レイアウト】
 *   ┌──────────────────────────────┐
 *   │  [天気アイコン]   [顔文字]   │  ← 上段
 *   │  説明文（英語）              │  ← 中段
 *   │  気温: XX.X C                │  ← 下段
 *   └──────────────────────────────┘
 *
 * 天気アイコンはASCII＋記号で表現（フォント依存しない）
 *   晴れ  :  *  （アスタリスク）
 *   曇り  :  ~~ （波線）
 *   雨    :  // （スラッシュ）
 *   雷雨  :  !/ （雷＋雨）
 *   雪    :  ** （雪）
 *   霧など:  .. （ドット）
 *
 * 顔文字はEmotionTypeに対応
 *   HAPPY  : (^_^)
 *   SAD    : (T_T)
 *   HOT    : (~_~)
 *   ANGRY  : (>_<)
 *   NORMAL : (-_-)
 */
 
#include "weather_display.h"
#include <Arduino.h>

 
// ── 天気ID → アイコン文字列 ──────────────────────
static const char* weatherIcon(int id) {
    if (id >= 200 && id < 300) return "!/";   // 雷雨
    if (id >= 300 && id < 400) return "'/";   // 霧雨
    if (id >= 500 && id < 600) return "//";   // 雨
    if (id >= 600 && id < 700) return "**";   // 雪
    if (id >= 700 && id < 800) return "..";   // 霧・もや
    if (id == 800)              return " *";   // 快晴
    if (id >= 801 && id < 900) return "~~";   // 曇り
    return "??";
}
 
// ── 天気ID → 天気名（日本語短縮）────────────────
static const char* weatherName(int id) {
    if (id >= 200 && id < 300) return "Thunder";
    if (id >= 300 && id < 400) return "Drizzle";
    if (id >= 500 && id < 600) return "Rain";
    if (id >= 600 && id < 700) return "Snow";
    if (id >= 700 && id < 800) return "Mist";
    if (id == 800)              return "Clear";
    if (id >= 801 && id < 900) return "Cloudy";
    return "N/A";
}
 
// ── EmotionType → 顔文字 ─────────────────────────
static const char* emotionFace(EmotionType e) {
    switch (e) {
        case EMOTION_HAPPY:    return "(^_^)";
        case EMOTION_HOT:      return "(~o~)";
        case EMOTION_ANGRY:    return "(>_<)";
        case EMOTION_SLEEPY:   return "(-_-)";
        case EMOTION_CONFUSED: return "(x_x)";
        case EMOTION_WORRIED:  return "(;_;)";
        case EMOTION_RAINY:    return "(>_<)"; // 雨用の顔文字など
        default:               return "(-_-)";
    }
}
 
// ── ゼロ埋め整数描画ヘルパー ──────────────────────
static void printFixed(Adafruit_SSD1306 &dsp, int val, int digits) {
    // 簡易ゼロ埋め（最大2桁）
    if (digits == 2 && val < 10) dsp.print('0');
    dsp.print(val);
}
 
// ── 公開関数 ──────────────────────────────────────
void weatherDraw(Adafruit_SSD1306 &display,
                 int weatherId,
                 float tempC,
                 const char *desc,
                 EmotionType emotion)
{

    EmotionType displayEmotion = emotion;

    if (weatherId >= 200 && weatherId < 600) {
        // 雨や雷なら、悲しい/心配な顔にする
        displayEmotion = EMOTION_SURPRISED;
    } else if (weatherId == 800 && tempC > 30.0f) {
        // 晴れで暑いなら、HOTな顔にする
        displayEmotion = EMOTION_HOT;
    } else if (weatherId >= 801) {
        // 曇りなら、少しぼーっとした顔に
        displayEmotion = EMOTION_NORMAL;
    } else if (weatherId == 600) {
        
        displayEmotion = EMOTION_RAINY; // 追加した感情
    }
    display.clearDisplay();
    display.setTextColor(WHITE);
 
    // ── 上段: アイコン（左）+ 顔文字（右）──────────
    display.setTextSize(2);
 
    // 天気アイコン（左寄せ）
    display.setCursor(4, 4);
    display.print(weatherIcon(weatherId));
    display.print(" ");
    display.print(weatherName(weatherId));
 
    // 顔文字（右寄せ、size=1 で小さめ）
    display.setTextSize(1);
    display.setCursor(80, 10);
    display.print(emotionFace(displayEmotion));
 
    // ── 区切り線 ───────────────────────────────────
    display.drawFastHLine(0, 28, 128, WHITE);
 
    // ── 中段: 気温（大きく）──────────────────────
    display.setTextSize(3);
    display.setCursor(10, 34);
 
    // 気温を整数部・小数部に分けて描画（floatをそのままprintすると不安定なため）
    int tempInt  = (int)tempC;
    int tempFrac = (int)((tempC - tempInt) * 10);
    if (tempFrac < 0) tempFrac = -tempFrac;
 
    display.print(tempInt);
    display.print('.');
    display.print(tempFrac);
    display.setTextSize(2);
    display.print(" C");
 
    display.display();
}


/**
 * sound.cpp
 * 感情に対応した「鳴き声」をパッシブブザーで再生する
 *
 * 【設計方針】
 *   - tone()/noTone() を使用。delay()ではなく vTaskDelay() で
 *     音の長さを管理し、他タスクをブロックしない。
 *   - 全関数は TaskSound からのみ呼ばれることを想定（Serialと同様、
 *     ブザー操作を1タスクに専有させることで競合を防ぐ）。
 *   - メロディは「ピッ」「ピロリン」のような短い電子音で構成し、
 *     LOVOTやたまごっちのような「鳴き声」を演出する。
 *
 * 【感情 → 鳴き声の対応】
 *   NORMAL   : 「ピ」        単発の短い確認音
 *   HAPPY    : 「ピロリン↑」 上昇アルペジオ（明るい）
 *   SAD      : 「ピョーン↓」 下降スロー（しょんぼり）
 *   HOT      : 「ハッ、ハッ」 短い連続音（あえぎ）
 *   ANGRY    : 「ブブブ」    低音の連続バズ
 *   SLEEPY   : 「アア〜」    周波数が下がっていくあくび
 *   CONFUSED : 「ピポピポ？」 高低を往復するウォブル
 *   WORRIED  : 「ピピピ…」   近い2音を素早く往復（震え）
 *   VOID     : 「…」         低い単調音（無音に近い）
 */
 #include "Sound.h"
#include "pitches.h"
#include "eye_animation.h"
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>

#if ENABLE_SOUND > 0
#include <Tone.h>

// 音の間のギャップを少し調整
static void beep(uint16_t freq, uint16_t durationMs, uint16_t gapMs = 15) {
    tone(BUZZER_PIN, freq);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    noTone(BUZZER_PIN);
    if (gapMs > 0) vTaskDelay(pdMS_TO_TICKS(gapMs));
}

void soundInit() {
    pinMode(BUZZER_PIN, OUTPUT);
}

void playCry(EmotionType emotion) {
    switch (emotion) {
        case EMOTION_HAPPY:
            beep(NOTE_C5, 80);
            beep(NOTE_E5, 80);
            break;

        case EMOTION_SAD:
            beep(NOTE_G4, 150);
            beep(NOTE_E4, 150);
            break;

        default:
            beep(NOTE_A4, 100);
            break;
    }
}
#else
// サウンド機能OFF時のスタブ（空関数）
void soundInit() {}
void playCry(EmotionType emotion) { (void)emotion; } 
#endif
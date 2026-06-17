#include "Sound.h"
#include "pitches.h"
#include "eye_animation.h" 
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>

#define DAC_PIN BUZZER_PIN

// ── 【アンプ専用】FreeRTOSと衝突しない電子音生成関数 ──
// マイコンのタイマーを一切汚さず、安全になめらかな音の波をアンプへ送ります。
static void r4_dac_tone(uint32_t freq, uint32_t durationMs, uint32_t gapMs = 30) {
    if (freq == 0) {
        analogWrite(DAC_PIN, 0); // 音を止める
        vTaskDelay(pdMS_TO_TICKS(durationMs));
    } else {
        // 周波数(Hz)から1周期の時間(マイクロ秒)を計算
        uint32_t periodUs = 1000000UL / freq;
        uint32_t halfPeriodUs = periodUs / 2;
        
        uint32_t startMs = millis();
        // 指定されたミリ秒の間、A0ピンの電圧を上下させて綺麗な波を作ります
        while ((millis() - startMs) < durationMs) {
            analogWrite(DAC_PIN, 15); // ほどよい高さの電圧
            delayMicroseconds(halfPeriodUs);
            analogWrite(DAC_PIN, 0);   // 0V
            delayMicroseconds(halfPeriodUs);
        }
    }

    // 演奏が終わったら完全に0Vにして、待機中の「ジー」というノイズを防止
    analogWrite(DAC_PIN, 0); 

    // 音の切れ目をパキッとさせて、たまごっち感を出すための無音時間
    if (gapMs > 0) {
        vTaskDelay(pdMS_TO_TICKS(gapMs));
    }
}

void soundInit() {
    // Arduino UNO R4のDAC（A0）を音楽出力モードに設定
    analogWriteResolution(8); 
    analogWrite(DAC_PIN, 0); // 起動時は完全に無音状態にする
}

void playCry(EmotionType emotion) {
    // 以前のトリガー型とは違い、「ピピッ」「ピロリ♪」という
    // 実際のたまごっちのメロディ音（周波数）を直接演奏できるようになりました！
    switch (emotion) {
        case EMOTION_NORMAL:
            // ピピッ（おなじみの電子音）
            r4_dac_tone(NOTE_A5, 60);
            r4_dac_tone(NOTE_A5, 60);
            break;

        case EMOTION_HAPPY:
            // ピロリロリン♪（ご機嫌な上昇音）
            r4_dac_tone(NOTE_C6, 40);
            r4_dac_tone(NOTE_E6, 40);
            r4_dac_tone(NOTE_G6, 40);
            r4_dac_tone(NOTE_C7, 80);
            break;

        case EMOTION_HOT:
            // ホーー…（ちょっと気の抜けた低い音）
            r4_dac_tone(NOTE_D5, 250);
            break;

        case EMOTION_CONFUSED:
            // ピョコピョコ（あれ？っとなっている音）
            r4_dac_tone(NOTE_CS6, 50);
            r4_dac_tone(NOTE_F5,  50);
            r4_dac_tone(NOTE_CS6, 50);
            break;

        case EMOTION_ANGRY:
            // プンプン！（低めの警告音）
            r4_dac_tone(NOTE_G4, 80);
            r4_dac_tone(NOTE_G4, 150);
            break;

        case EMOTION_SLEEPY:
            // ふぁ〜あ（だんだん低くなる音）
            r4_dac_tone(NOTE_E5, 150, 10);
            r4_dac_tone(NOTE_C5, 250);
            break;

        default:
            r4_dac_tone(NOTE_A5, 100);
            break;
    }
}
void playSwitchSound() {
    // 2500Hz（高めのピピッという音）を 30ミリ秒だけ短く鳴らす（たまごっちのボタン音風）
    r4_dac_tone(2500, 30); 
}
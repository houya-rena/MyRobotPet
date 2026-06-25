#include "sound.h"
#include "pitches.h"
#include "eye_animation.h" 
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>

#define DAC_PIN BUZZER_PIN  // 音を出力するピンの番号（DAC機能を持つピン）

// ── 【アンプ専用】FreeRTOSと衝突しない電子音生成関数 ──
// マイコンのタイマー（tone関数用）を一切汚さず、安全になめらかな音の波をアンプへ送る
// gapMs は音と音の間の「一瞬の無音時間」で、音がつながってべったりするのを防ぐ
static void r4_dac_tone(uint32_t freq, uint32_t durationMs, uint32_t gapMs = 30) {
    if (freq == 0) {
        // 周波数が0=無音（休符）の処理
        analogWrite(DAC_PIN, 0);                // 電圧を0Vにして音を止める
        vTaskDelay(pdMS_TO_TICKS(durationMs));  // OSのリズムに合わせて指定ミリ秒だけ待機
    } else {
        // 【音の波を作る計算】
        // 周波数(Hz)から、音の波が1往復するのに必要な「1周期の時間(マイクロ秒)」を計算
        uint32_t periodUs = 1000000UL / freq;
        uint32_t halfPeriodUs = periodUs / 2;   // 電圧を「かける時間」と「落とす時間」で半分ずつに分ける
        
        uint32_t startMs = millis();            // 演奏を始めた時刻を記録

        // 指定された演奏時間（durationMs）が経過するまで、ひたすら波をループ生成
        // ミリ秒の間、A0ピンの電圧を上下させて綺麗な波を作成
        while ((millis() - startMs) < durationMs) {
            analogWrite(DAC_PIN, 15);           // ほどよい高さの電圧に上げる
            delayMicroseconds(halfPeriodUs);    // 半周期分キープ
            analogWrite(DAC_PIN, 0);            // 電圧を0Vに落とす
            delayMicroseconds(halfPeriodUs);    // 半周期分キープ
        }
    }

    // 演奏が終わったら完全に0Vにして、待機中の「ジー」というノイズを防止
    analogWrite(DAC_PIN, 0); 

    // 音の切れ目をパキッとさせて、たまごっちのような歯切れの良いレトロ感を出すための無音時間
    // ここではFreeRTOSの機能を使用し、他のタスク（目の描画など）にマイコンの処理権を譲る
    if (gapMs > 0) {
        vTaskDelay(pdMS_TO_TICKS(gapMs));
    }
}


// ══════════════════════════════════════════════════
// soundInt: オーディオの初期化
// ══════════════════════════════════════════════════
void soundInit() {
    // Arduino UNO R4のDAC（アナログ出力）の細かさを8ビット（0~255段階）に設定
    analogWriteResolution(8); 
    analogWrite(DAC_PIN, 0); // 起動直後にノイズが出ないよう、起動時は完全に無音（0V）状態にする
}


// ══════════════════════════════════════════════════
// playCry: ロボットの「感情」に合わせた鳴き声を演奏
// emotion: ロボットの現在の気分
// ══════════════════════════════════════════════════
void playCry(EmotionType emotion) {
    // 「ピピッ」「ピロリ♪」という、実際のたまごっちのメロディ音（周波数）を直接再現
    switch (emotion) {
        case EMOTION_NORMAL:
            // ピピッ（おなじみの電子音）
            r4_dac_tone(NOTE_A5, 60);   // ラの音
            r4_dac_tone(NOTE_A5, 60);
            break;

        case EMOTION_HAPPY:
            // ピロリロリン♪（ご機嫌な上昇音）
            r4_dac_tone(NOTE_C6, 40);   // ド
            r4_dac_tone(NOTE_E6, 40);   // ミ
            r4_dac_tone(NOTE_G6, 40);   // ソ
            r4_dac_tone(NOTE_C7, 80);   // 高いド（長め）
            break;

        case EMOTION_HOT:
            // ホーー…（ちょっと気の抜けた低い音）
            r4_dac_tone(NOTE_D5, 250);  // レの音を長めに
            break;

        case EMOTION_CONFUSED:
            // ピョコピョコ（あれ？っとなっている音）
            r4_dac_tone(NOTE_CS6, 50);  // 高いド#
            r4_dac_tone(NOTE_F5,  50);  // 低いファ
            r4_dac_tone(NOTE_CS6, 50);  // 高いド#
            break;

        case EMOTION_ANGRY:
            // プンプン！（低めの警告音）
            r4_dac_tone(NOTE_G4, 80);   // 低いソを短く
            r4_dac_tone(NOTE_G4, 150);  // 同じ低いソを強調して長めに
            break;

        case EMOTION_SLEEPY:
            // ふぁ〜あ（だんだん低くなる音）
            r4_dac_tone(NOTE_E5, 150, 10); // ミの音（次の音へとすぐ繋げるためgapを10msに短縮）
            r4_dac_tone(NOTE_C5, 250);     // 低いドへ滑らかに落ちて余韻を残す
            break;

        default:
            // 想定外の感情の際は、短くプッと鳴らす
            r4_dac_tone(NOTE_A5, 100);
            break;
    }
}

// ══════════════════════════════════════════════════
// playSwitchSound: ボタンを押した時の操作音
// ══════════════════════════════════════════════════
void playSwitchSound() {
    // 2500Hz（高めのピピッという音）を 30ミリ秒だけ短く鳴らす（たまごっちの決定ボタン音風）
    r4_dac_tone(2500, 30); 
}
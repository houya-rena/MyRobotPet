#include "sound.h"
#include "pitches.h"
#include "eye_animation.h" // 既存の定義コンパイルを通すため残しています
#include <Arduino.h>

#define DAC_PIN BUZZER_PIN  // 音を出力するピン

// =================================================================================
// [1] 調整用パラメータ（音量や歯切れの良さを変えたい場合はここ！）
// =================================================================================
#define DAC_VOLUME_LEVEL  15  // 音の大きさ（0〜255 / 15〜30程度が歪まず最適）
#define DEFAULT_GAP_MS    30  // 音と音の間の無音時間（たまごっち風のピピッとした切れ味を作ります）

// =================================================================================
// [2] 電子音生成関数（FreeRTOS非依存・テスト用軽量版）
// =================================================================================
static void r4_dac_tone(uint32_t freq, uint32_t durationMs, uint32_t gapMs = DEFAULT_GAP_MS) {
    if (freq == 0) {
        // 休符処理
        analogWrite(DAC_PIN, 0);
        delay(durationMs); 
    } else {
        // 周波数から1周期（マイクロ秒）を計算
        uint32_t periodUs = 1000000UL / freq;
        uint32_t halfPeriodUs = periodUs / 2;
        uint32_t startMs = millis();

        // 指定時間が経過するまで矩形波をループ生成
        while ((millis() - startMs) < durationMs) {
            analogWrite(DAC_PIN, DAC_VOLUME_LEVEL);
            delayMicroseconds(halfPeriodUs);
            analogWrite(DAC_PIN, 0);
            delayMicroseconds(halfPeriodUs);
        }
    }

    // 演奏直後のノイズ防止（完全にカット）
    analogWrite(DAC_PIN, 0); 

    // 次の音へ移る前の無音時間
    if (gapMs > 0) {
        delay(gapMs); 
    }
}

// =================================================================================
// [3] サウンド制御関数
// =================================================================================
void soundInit() {
    analogWriteResolution(8); // Arduino UNO R4のDACを8ビットに設定
    analogWrite(DAC_PIN, 0);  // 起動時のポップノイズ防止
}

void playCry(EmotionType emotion) {
    switch (emotion) {
        case EMOTION_NORMAL:
            // ピピッ（標準的な電子音）
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
            r4_dac_tone(NOTE_E5, 150, 10); // 次の音へ滑らかに繋ぐためgapを10msに
            r4_dac_tone(NOTE_C5, 250);
            break;

        default:
            // 想定外の感情のセーフティ（プッ）
            r4_dac_tone(NOTE_A5, 100);
            break;
    }
}

void playSwitchSound() {
    // 2500Hz の高音を 30ms だけ短く鳴らす（ボタン決定音）
    r4_dac_tone(2500, 30); 
}

// =================================================================================
// [4] メインテストコード（くっつけたメイン処理）
// =================================================================================
void setup() {
    // シリアルモニターでの動作確認用
    Serial.begin(115200);
    while (!Serial); // シリアル接続を待つ
    
    soundInit();
    Serial.println("====== Sound Logic Test Studio ======");
}

void loop() {
    // 1. スイッチ音（ポチッ）
    Serial.println("[Play] Switch Sound");
    playSwitchSound();
    delay(1500);

    // 2. 正常時（ピピッ）
    Serial.println("[Play] Emotion: NORMAL");
    playCry(EMOTION_NORMAL);
    delay(2000);

    // 3. 嬉しい（ピロリロリン♪）
    Serial.println("[Play] Emotion: HAPPY");
    playCry(EMOTION_HAPPY);
    delay(2000);

    // 4. 怒り（プンプン！）
    Serial.println("[Play] Emotion: ANGRY");
    playCry(EMOTION_ANGRY);
    delay(2000);

    // 5. 眠い（ふぁ〜あ）
    Serial.println("[Play] Emotion: SLEEPY");
    playCry(EMOTION_SLEEPY);
    
    // 一通り鳴らし終わったら、次のサイクルまで4秒待機
    Serial.println("-------------------------------------");
    delay(4000);
}
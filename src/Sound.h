#ifndef SOUND_H
#define SOUND_H

// --- サウンド機能の切り替え ---
// 1 = 有効, 0 = 無効
#define ENABLE_SOUND 0

#include <Arduino.h>
#include "eye_animation.h" // ★これが必要です！

// 1. ピン定義をここに移す
#define BUZZER_PIN 8 

// 2. 音のイベント構造体を定義
typedef struct {
    bool isClick;
    EmotionType emotion;
} SoundEvent;

// 3. 関数のプロトタイプ宣言
void soundInit();
void playClick();
void playCry(EmotionType emotion);

#endif
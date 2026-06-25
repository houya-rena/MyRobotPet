#ifndef SOUND_H
#define SOUND_H

#include "eye_animation.h" // EmotionType（感情の定義）を使用するために読み込む

// ── SoundEvent 構造体 ─────────────────────────────────────────
// robot_pet.ino の xSoundQueue で送受信するデータ型。
//   isClick : true  → ボタン操作音（playSwitchSound）
//             false → 感情の鳴き声（playCry）
//   emotion : isClick が false のとき有効。再生する感情ID。
typedef struct {
    bool        isClick;
    EmotionType emotion;
} SoundEvent;

// ── 外部公開関数の宣言 ───────────────────────────────────────

/**
 * サウンドモジュールの初期化
 * Arduino UNO R4 のDAC（A0ピン）を8ビットに設定し、ノイズ対策を行う。
 */
void soundInit();

/**
 * 感情に合わせた鳴き声を再生
 * @param emotion 現在のロボットの感情
 */
void playCry(EmotionType emotion);

/**
 * ボタンが押されたときなどのシステム決定音（ポチッ）を再生
 */
void playSwitchSound();

#endif // SOUND_H


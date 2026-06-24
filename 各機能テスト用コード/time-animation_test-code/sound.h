#ifndef SOUND_H
#define SOUND_H

#include "eye_animation.h" // EmotionType（感情の定義）を使用するために読み込む

// ── 外部公開関数の宣言 ───────────────────────────────────────
// メインプログラム（robot_pet.ino）から呼び出すためのサウンド関数群

/**
 * サウンドモジュールの初期化
 * Arduino UNO R4のDAC（アナログ出力）を8ビットに設定し、ノイズ対策を行う
 */
void soundInit();

/**
 * 感情に合わせた鳴き声を再生
 *emotion 現在のロボットの感情 (EMOTION_NORMAL 〜 EMOTION_SLEEPY)
 */
void playCry(EmotionType emotion);

/**
 *ボタンが押されたときなどのシステム決定音（ポチッ）を再生
 */
void playSwitchSound();

#endif // SOUND_H
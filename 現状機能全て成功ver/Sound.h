#ifndef SOUND_H     // 「もしSOUND_Hが定義されていなければ」
#define SOUND_H     // 「ここを定義せよ（同じファイルを何度を読み込むバグを防ぐガード）」

// --- サウンド機能の切り替え ---
// 1 = 有効, 0 = 無効
// 音を消してテストしたい際、ここを0にすることで消音に切り替え可能
#define ENABLE_SOUND 1

#include <Arduino.h>
#include "eye_animation.h" // 「EmotionType（感情の定義）」を使用するために読み込む

// 1. ピン定義
// 音声信号を出力するハードウェアのピン番号
#define BUZZER_PIN A0

// 2. 音のイベント構造体を定義（FreeRTOSのメッセージ通信などで使用）
// 「今どんな音を鳴らすべきか」というリクエストを、1つの荷物としてまとめるための箱
typedef struct {
    bool isClick;          // trueならボタン操作音、falseなら鳴き声
    EmotionType emotion;   // 鳴き声（false）の時に、どの感情（喜怒哀楽）の声を出すか
} SoundEvent;

// 3. 関数のプロトタイプ宣言
void soundInit();                   // スピーカー（DAC）の初期設定を行う関数
void playSwitchSound();             // ボタンを押した時の「ピッ」という操作音を鳴らす関数
void playCry(EmotionType emotion);  // 感情（喜怒哀楽）に合わせたロボットの鳴き声を演奏する関数

#endif  // SOUND_H
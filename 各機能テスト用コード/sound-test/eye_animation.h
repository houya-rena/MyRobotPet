#ifndef EYE_ANIMATION_H // 二重読み込み防止ガード
#define EYE_ANIMATION_H
 
#include <Adafruit_GFX.h>       // グラフィック描画用ライブラリ
#include <Adafruit_SSD1306.h>   // OLEDディスプレイ制御用ライブラリ
 
// ── OLEDディスプレイの物理的なサイズ設定 ──────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
 
// ── 目の位置とサイズの基本設計図 ─────────────────────────────────
#define EYE_CENTER_Y        28  // 目の中心のY座標
#define EYE_L_CENTER_X      36  // 左目の中心のX座標
#define EYE_R_CENTER_X      92  // 右目の中心のX座標
#define EYE_RADIUS_X        22  // 目の基本サイズ（半径）
#define PUPIL_MOVE_RANGE     6  // 黒目が動く最大範囲
 
// ── タイミング設定（ms）────────────────────────────
#define BLINK_INTERVAL_MIN 2000  
#define BLINK_INTERVAL_MAX 5000  
#define GAZE_INTERVAL_MIN  1500
#define GAZE_INTERVAL_MAX  4000
 
// ── 感情タイプ（EMOTION_SURPRISEDが入ったテスト用配列に完全準拠） ───────
typedef enum {
    EMOTION_NORMAL = 0,  // 通常
    EMOTION_HAPPY,       // 嬉しい（ω口・4点ハイライト）
    EMOTION_ANGRY,       // 怒り
    EMOTION_SAD,         // 悲しい
    EMOTION_CONFUSED,    // 困惑
    EMOTION_SLEEPY,      // ウトウト
    EMOTION_COUNT        // 感情の総数
} EmotionType;
 
// ── 公開関数 ───────────────────────────────────────────────────────
// メインプログラム（.ino）から呼び出すための関数宣言です
void eyeUpdate(Adafruit_SSD1306 &dsp, EmotionType emotion);

#endif // EYE_ANIMATION_H
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

#define MOUTH_Y_POS 50
#define ITEM_X_POS 104
#define ITEM_Y_POS 48

// ── タイミング設定（ms）────────────────────────────
#define BLINK_INTERVAL_MIN 2000  
#define BLINK_INTERVAL_MAX 5000

// ── 視線移動定数 ─────────────────────────────────
#define GAZE_INTERVAL_MIN  1500
#define GAZE_INTERVAL_MAX  4000

// ── 目のパラメーター 構造体の定義 ───────────────────────
typedef struct {
    float lidClose;    // まぶたの閉じ度合い（0.0〜1.0）
    float lidCut;      // まぶたのカット量
    bool  tearLeft;    // 左目の特殊パーツ（涙やほっぺ）表示フラグ
    bool  tearRight;   // 右目の特殊パーツ表示フラグ
    float gazeScale;   // 視線移動の大きさ倍率
} EyeParam;

// 画面モードの定義
typedef enum {
    MODE_FACE,     
    MODE_EVENT     
} DisplayModeType;


// ★新しく「ご飯中」と「睡眠中」の感情（ステート）を追加！
// ※もし eye_animation.h 側にも EmotionType があれば、そこにも追加してください
typedef enum {
    EMOTION_NORMAL = 0,
    EMOTION_HAPPY,
    EMOTION_ANGRY,
    EMOTION_SAD,
    EMOTION_CONFUSED,
    EMOTION_SLEEPY,
    EMOTION_EATING,
    EMOTION_SLEEPING,
    EMOTION_BATH,
    EMOTION_SNACK,
    EMOTION_COUNT
} EmotionType;

// ── 公開関数 ───────────────────────────────────────────────────────
// メインプログラム（.ino）から呼び出すための関数宣言です
void eyeUpdate(Adafruit_SSD1306 &dsp, EmotionType emotion);
// eye_animation.h の一番下（#endif のすぐ上）にこれがあるか確認！
void eyeUpdateRTC(Adafruit_SSD1306 &dsp, EmotionType emotion, uint8_t hour, uint8_t minute);

#endif // EYE_ANIMATION_H
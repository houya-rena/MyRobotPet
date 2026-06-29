#ifndef EYE_ANIMATION_H
#define EYE_ANIMATION_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── OLEDディスプレイの物理サイズ ──────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64

// ── 目の位置・サイズ定数 ──────────────────────────────────────
#define EYE_CENTER_Y        28
#define EYE_L_CENTER_X      36
#define EYE_R_CENTER_X      92
#define EYE_RADIUS_X        22
#define PUPIL_MOVE_RANGE     6

#define MOUTH_Y_POS 50
#define ITEM_X_POS  104
#define ITEM_Y_POS   48

// ── タイミング設定（ms）──────────────────────────────────────
#define BLINK_INTERVAL_MIN 2000
#define BLINK_INTERVAL_MAX 5000
#define GAZE_INTERVAL_MIN  1500
#define GAZE_INTERVAL_MAX  4000

// ── 目パラメータ構造体 ────────────────────────────────────────
typedef struct {
    float lidClose;    // まぶたの閉じ度合い（0.0〜1.0）
    float lidCut;      // まぶたのカット量（拡張用予約）
    bool  tearLeft;    // 左目特殊パーツ表示フラグ
    bool  tearRight;   // 右目特殊パーツ表示フラグ
    float gazeScale;   // 視線移動の大きさ倍率
} EyeParam;

// ── 画面モード ────────────────────────────────────────────────
typedef enum {
    MODE_FACE,
    MODE_EVENT
} DisplayModeType;

// ── 感情列挙型 ────────────────────────────────────────────────
// ▼ EMOTION_COUNT は TIME_WEIGHTS テーブルの列数と必ず一致させること
typedef enum {
    EMOTION_NORMAL = 0,  // [0]
    EMOTION_HAPPY,       // [1]
    EMOTION_ANGRY,       // [2]
    EMOTION_SAD,         // [3]
    EMOTION_CONFUSED,    // [4]
    EMOTION_SLEEPY,      // [5]
    EMOTION_EATING,      // [6]
    EMOTION_SLEEPING,    // [7]
    EMOTION_BATH,        // [8]
    EMOTION_SNACK,       // [9]
    EMOTION_COUNT        // 要素数（テーブルのサイズ管理に使用）
} EmotionType;

// ── 公開関数 ──────────────────────────────────────────────────

/**
 * eyeInit : OLEDと内部状態をまとめて初期化する。
 * TaskInitAndLaunch から一度だけ呼ぶこと。
 * （旧: eyeStateInit を直接呼んでいた部分をこれに置き換える）
 */
void eyeInit(Adafruit_SSD1306 &dsp);

/**
 * eyeUpdate : 毎フレーム呼び出す描画関数。
 * TaskDisplay の描画ループから呼ぶこと。
 */
void eyeUpdate(Adafruit_SSD1306 &dsp, EmotionType emotion);

// ▼ eyeUpdateRTC は削除。
//   時刻→感情の変換は TaskEmotion / robot_pet.ino 側で行うため不要。

#endif // EYE_ANIMATION_H
#ifndef EYE_ANIMATION_H
#define EYE_ANIMATION_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── OLED設定 ──────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64

// ── 目のパラメータ ─────────────────────────────────
#define EYE_CENTER_Y   32          // 目の縦中心
#define EYE_L_CENTER_X 38          // 左目の横中心
#define EYE_R_CENTER_X 90          // 右目の横中心
#define EYE_RADIUS_X   22          // 目の横幅半径
#define EYE_RADIUS_Y_MAX 20        // 目の縦幅半径（全開）
#define EYE_RADIUS_Y_MIN  2        // まばたき最小値

// ── まばたき設定 ───────────────────────────────────
#define BLINK_INTERVAL_MIN 3000    // ms
#define BLINK_INTERVAL_MAX 7000    // ms

// ── 視線移動設定 ───────────────────────────────────
#define PUPIL_RADIUS       6
#define PUPIL_MOVE_RANGE   8       // 瞳が動ける最大ピクセル
#define GAZE_INTERVAL_MIN  3000    // ms
#define GAZE_INTERVAL_MAX  5000    // ms

// ── 感情タイプ ─────────────────────────────────────
typedef enum {
    EMOTION_NORMAL = 0,   // 通常
    EMOTION_HAPPY,        // 嬉しい（アーチ目）
    EMOTION_SAD,          // 悲しい（タレ目）
    EMOTION_HOT,          // 暑い（半目）
    EMOTION_ANGRY,        // 怒り（つり目）
    EMOTION_COUNT
} EmotionType;

// ── 関数プロトタイプ ───────────────────────────────
void eyeInit(Adafruit_SSD1306 &display);
void eyeUpdate(Adafruit_SSD1306 &display, EmotionType emotion);
void eyeSetEmotion(EmotionType emotion);
void eyeDrawFrame(Adafruit_SSD1306 &display);

#endif // EYE_ANIMATION_H

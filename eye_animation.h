#ifndef EYE_ANIMATION_H
#define EYE_ANIMATION_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── OLED設定 ──────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64

// ── 目のパラメータ ─────────────────────────────────
#define EYE_CENTER_Y      32
#define EYE_L_CENTER_X    38
#define EYE_R_CENTER_X    90
#define EYE_RADIUS_X      22
#define EYE_RADIUS_Y_MAX  20
#define PUPIL_RADIUS       6
#define PUPIL_MOVE_RANGE   8

// ── タイミング設定（ms）────────────────────────────
#define BLINK_INTERVAL_MIN  3000
#define BLINK_INTERVAL_MAX  7000
#define GAZE_INTERVAL_MIN   3000
#define GAZE_INTERVAL_MAX   5000

// ── 感情タイプ ─────────────────────────────────────
typedef enum {
    EMOTION_NORMAL = 0,
    EMOTION_HAPPY,
    EMOTION_SAD,
    EMOTION_HOT,
    EMOTION_ANGRY,
    EMOTION_COUNT       // センチネル（"変化なし"）
} EmotionType;

// ── 公開関数 ───────────────────────────────────────
void eyeInit(Adafruit_SSD1306 &display);
void eyeUpdate(Adafruit_SSD1306 &display, EmotionType emotion);

#endif // EYE_ANIMATION_H

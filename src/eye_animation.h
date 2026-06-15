#ifndef EYE_ANIMATION_H
#define EYE_ANIMATION_H
 
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
 
// ── OLED設定 ──────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
 
// ── 目のパラメータ ─────────────────────────────────
#define EYE_CENTER_Y      28
#define EYE_L_CENTER_X    36
#define EYE_R_CENTER_X    92
#define EYE_RADIUS_X      22
#define EYE_RADIUS_Y_MAX  18
#define PUPIL_RADIUS       6
#define PUPIL_MOVE_RANGE   8
 
// ── タイミング設定（ms）────────────────────────────
#define BLINK_INTERVAL_MIN  2000
#define BLINK_INTERVAL_MAX  5000
#define GAZE_INTERVAL_MIN   3000
#define GAZE_INTERVAL_MAX   5000
 
// ── 口の種類 ───────────────────────────────────────
typedef enum {
    MOUTH_NONE = 0,   // 口なし（通常・HOT・ANGRYなど）
    MOUTH_HAPPY,      // ニコニコ（上カーブ）
    MOUTH_SAD,        // 悲しい（下カーブ）
    MOUTH_FLAT,       // 虚無（横線）
} MouthType;
 
// ── 感情タイプ ─────────────────────────────────────
typedef enum {
    EMOTION_NORMAL = 0,  // 通常
    EMOTION_HAPPY,       // 嬉しい  ↑↑（^^目 + ニコニコ口）
    EMOTION_SAD,         // 悲しい  （タレ目 + 悲し口 + 涙）
    EMOTION_HOT,         // 暑い    （半目）
    EMOTION_ANGRY,       // 怒り    （つり目）
    EMOTION_SLEEPY,      // ウトウト （とろ〜ん目・まばたきが遅い）
    EMOTION_CONFUSED,    // 困惑    （><目）
    EMOTION_WORRIED,     // 心配    （×× 目 + 悲し口）
    EMOTION_VOID,        // 虚無    （-- 目 + 横線口）
    EMOTION_RAINY,
    EMOTION_COUNT        // センチネル（変化なし）
} EmotionType;
 
// ── 公開関数 ───────────────────────────────────────
void eyeInit(Adafruit_SSD1306 &display);
void eyeUpdate(Adafruit_SSD1306 &display, EmotionType emotion);
 
#endif // EYE_ANIMATION_H
 

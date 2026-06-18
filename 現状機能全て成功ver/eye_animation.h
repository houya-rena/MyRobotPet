#ifndef EYE_ANIMATION_H // 「もしEYE_ANIMSTION_Hが定義されていなければ」
#define EYE_ANIMATION_H // 「ここで定義せよ（二重読み込み防止のガード）」
 
#include <Adafruit_GFX.h>       // グラフィック描写用ライブラリ
#include <Adafruit_SSD1306.h>   // OLEDディスプレイ制御用ライブラリ
 
// ── OLEDディスプレイの物理的なサイズ設定 ──────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
 
// ── 目の位置とサイズの基本設計図 ─────────────────────────────────
// 左右の目の中心座標と半径を定義。ここを変更すると顔の配置が変わる
#define EYE_CENTER_Y      28
#define EYE_L_CENTER_X    36
#define EYE_R_CENTER_X    92
#define EYE_RADIUS_X      22
#define EYE_RADIUS_Y_MAX  18 // ※現在は正円で描画しているため、あまり影響しない
#define PUPIL_RADIUS       6 // 黒目の大きさ
#define PUPIL_MOVE_RANGE   8 // 黒目が動く最大範囲
 
// ── タイミング設定（ms）────────────────────────────
// まばたきや視線移動の感覚。ここを大きくすると落ち着いたロボットになる
#define BLINK_INTERVAL_MIN  2000
#define BLINK_INTERVAL_MAX  5000
#define GAZE_INTERVAL_MIN   3000
#define GAZE_INTERVAL_MAX   5000
 
// ── 口の種類 ───────────────────────────────────────
// どのような口を描くかを番号で整理
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
    EMOTION_HOT,         // 暑い    （半目）
    EMOTION_ANGRY,       // 怒り    （つり目）
    EMOTION_SLEEPY,      // ウトウト （とろ〜ん目・まばたきが遅い）
    EMOTION_CONFUSED,    // 困惑    （><目）
    EMOTION_WORRIED,     // 心配    （×× 目 + 悲し口）
    EMOTION_SURPRISED,   // 驚き
    EMOTION_RAINY,
    EMOTION_COUNT        // 感情の総数を知るためのダミー項目
} EmotionType;
 
// ── 公開関数（外部から呼び出す「入口」） ───────────────────────────────────────
// ロボット全体で使用できるように、関数を公開
void eyeInit(Adafruit_SSD1306 &display);    // ロボット起動時の初期化
void eyeUpdate(Adafruit_SSD1306 &display, EmotionType emotion); // 表情の更新処理

#endif // EYE_ANIMATION_H
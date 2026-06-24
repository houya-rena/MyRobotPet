/**
 * eye_state.cpp
 * まばたき・視線・感情遷移の内部状態管理
 *
 * 【修正点】
 * - s_tearOffset を EMOTION_SAD 以外に切り替わった時に 0.0f にリセット
 * - 黒目Y位置追従（currentPgy）を static ローカルから本ファイルの
 *   static 変数に移動。drawOneEye は純粋な描画関数として分離済み。
 */

#include "eye_state.h"
#include "eye_params.h"

// ── 内部状態 ─────────────────────────────────────
static float         s_blinkOpen      = 1.0f;
static bool          s_blinkClosing   = false;
static bool          s_blinkOpening   = false;
static bool          s_blinkHolding   = false;
static unsigned long s_blinkHoldStart = 0;
static unsigned long s_nextBlink      = 0;

static int8_t        s_gazeX      = 0,  s_gazeY      = 0;
static int8_t        s_gazeTargX  = 0,  s_gazeTargY  = 0;
static unsigned long s_nextGaze   = 0,  s_gazeStart  = 0;
static bool          s_gazeMoving = false;

static EmotionType   s_emotion          = EMOTION_NORMAL;
static unsigned long s_emotionStartTime = 0;
static int8_t        s_sleepyBlinkCount = 0;

// 感情遷移補間キャッシュ
static float s_curLidClose  = 0.0f;
static float s_curLidCut    = 0.0f;
static float s_curGazeScale = 1.0f;

// 【追加】線形補間関数の定義
static float lerpF(float start, float end, float t) {
    return start + (end - start) * t;
}

// 涙のスクロールオフセット
static float s_tearOffset = 0.0f;

// ── 公開変数（eye_animation.cpp から参照） ────────
float  g_tearOffset = 0.0f;   // 涙のY位置（eye_animation.cppが描画に使う）
bool   g_showTear   = false;  // 涙を表示するか

// ── 初期化 ───────────────────────────────────────
void eyeStateInit() {
    s_blinkOpen      = 1.0f;
    s_blinkClosing   = false;
    s_blinkOpening   = false;
    s_blinkHolding   = false;
    s_blinkHoldStart = 0;
    s_nextBlink      = 0;
    s_gazeX = s_gazeY = s_gazeTargX = s_gazeTargY = 0;
    s_nextGaze = s_gazeStart = 0;
    s_gazeMoving     = false;
    s_emotion        = EMOTION_NORMAL;
    s_emotionStartTime = 0;
    s_sleepyBlinkCount = 0;
    s_curLidClose    = 0.0f;
    s_curLidCut      = 0.0f;
    s_curGazeScale   = 1.0f;
    s_tearOffset     = 0.0f;
    g_tearOffset     = 0.0f;
    g_showTear       = false;
}

// ── 毎フレーム更新 ───────────────────────────────
void eyeStateUpdate(EmotionType emotion, unsigned long now,
                    float &blinkLid, float &lidCut,
                    int8_t &gazeX, int8_t &gazeY,
                    float &gazeScale)
{
    // ── 感情切り替え検出 ──
    if (emotion != s_emotion) {
        // 涙オフセットは SAD 以外に切り替わった時にリセット（修正箇所）
        if (emotion != EMOTION_SAD) {
            s_tearOffset = 0.0f;
        }
        s_emotion          = emotion;
        s_emotionStartTime = now;
        s_sleepyBlinkCount = 0;
        s_blinkHolding     = false;
        s_blinkClosing     = false;
        s_blinkOpening     = false;
        s_blinkOpen        = 1.0f;
    }

    // ── 感情パラメータ取得・補間 ──
    EyeParam target = getEyeParam(s_emotion, s_sleepyBlinkCount);
    float morphRate  = constrain((float)(now - s_emotionStartTime) / 300.0f, 0.0f, 1.0f);

    s_curLidClose  = lerpF(s_curLidClose,  target.lidClose,  morphRate);
    s_curLidCut    = lerpF(s_curLidCut,    target.lidCut,    morphRate);
    s_curGazeScale = lerpF(s_curGazeScale, target.gazeScale, morphRate);

    // ── まばたきロジック ──
    float blinkSpeed = (s_emotion == EMOTION_SLEEPY)
                       ? ((s_sleepyBlinkCount == 1) ? 0.03f : 0.08f)
                       : 0.22f;

    if (s_blinkHolding) {
        if (now - s_blinkHoldStart >= 1200) {
            s_blinkHolding = false;
            s_blinkOpening = true;
        }
    } else if (!s_blinkClosing && !s_blinkOpening
               && now >= s_nextBlink
               && s_emotion != EMOTION_SLEEPING)
    {
        s_blinkClosing = true;
    }

    if (s_blinkClosing) {
        s_blinkOpen -= blinkSpeed;
        if (s_blinkOpen <= 0.0f) {
            s_blinkOpen    = 0.0f;
            s_blinkClosing = false;
            if (s_emotion == EMOTION_SLEEPY && --s_sleepyBlinkCount <= 0) {
                s_sleepyBlinkCount = (int8_t)random(3, 6);
                s_blinkHolding     = true;
                s_blinkHoldStart   = now;
            } else {
                s_blinkOpening = true;
            }
        }
    } else if (s_blinkOpening) {
        s_blinkOpen += blinkSpeed;
        if (s_blinkOpen >= 1.0f) {
            s_blinkOpen    = 1.0f;
            s_blinkOpening = false;
            unsigned long minI = (s_emotion == EMOTION_SLEEPY) ?  800 : BLINK_INTERVAL_MIN;
            unsigned long maxI = (s_emotion == EMOTION_SLEEPY) ? 1800 : BLINK_INTERVAL_MAX;
            s_nextBlink = now + random(minI, maxI);
        }
    }

    // ── 視線ロジック ──
    if (now >= s_nextGaze) {
        s_gazeMoving = true;
        s_gazeStart  = now;
        s_gazeTargX  = (int8_t)random(-PUPIL_MOVE_RANGE, PUPIL_MOVE_RANGE + 1);
        s_gazeTargY  = (int8_t)random(-3, 4);
        s_nextGaze   = now + random(GAZE_INTERVAL_MIN, GAZE_INTERVAL_MAX);
    }
    if (s_gazeMoving) {
        float t = constrain((float)(now - s_gazeStart) / 150.0f, 0.0f, 1.0f);
        s_gazeX = (int8_t)lerpF((float)s_gazeX, (float)s_gazeTargX, t);
        s_gazeY = (int8_t)lerpF((float)s_gazeY, (float)s_gazeTargY, t);
        if (t >= 1.0f) s_gazeMoving = false;
    }

    // ── 涙オフセット更新 ──
    if (s_emotion == EMOTION_SAD) {
        s_tearOffset = (s_tearOffset + 0.5f > 12.0f) ? 0.0f : s_tearOffset + 0.5f;
        g_tearOffset = s_tearOffset;
        g_showTear   = true;
    } else {
        g_showTear = false;
    }

    // ── 出力値セット ──
    float effectiveLidClose = s_curLidClose + (1.0f - s_blinkOpen);
    if (s_emotion == EMOTION_SLEEPING) effectiveLidClose = 1.0f;

    blinkLid  = constrain(effectiveLidClose, 0.0f, 1.0f);
    lidCut    = s_curLidCut;
    gazeX     = s_gazeX;
    gazeY     = s_gazeY;
    gazeScale = s_curGazeScale;
}
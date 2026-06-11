#include "eye_animation.h"
#include <Arduino.h>
#include <math.h>

/**
 * eye_animation.cpp
 * 目の描画・まばたき・視線移動・感情モーフィング
 *
 * 【設計方針】
 *   - ビットマップ不使用。fillEllipse + 数式オフセットのみでSRAM節約。
 *   - delay()不使用。millis()タイマーで非同期管理。
 *   - 内部状態はすべてstatic（タスクスタックを圧迫しない）。
 *   - eyeUpdate()をTaskDisplayから33ms周期で呼ぶだけでOK。
 */


// ══════════════════════════════════════════════════
// 感情パラメータ定義（ビットマップ不使用・数値のみ）
// ══════════════════════════════════════════════════
struct EyeShape {
    float upperCurve;  // 上まぶたカーブ（正=アーチ/負=タレ目）
    float scaleY;      // 縦サイズ倍率
    int8_t tiltInner;  // 内側まぶた傾き（正=タレ目/負=つり目）
};

static const EyeShape SHAPES[EMOTION_COUNT] = {
    {  0.0f, 1.0f,  0 },  // NORMAL
    {  1.2f, 1.1f, -3 },  // HAPPY  アーチ型・少し大きめ
    { -1.0f, 0.9f,  4 },  // SAD    タレ目
    {  0.0f, 0.5f,  0 },  // HOT    半目
    { -0.8f, 1.0f, -5 },  // ANGRY  つり目
};

// ══════════════════════════════════════════════════
// 内部状態（static = SRAM固定配置、スタック消費なし）
// ══════════════════════════════════════════════════

// まばたき
static float         s_eyeOpen    = 1.0f;
static bool          s_blinkClose = false;
static bool          s_blinkOpen  = false;
static unsigned long s_nextBlink  = 0;

// 視線
static int8_t        s_gazeX      = 0, s_gazeY      = 0;
static int8_t        s_gazeTargX  = 0, s_gazeTargY  = 0;
static unsigned long s_nextGaze   = 0, s_gazeStart   = 0;
static bool          s_gazeMoving = false;

// 感情モーフィング
static EmotionType   s_emotion    = EMOTION_NORMAL;
static float         s_morphT     = 1.0f;
static EyeShape      s_shapeFrom  = SHAPES[EMOTION_NORMAL];
static EyeShape      s_shapeCur   = SHAPES[EMOTION_NORMAL];

// ══════════════════════════════════════════════════
// 内部ユーティリティ
// ══════════════════════════════════════════════════
static inline float  lerpF(float a, float b, float t){ return a+(b-a)*t; }
static inline int8_t lerpI(int8_t a, int8_t b, float t){
    return (int8_t)(a+(b-a)*t);
}

// 目を1つ描画// ══════════════════════════════════════════════════
// 「うるうるロボお目目」描画関数（入れ替え案）
// ══════════════════════════════════════════════════
static void drawOneEye(Adafruit_SSD1306 &dsp,
                       int cx, int cy, int rx, int ry,
                       const EyeShape &sh,
                       int8_t gx, int8_t gy)
{
    // 1. 白目（ベース）
    dsp.fillEllipse(cx, cy, rx, ry, WHITE);

    // 2. まぶた（閉じ度合いを黒で塗りつぶす）
    // ★垂直線(drawFastVLine)方式を維持しつつ、カーブを少し緩やかに
    int lidH = (int)(ry * (1.0f - s_eyeOpen));
    if (lidH > 0) {
        for (int px = cx - rx; px <= cx + rx; px++) {
            float t    = (float)(px - cx) / (float)rx;
            // カーブと傾きを少し抑えて、自然な閉じ方に
            float curv = sh.upperCurve * (1.0f - t*t) * ry * 0.2f; 
            float tilt = sh.tiltInner  * (1.0f - fabsf(t)) * (-0.8f);
            int   bot  = (int)(cy - ry + lidH + curv + tilt);
            // まぶたの端を少し滑らかにするために、BLACKで太めに描画
            dsp.drawFastVLine(px, cy - ry - 2, bot - (cy - ry) + 4, BLACK);
        }
    }

    // 3. 瞳（クリップ付き、少し大きく）
    // ★PUPIL_RADIUSを基準に、虹彩を少し大きめの円に
    int irisR = PUPIL_RADIUS + 3; // 虹彩（外側の大きな円）
    int pupilR = PUPIL_RADIUS - 1; // 瞳孔（内側の小さな円）

    int px = constrain(cx + gx, cx - rx + irisR + 1,
                                cx + rx - irisR - 1);
    int py = constrain(cy + gy, cy - ry + irisR + 1,
                                cy + ry - irisR - 1);

    // 虹彩（外側：少しグレーに近い黒...SSD1306では無理なので、WHITEの点々で擬似グラデーション）
    // ★ここが「うるうる」の鍵！虹彩に少し表情をつける
    dsp.fillCircle(px, py, irisR, BLACK);
    
    // 擬似虹彩グラデーション（WHITEで細かく点を打つ）
    for(int i=0; i<360; i+=45){
        float rad = i * DEG_TO_RAD;
        dsp.drawPixel(px + cos(rad)*(irisR-1), py + sin(rad)*(irisR-1), WHITE);
    }

    // 瞳孔（内側：真っ黒）
    dsp.fillCircle(px, py, pupilR, BLACK);

    // 4. うるうるハイライト（ここが命！）
    // ★大きな「光の映り込み」を楕円で追加
    
    // ハイライトA（大きな楕円：左上）
    dsp.fillEllipse(px - 2, py - 2, 2, 3, WHITE);

    // ハイライトB（小さな丸：右下、少し小さく）
    // ★これにより、光が複数の方向から当たっているように見せる
    dsp.fillCircle(px + 2, py + 2, 1, WHITE);

    // 5. 縁取り（最後に BLACK で締める）
    dsp.drawEllipse(cx, cy, rx, ry, BLACK);
}
// ══════════════════════════════════════════════════
// 公開関数
// ══════════════════════════════════════════════════
void eyeInit(Adafruit_SSD1306 &display) {
    randomSeed(analogRead(A0));
    unsigned long now = millis();
    s_nextBlink = now + random(BLINK_INTERVAL_MIN, BLINK_INTERVAL_MAX);
    s_nextGaze  = now + random(GAZE_INTERVAL_MIN,  GAZE_INTERVAL_MAX);
}

void eyeUpdate(Adafruit_SSD1306 &display, EmotionType emotion) {
    unsigned long now = millis();

    // ── 感情モーフィング ──────────────────────────
    if (emotion != s_emotion) {
        s_shapeFrom = s_shapeCur;
        s_emotion   = emotion;
        s_morphT    = 0.0f;
    }
    if (s_morphT < 1.0f) {
        s_morphT = min(s_morphT + 0.05f, 1.0f);
        const EyeShape &tgt = SHAPES[s_emotion];
        s_shapeCur.upperCurve = lerpF(s_shapeFrom.upperCurve, tgt.upperCurve, s_morphT);
        s_shapeCur.scaleY     = lerpF(s_shapeFrom.scaleY,     tgt.scaleY,     s_morphT);
        s_shapeCur.tiltInner  = lerpI(s_shapeFrom.tiltInner,  tgt.tiltInner,  s_morphT);
    }

    // ── まばたき ──────────────────────────────────
    if (!s_blinkClose && !s_blinkOpen && now >= s_nextBlink) {
        s_blinkClose = true;
    }
    if (s_blinkClose) {
        s_eyeOpen -= 0.15f;
        if (s_eyeOpen <= 0.0f) {
            s_eyeOpen = 0.0f;
            s_blinkClose = false;
            s_blinkOpen  = true;
        }
    }
    if (s_blinkOpen) {
        s_eyeOpen += 0.12f;
        if (s_eyeOpen >= 1.0f) {
            s_eyeOpen = 1.0f;
            s_blinkOpen = false;
            s_nextBlink = now + random(BLINK_INTERVAL_MIN, BLINK_INTERVAL_MAX);
        }
    }

    // ── 視線移動（イーズアウト補間）────────────────
    if (!s_gazeMoving && now >= s_nextGaze) {
        s_gazeTargX  = (int8_t)random(-PUPIL_MOVE_RANGE, PUPIL_MOVE_RANGE + 1);
        s_gazeTargY  = (int8_t)random(-PUPIL_MOVE_RANGE/2, PUPIL_MOVE_RANGE/2 + 1);
        s_gazeStart  = now;
        s_gazeMoving = true;
    }
    if (s_gazeMoving) {
        float t = min((float)(now - s_gazeStart) / 400.0f, 1.0f);
        float ease = 1.0f - (1.0f - t) * (1.0f - t);
        s_gazeX = lerpI(s_gazeX, s_gazeTargX, ease);
        s_gazeY = lerpI(s_gazeY, s_gazeTargY, ease);
        if (t >= 1.0f) {
            s_gazeMoving = false;
            s_nextGaze = now + random(GAZE_INTERVAL_MIN, GAZE_INTERVAL_MAX);
        }
    }

    // ── 描画 ──────────────────────────────────────
    display.clearDisplay();
    int ry = (int)(EYE_RADIUS_Y_MAX * s_shapeCur.scaleY);
    drawOneEye(display, EYE_L_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X, ry,
               s_shapeCur, s_gazeX, s_gazeY);
    drawOneEye(display, EYE_R_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X, ry,
               s_shapeCur, s_gazeX, s_gazeY);
    display.display();
}

/**
 * eye_animation.cpp
 * 目の描画・まばたき・視線移動・感情モーフィング
 *
 * 【設計方針】
 *   - ビットマップ不使用。fillEllipse + 数式オフセットのみでSRAM節約。
 *   - delay()不使用。millis()タイマーで非同期管理。
 *   - 内部状態はすべてstatic（タスクスタックを圧迫しない）。
 *   - eyeUpdate()をTaskDisplayから33ms周期で呼ぶだけでOK。
 *
 * 【感情と目の見た目の対応】
 *   NORMAL   : 丸目 + 口なし
 *   HAPPY    : ^^記号目 + ニコニコ口
 *   SAD      : タレ目 + 悲し口 + 涙
 *   HOT      : 半目（まぶた半開き）
 *   ANGRY    : つり目
 *   SLEEPY   : とろ〜ん目（まばたき遅・まぶた重い）
 *   CONFUSED : ><記号目
 *   WORRIED  : ×× 記号目 + 悲し口
 *   VOID     : -- 記号目 + 横線口
 */
 
#include "eye_animation.h"
#include <math.h>
 
// ══════════════════════════════════════════════════
// 感情パラメータ定義
// ══════════════════════════════════════════════════
struct EyeShape {
    float  upperCurve;   // 上まぶたカーブ（正=アーチ / 負=タレ目）
    float  scaleY;       // 縦サイズ倍率
    int8_t tiltInner;    // 内側まぶた傾き（正=タレ目 / 負=つり目）
    MouthType mouth;     // 口の種類
    bool   isSymbol;     // true = 目を記号で描く（^^や><など）
};
 
//                          curve   scaleY  tilt  mouth         symbol
static const EyeShape SHAPES[EMOTION_COUNT] = {
    {  0.0f, 1.0f,  0, MOUTH_NONE,  false },  // NORMAL
    {  0.0f, 0.6f,  0, MOUTH_HAPPY, true  },  // HAPPY    (^^)
    { -1.0f, 0.9f,  4, MOUTH_SAD,   false },  // SAD
    {  0.0f, 0.5f,  0, MOUTH_NONE,  false },  // HOT      (半目)
    { -0.8f, 1.0f, -5, MOUTH_NONE,  true  },  // ANGRY
    {  0.0f, 0.4f,  2, MOUTH_NONE,  false },  // SLEEPY   (とろ〜ん)
    {  0.0f, 0.8f,  0, MOUTH_NONE,  true  },  // CONFUSED (><)
    {  0.0f, 0.8f,  0, MOUTH_SAD,   true  },  // WORRIED  (××)
    {  0.0f, 0.6f,  0, MOUTH_FLAT,  true  },  // VOID     (--)
};
 
// ══════════════════════════════════════════════════
// 内部状態（static = SRAM固定配置）
// ══════════════════════════════════════════════════
 
// まばたき
static float         s_blinkOpen   = 1.0f;  // 瞬きによる開き（1.0=全開）
static bool          s_blinkClose  = false;
static bool          s_blinkOpening= false;
static unsigned long s_nextBlink   = 0;
 
// 視線
static int8_t        s_gazeX       = 0, s_gazeY      = 0;
static int8_t        s_gazeTargX   = 0, s_gazeTargY  = 0;
static unsigned long s_nextGaze    = 0, s_gazeStart  = 0;
static bool          s_gazeMoving  = false;
 
// 感情モーフィング
static EmotionType   s_emotion     = EMOTION_NORMAL;
static float         s_morphT      = 1.0f;
static EyeShape      s_shapeFrom   = SHAPES[EMOTION_NORMAL];
static EyeShape      s_shapeCur    = SHAPES[EMOTION_NORMAL];
 
// 感情ごとのベース開度（HOT=半目、SLEEPY=とろ〜んなど）
static float         s_baseOpen    = 1.0f;
 
// 最終的な目の開き（baseOpen × blinkOpen で合成）
static float         s_eyeOpen     = 1.0f;
 
// ══════════════════════════════════════════════════
// 内部ユーティリティ
// ══════════════════════════════════════════════════
static inline float  lerpF(float a, float b, float t) { return a + (b-a)*t; }
static inline int8_t lerpI(int8_t a, int8_t b, float t) {
    return (int8_t)(a + (b-a)*t);
}
 
// ══════════════════════════════════════════════════
// 記号目の描画（^^  ><  ××  --）
// ══════════════════════════════════════════════════
static void drawSymbolEye(Adafruit_SSD1306 &dsp, int cx, int cy, int rx, int ry) {
    // 黒背景の楕円（白目を消す）
    dsp.fillEllipse(cx, cy, rx + 6, ry, BLACK);
 
    if (s_emotion == EMOTION_HAPPY) {
        // ^^ ニコニコ：Vの字を逆にした弧
        for (int i = -1; i <= 1; i++) {
            dsp.drawLine(cx - rx + 2, cy - 3 + i, cx,       cy - 8 + i, WHITE);
            dsp.drawLine(cx,          cy - 8 + i, cx + rx - 2, cy - 3 + i, WHITE);
        }
 
    } else if (s_emotion == EMOTION_CONFUSED) {
        // >< 困惑：X字（目の中心でクロス）
        for (int i = -1; i <= 1; i++) {
            dsp.drawLine(cx - 7, cy - 7 + i, cx + 7, cy + 7 + i, WHITE);
            dsp.drawLine(cx - 7, cy + 7 + i, cx + 7, cy - 7 + i, WHITE);
        }
 
    } else if (s_emotion == EMOTION_WORRIED) {
        // ×× 心配：斜め×（CONFUSEDより少し小さめ）
        for (int i = -1; i <= 1; i++) {
            dsp.drawLine(cx - 5, cy - 5 + i, cx + 5, cy + 5 + i, WHITE);
            dsp.drawLine(cx - 5, cy + 5 + i, cx + 5, cy - 5 + i, WHITE);
        }
 
    } else if (s_emotion == EMOTION_VOID) {
        // -- 虚無：太い横線
        dsp.fillRect(cx - 12, cy - 2, 24, 4, WHITE);
    }
 
    // 記号目にも縁取り
    dsp.drawEllipse(cx, cy, rx + 6, ry, BLACK);
}
 
// ══════════════════════════════════════════════════
// 通常目の描画（丸目・タレ目・つり目・半目など）
// ══════════════════════════════════════════════════
static void drawNormalEye(Adafruit_SSD1306 &dsp,
                          int cx, int cy, int rx, int ry,
                          const EyeShape &sh,
                          int8_t gx, int8_t gy)
{
    // 白目
    dsp.fillEllipse(cx, cy, rx, ry, WHITE);
 
    // 瞳（クリップ付き）
    int irisR = PUPIL_RADIUS + 2;
    int px = constrain(cx + gx, cx - rx + irisR, cx + rx - irisR);
    int py = constrain(cy + gy, cy - ry + irisR, cy + ry - irisR);
 
    if (s_eyeOpen > 0.15f) {
        dsp.fillCircle(px, py, irisR, BLACK);
        // ハイライト（うるうる感）
        dsp.fillEllipse(px - 2, py - 2, 2, 3, WHITE);
        dsp.fillCircle (px + 2, py + 2, 1, WHITE);
    }
 
    // まぶた（上から黒で塗りつぶして閉じを表現）
    int lidH = (int)(ry * 2.0f * (1.0f - s_eyeOpen));
    if (lidH > 0) {
        dsp.fillRect(cx - rx, cy - ry - 1, rx * 2 + 1, lidH + 1, BLACK);
    }
 
    // 縁取り
    dsp.drawEllipse(cx, cy, rx, ry, BLACK);
}
 
// ══════════════════════════════════════════════════
// 口の描画
// ══════════════════════════════════════════════════
static void drawMouth(Adafruit_SSD1306 &dsp, MouthType mouth) {
    const int mx = SCREEN_WIDTH / 2;  // 64
    const int my = 54;                // 目の下
 
    switch (mouth) {
        case MOUTH_HAPPY:
            // ニコニコ：上カーブ
            dsp.drawLine(mx - 8, my,     mx - 3, my + 4, WHITE);
            dsp.drawLine(mx - 3, my + 4, mx + 3, my + 4, WHITE);
            dsp.drawLine(mx + 3, my + 4, mx + 8, my,     WHITE);
            break;
 
        case MOUTH_SAD:
            // 悲しい：下カーブ
            dsp.drawLine(mx - 8, my + 4, mx - 3, my,     WHITE);
            dsp.drawLine(mx - 3, my,     mx + 3, my,     WHITE);
            dsp.drawLine(mx + 3, my,     mx + 8, my + 4, WHITE);
            break;
 
        case MOUTH_FLAT:
            // 虚無：横線
            dsp.drawFastHLine(mx - 8, my + 2, 16, WHITE);
            break;
 
        case MOUTH_NONE:
        default:
            break;
    }
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
 
    // ── 1. 感情切替 ────────────────────────────────
    if (emotion != s_emotion) {
        s_shapeFrom = s_shapeCur;
        s_emotion   = emotion;
        s_morphT    = 0.0f;
 
        // 感情ごとのベース開度
        switch (s_emotion) {
            case EMOTION_HOT:     s_baseOpen = 0.45f; break;
            case EMOTION_SLEEPY:  s_baseOpen = 0.35f; break;  // とろ〜ん
            case EMOTION_HAPPY:
            case EMOTION_CONFUSED:
            case EMOTION_VOID:    s_baseOpen = 0.50f; break;  // 記号目は半開き
            default:              s_baseOpen = 1.0f;  break;
        }
    }
 
    // ── 2. 形状モーフィング（スムースステップ補間）──
    if (s_morphT < 1.0f) {
        s_morphT = min(s_morphT + 0.04f, 1.0f);
        // スムースステップで加速→減速
        float t = s_morphT * s_morphT * (3.0f - 2.0f * s_morphT);
        const EyeShape &tgt = SHAPES[s_emotion];
        s_shapeCur.upperCurve = lerpF(s_shapeFrom.upperCurve, tgt.upperCurve, t);
        s_shapeCur.scaleY     = lerpF(s_shapeFrom.scaleY,     tgt.scaleY,     t);
        s_shapeCur.tiltInner  = lerpI(s_shapeFrom.tiltInner,  tgt.tiltInner,  t);
        s_shapeCur.mouth      = tgt.mouth;
        s_shapeCur.isSymbol   = tgt.isSymbol;
    }
 
    // ── 3. まばたき ────────────────────────────────
    // SLEEPY は特別：ゆっくり閉じてゆっくり開く
    bool isSleepy = (s_emotion == EMOTION_SLEEPY);
    float blinkSpeed = isSleepy ? 0.06f : 0.25f;
    // HAPPY / 記号目はまばたきしない
    bool canBlink = !s_shapeCur.isSymbol;
 
    if (canBlink) {
        if (!s_blinkClose && !s_blinkOpening && now >= s_nextBlink) {
            s_blinkClose = true;
        }
        if (s_blinkClose) {
            s_blinkOpen -= blinkSpeed;
            if (s_blinkOpen <= 0.0f) {
                s_blinkOpen  = 0.0f;
                s_blinkClose = false;
                s_blinkOpening = true;
            }
        } else if (s_blinkOpening) {
            s_blinkOpen += blinkSpeed;
            if (s_blinkOpen >= 1.0f) {
                s_blinkOpen    = 1.0f;
                s_blinkOpening = false;
                // SLEEPY は短い間隔でまたまばたき
                int interval = isSleepy
                    ? random(500, 2000)
                    : random(BLINK_INTERVAL_MIN, BLINK_INTERVAL_MAX);
                s_nextBlink = now + interval;
            }
        }
    } else {
        s_blinkOpen = 1.0f;
    }
 
    // ベース開度 × まばたき開度 を合成
    s_eyeOpen = s_baseOpen * s_blinkOpen;
 
    // ── 4. 視線移動 ────────────────────────────────
    // VOID・CONFUSED は視線が動かない（虚ろ）
    bool gazeEnabled = (s_emotion != EMOTION_VOID);
 
    if (gazeEnabled && !s_gazeMoving && now >= s_nextGaze) {
        s_gazeTargX  = (int8_t)random(-PUPIL_MOVE_RANGE, PUPIL_MOVE_RANGE + 1);
        s_gazeTargY  = (int8_t)random(-PUPIL_MOVE_RANGE/2, PUPIL_MOVE_RANGE/2 + 1);
        s_gazeStart  = now;
        s_gazeMoving = true;
    }
    if (s_gazeMoving) {
        float t    = min((float)(now - s_gazeStart) / 400.0f, 1.0f);
        float ease = 1.0f - (1.0f - t) * (1.0f - t);
        s_gazeX = lerpI(s_gazeX, s_gazeTargX, ease);
        s_gazeY = lerpI(s_gazeY, s_gazeTargY, ease);
        if (t >= 1.0f) {
            s_gazeMoving = false;
            s_nextGaze = now + random(GAZE_INTERVAL_MIN, GAZE_INTERVAL_MAX);
        }
    }
 
    // ── 5. 描画 ────────────────────────────────────
    display.clearDisplay();
    int ry = (int)(EYE_RADIUS_Y_MAX * s_shapeCur.scaleY);
 
    // 左右の目を描画
    for (int side = 0; side < 2; side++) {
        int cx = (side == 0) ? EYE_L_CENTER_X : EYE_R_CENTER_X;
        int cy = EYE_CENTER_Y;
        if (s_shapeCur.isSymbol) {
            drawSymbolEye(display, cx, cy, EYE_RADIUS_X, ry);
        } else {
            drawNormalEye(display, cx, cy, EYE_RADIUS_X, ry,
                          s_shapeCur, s_gazeX, s_gazeY);
        }
    }
 
    // SAD の涙（目の外側下に小さい点）
    if (s_emotion == EMOTION_SAD) {
        display.fillCircle(EYE_L_CENTER_X - 12, EYE_CENTER_Y + ry + 2, 1, WHITE);
        display.fillCircle(EYE_R_CENTER_X + 12, EYE_CENTER_Y + ry + 2, 1, WHITE);
    }
 
    // 口
    drawMouth(display, s_shapeCur.mouth);
 
    display.display();
}
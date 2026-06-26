/**
 * eye_animation.cpp
 * かわいいキャラクター風の目・鼻・口アニメーション
 *
 * 【デザイン方針】
 *   スケッチのキャラクターに合わせた「大きな黒目」スタイル。
 *   - 目: 大きな黒塗り楕円 + 星型ハイライト + 下まつ毛ライン
 *   - 鼻: 中央に小さな点（・）
 *   - 口: ω型（HAPPY）/ へ型（SAD）/ 点（NORMAL）/ 横線（VOID）
 *
 * 【安全対策】
 *   Adafruit_SSD1306の fillEllipse/drawEllipse は半径が0以下になると
 *   未定義動作（クラッシュ）になるため、全描画呼び出しをSAFE_*マクロで保護。
 */

#include "eye_animation.h"
#include <math.h>

// ── 安全マクロ：半径が1未満になる描画を skip する ──────────
#define SAFE_FILL_ELLIPSE(d, x, y, r, ry, c) \
    do { if ((r) >= 1 && (ry) >= 1) (d).fillEllipse((x),(y),(r),(ry),(c)); } while(0)
#define SAFE_DRAW_ELLIPSE(d, x, y, r, ry, c) \
    do { if ((r) >= 1 && (ry) >= 1) (d).drawEllipse((x),(y),(r),(ry),(c)); } while(0)

// ══════════════════════════════════════════════════
// 内部状態（static = SRAM固定配置、タスクスタック圧迫なし）
// ══════════════════════════════════════════════════
static float         s_blinkOpen    = 1.0f;
static bool          s_blinkClosing = false;
static bool          s_blinkOpening = false;
static unsigned long s_nextBlink    = 0;

static int8_t        s_gazeX       = 0, s_gazeY     = 0;
static int8_t        s_gazeTargX   = 0, s_gazeTargY = 0;
static unsigned long s_nextGaze    = 0, s_gazeStart = 0;
static bool          s_gazeMoving  = false;

static EmotionType   s_emotion     = EMOTION_NORMAL;
static bool          s_confuseFlip = false;

static float s_winkProgress = 0.0f; // 0.0〜1.0
static bool  s_isWinking    = false;

static inline float lerpF(float a, float b, float t) {
    return a + (b - a) * t;
}

// ══════════════════════════════════════════════════
// 下まつ毛（目の下縁に3本の短いライン）
// ══════════════════════════════════════════════════
static void drawLowerLash(Adafruit_SSD1306 &dsp,
                           int cx, int cy, int r)
{
    int baseY = cy + (r - 4) - 1;

    if (baseY + 2 >= SCREEN_HEIGHT) return;

    dsp.drawLine(cx,     baseY,     cx,     baseY + 2, WHITE);
    dsp.drawLine(cx - 5, baseY - 1, cx - 5, baseY + 1, WHITE);
    dsp.drawLine(cx + 5, baseY - 1, cx + 5, baseY + 1, WHITE);
}

// ══════════════════════════════════════════════════
// 1つの目を描画
// ══════════════════════════════════════════════════
static void drawOneEye(Adafruit_SSD1306 &dsp,
                       int cx, int cy,
                       int r,  // 正円用（rx, ryを統合）
                       int8_t gx, int8_t gy,
                       float lidClose, float lidCut,
                       bool isLeft)
{
    // ── まぶたの計算 ──────────────────────────────
    float effectiveLid = constrain(lidClose + lidCut, 0.0f, 1.0f);
    int openRy = max(1, (int)(r * (1.0f - effectiveLid)));

    // 1. 閉じている時の「への字」
    if (effectiveLid > 0.8f) {
        dsp.drawLine(cx - r + 2, cy, cx, cy - 2, WHITE);
        dsp.drawLine(cx, cy - 2, cx + r - 2, cy, WHITE);
        return;
    }

    // 2. 白目 (正円)
    // r - 4 にすることで、全体をさらに一回り小さく設定
    int drawR = r - 4; 
    dsp.fillCircle(cx, cy, drawR, WHITE);

    // 3. 黒目 (白目の中に小さく配置)
    // drawR からさらに引くことで、黒目の面積を小さくする
    int iR = max(2, drawR - 8); 

    int pgx = constrain((int)gx, -(drawR - iR - 1), (drawR - iR - 1));
    int pgy = constrain((int)gy, -(drawR - iR - 1), (drawR - iR - 1));
    dsp.fillCircle(cx + pgx, cy + pgy, iR, BLACK);

    // ── 【重要】ハイライトの描画 ───────────────────
    // 視線(gx, gy)ではなく、白目の中心(cx, cy)を基準にする
    if (effectiveLid < 0.6f && openRy >= 4) {
       // メイン（右上）：分母を「2」から「4」などに大きくすると中央に寄ります
        int mainX = cx + (r / 4); 
        int mainY = cy - (openRy / 4);
        dsp.fillCircle(mainX, mainY, 2, WHITE); 
        
        // サブ（左下）：分母を「3」から「6」などに大きくすると中央に寄ります
        int subX = cx - (r / 6);
        int subY = cy + (openRy / 6);
        dsp.fillCircle(subX, subY, 1, WHITE);
    }

    // ── まぶた（上から黒矩形で閉じを表現）────────
    if (lidClose > 0.02f) {
        int lidH = max(1, (int)(r * 2.0f * effectiveLid));
        int lidTop = cy - r - 1;
        dsp.fillRect(cx - r - 1, lidTop,
                     r * 2 + 3, lidH + 1, BLACK);
        // まぶたの下縁を白ラインで
        int lidBottom = lidTop + lidH;
        if (lidBottom < SCREEN_HEIGHT) {
            dsp.drawFastHLine(cx - r, lidBottom, r * 2, WHITE);
        }
    }

    // ── 下まつ毛 ─────────────────────────────────
    if (effectiveLid < 0.5f && openRy >= 3) {
        drawLowerLash(dsp, cx, cy, r);
    }

    // // ── 眉 ───────────────────────────────────────
    // if (drawBrow) {
    //     int browY  = cy - ry - 4;
    //     if (browY < 1) browY = 1;
    //     int browX1 = cx - r + 2;
    //     int browX2 = cx + r - 2;
    //     int dy = (int)browTilt;
    //     // 左目(cx<64)と右目で傾き方向を対称に
    //     int yL = (cx < SCREEN_WIDTH / 2) ? browY + dy : browY - dy;
    //     int yR = (cx < SCREEN_WIDTH / 2) ? browY - dy : browY + dy;
        
    //     dsp.drawLine(browX1, yL , browX2, yR , WHITE);
    // }
}

// ══════════════════════════════════════════════════
// 鼻（中央・目の下に点）
// ══════════════════════════════════════════════════
static void drawNose(Adafruit_SSD1306 &dsp) {
    int nx = SCREEN_WIDTH / 2;
    int ny = EYE_CENTER_Y + 12;
    if (ny < SCREEN_HEIGHT) {
        dsp.fillCircle(nx, ny, 1, WHITE);
    }
}

// ══════════════════════════════════════════════════
// 口（感情によって変化）
// ══════════════════════════════════════════════════
static void drawMouth(Adafruit_SSD1306 &dsp, EmotionType emotion) {
    const int mx = SCREEN_WIDTH / 2;
    const int my = 52;

    if (my >= SCREEN_HEIGHT) return;

    switch (emotion) {
        case EMOTION_HAPPY:
            // ω: 小さなW型
            dsp.drawLine(mx - 4, my - 1, mx - 2, my + 1, WHITE);
            dsp.drawLine(mx - 2, my + 1, mx,     my - 1, WHITE);
            dsp.drawLine(mx,     my - 1, mx + 2, my + 1, WHITE);
            dsp.drawLine(mx + 2, my + 1, mx + 4, my - 1, WHITE);
            break;

        case EMOTION_SURPRISED:
        case EMOTION_RAINY:
        case EMOTION_WORRIED:
            // 下カーブ（悲しい口）
            dsp.drawLine(mx - 4, my + 2, mx + 4, my + 2,WHITE);
            // 左右の端に点を打つ（drawPixel は引数が3つでOK）
            dsp.drawPixel(mx - 5, my + 1, WHITE);
            dsp.drawPixel(mx + 5, my + 1, WHITE);
            break;


        default:
            // 横線「ー」に変更
            dsp.drawFastHLine(mx - 4, my, 8, WHITE);
            break;
    }
}

// ══════════════════════════════════════════════════
// 感情ごとのパラメータ
// ══════════════════════════════════════════════════
struct EyeParam {
    float  lidClose;   //基本の閉じ度 (0.0f: 全開, 0.4f: 半目, 1.0f: 閉)
    float  lidCut;     // ← これを追加！
    bool   drawBrow;   // 角度
    // int8_t browTilt;   // 角度
    bool   tearLeft;
    bool   tearRight;
    float  gazeScale;
    // int8_t browOffset; // 正の値で上に、負で下に
};

static EyeParam getEyeParam(EmotionType e) {
    switch (e) {
        // 既存の数値の間に 0.0f を追加しました
        case EMOTION_HAPPY:    return { 0.15f, 0.0f, false, false, false, 0.5f };
        case EMOTION_ANGRY:    return { 0.1f,  0.0f, true,  false, false, 0.8f };
        case EMOTION_SLEEPY:   return { 0.1f,  0.0f, false, false, false, 0.2f }; // ここだけ「けだるげ」に調整
        case EMOTION_CONFUSED: return { 0.2f,  0.0f, false,  false, false, 0.6f };
        case EMOTION_WORRIED:  return { 0.0f,  0.0f, true,  false, false, 0.4f };
        case EMOTION_SURPRISED:return { 0.0f, -0.2f, true,   false, false, 1.2f };
        case EMOTION_RAINY:    return { 0.3f,  0.0f, false,  true,  false, 0.3f };
        default:               return { 0.0f,  0.0f, false,  false, false, 1.0f };
    }
}

// ══════════════════════════════════════════════════
// 公開関数
// ══════════════════════════════════════════════════
void eyeInit(Adafruit_SSD1306 &display) {
    randomSeed(analogRead(A0));
    unsigned long now = millis();
    s_nextBlink   = now + random(BLINK_INTERVAL_MIN, BLINK_INTERVAL_MAX);
    s_nextGaze    = now + random(GAZE_INTERVAL_MIN,  GAZE_INTERVAL_MAX);
    s_confuseFlip = false;
    (void)display;
}

void eyeUpdate(Adafruit_SSD1306 &display, EmotionType emotion) {
    unsigned long now = millis();

    // ── 感情更新 ──────────────────────────────────
    if (emotion != s_emotion) {
        s_emotion = emotion;
        if (s_emotion == EMOTION_CONFUSED) {
            s_confuseFlip = (bool)(random(0, 2));
        }
    }

    EyeParam p = getEyeParam(s_emotion);

    // ── まばたき ──────────────────────────────────
    // ── 2. まばたき計算（ウィンク中は停止） ─────────────
    bool canBlink = (s_emotion != EMOTION_NORMAL && s_emotion != EMOTION_SLEEPY);
    if (canBlink) {
        if (!s_blinkClosing && !s_blinkOpening && now >= s_nextBlink) s_blinkClosing = true;
        if (s_blinkClosing) {
            s_blinkOpen -= 0.22f;
            if (s_blinkOpen <= 0.0f) { s_blinkOpen = 0.0f; s_blinkClosing = false; s_blinkOpening = true; }
        } else if (s_blinkOpening) {
            s_blinkOpen += 0.22f;
            if (s_blinkOpen >= 1.0f) { s_blinkOpen = 1.0f; s_blinkOpening = false; s_nextBlink = now + random(2000, 5000); }
        }
    } else {
        s_blinkOpen = 1.0f;
    }

    // ── 3. 最終的な「目閉じ度」決定 ──────────────────
    // まばたきによる閉じ度
    float blinkLid = lerpF(p.lidClose, 1.0f, 1.0f - s_blinkOpen);
    float lidL = blinkLid;
    float lidR = blinkLid;

    // ── 視線移動 ──────────────────────────────────
    bool gazeEnabled = (s_emotion != EMOTION_NORMAL && s_emotion != EMOTION_SLEEPY);

    if (gazeEnabled && !s_gazeMoving && now >= s_nextGaze) {
        int r = max(1, (int)(PUPIL_MOVE_RANGE * p.gazeScale));
        s_gazeTargX  = (int8_t)random(-r, r + 1);
        s_gazeTargY  = (int8_t)random(-r / 2, r / 2 + 1);
        s_gazeStart  = now;
        s_gazeMoving = true;
    }
    if (s_gazeMoving) {
        float t    = min((float)(now - s_gazeStart) / 350.0f, 1.0f);
        float ease = 1.0f - (1.0f - t) * (1.0f - t);
        s_gazeX = (int8_t)lerpF((float)s_gazeX, (float)s_gazeTargX, ease);
        s_gazeY = (int8_t)lerpF((float)s_gazeY, (float)s_gazeTargY, ease);
        if (t >= 1.0f) {
            s_gazeMoving = false;
            s_nextGaze = now + random(GAZE_INTERVAL_MIN, GAZE_INTERVAL_MAX);
        }
    }
    if (!gazeEnabled) {
        s_gazeX = (int8_t)lerpF((float)s_gazeX, 0.0f, 0.15f);
        s_gazeY = (int8_t)lerpF((float)s_gazeY, 0.0f, 0.15f);
    }

    // ── 描画 ─────────────────────────────────────
    int r = EYE_RADIUS_X;
    int ry = EYE_RADIUS_Y_MAX;

    display.clearDisplay();
    // 左目(isLeft=true): 眉毛引数を削除
    drawOneEye(display, EYE_L_CENTER_X, EYE_CENTER_Y,
               r, s_gazeX, s_gazeY,
               lidL, p.lidCut, true);

    // 右目(isLeft=false): 眉毛引数を削除
    drawOneEye(display, EYE_R_CENTER_X, EYE_CENTER_Y,
               r, s_gazeX, s_gazeY,
               lidR, p.lidCut, false);
    
    // 涙
    if (p.tearLeft) {
        int ty = EYE_CENTER_Y + r + 3;
        if (ty + 3 < SCREEN_HEIGHT) {
            display.fillCircle(EYE_L_CENTER_X - 8, ty,     1, WHITE);
            display.fillCircle(EYE_L_CENTER_X - 9, ty + 3, 1, WHITE);
        }
    }
    if (p.tearRight) {
        int ty = EYE_CENTER_Y + r + 3;
        if (ty + 3 < SCREEN_HEIGHT) {
            display.fillCircle(EYE_R_CENTER_X + 8, ty,     1, WHITE);
            display.fillCircle(EYE_R_CENTER_X + 9, ty + 3, 1, WHITE);
        }
    }

    drawNose(display);
    drawMouth(display, s_emotion);

    display.display();
}
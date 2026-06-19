/**
 * 【eye_animation.cppのテスト用コード】
 * かわいいキャラクター風の目・鼻・口アニメーション作成用単体コード
 * このコードだけで書き込み実行可能
 * 表情変更や確認、微修正に使用する用のコード
 *
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── OLED物理設定 ────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── 各種設定値（目のちや瞬きのタイミングなど） ─────────────
#define EYE_CENTER_Y        28
#define EYE_L_CENTER_X      36
#define EYE_R_CENTER_X      92
#define EYE_RADIUS_X        22
#define PUPIL_MOVE_RANGE     6
#define BLINK_INTERVAL_MIN 2000  
#define BLINK_INTERVAL_MAX 5000  
#define GAZE_INTERVAL_MIN  1500
#define GAZE_INTERVAL_MAX  4000

// ── 感情タイプ ──────────────────────────────────────
typedef enum {
    EMOTION_NORMAL = 0,
    EMOTION_HAPPY,
    EMOTION_ANGRY,
    EMOTION_SAD,         
    EMOTION_CONFUSED,
    EMOTION_SURPRISED,
    EMOTION_SLEEPY,
    EMOTION_COUNT
} EmotionType;

// ── 感情ごとの描画パラメータ構造体の定義 ─────────────────
struct EyeParam {
    float  lidClose;   // 瞼の閉じ具合（0:開, 1:閉）
    float  lidCut;     // タレ目/つり目の補正値
    bool   tearLeft;   // 涙や頬の表示フラグ
    bool   tearRight;  
    float  gazeScale;  // 視線の可動範囲倍率
};

// ── 内部状態管理（アニメーション用変数） ─────────────────
static float         s_blinkOpen    = 1.0f;     
static bool          s_blinkClosing = false;    
static bool          s_blinkOpening = false;    
static bool          s_blinkHolding = false;    // ★目を閉じているキープ状態
static unsigned long s_blinkHoldStart = 0;      // ★閉じ始めた時間の記録
static unsigned long s_nextBlink    = 0;        

static int8_t        s_gazeX       = 0, s_gazeY     = 0; 
static int8_t        s_gazeTargX   = 0, s_gazeTargY = 0; 
static unsigned long s_nextGaze    = 0, s_gazeStart = 0; 
static bool          s_gazeMoving  = false;              

static EmotionType   s_emotion     = EMOTION_NORMAL;
static unsigned long s_emotionStartTime = 0;

// 滑らかな変化用の現在値キャッシュ
static float         s_curLidClose = 0.0f;
static float         s_curLidCut   = 0.0f;
static float         s_curGazeScale= 1.0f;

static float         s_tearOffset  = 0.0f;
static bool          s_isCrying    = false;

// 【線形補完関数】
static inline float lerpF(float a, float b, float t) {
    return a + (b - a) * t;
}

// ══════════════════════════════════════════════════
// ほっぺ（照れ線）
// ══════════════════════════════════════════════════
static void drawCheek(Adafruit_SSD1306 &dsp, int cx, int cy, int r) {
    int cheekY = cy + r + 2;
    if (cheekY + 4 >= SCREEN_HEIGHT) return; 

    int offset = (cx < SCREEN_WIDTH / 2) ? -8 : 8;
    int centerX = cx + offset;
    
    // 短い斜線を組み合わせて照れ線を描画
    dsp.drawLine(centerX - 4, cheekY,     centerX - 3, cheekY + 3, WHITE); 
    dsp.drawLine(centerX - 3, cheekY,     centerX - 2, cheekY + 3, WHITE); 
    dsp.drawLine(centerX - 1, cheekY - 1, centerX,     cheekY + 2, WHITE); 
    dsp.drawLine(centerX,     cheekY - 1, centerX + 1, cheekY + 2, WHITE); 
    dsp.drawLine(centerX + 2, cheekY,     centerX + 3, cheekY + 3, WHITE); 
    dsp.drawLine(centerX + 3, cheekY,     centerX + 4, cheekY + 3, WHITE); 
}

// ══════════════════════════════════════════════════
// 感情ごとのパラメータ設定テーブル
// ══════════════════════════════════════════════════
static EyeParam getEyeParam(EmotionType e) {
    switch (e) {
        case EMOTION_NORMAL:   return { 0.0f,   0.0f,  true,   true,    1.0f }; // ここをtrueに変更
        case EMOTION_HAPPY:    return { 0.0f,   0.0f,  true,   true,    0.8f }; 
        case EMOTION_ANGRY:    return { 0.35f,  0.0f,  true,   true,    0.8f }; // 三角カットを廃止したため、まぶたを少し下げて怒りっぽく
        case EMOTION_SAD:      return { 0.35f,  0.0f,  true,   true,    0.4f }; // 三角カットを廃止したため、まぶたを少し下げて悲しげに
        case EMOTION_CONFUSED: return { 0.1f,   0.0f,  false,  false,   0.8f }; 
        case EMOTION_SURPRISED:return { 0.0f,  -0.2f,  false,  false,   1.2f }; 
        case EMOTION_SLEEPY:   return { 0.5f,   0.0f,  false,  false,   0.3f }; // うとうと時：50%閉じたトロンとした半目
        default:               return { 0.0f,   0.0f,  false,  true,   1.0f }; 
    }
}

// ══════════════════════════════════════════════════
// 1つの目を描画
// ══════════════════════════════════════════════════
static void drawOneEye(Adafruit_SSD1306 &dsp, int cx, int cy, int r, 
                       int8_t gx, int8_t gy, float lidClose, float lidCut,
                       bool isLeft, bool showAngryEye) 
{
    float effectiveLid = constrain(lidClose + lidCut, 0.0f, 1.0f);
    
    
    // まぶたが完全に閉じている時の処理（スリープ時やしっかり閉じた瞬き時）
    if (effectiveLid > 0.9f) {
        dsp.drawLine(cx - r + 2, cy, cx, cy - 2, WHITE);
        dsp.drawLine(cx, cy - 2, cx + r - 2, cy, WHITE);
        return;
    }

    // 基本の白目の正円
    int drawR = r - 4; 
    dsp.fillCircle(cx, cy, drawR, WHITE);
    
    // 感情ごとの形状加工（怒り・悲しい）
    if (s_emotion == EMOTION_ANGRY && showAngryEye) {
        if (isLeft) {
            // 目の上部を三角に削ることで吊り上げる
            dsp.fillTriangle(cx - drawR, cy - drawR, cx + drawR, cy - drawR, cx + drawR, cy - 2, BLACK); 
        } else {
            dsp.fillTriangle(cx - drawR, cy - drawR, cx + drawR, cy - drawR, cx - drawR, cy - 2, BLACK); 
        }
    }

    // ─── ★怒り（ANGRY）の時のツリ目カット（エラー修正済） ───
    if (s_emotion == EMOTION_CONFUSED) {
        if (isLeft) {
            dsp.fillTriangle(cx - drawR - 1, cy - drawR - 1, 
                             cx + drawR + 1, cy - drawR - 1, 
                             cx - drawR - 1, cy - 4, BLACK);
            dsp.drawLine(cx - drawR, cy - 4, cx + drawR, cy - drawR, WHITE);
        } else {
            dsp.fillTriangle(cx - drawR - 1, cy - drawR - 1, 
                             cx + drawR + 1, cy - drawR - 1, 
                             cx + drawR + 1, cy - 4, BLACK);
            dsp.drawLine(cx - drawR, cy - drawR, cx + drawR, cy - 4, WHITE);
        }
    }

    // 黒目の描画（視線移動gx, gyを反映）
    int iR = max(2, drawR - 8); 
    int pgx = constrain((int)gx, -(drawR - iR - 1), (drawR - iR - 1));
    int pgy = constrain((int)gy, -(drawR - iR - 1), (drawR - iR - 1));
    
    if (s_emotion == EMOTION_SLEEPY) {
        pgx = constrain(pgx, -2, 2);
        pgy = constrain(pgy, 2, 4); // 黒目を下に固定してトロンと感を出す
    }
    
    dsp.fillCircle(cx + pgx, cy + pgy, iR, BLACK);

    // ハイライトの描画
    int openRy = max(1, (int)(r * (1.0f - effectiveLid)));
    if (effectiveLid < 0.7f && openRy >= 4) {
        if (s_emotion == EMOTION_HAPPY) {
            // 【ハッピー：十字星4点きらきらハイライト】瞳の中が光り輝きます
            dsp.fillCircle(cx + (r / 4),     cy - (openRy / 4), 2, WHITE); // 主光（右上）
            dsp.fillCircle(cx - (r / 6),     cy + (openRy / 6), 1, WHITE); // 副光1（左下）
            dsp.fillCircle(cx + (r / 4) - 3, cy + (openRy / 4), 1, WHITE); // 副光2（右下）
            dsp.fillCircle(cx - (r / 6),     cy - (openRy / 4) + 1, 1, WHITE); // 追加（左上）
        } else {
            // 通常時の2重ハイライト
            dsp.fillCircle(cx + (r / 4), cy - (openRy / 4), 2, WHITE); 
            dsp.fillCircle(cx - (r / 6), cy + (openRy / 6), 1, WHITE);
        }
    }

    // まぶた（上から下に降りてくる黒塗り）
    if (effectiveLid > 0.02f) {
        int lidH = max(1, (int)(r * 2.0f * effectiveLid));
        dsp.fillRect(cx - r - 1, cy - r - 1, r * 2 + 3, lidH + 1, BLACK);
        if (cy - r - 1 + lidH < SCREEN_HEIGHT) {
            dsp.drawFastHLine(cx - r, cy - r - 1 + lidH, r * 2, WHITE);
        }
    }
}

// ══════════════════════════════════════════════════
// 鼻
// ══════════════════════════════════════════════════
static void drawNose(Adafruit_SSD1306 &dsp) {
    int nx = SCREEN_WIDTH / 2;
    int ny = EYE_CENTER_Y + 12;
    if (ny < SCREEN_HEIGHT) {
        dsp.fillCircle(nx, ny, 1, WHITE);
    }
}

// ══════════════════════════════════════════════════
// 口（★ハッピー時：ぷっくりω口に戻しました）
// ══════════════════════════════════════════════════
static void drawMouth(Adafruit_SSD1306 &dsp, EmotionType emotion) {
    const int mx = SCREEN_WIDTH / 2;
    const int my = 50; 

    if (my >= SCREEN_HEIGHT) return;

    switch (emotion) {
        case EMOTION_HAPPY: // ★【ハッピー：元の可愛いω口に完全復帰】
            dsp.drawPixel(mx - 5, my,     WHITE);
            dsp.drawPixel(mx - 4, my + 2, WHITE);
            dsp.drawPixel(mx - 3, my + 3, WHITE);
            dsp.drawPixel(mx - 2, my + 3, WHITE);
            dsp.drawPixel(mx - 1, my + 1, WHITE);
            dsp.drawPixel(mx,     my,     WHITE); 

            dsp.drawPixel(mx + 5, my,     WHITE);
            dsp.drawPixel(mx + 4, my + 2, WHITE);
            dsp.drawPixel(mx + 3, my + 3, WHITE);
            dsp.drawPixel(mx + 2, my + 3, WHITE);
            dsp.drawPixel(mx + 1, my + 1, WHITE);
            break;

        case EMOTION_ANGRY: 
        case EMOTION_CONFUSED:
            dsp.drawLine(mx - 5, my + 3, mx,     my,     WHITE); 
            dsp.drawLine(mx,     my,     mx + 5, my + 3, WHITE); 
            dsp.drawPixel(mx,    my + 1, WHITE); 
            break;

        case EMOTION_SAD: 
            dsp.drawLine(mx - 4, my + 2, mx + 4, my + 2, WHITE);
            dsp.drawPixel(mx - 5, my + 1, WHITE);
            dsp.drawPixel(mx + 5, my + 1, WHITE);
            break;

        case EMOTION_SURPRISED: 
            dsp.drawCircle(mx, my + 1, 3, WHITE);
            break;

        case EMOTION_SLEEPY:
            dsp.drawCircle(mx, my + 1, 1, WHITE);
            break;

        default: 
            dsp.drawFastHLine(mx - 4, my, 8, WHITE);
            break;
    }
}

// ══════════════════════════════════════════════════
// 更新処理ループ（状態更新と描画の実行）
// ══════════════════════════════════════════════════
void eyeUpdate(Adafruit_SSD1306 &dsp, EmotionType emotion) {
    unsigned long now = millis();

    if (emotion != s_emotion) {
        s_emotion = emotion;
        s_emotionStartTime = now;
    }

    // 目標パラメータ
    EyeParam target = getEyeParam(s_emotion);

    // 滑らかな変化（モーフィング）
    float morphRate = (float)(now - s_emotionStartTime) / 300.0f;
    if (morphRate > 1.0f) morphRate = 1.0f;

    s_curLidClose  = lerpF(s_curLidClose,  target.lidClose,  morphRate);
    s_curLidCut    = lerpF(s_curLidCut,    target.lidCut,    morphRate);
    s_curGazeScale = lerpF(s_curGazeScale, target.gazeScale, morphRate);

    // ─── まばたきの計算（★うとうと時の「閉じキープ」処理を追加） ───
    unsigned long minBlinkInt = (s_emotion == EMOTION_SLEEPY) ? 3000 : BLINK_INTERVAL_MIN;
    unsigned long maxBlinkInt = (s_emotion == EMOTION_SLEEPY) ? 6000 : BLINK_INTERVAL_MAX;
    
    // 閉じる・開く速度（うとうと時は通常0.22fのところ、0.07fまで落として超ゆっくりに）
    float blinkSpeed = (s_emotion == EMOTION_SLEEPY) ? 0.04f : 0.22f; 

    if (!s_blinkClosing && !s_blinkOpening && !s_blinkHolding && now >= s_nextBlink) {
        s_blinkClosing = true;
    }

    if (s_blinkClosing) {
        s_blinkOpen -= blinkSpeed;
        if (s_blinkOpen <= 0.0f) { 
            s_blinkOpen = 0.0f; 
            s_blinkClosing = false; 
            s_blinkOpening = true; // 閉じた直後にすぐ開く処理へ（キープ状態を削除）
        }
    } 
    else if (s_blinkOpening) {
        s_blinkOpen += blinkSpeed;
        if (s_blinkOpen >= 1.0f) { 
            s_blinkOpen = 1.0f; 
            s_blinkOpening = false; 
            s_nextBlink = now + random(minBlinkInt, maxBlinkInt); 
        }
    }

    // 視線移動
    if (now >= s_nextGaze) {
        s_gazeMoving = true;
        s_gazeStart = now;
        s_gazeTargX = random(-PUPIL_MOVE_RANGE, PUPIL_MOVE_RANGE + 1) * s_curGazeScale;
        s_gazeTargY = random(-3, 4) * s_curGazeScale;
        s_nextGaze = now + random(GAZE_INTERVAL_MIN, GAZE_INTERVAL_MAX);
    }
    if (s_gazeMoving) {
        float t = (float)(now - s_gazeStart) / 150.0f; 
        if (t >= 1.0f) {
            s_gazeX = s_gazeTargX; s_gazeY = s_gazeTargY; s_gazeMoving = false;
        } else {
            s_gazeX = lerpF(s_gazeX, s_gazeTargX, t); s_gazeY = lerpF(s_gazeY, s_gazeTargY, t);
        }
    }

    float blinkLid = lerpF(s_curLidClose, 1.0f, 1.0f - s_blinkOpen);

    if (s_emotion == EMOTION_SAD) {
        s_isCrying = true; s_tearOffset += 0.5f;
        if (s_tearOffset > 12.0f) s_tearOffset = 0.0f;
    } else {
        s_isCrying = false;
    }

    // ─── 描画 ───
    int r = EYE_RADIUS_X;
    dsp.clearDisplay(); 

    // 左右の目を描画

    drawOneEye(dsp, EYE_L_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X, s_gazeX, s_gazeY, blinkLid, s_curLidCut, true, true);
    drawOneEye(dsp, EYE_R_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X, s_gazeX, s_gazeY, blinkLid, s_curLidCut, false, true);

    // 特殊パーツ（涙・ほっぺ）
    if (target.tearLeft || target.tearRight) { 
        if (s_emotion == EMOTION_SAD && s_isCrying) {
            int ty = EYE_CENTER_Y + r + 2 + (int)s_tearOffset;
            if (ty + 3 < SCREEN_HEIGHT) {
                if (target.tearLeft)  dsp.fillCircle(EYE_L_CENTER_X - 8, ty, 1, WHITE);
                if (target.tearRight) dsp.fillCircle(EYE_R_CENTER_X + 8, ty, 1, WHITE);
            }
        } 
        else if (s_emotion == EMOTION_HAPPY || s_emotion == EMOTION_NORMAL) {
            if (target.tearLeft)  drawCheek(dsp, EYE_L_CENTER_X, EYE_CENTER_Y, r);
            if (target.tearRight) drawCheek(dsp, EYE_R_CENTER_X, EYE_CENTER_Y, r);
        }
    }

    drawNose(dsp);
    drawMouth(dsp, s_emotion); 
    dsp.display();
}

// ── Arduino標準設定 ─────────────────────────────────
static EmotionType s_currentTestRoute = EMOTION_NORMAL;

void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(A0));

    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        for(;;);
    }
    
    display.clearDisplay();
    display.display();

    Serial.println(F("\n=== ベラちゃん 表情システム（うとうとディレイ＋ω口復活版） ==="));
}

void loop() {
    while (Serial.available() > 0) {
        char incoming = Serial.read();
        if (incoming == '\n' || incoming == '\r' || incoming == ' ') continue; 

        if (incoming >= '0' && incoming <= '6') {
            s_currentTestRoute = (EmotionType)(incoming - '0');
            Serial.print(F("★感情切り替え -> "));
            Serial.println((int)s_currentTestRoute);
        }
    }

    eyeUpdate(display, s_currentTestRoute);
    delay(10);
} 
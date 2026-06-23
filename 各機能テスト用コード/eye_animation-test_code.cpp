#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── OLED物理設定 ────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── 各種設定値 (全体のバランス調整)─────────────────────
#define EYE_CENTER_Y        28  // 目の中心のY座標（上下位置）、数値を大きくすると下に移動
#define EYE_L_CENTER_X      36  // 左目の中心のX座標
#define EYE_R_CENTER_X      92  // 右目の中心のY座標
#define EYE_RADIUS_X        22  // 目の基本サイズ（半径）。数字をさらに大きくするとよりデカ目になる
#define PUPIL_MOVE_RANGE     6  // 黒目がキョロキョロ動く最大ピクセル範囲
#define BLINK_INTERVAL_MIN 2000 // 通常時の瞬き間隔の最短（2秒） 
#define BLINK_INTERVAL_MAX 5000 // 通常時の瞬き間隔の最長（5秒）
#define GAZE_INTERVAL_MIN  1500 // 視線が動く感覚の最短（1.5秒）
#define GAZE_INTERVAL_MAX  4000 // 視線が動く間隔の最長（4秒）

// ── 感情タイプの一覧（0~5の数字に対応） ────────────────
typedef enum {
    EMOTION_NORMAL = 0,    // 通常（丸目、キョロキョロ）
    EMOTION_HAPPY,         // 嬉しい（キラキラの瞳） 
    EMOTION_ANGRY,         // 怒り（吊り上がった瞳、白目を内側へ斜めカット）
    EMOTION_SAD,           // 悲しみ（目尻の垂れ下がった悲しそうな瞳、白目を外側へ斜めカット、涙にウルウルの瞳）
    EMOTION_CONFUSED,      // ジト目（瞼が少し下がった状態）
    EMOTION_SLEEPY,        // うとうと（うとうとと閉じかけて、たまに開いてまたうとうとする）
    EMOTION_COUNT
} EmotionType;

// ── 感情ごとのベース形状を決める構造体の定義 ─────────────
struct EyeParam {
    float  lidClose;   // 基本の瞼の閉じ具合（0.0 = 全開 ~ 1.0 = 全閉）
    float  lidCut;     // 形の補正用
    bool   tearLeft;   // 左目の下（涙or頬の照れ線）の描画の有無（true = あり、false = なし）
    bool   tearRight;  // 右目の下（涙or頬の照れ線）の描画の有無（true = あり、false = なし）
    float  gazeScale;  // 黒目の可動範囲の倍率（sleepyの時などは動きを小さくする）
};

// ── 内部状態管理（アニメーションの進捗など）──────────────
static float         s_blinkOpen    = 1.0f;     // まばたきの開き度(1.0=開、0.0=閉)
static bool          s_blinkClosing = false;    // いま「目を閉じている最中」か
static bool          s_blinkOpening = false;    // いま「目を開けている最中」か
static bool          s_blinkHolding = false;    // いま「目を閉じたままキープ中」か（sleepy用）
static unsigned long s_blinkHoldStart = 0;      // 目を閉じ始めた時刻
static unsigned long s_nextBlink    = 0;        // 次にまばたきをする時刻

static int8_t        s_gazeX       = 0, s_gazeY     = 0;    // 現在の黒目オフセット（X, Y）
static int8_t        s_gazeTargX   = 0, s_gazeTargY = 0;    // 黒目の移動先ターゲット
static unsigned long s_nextGaze    = 0, s_gazeStart = 0;    // 視線移動のタイマー
static bool          s_gazeMoving  = false;                 // 視線移動中かどうか

static EmotionType   s_emotion     = EMOTION_NORMAL;        // 現在の感情
static unsigned long s_emotionStartTime = 0;                // 感情が変わった時刻

static int           s_sleepyBlinkCount = 0;       // うとうと時の連続まばたきの回数
static bool          s_isDeepSleeping   = false; 

static float         s_curLidClose = 0.0f;      // じわっと動く現在の瞼位置
static float         s_curLidCut   = 0.0f;
static float         s_curGazeScale= 1.0f;

static float         s_tearOffset  = 0.0f;     // 涙がボロボロ落ちるアニメーション用
static bool          s_isCrying    = false;

// 2つの値の間をスムーズに補完する関数（イージング）
static inline float lerpF(float a, float b, float t) {
    return a + (b - a) * t;
}

// ══════════════════════════════════════════════════
// ほっぺ（照れ線）
// ══════════════════════════════════════════════════
static void drawCheek(Adafruit_SSD1306 &dsp, int cx, int cy, int r) {
    int cheekY = cy + r + 2;    // 目のすぐ下に配置
    if (cheekY + 4 >= SCREEN_HEIGHT) return; 

    int offset = (cx < SCREEN_WIDTH / 2) ? -8 : 8; // 左右で外側にずらす
    int centerX = cx + offset;
    
    // 斜めの３本線を描画
    dsp.drawLine(centerX - 4, cheekY,     centerX - 3, cheekY + 3, WHITE); 
    dsp.drawLine(centerX - 3, cheekY,     centerX - 2, cheekY + 3, WHITE); 
    dsp.drawLine(centerX - 1, cheekY - 1, centerX,     cheekY + 2, WHITE); 
    dsp.drawLine(centerX,     cheekY - 1, centerX + 1, cheekY + 2, WHITE); 
    dsp.drawLine(centerX + 2, cheekY,     centerX + 3, cheekY + 3, WHITE); 
    dsp.drawLine(centerX + 3, cheekY,     centerX + 4, cheekY + 3, WHITE); 
}

// ══════════════════════════════════════════════════
// 感情ごとのパラメータ設定テーブル
// 【ここを変更すると、各表情のデフォルトの目の開き具合を変更可能】
// ══════════════════════════════════════════════════
static EyeParam getEyeParam(EmotionType e) {
    switch (e) {
        //                      【A:まぶた】 【B:補正】【C:左下】【D:右下】【E:黒目範囲】
        case EMOTION_NORMAL:   return { 0.0f,   0.0f,  true,   true,    1.0f }; // 通常（目ぱっちり）
        case EMOTION_HAPPY:    return { 0.0f,   0.0f,  true,   true,    1.0f }; // 嬉しい（目ぱっちり）

        // 怒りと悲しいのベースのまぶた位置（A）を少し下げている
        // もしもっと目を見開いた状態から怒らせたい場合は、ここを 0.0f や 0.2f に変更がオススメ
        case EMOTION_ANGRY:    return { 0.4f,   0.0f,  false,  false,   1.0f }; // 怒り
        case EMOTION_SAD:      return { 0.35f,  0.0f,  true,   true,    0.8f }; // 悲しみ（しょぼん）
        
        // 1つ目の数値「0.15f」を大きくする（例: 0.35f）と、さらに細められた強いジト目になり、反対に 0.05f などにすると、通常に近い、かすかなジト目になる
        case EMOTION_CONFUSED: return { 0.25f,  0.0f,  false,  false,   0.6f }; // 混乱・ジト目
        case EMOTION_SLEEPY:    // 眠い・うとうと
            if (s_sleepyBlinkCount == 0) {
                s_sleepyBlinkCount = random(3, 6); // うとうとする回数をランダムで決定
            }
            return { 0.45f,   0.0f,  false,  false,   0.3f }; // 常にうとうとベースのまぶたに（半分閉じた状態）
        default:               return { 0.0f,   0.0f,  true,  true,    1.0f };  // 通常と同様
    }
}

// ══════════════════════════════════════════════════
// 1つの目を描画するメインロジック
// ══════════════════════════════════════════════════
static void drawOneEye(Adafruit_SSD1306 &dsp, int cx, int cy, int r, 
                       int8_t gx, int8_t gy, float lidClose, float lidCut,
                       bool isLeft, bool showAngryEye) 
{
    // 瞼の総合的な閉じ具合（0.0 = 全開 ~ 1.0 = 全閉）
    float effectiveLid = constrain(lidClose + lidCut, 0.0f, 1.0f);
    int drawR = r - 4;  // フチにゆとりを持たせる内径
    
    // ─── 【状態A】まぶたが完全に閉じている時の形状 ───
    if (effectiveLid > 0.9f) {
        if (s_emotion == EMOTION_SLEEPY) {
            // うとうとで目を閉じた時の目の線画
            int x_offset = r - 4; 
            int sleepY = cy + (drawR / 2) - 1; 
            dsp.drawLine(cx - x_offset,     sleepY - 4, cx - x_offset + 3, sleepY - 1, WHITE);
            dsp.drawLine(cx - x_offset + 3, sleepY - 1, cx - x_offset + 7, sleepY + 1, WHITE);
            dsp.drawFastHLine(cx - x_offset + 7, sleepY + 1, (x_offset - 7) * 2 + 1, WHITE);
            dsp.drawLine(cx + x_offset - 7, sleepY + 1, cx + x_offset - 3, sleepY - 1, WHITE);
            dsp.drawLine(cx + x_offset - 3, sleepY - 1, cx + x_offset,     sleepY - 4, WHITE);     
        } else {
            // 通常のまばたき時：一般的な横一本（への字型）の閉じた目
            dsp.drawLine(cx - r + 2, cy, cx, cy - 2, WHITE);
            dsp.drawLine(cx, cy - 2, cx + r - 2, cy, WHITE);
        }
        return; // 閉じているときは白目や黒目は描かないのでここで終了
    }

    // ─── 【状態B】目が開いている時の描画 ───
    // 1. ベースの白い丸目
    dsp.fillCircle(cx, cy, drawR, WHITE);
    
    // 2. 特殊な感情による白目カット（ANGRY / SAD）
    // ★まぶたが大きく閉じている間（瞬き中）は斜めカットを非表示にして滑らかに接続！
    if (s_emotion == EMOTION_ANGRY && showAngryEye && effectiveLid < 0.45f) { // 怒り
        if (isLeft) {
            // 左目：目頭側（右側）の上の黒い三角で削ってつり目に
            dsp.fillRect(cx - drawR - 1, cy - drawR - 2, drawR * 2 + 3, 2, BLACK);
            dsp.fillTriangle(cx - drawR - 1, cy - drawR, cx + drawR + 2, cy - drawR, cx + drawR + 2, cy - (drawR / 3), BLACK); 
        } else {
            // 右目：目頭側（左側）の上を黒い三角で削ってつり目に
            dsp.fillRect(cx - drawR - 1, cy - drawR - 2, drawR * 2 + 3, 2, BLACK);
            dsp.fillTriangle(cx - drawR - 1, cy - drawR, cx - drawR - 1, cy - (drawR / 3), cx + drawR + 2, cy - drawR, BLACK); 
        }
    } 
    else if (s_emotion == EMOTION_SAD && effectiveLid < 0.45f) {    // 悲しみ
        int shortOffset = drawR / 3;
        if (isLeft) {
            // 左目：外側（左側）の上を削って困ったタレ目に
            dsp.fillRect(cx - drawR - 1, cy - drawR - 2, drawR * 2 + 3, 2, BLACK);
            dsp.fillTriangle(cx - drawR - 1, cy - (drawR / 2.2), cx - drawR - 1, cy - drawR, cx + drawR + 2 - shortOffset, cy - drawR, BLACK); 
        } else {
            // 右目：外側（右側）の上を削って困ったタレ目に
            dsp.fillRect(cx - drawR - 1, cy - drawR - 2, drawR * 2 + 3, 2, BLACK);
            dsp.fillTriangle(cx - drawR - 1 + shortOffset, cy - drawR, cx + drawR + 2, cy - drawR, cx + drawR + 2, cy - (drawR / 2.2), BLACK); 
        }
    }

    // 3. 黒目（瞳）の計算と描画
    int iR = max(2, drawR - 8);  // 黒目のサイズ
    int pgx = constrain((int)gx, -(drawR - iR - 1), (drawR - iR - 1));
    int pgy = constrain((int)gy, -(drawR - iR - 1), (drawR - iR - 1));
    
    // 悲しい時は黒目を下寄りに配置し、より悲しげに
    int targetPgy = pgy;
    if (s_emotion == EMOTION_SAD) {
        targetPgy = pgy + (drawR / 4); 
    }
    targetPgy = constrain(targetPgy, -(drawR - iR - 1), (drawR - iR - 1));
    
    // 黒目の動きがカクつかないように、遅延追従処理（ヌルヌル動く効果）
    static float currentPgyLeft = 0;
    static float currentPgyRight = 0;

    if (isLeft) {
        currentPgyLeft += (targetPgy - currentPgyLeft) * 0.2f; 
        pgy = (int)currentPgyLeft;
    } else {
        currentPgyRight += (targetPgy - currentPgyRight) * 0.2f;
        pgy = (int)currentPgyRight;
    }
    
    int pupilX = cx + pgx;
    int pupilY = cy + pgy;
    dsp.fillCircle(pupilX, pupilY, iR, BLACK); // 黒目部分（黒い丸）

    // 4. ハイライトの描画
    if (s_emotion == EMOTION_SAD) {
        // 悲しい時は光を3つ入れてよりうるうるした瞳に
        dsp.fillCircle(pupilX + (iR / 2), pupilY - (iR / 2), 3, WHITE); 
        dsp.fillCircle(pupilX - (iR / 2), pupilY + (iR / 2), 1, WHITE); 
        dsp.fillCircle(pupilX + (iR / 4) - 3, pupilY + (iR / 2), 1, WHITE); 
    }
    else if (s_emotion == EMOTION_CONFUSED) {   // ジト目の時
        dsp.fillCircle(pupilX + (iR / 3), pupilY + (iR / 4), 2, WHITE); 
        dsp.fillCircle(pupilX - (iR / 2), pupilY + (iR / 2), 1, WHITE); 
    }
    else {

        // 通常・嬉しい・その他の時の標準ハイライト
        int openRy = max(1, (int)(r * (1.0f - effectiveLid)));
        if (effectiveLid < 0.7f && openRy >= 4) {
            if (s_emotion == EMOTION_HAPPY) {
                dsp.fillCircle(cx + (r / 4),     cy - (openRy / 4), 2, WHITE);  // 嬉しい時は多めにキラキラ
                dsp.fillCircle(cx - (r / 6),     cy + (openRy / 6), 1, WHITE); 
                dsp.fillCircle(cx + (r / 4) - 3, cy + (openRy / 4), 1, WHITE); 
                dsp.fillCircle(cx - (r / 6),     cy - (openRy / 4) + 1, 1, WHITE); 
            } else {
                dsp.fillCircle(cx + (iR / 4), pupilY - (iR / 2), 2, WHITE);     // 通常時のシンプルな光
                dsp.fillCircle(cx - (iR / 6), pupilY + (iR / 2), 1, WHITE);
            }
        }
    }

    // ─── 5. まぶた・フチ線の最終処理 ───
    if (s_emotion == EMOTION_CONFUSED) {
        // 調整ポイント【CONFUSED（ジト目）の限界シャッターライン】
        // 下記の「0.15f」を、上のテーブルで指定した数値（デフォルト0.15f）と必ず一致させる必要がある
        // これによってまぶたの基本位置が綺麗に揃い、そこから下に向かって滑らかにまばたきが始まる
        float confLid = max(effectiveLid, 0.15f); 
        int lidH = max(1, (int)(r * 2.0f * confLid));
        dsp.fillRect(cx - r - 1, cy - r - 1, r * 2 + 3, lidH + 1, BLACK);   // 上から黒で目を隠す
        if (cy - r - 1 + lidH < SCREEN_HEIGHT) {
            dsp.drawFastHLine(cx - r, cy - r - 1 + lidH, r * 2, WHITE);     // 瞼のキワの白いアイライン
        }
    }
    else if (s_emotion == EMOTION_ANGRY && showAngryEye && effectiveLid < 0.45f) {
        // 怒り用のキリッとした白いライン
        if (isLeft) {
            dsp.drawLine(cx - drawR - 1, cy - drawR, cx + drawR + 2, cy - (drawR / 3), WHITE);
        } else {
            dsp.drawLine(cx - drawR - 1, cy - (drawR / 3), cx + drawR + 2, cy - drawR, WHITE);
        }
    }
    else if (s_emotion == EMOTION_SAD && effectiveLid < 0.45f) {
        // 悲しい表情用のハの字の白いライン
        if (isLeft) {
            dsp.drawLine(cx - drawR - 1, cy - (drawR / 2.2), cx + drawR + 2, cy - drawR, WHITE);
        } else {
            dsp.drawLine(cx - drawR - 1, cy - drawR, cx + drawR + 2, cy - (drawR / 2.2), WHITE);
        }
    }
    else if (effectiveLid > 0.02f) {
        // 通常時や嬉しい表情時の、上から真っ直ぐに降りてくる通常のまぶた
        int lidH = max(1, (int)(r * 2.0f * effectiveLid));
        dsp.fillRect(cx - r - 1, cy - r - 1, r * 2 + 3, lidH + 1, BLACK);
        if (cy - r - 1 + lidH < SCREEN_HEIGHT) {
            dsp.drawFastHLine(cx - r, cy - r - 1 + lidH, r * 2, WHITE);
        }
    }
}

// ══════════════════════════════════════════════════
// 鼻（真ん中にちょこんと1ドット
// ══════════════════════════════════════════════════
static void drawNose(Adafruit_SSD1306 &dsp) {
    int nx = SCREEN_WIDTH / 2;
    int ny = EYE_CENTER_Y + 12;
    if (ny < SCREEN_HEIGHT) {
        dsp.fillCircle(nx, ny, 1, WHITE);
    }
}

// ══════════════════════════════════════════════════
// 口の描画関数
// ══════════════════════════════════════════════════
static void drawMouth(Adafruit_SSD1306 &dsp, EmotionType emotion) {
    const int mx = SCREEN_WIDTH / 2;
    const int my = 50; 

    if (my >= SCREEN_HEIGHT) return;

    switch (emotion) {
        case EMOTION_HAPPY:  // ハッピー
            // にっこり笑った口
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

        case EMOTION_ANGRY:   // 怒り
        case EMOTION_SAD:     // 悲しい
            // 不満・悲しい時の「へ」の字口
            dsp.drawLine(mx - 5, my + 3, mx,     my,     WHITE); 
            dsp.drawLine(mx,     my,     mx + 5, my + 3, WHITE); 
            dsp.drawPixel(mx,    my + 1, WHITE); 
            break;

        case EMOTION_CONFUSED: // 困惑・ジト目
            // ポカンとしたような小さな口（1ドット）
            dsp.drawPixel(mx, my, WHITE);
            break;
        
        case EMOTION_SLEEPY:  // うとうと
            // 眠い時のぽかーんとしたゆるい口（小さな丸）
            dsp.drawCircle(mx, my + 1, 1, WHITE);   
            break;

        default: 
            // 通常時の口（ - ）
            dsp.drawFastHLine(mx - 3, my, 6, WHITE);
            break;
    }
}

// ══════════════════════════════════════════════════
// 毎フレーム呼ばれる目の状態更新処理ループ
// ══════════════════════════════════════════════════
void eyeUpdate(Adafruit_SSD1306 &dsp, EmotionType emotion) {
    unsigned long now = millis();

    // 感情が新しく切り替わった瞬間の初期化
    if (emotion != s_emotion) {
        s_emotion = emotion;
        s_emotionStartTime = now;
        s_sleepyBlinkCount = 0;
        s_isDeepSleeping = false;
        s_blinkHolding = false;
        s_blinkClosing = false;
        s_blinkOpening = false;
        s_blinkOpen = 1.0f;   // 目を開けた状態からスタート
    }

    EyeParam target = getEyeParam(s_emotion);

    // 感情が切り替わった際、0.6秒（600ms）かけて目をモーフィングさせる
    float morphRate = (float)(now - s_emotionStartTime) / 600.0f;
    if (morphRate > 1.0f) morphRate = 1.0f;

    s_curLidClose  = lerpF(s_curLidClose,  target.lidClose,  morphRate);
    s_curLidCut    = lerpF(s_curLidCut,    target.lidCut,    morphRate);
    s_curGazeScale = lerpF(s_curGazeScale, target.gazeScale, morphRate);

    // ──★ まばたきスピードの調整ポイント ──
    // 調整ポイント【まばたきの開閉スピード】
    // デフォルトの「0.22f」を小さくする（例: 0.10f）と、アニメーションがスローモーションになり、まばたきがさらにゆっくりになる
    // 大きくする（例: 0.40f）と、アニメーションコマ数が減り、素早いまばたきになる
    float blinkSpeed = 0.22f; 

    if (s_emotion == EMOTION_SLEEPY) {
        // SLEEPY（うとうと）の時は、スピードを落とし眠そうに演出
        // 最後の1回（目を閉じる際）は「0.03f」というスローに設定
        blinkSpeed = (s_sleepyBlinkCount == 1) ? 0.03f : 0.08f;  
    }

    // 調整ポイント【うとうとループ：目を閉じている時間の長さ】
    // 目が完全に閉じた状態（キープ中）の時、どれくらい眠るかを判定
    if (s_blinkHolding) {
        // 現在は「1200ms（1.2秒）」目を閉じている
        // この数値を 3000 に変更すると、目を3秒間ぎゅっと閉じてから、ハッと目を開けるループになる
        if (now - s_blinkHoldStart >= 1200) { 
            s_blinkHolding = false;
            s_blinkOpening = true;  // 時間が経ったので目を開け始める
        }
    }

    // まばたきタイマーが来たら、閉じるフラグをONにする
    if (!s_blinkClosing && !s_blinkOpening && !s_blinkHolding && now >= s_nextBlink) {
        s_blinkClosing = true;
    }

    // まばたきアニメーション：まぶたが閉じていく処理
    if (s_blinkClosing) {
        s_blinkOpen -= blinkSpeed;
        if (s_blinkOpen <= 0.0f) { 
            s_blinkOpen = 0.0f; 
            s_blinkClosing = false; 
            
            if (s_emotion == EMOTION_SLEEPY) {
                s_sleepyBlinkCount--; 
                // うとうと回数が0になったら、完全に目を閉じるキープ状態（Holding）へ移行
                if (s_sleepyBlinkCount <= 0) {
                    s_sleepyBlinkCount = random(3, 6);  // 次回用のカウントをリセット再設定
                    s_blinkHolding = true;              // 目を閉じたままぷ開始
                    s_blinkHoldStart = now;             // 閉じ始めた時刻を記憶
                } else {
                    s_blinkOpening = true;   // まだ回数が残っていれば普通に開く
                }
            } else {
                s_blinkOpening = true;  // 通常の感情なら閉じたらすぐ開く
            }
        }
    } 

    // まばたきアニメーション：まぶたを開けていく処理
    else if (s_blinkOpening) {
        s_blinkOpen += blinkSpeed;
        if (s_blinkOpen >= 1.0f) { 
            s_blinkOpen = 1.0f; 
            s_blinkOpening = false; 
            // 調整ポイント【次にまばたきするまでの待ち時間】
            // 以下の数値を変更すると、まばたきする頻度を増やしたり減らしたりすることができる
            unsigned long minInt = (s_emotion == EMOTION_SLEEPY) ? 800 : BLINK_INTERVAL_MIN;
            unsigned long maxInt = (s_emotion == EMOTION_SLEEPY) ? 1800 : BLINK_INTERVAL_MAX;
            s_nextBlink = now + random(minInt, maxInt);     // 次のランダムタイマーを設定
        }
    }

    // 視線（黒目）のランダム巡回移動ロジック
    if (now >= s_nextGaze) {
        s_gazeMoving = true;
        s_gazeStart = now;
        s_gazeTargX = random(-PUPIL_MOVE_RANGE, PUPIL_MOVE_RANGE + 1); 
        s_gazeTargY = random(-3, 4);
        s_nextGaze = now + random(GAZE_INTERVAL_MIN, GAZE_INTERVAL_MAX);
    }
    if (s_gazeMoving) {
        float t = (float)(now - s_gazeStart) / 350.0f; // 0.35秒かけて黒目がスライド移動
        if (t >= 1.0f) {
            s_gazeX = s_gazeTargX; s_gazeY = s_gazeTargY; s_gazeMoving = false;
        } else {
            s_gazeX = lerpF(s_gazeX, s_gazeTargX, t); s_gazeY = lerpF(s_gazeY, s_gazeTargY, t);
        }
    }

    // まばたきアニメーション値を瞼の適用値に換算
    float blinkLid = s_curLidClose + (1.0f - s_blinkOpen);

    // 悲しい（SAD）時は、涙を流すフラグをアニメーション変数を進める
    if (s_emotion == EMOTION_SAD) {
        s_isCrying = true;
        s_tearOffset += 0.5f;    // 数値を大きくすると涙が流れるスピードが早くなる
        if (s_tearOffset > 12.0f) s_tearOffset = 0.0f;  // 12ピクセル流れたら上に戻る
    } else {
        s_isCrying = false;
    }

    // ─── 実際の画面へ描画・リフレッシュ ───
    int r = EYE_RADIUS_X;
    dsp.clearDisplay();  // 画面を一度マックにクリア

    // 1. 左目の描画
    drawOneEye(dsp, EYE_L_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X, 
               s_gazeX * s_curGazeScale, s_gazeY * s_curGazeScale, 
               blinkLid, s_curLidCut, true, true);
    
    // 2. 右目の描画
    drawOneEye(dsp, EYE_R_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X, 
               s_gazeX * s_curGazeScale, s_gazeY * s_curGazeScale, 
               blinkLid, s_curLidCut, false, true);

    // 3. 涙またはほっぺの装飾描画
    if (target.tearLeft || target.tearRight) { 
        if (s_emotion == EMOTION_SAD && s_isCrying) {
            // 悲しい（SAD）の時は目の端から下に落ちる涙のドットを描画
            int ty = EYE_CENTER_Y + r + 2 + (int)s_tearOffset;
            if (ty + 3 < SCREEN_HEIGHT) {
                if (target.tearLeft)  dsp.fillCircle(EYE_L_CENTER_X - 8, ty, 1, WHITE);
                if (target.tearRight) dsp.fillCircle(EYE_R_CENTER_X + 8, ty, 1, WHITE);
            }
        } 
        else if (s_emotion == EMOTION_HAPPY || s_emotion == EMOTION_NORMAL) {
            // 通常・嬉しい時は、頬に照れ線を描画
            if (target.tearLeft)  drawCheek(dsp, EYE_L_CENTER_X, EYE_CENTER_Y, r);
            if (target.tearRight) drawCheek(dsp, EYE_R_CENTER_X, EYE_CENTER_Y, r);
        }
    }

    // 4. 鼻を口を描画してディスプレイに反映
    drawNose(dsp);
    drawMouth(dsp, s_emotion); 
    dsp.display();  // 描画バッファをOLEDパネルに一括転送
}

// ── Arduino標準設定（起動時に1回だけ実行） ──────────────────
static EmotionType s_currentTestRoute = EMOTION_NORMAL;

void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(A0)); // ランダムのシード値を未接続ピンから取得してばらつかせる

    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        for(;;);    // OLEDが見つからない場合は安全のためここで無限ループを停止
    }
    
    display.clearDisplay();
    display.display();

    unsigned long now = millis();
    s_nextBlink = now + 2000;
    s_nextGaze  = now + 1500;

    Serial.println(F("\n=== ロボットペット:表情システム（全感情なめらかループ版） ==="));
}
// ── メインループ（高速で回り続ける） ─────────────────────
void loop() {
    // シリアルモニタから「0」〜「5」の数値を送ることで、手動で感情テストを切り替え可能
    while (Serial.available() > 0) {
        char incoming = Serial.read();
        if (incoming == '\n' || incoming == '\r' || incoming == ' ') continue; 

        if (incoming >= '0' && incoming <= '5') { 
            s_currentTestRoute = (EmotionType)(incoming - '0');
            Serial.print(F("★感情切り替え -> "));
            Serial.println((int)s_currentTestRoute);
        }
    }

    eyeUpdate(display, s_currentTestRoute);
    delay(10);
}
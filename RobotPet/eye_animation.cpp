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
// static で管理し、このファイル専用の記憶領域とする
// ══════════════════════════════════════════════════
static float         s_blinkOpen    = 1.0f;     // まばたきの開度（1.0 = 前回, 0.0 = 閉）
static bool          s_blinkClosing = false;    // 閉じ具合
static bool          s_blinkOpening = false;    // 開き具合
static unsigned long s_nextBlink    = 0;        // 次のまばたき時刻

static int8_t        s_gazeX       = 0, s_gazeY     = 0; // 現在の視線位置
static int8_t        s_gazeTargX   = 0, s_gazeTargY = 0; // 目標の座標位置
static unsigned long s_nextGaze    = 0, s_gazeStart = 0; // 視線移動の開始時刻
static bool          s_gazeMoving  = false;              // 移動中フラグ

// 今、ロボットはどんな気分かを記録する場所
// 初期値は EMOTION_NORMAL （通常）からスタートする
static EmotionType   s_emotion     = EMOTION_NORMAL;

// 「困惑（CONFUSED）」している時、表情の揺らぎを管理するスイッチ
static bool          s_confuseFlip = false; 

// 【線形補完関数】: 現在の値から目標値へ、少しずつ近づける数学的テクニック
static inline float lerpF(float a, float b, float t) {
    return a + (b - a) * t;
}

// ══════════════════════════════════════════════════
// ほっぺ（頬に3本の短いライン、照れ線）
// ══════════════════════════════════════════════════
static void drawCheek(Adafruit_SSD1306 &dsp,
                           int cx, int cy, int r)
{
    // 目の中心（cr）から、半径（r）より少し下の位置に頬を設定
    int cheekY = cy + r + 2;

    // 画面外チェック（安全対策）
    if (cheekY + 2 >= SCREEN_HEIGHT) return;

    // 左右のどちらの目かによって頬の位置を調整する
    // 右の目なら右側、左の目なら左側に描画
    int ooffset = (cx < SCREEN_WIDTH / 2) ? -8 : 8;
    int centerX = cx + offset;

    // 3本の短い斜め線（照れ線）
    // 描画位置を少しずつずらして3本並べる
    dsp.drawLine(centerX - 4, ceekY,     centerX - 2, ceekY + 2, WHITE);
    dsp.drawLine(centerX - 1, ceekY - 1, centerX + 1, ceekY + 3, WHITE);
    dsp.drawLine(centerX + 2, ceekY,     centerX + 4, ceekY + 2, WHITE);
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
    // 感情（lidClose）とまばたき（lidCut）を合成して開度を決める
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

    // 黒目の「キョロキョロ」移動
    // 黒目（iR）が白目からはみ出さないように制限をかけている
    int pgx = constrain((int)gx, -(drawR - iR - 1), (drawR - iR - 1));
    int pgy = constrain((int)gy, -(drawR - iR - 1), (drawR - iR - 1));
    dsp.fillCircle(cx + pgx, cy + pgy, iR, BLACK);

    // ── 【重要】ハイライトの描画 ───────────────────
    // 視線(gx, gy)ではなく、白目の中心(cx, cy)を基準にする
    if (effectiveLid < 0.6f && openRy >= 4) {
       // メイン（右上）：分母を「2」から「4」などに大きくすると中央に寄る
        int mainX = cx + (r / 4); 
        int mainY = cy - (openRy / 4);
        dsp.fillCircle(mainX, mainY, 2, WHITE); 
        
        // サブ（左下）：分母を「3」から「6」などに大きくすると中央に寄る
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

    // ── 頬 ─────────────────────────────────
    if (p.tearLeft) {
        drawCheek(dsp, EYE_L_CENTER_X, EYE_CENTER_Y, r);
        drawCheek(dsp, EYE_R_CENTER_X, EYE_CENTER_Y, r);
    }
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

    // 画面の外への描画を防ぐためのバリア（安全策）
    // my : 描こうとしている口の位置
    // SCREEN_HEIGHT: 画面の高さ（この場合は63までの範囲に限定）
    // もし、口の位置が64以上の場合、描画処理をスキップする
    if (my >= SCREEN_HEIGHT) return;

    switch (emotion) {
        case EMOTION_HAPPY:
            // ω: 小さなW型
            // ω型の口を描画するために、短い線を4本繋いでいる
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
            // 通常時、横線「ー」
            dsp.drawFastHLine(mx - 4, my, 8, WHITE);
            break;
    }
}

// ══════════════════════════════════════════════════
// 感情ごとのパラメータ
// ══════════════════════════════════════════════════
struct EyeParam {
    float  lidClose;   // 基本の閉じ度 (0.0f: 全開, 0.4f: 半目, 1.0f: 閉)
    float  lidCut;     // 驚きなどの際に目を大きく見せるための調整値
    bool   tearLeft;   // 左の涙
    bool   tearRight;  // 右の涙
    float  gazeScale;  // 視線の動きの大きさ(1.0で通常)
};

// 感情と表情のデータテーブル
static EyeParam getEyeParam(EmotionType e) {
    switch (e) {
        case EMOTION_HAPPY:    return { 0.15f, 0.0f, false, false, 0.5f };
        case EMOTION_ANGRY:    return { 0.1f,  0.0f, false, false, 0.8f };
        case EMOTION_SLEEPY:   return { 0.1f,  0.0f, false, false, 0.2f }; // ここだけ「けだるげ」に調整
        case EMOTION_CONFUSED: return { 0.2f,  0.0f, false, false, 0.6f };
        case EMOTION_WORRIED:  return { 0.0f,  0.0f, false, false, 0.4f };
        case EMOTION_SURPRISED:return { 0.0f, -0.2f, false, false, 1.2f };
        case EMOTION_RAINY:    return { 0.3f,  0.0f, true,  true , 0.3f };
        default:               return { 0.0f,  0.0f, false, false, 1.0f };
    }
}

// ══════════════════════════════════════════════════
// 公開関数
// eyeInit : 表情アニメーションシステムの初期化
// display : 使用するディスプレイオブジェクト（今回は未使用）
// ══════════════════════════════════════════════════
void eyeInit(Adafruit_SSD1306 &display) {

    // 【乱数の初期化】
    // 未接続のA0ピンを使用し、ノイズを読み取り、起動するたびに異なる乱数列を生成
    // 毎回同じタイミングでまばたきするのを防ぐ
    randomSeed(analogRead(A0));

    // 【現在の時刻を取得】
    // システム起動から経過時間を基準にして、今後の動作スケジュールを決定
    unsigned long now = millis();

    // 【まばたきの予約】
    // 次にまばたきをするまでの待ち時間をランダムに決定し、今の時刻に足して予約
    // 例：(今) + (ランダムな数秒後) = 「次にまばたきする時刻」
    s_nextBlink   = now + random(BLINK_INTERVAL_MIN, BLINK_INTERVAL_MAX);

    // 【視線移動の予約】
    // キョロキョロと動き出すまでの時間を予約
    // 起動直後に目がピタっと止まっている時間をランダムに作成することで自然さを出す
    s_nextGaze    = now + random(GAZE_INTERVAL_MIN,  GAZE_INTERVAL_MAX);

    // 【初期状態のセット】
    // 困惑表情の左右反転フラグを初期化
    s_confuseFlip = false;

    // 【未使用変数の明示】
    // 関数定義上、displayを引数に使用しているが、ここでは使用しないため
    // コンパイラの「使っていない警告」を抑制するおまじない
    (void)display;
}

// ══════════════════════════════════════════════════
// 更新処理（eyeUpdate）: 表情エンジンのメインループ
// 感情と時間経過に基づき、キャラクターの動きを計算・描画する
// ══════════════════════════════════════════════════

void eyeUpdate(Adafruit_SSD1306 &display, EmotionType emotion) {

    // 現在のシステム時刻を取得（アニメーションの滑らかさの基準になる）
    unsigned long now = millis();

    // ── 1. 感情更新 ──────────────────────────────────
    // 前回と異なる感情が指示された状態をリフレッシュする
    if (emotion != s_emotion) {
        s_emotion = emotion;

        // 「困惑」ならランダムに左右反転させて表情にバリエーションを持たせる
        if (s_emotion == EMOTION_CONFUSED) {
            s_confuseFlip = (bool)(random(0, 2));
        }
    }

    // 現在の感情に基づく描画パラメータ（瞼の閉じ具合など）を取得
    EyeParam p = getEyeParam(s_emotion);

    // ── 2. まばたきアニメーションの計算 ─────────────────
    // 感情によってまばたきをするか判定(眠い時は抑制)
    bool canBlink = (s_emotion != EMOTION_SLEEPY);

    if (canBlink) {

        // タイマーが満了したらプロセスを開始
        if (!s_blinkClosing && !s_blinkOpening && now >= s_nextBlink) s_blinkClosing = true;

        // 閉じている最中: 0.22ずつ閉じていく
        if (s_blinkClosing) {
            s_blinkOpen -= 0.22f;
            if (s_blinkOpen <= 0.0f) { s_blinkOpen = 0.0f; s_blinkClosing = false; s_blinkOpening = true; }
        } 
        
        // 開いている最中: 0.22ずつ開いていき、完了したら次の予約をランダム生成
        else if (s_blinkOpening) {
            s_blinkOpen += 0.22f;
            if (s_blinkOpen >= 1.0f) { s_blinkOpen = 1.0f; s_blinkOpening = false; s_nextBlink = now + random(2000, 5000); }
        }
    } else {
        s_blinkOpen = 1.0f; // まばたき無効時は全開状態を維持
    }

    // ── 最終的な「目閉じ度」決定 ──────────────────
    // まばたきの度合いを考慮した最終的な目の開き具合を算出
    float blinkLid = lerpF(p.lidClose, 1.0f, 1.0f - s_blinkOpen);
    float lidL = blinkLid;
    float lidR = blinkLid;

    // ── 3. 視線移動（キョロキョロ）の計算 ─────────────
    bool gazeEnabled = (s_emotion != EMOTION_NORMAL && s_emotion != EMOTION_SLEEPY);

    // まばたきのアニメーションの計算
    // タイマーが満了したら新しい視線の目的地点をランダム決定
    if (gazeEnabled && !s_gazeMoving && now >= s_nextGaze) {
        int r = max(1, (int)(PUPIL_MOVE_RANGE * p.gazeScale));
        s_gazeTargX  = (int8_t)random(-r, r + 1);
        s_gazeTargY  = (int8_t)random(-r / 2, r / 2 + 1);
        s_gazeStart  = now;
        s_gazeMoving = true;
    }

    // 視線の移動計算(gaze)
    // 350msかけて、目標位置まで滑らかに移動させる（イージング処理）
    if (s_gazeMoving) {
        float t    = min((float)(now - s_gazeStart) / 350.0f, 1.0f);
        float ease = 1.0f - (1.0f - t) * (1.0f - t);    // 加速度の演出
        s_gazeX = (int8_t)lerpF((float)s_gazeX, (float)s_gazeTargX, ease);
        s_gazeY = (int8_t)lerpF((float)s_gazeY, (float)s_gazeTargY, ease);
        if (t >= 1.0f) {
            s_gazeMoving = false;
            s_nextGaze = now + random(GAZE_INTERVAL_MIN, GAZE_INTERVAL_MAX);
        }
    }

    // 視線無効時は中心(0,0)へゆっくり戻す
    if (!gazeEnabled) {
        s_gazeX = (int8_t)lerpF((float)s_gazeX, 0.0f, 0.15f);
        s_gazeY = (int8_t)lerpF((float)s_gazeY, 0.0f, 0.15f);
    }

    // ── 4. 描画の実行 ──────────────────────────────
    int r = EYE_RADIUS_X;
    display.clearDisplay(); // 画面を一旦リセット

    // 目を描画（左・右）
    // 左目(isLeft=true)
    drawOneEye(display, EYE_L_CENTER_X, EYE_CENTER_Y,
               r, s_gazeX, s_gazeY,
               lidL, p.lidCut, true);

    // 右目(isLeft=false)
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

    // 鼻と口を描画して顔を完成させる
    drawNose(display);
    drawMouth(display, s_emotion);

    // バッファの内容を最後に一括で画面に転送（チラつき防止）
    display.display();
}
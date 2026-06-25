/**
 * eye_animation.cpp
 * かわいいキャラクター風の目・鼻・口アニメーション
 *
 * 【ファイル構成】
 *   eye_animation.h  : 定数・型定義・公開API宣言
 *   eye_params.h/cpp : 感情パラメータテーブル（表情調整はここ）
 *   eye_draw.h/cpp   : 目・鼻・口・アイテムの描画関数
 *   eye_state.h/cpp  : まばたき・視線・感情遷移の状態管理
 *   eye_animation.cpp: eyeUpdate / eyeUpdateRTC（このファイル）
 *
 * 【デザイン方針】
 *   スケッチキャラクターに合わせた「大きな黒目」スタイル。
 *   - 目: 大きな黒塗り楕円 + 星型ハイライト + まつ毛ライン
 *   - 鼻: 中央に小さな点（・）
 *   - 口: ω型(HAPPY) / へ型(SAD) / 点(NORMAL) / 横線(VOID)
 */

#include "eye_animation.h"
#include "eye_draw.h"
#include "eye_state.h"

// eye_state.cpp が管理する涙情報（extern参照）
extern float g_tearOffset;
extern bool  g_showTear;

// ── 追加する初期化関数 ──────────────────────────────
void eyeInit(Adafruit_SSD1306 &dsp) {
    // 依存している eye_state.cpp 側の初期化関数を内部で呼び出す
    // （もし eyeStateInit の引数が異なる、または不要なら環境に合わせて調整してください）
    eyeStateInit(); 
    
    Serial.println(F("[EYE] eyeInit completed."));
}

// ══════════════════════════════════════════════════
// メイン更新関数
// ══════════════════════════════════════════════════
void eyeUpdate(Adafruit_SSD1306 &dsp, EmotionType emotion) {
    unsigned long now   = millis();
    int           frame = (int)((now / 500) % 2);

    // 状態更新（まばたき・視線・感情遷移）
    float  blinkLid, lidCut, gazeScale;
    int8_t gazeX, gazeY;
    eyeStateUpdate(emotion, now, blinkLid, lidCut, gazeX, gazeY, gazeScale);

    // 描画開始
    dsp.clearDisplay();

    // 左右の目
    drawOneEye(dsp,
               EYE_L_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X,
               (int8_t)(gazeX * gazeScale), (int8_t)(gazeY * gazeScale),
               blinkLid, lidCut,
               true, emotion);

    drawOneEye(dsp,
               EYE_R_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X,
               (int8_t)(gazeX * gazeScale), (int8_t)(gazeY * gazeScale),
               blinkLid, lidCut,
               false, emotion);

    // 感情エフェクト
    if (g_showTear) {
        // 涙（SAD時）
        int ty = EYE_CENTER_Y + EYE_RADIUS_X + 2 + (int)g_tearOffset;
        dsp.fillCircle(EYE_L_CENTER_X - 8, ty, 1, WHITE);
        dsp.fillCircle(EYE_R_CENTER_X + 8, ty, 1, WHITE);
    } else if (emotion == EMOTION_HAPPY  ||
               emotion == EMOTION_NORMAL ||
               emotion == EMOTION_BATH   ||
               emotion == EMOTION_SNACK  ||
               emotion == EMOTION_EATING)
    {
        // ほっぺ（照れ線）
        drawCheek(dsp, EYE_L_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X);
        drawCheek(dsp, EYE_R_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X);
    }

    // 顔パーツ
    drawNose(dsp);
    drawMouth(dsp, emotion, frame);

    // 時間帯・状態アイテム
    switch (emotion) {
        case EMOTION_EATING:  drawFoodItem(dsp, frame);              break;
        case EMOTION_SNACK:   drawSnackItem(dsp, frame);             break;
        case EMOTION_SLEEPING: drawSleepZzz(dsp, frame);             break;
        case EMOTION_BATH:    drawWaterLine(dsp, frame);
                              drawHeadTowel(dsp);                    break;
        default:                                                     break;
    }

    dsp.display();
}

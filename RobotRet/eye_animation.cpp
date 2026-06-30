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

// 外部（状態管理用タスク）から流れてくる涙の物理シミュレーション変数
// eye_state.cpp が管理する涙情報（extern参照）
extern float g_tearOffset;
extern bool  g_showTear;

// ── する初期化関数 ──────────────────────────────
void eyeInit(Adafruit_SSD1306 &dsp) {


    // 依存している eye_state.cpp 側の初期化関数を内部で呼び出す
    eyeStateInit(); 
    
    Serial.println(F("[EYE] eyeInit completed."));
}

// ══════════════════════════════════════════════════
// 更新処理（eyeUpdate）: 表情エンジンのメインループ
//                      15fps程度で実行
// ══════════════════════════════════════════════════
void eyeUpdate(Adafruit_SSD1306 &dsp, EmotionType emotion) {
    
    // 現在のシステム時刻を取得（アニメーションの滑らかさの基準になる）
    unsigned long now   = millis();
    // 口パクなどを交互に切り替えるためのフレームワークインデックス
    int           frame = (int)((now / 500) % 2);

    // 状態更新（まばたき・視線・感情遷移）：現在の感情に基づき、まぶたの開き具合や視線（gaze）を計算
    // ここで計算された値が描画レイヤーの引数になる
    float  blinkLid, lidCut, gazeScale;
    int8_t gazeX, gazeY;
    eyeStateUpdate(emotion, now, blinkLid, lidCut, gazeX, gazeY, gazeScale);

    // 描画バッファのクリア（前のフレームの残像を消去）
    dsp.clearDisplay();

    // ── 目（レイヤー1） ───
    // 左右の目を個別に計算・描画。視線の位置を gazeScale で伸縮・反映させる
    // 左目（isLeft=true）
    drawOneEye(dsp,
               EYE_L_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X,
               (int8_t)(gazeX * gazeScale), (int8_t)(gazeY * gazeScale),
               blinkLid, lidCut,
               true, emotion);

    // 右目（isLeft=false）
    drawOneEye(dsp,
               EYE_R_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X,
               (int8_t)(gazeX * gazeScale), (int8_t)(gazeY * gazeScale),
               blinkLid, lidCut,
               false, emotion);

    // ── 特殊演出（レイヤー2） ──
    // スマホからエサやり要求時、ハートをアニメーション表示
    if (emotion == EMOTION_FEAST) {
        drawFloatingHearts(dsp, frame);
    }

    // ── 感情エフェクト・ほっぺ（レイヤー3） ──
    if (g_showTear) {
        // [涙の描画（SAD時）]: 目の中心Y(EYE_CENTER_Y) + 半径X(EYE_RADIUS_Y) = 下辺
        // 目の半径分（EYE_RADIUS_Y）足すことで、目の中心から下のラインに移動
        int tearBaseY = EYE_CENTER_Y + EYE_RADIUS_X; 
        int ty = tearBaseY + (int)g_tearOffset;

        // 描画位置を微調整（もし左右がズレているならここの値を調整）
        // 左右の目の外側に小さな涙を描画
        dsp.fillCircle(EYE_L_CENTER_X - 8, ty, 1, WHITE);
        dsp.fillCircle(EYE_R_CENTER_X + 8, ty, 1, WHITE);
        
    } else if (emotion == EMOTION_HAPPY  ||
               emotion == EMOTION_NORMAL ||
               emotion == EMOTION_BATH   ||
               emotion == EMOTION_SNACK  ||
               emotion == EMOTION_SMILE  ||
               emotion == EMOTION_EATING)
    {
        // ほっぺ（照れ線）
        drawCheek(dsp, EYE_L_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X);
        drawCheek(dsp, EYE_R_CENTER_X, EYE_CENTER_Y, EYE_RADIUS_X);
    }

    // ── 顔パーツ（レイヤー4） ──
    // 鼻と口は感情によって形状が動的に変化する(eye_draw.cppで定義)
    drawNose(dsp);
    drawMouth(dsp, emotion, frame);

    // ── 小物アイテム（レイヤー5） ──
    // 感情に応じた装飾パーツ（食べ物、タオル、お布団のZzzなど）を追加描画
    switch (emotion) {
        case EMOTION_EATING:  drawFoodItem(dsp, frame);              break;
        case EMOTION_SNACK:   drawSnackItem(dsp, frame);             break;
        case EMOTION_SLEEPING: drawSleepZzz(dsp, frame);             break;
        case EMOTION_BATH:    drawWaterLine(dsp, frame);
                              drawHeadTowel(dsp);                    break;
        default:                                                     break;
    }

    // バッファの内容を最後に一括で画面に転送（チラつき防止）
    dsp.display();
}

/**
 * eye_params.cpp
 * 感情ごとの目パラメータ設定テーブル
 *
 * 【カスタマイズガイド】
 * getEyeParam() の各行を変更することで表情を調整できる。
 *
 * EyeParam フィールド説明：
 *   lidClose  : まぶたの基本閉じ度（0.0=全開 / 1.0=全閉）
 *   lidCut    : 追加カット量（現在は常に0.0f、拡張用予約）
 *   tearLeft  : 左目特殊パーツ表示フラグ（将来拡張用）
 *   tearRight : 右目特殊パーツ表示フラグ（将来拡張用）
 *   gazeScale : 黒目の動き範囲（0.0=動かない / 1.0=通常）
 *
 * 【変更点】
 *   - getCurrentEventEmotion() を削除。
 *     時刻→感情の変換は robot_pet.ino の TaskEmotion が担当するため不要。
 */

#include "eye_animation.h"
#include "eye_params.h"

// ══════════════════════════════════════════════════
// 感情パラメータテーブル
// ══════════════════════════════════════════════════
EyeParam getEyeParam(EmotionType e, int8_t &sleepyCnt) {
    switch (e) {
        //                        lidClose  lidCut  lashL  lashR  gazeScale
        case EMOTION_NORMAL:   return { 0.0f,  0.0f, true,  true,  1.0f };
        case EMOTION_HAPPY:    return { 0.0f,  0.0f, true,  true,  1.0f };

        // 怒りと悲しみはまぶたを少し下げてから表情を付ける
        case EMOTION_ANGRY:    return { 0.4f,  0.0f, false, false, 1.0f };
        case EMOTION_SAD:      return { 0.35f, 0.0f, true,  true,  0.8f };

        // lidClose を大きくすると強いジト目、小さくすると通常に近いジト目
        case EMOTION_CONFUSED: return { 0.25f, 0.0f, false, false, 0.6f };

        case EMOTION_SLEEPY:
            // うとうと回数が未設定の場合のみランダム初期化
            if (sleepyCnt == 0) {
                sleepyCnt = (int8_t)random(3, 6);
            }
            return { 0.45f, 0.0f, false, false, 0.3f };

        case EMOTION_EATING:   return { 0.0f,  0.0f, true,  true,  0.5f };
        case EMOTION_SLEEPING: return { 0.95f, 0.0f, false, false, 0.0f };
        case EMOTION_BATH:     return { 0.0f,  0.0f, true,  true,  0.8f };
        case EMOTION_SNACK:    return { 0.0f,  0.0f, true,  true,  0.5f };

        default:               return { 0.0f,  0.0f, true,  true,  1.0f };
    }
}
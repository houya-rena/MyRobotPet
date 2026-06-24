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
 *   showLashL : 左目の下まつ毛ON/OFF（将来拡張用）
 *   showLashR : 右目の下まつ毛ON/OFF（将来拡張用）
 *   gazeScale : 黒目の動き範囲（0.0=動かない / 1.0=通常）
 */
#include "eye_animation.h" // EyeParam 構造体はここで定義されているはず
#include "eye_params.h"    // プロトタイプ宣言（関数名など）を読み込む

EyeParam getEyeParam(EmotionType e, int8_t &sleepyCnt) {
    switch (e) {
        //                        lidClose  lidCut  lashL  lashR  gazeScale
        case EMOTION_NORMAL:   return { 0.0f,   0.0f,  true,  true,  1.0f };
        case EMOTION_HAPPY:    return { 0.0f,   0.0f,  true,  true,  1.0f };

        // 怒りと悲しみはまぶたを少し下げてから表情を付ける。
        // さらに目を見開いた状態から始めたい場合は lidClose を 0.0f〜0.2f に下げる。
        case EMOTION_ANGRY:    return { 0.4f,   0.0f,  false, false, 1.0f };
        case EMOTION_SAD:      return { 0.35f,  0.0f,  true,  true,  0.8f };

        // lidClose の 0.25f を大きくすると強いジト目、小さくすると通常に近いジト目になる
        case EMOTION_CONFUSED: return { 0.25f,  0.0f,  false, false, 0.6f };

        case EMOTION_SLEEPY:
            // うとうとする回数が未設定の場合のみランダム初期化（呼び出し元で管理）
            if (sleepyCnt == 0) {
                sleepyCnt = (int8_t)random(3, 6);
            }
            return { 0.45f, 0.0f, false, false, 0.3f };

        case EMOTION_EATING:   return { 0.0f,   0.0f,  true,  true,  0.5f };
        case EMOTION_SNACK:    return { 0.0f,   0.0f,  true,  true,  0.5f };
        case EMOTION_SLEEPING: return { 0.95f,  0.0f,  false, false, 0.0f };
        case EMOTION_BATH:     return { 0.0f,   0.0f,  true,  true,  0.8f };

        default:               return { 0.0f,   0.0f,  true,  true,  1.0f };
    }
}
// 現在の時刻から「今のイベント状態」を判定する
EmotionType getCurrentEventEmotion(int h, int m) {
    if (h == 15 && m < 15) return EMOTION_SNACK;
    if ((h == 12 && m < 30) || (h == 18 && m < 30)) return EMOTION_EATING;
    if (h == 20 && m < 30) return EMOTION_BATH;
    return EMOTION_NORMAL;
}
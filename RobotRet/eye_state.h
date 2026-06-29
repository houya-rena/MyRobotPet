/**
 * eye_state.h
 * まばたき・視線・感情遷移の内部状態管理
 */
#pragma once

#include "eye_animation.h"

// ── 状態初期化 ───────────────────────────────────
/**
 * 全内部状態をリセットする。
 * setup() の中で一度だけ呼ぶこと。
 */
void eyeStateInit();

// ── 毎フレーム更新 ───────────────────────────────
/**
 * 感情・まばたき・視線の状態を更新し、描画に必要な値を書き出す。
 *
 * @param emotion      現在の感情
 * @param now          現在のミリ秒（millis()）
 * @param[out] blinkLid 上まぶた閉じ度（0.0=全開 / 1.0=全閉）
 * @param[out] lidCut   追加カット量（現在は常に0.0f）
 * @param[out] gazeX    視線X方向オフセット
 * @param[out] gazeY    視線Y方向オフセット
 * @param[out] gazeScale 黒目の動き範囲スケール
 */
void eyeStateUpdate(EmotionType emotion, unsigned long now,
                    float &blinkLid, float &lidCut,
                    int8_t &gazeX, int8_t &gazeY,
                    float &gazeScale);
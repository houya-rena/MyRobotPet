#ifndef EYE_PARAMS_H
#define EYE_PARAMS_H

#include "eye_animation.h"

/**
 * getEyeParam : 感情IDから目のパラメータを返す。
 * eye_state.cpp の eyeStateUpdate() から毎フレーム呼ばれる。
 */
EyeParam getEyeParam(EmotionType e, int8_t &sleepyCnt);

// ▼ getCurrentEventEmotion() は削除。
//   時刻→感情の変換は robot_pet.ino の TaskEmotion が担当するため不要。

#endif // EYE_PARAMS_H
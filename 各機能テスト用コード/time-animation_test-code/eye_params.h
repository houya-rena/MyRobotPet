#ifndef EYE_PARAMS_H
#define EYE_PARAMS_H

#include "eye_animation.h"

// 宣言のみ
EyeParam getEyeParam(EmotionType e, int8_t &sleepyCnt);
EmotionType getCurrentEventEmotion(int h, int m);

#endif // EYE_PARAMS_H
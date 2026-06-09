/**
 * serial_comm.cpp
 * シリアルコマンド受信（FreeRTOS TaskSerial から呼び出し）
 *
 * 【FreeRTOS対応の注意点】
 *   - Serial はスレッドセーフではないため、このモジュールは
 *     必ず1つのタスク（TaskSerial）からのみ呼び出すこと。
 *   - バッファ処理は非ブロッキング（available()チェック）のみ。
 */

#include "serial_comm.h"
#include <Arduino.h>
#include <string.h>

#define CMD_BUF_SIZE 32

static char    s_buf[CMD_BUF_SIZE];
static uint8_t s_len = 0;

static EmotionType parseCmd(const char *cmd) {
    if (strcmp(cmd, CMD_WEATHER_RAIN)   == 0) return EMOTION_SAD;
    if (strcmp(cmd, CMD_WEATHER_SUN)    == 0) return EMOTION_HAPPY;
    if (strcmp(cmd, CMD_WEATHER_CLOUD)  == 0) return EMOTION_NORMAL;
    if (strcmp(cmd, CMD_TEMP_HOT)       == 0) return EMOTION_HOT;
    if (strcmp(cmd, CMD_TEMP_NORMAL)    == 0) return EMOTION_NORMAL;
    if (strcmp(cmd, CMD_HUM_HIGH)       == 0) return EMOTION_SAD;
    if (strcmp(cmd, CMD_EMOTION_HAPPY)  == 0) return EMOTION_HAPPY;
    if (strcmp(cmd, CMD_EMOTION_SAD)    == 0) return EMOTION_SAD;
    if (strcmp(cmd, CMD_EMOTION_NORMAL) == 0) return EMOTION_NORMAL;
    return EMOTION_COUNT;
}

void serialCommInit() {
    memset(s_buf, 0, sizeof(s_buf));
    s_len = 0;
}

EmotionType serialCommUpdate() {
    EmotionType result = EMOTION_COUNT;

    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_len > 0) {
                s_buf[s_len] = '\0';
                result = parseCmd(s_buf);
                Serial.print(F("[RX] "));
                Serial.println(s_buf);
            }
            memset(s_buf, 0, sizeof(s_buf));
            s_len = 0;
        } else if (s_len < CMD_BUF_SIZE - 1) {
            s_buf[s_len++] = c;
        }
    }
    return result;
}

/**
 * sensor_task.cpp
 * 温湿度センサー（DHT22）読み取り（FreeRTOS TaskSensor から呼び出し）
 *
 * 【依存ライブラリ】
 *   - DHT sensor library by Adafruit
 *     Arduino IDE → ライブラリマネージャ → "DHT sensor library" でインストール
 *
 * 【FreeRTOS対応の注意点】
 *   - DHT.read() は内部で約2msのpulse計測を行う。
 *     TaskSensor は vTaskDelay(2000ms) で呼び出し間隔を確保しているため問題なし。
 *   - センサー値の共有変数は TaskSensor のみが書き込むので Mutex 不要。
 */

#include "sensor_task.h"
#include <Arduino.h>
#include <DHT.h>

static DHT  dht(DHT_PIN, DHT_TYPE);
static float s_temp = 25.0f;
static float s_hum  = 50.0f;

void sensorInit() {
    dht.begin();
}

EmotionType sensorUpdate() {
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    // 読み取り失敗時は前回値を維持
    if (isnan(t) || isnan(h)) {
        Serial.println(F("[SENSOR] read failed, using last value"));
        t = s_temp;
        h = s_hum;
    } else {
        s_temp = t;
        s_hum  = h;
        Serial.print(F("[SENSOR] T="));
        Serial.print(t, 1);
        Serial.print(F("C  H="));
        Serial.print(h, 1);
        Serial.println(F("%"));
    }

    // ── 感情判定（優先度順）──────────────────────
    if (t >= TEMP_HOT_THRESHOLD)  return EMOTION_HOT;
    if (t <= TEMP_COLD_THRESHOLD) return EMOTION_SAD;
    if (h >= HUM_HIGH_THRESHOLD)  return EMOTION_SAD;

    // 閾値に引っかからなければ変化なし
    return EMOTION_COUNT;
}

float sensorGetTemp() { return s_temp; }
float sensorGetHum()  { return s_hum;  }

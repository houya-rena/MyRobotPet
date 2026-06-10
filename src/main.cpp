/**
 * main.cpp
 * 多機能液晶ロボットガジェット メインファイル（PlatformIO版）
 *
 * 【robot_pet.ino からの変更点】
 *   1. #include <Arduino.h> を追加（PlatformIOでは必須）
 *   2. ファイル名を main.cpp に変更
 *   3. ヘッダは include/ フォルダから参照
 *   4. setup() / loop() のプロトタイプ宣言を追加
 *
 * 【タスク構成】
 *   TaskDisplay  (優先度3) : 目 or 時刻を描画
 *   TaskEmotion  (優先度2) : 感情自律変化 → xEmotionQueue送信
 *   TaskButton   (優先度2) : ボタン検知   → xModeQueue送信
 *   TaskNTP      (優先度1) : 起動時・1時間ごとにNTP時刻同期
 *
 * 【配線】
 *   OLED SSD1306 : SDA=A4, SCL=A5（I2C）
 *   ボタン       : D2 → GND（INPUT_PULLUP）
 */

// ⚠️ PlatformIO では必須（.ino では自動挿入されていた）

#include <Arduino.h>
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- FreeRTOS 関連の修正 ---
#include <Arduino_FreeRTOS.h>
// -------------------------

#include <WiFiS3.h>
#include <RTC.h>
// include/ フォルダのヘッダ
#include "eye_animation.h"
#include "clock_display.h"
#include "ntp_sync.h"
#include "arduino_secrets.h"

// ── ハードウェア設定 ───────────────────────────────
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C
#define BUTTON_PIN    2

#define CLOCK_SHOW_MS  5000UL
#define NTP_RESYNC_MS  3600000UL

// ── 表示モード ─────────────────────────────────────
typedef enum {
    MODE_EYE = 0,
    MODE_CLOCK
} DisplayMode;

// ── グローバル ─────────────────────────────────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

QueueHandle_t xEmotionQueue;
QueueHandle_t xModeQueue;

// ── 関数プロトタイプ ───────────────────────────────
// PlatformIO（C++）では .ino と違い前方宣言が必要
void setup();
void loop();
void TaskDisplay (void *pvParameters);
void TaskEmotion (void *pvParameters);
void TaskButton  (void *pvParameters);
void TaskNTP     (void *pvParameters);
static bool connectWiFi();
static EmotionType weightedRandom();

// ── WiFi接続 ──────────────────────────────────────
static bool connectWiFi() {
    if (WiFi.status() == WL_NO_MODULE) {
        Serial.println(F("[WiFi] Module not found"));
        return false;
    }
    Serial.print(F("[WiFi] Connecting"));
    for (int i = 0; i < 20; i++) {
        if (WiFi.begin(SECRET_SSID, SECRET_PASS) == WL_CONNECTED) {
            Serial.println();
            Serial.print(F("[WiFi] Connected: "));
            Serial.println(WiFi.localIP());
            return true;
        }
        Serial.print('.');
        delay(1500);
    }
    Serial.println(F("\n[WiFi] Failed"));
    return false;
}

// ── setup() ──────────────────────────────────────
void setup() {
    Serial.begin(9600);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println(F("OLED init failed"));
        while (true);
    }
    display.clearDisplay();
    display.display();

    RTC.begin();

    if (connectWiFi()) {
        if (!ntpSync()) {
            Serial.println(F("[NTP] Initial sync failed"));
        }
    }

    eyeInit(display);

    xEmotionQueue = xQueueCreate(4, sizeof(EmotionType));
    xModeQueue    = xQueueCreate(2, sizeof(DisplayMode));
    if (!xEmotionQueue || !xModeQueue) {
        Serial.println(F("Queue creation failed"));
        while (true);
    }

    xTaskCreate(TaskDisplay, "Display", 512, NULL, 3, NULL);
    xTaskCreate(TaskEmotion, "Emotion", 256, NULL, 2, NULL);
    xTaskCreate(TaskButton,  "Button",  128, NULL, 2, NULL);
    xTaskCreate(TaskNTP,     "NTP",     512, NULL, 1, NULL);

    vTaskStartScheduler();
}

void loop() {}  // FreeRTOS使用時は空

// ═══════════════════════════════════════════════════
// TaskDisplay
// ═══════════════════════════════════════════════════
void TaskDisplay(void *pvParameters) {
    DisplayMode  mode          = MODE_EYE;
    EmotionType  emotion       = EMOTION_NORMAL;
    unsigned long clockShowEnd = 0;
    DisplayMode  modeMsg;
    EmotionType  emotionMsg;

    for (;;) {
        if (xQueueReceive(xModeQueue, &modeMsg, 0) == pdTRUE) {
            if (modeMsg == MODE_CLOCK) {
                mode = MODE_CLOCK;
                clockShowEnd = millis() + CLOCK_SHOW_MS;
            }
        }
        if (mode == MODE_CLOCK && millis() >= clockShowEnd) {
            mode = MODE_EYE;
        }
        if (mode == MODE_EYE) {
            if (xQueueReceive(xEmotionQueue, &emotionMsg, 0) == pdTRUE) {
                emotion = emotionMsg;
            }
            eyeUpdate(display, emotion);
        } else {
            clockDraw(display);
        }
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

// ═══════════════════════════════════════════════════
// TaskEmotion
// ═══════════════════════════════════════════════════
static const uint8_t EMOTION_WEIGHTS[EMOTION_COUNT] = {
    55, 25, 10, 5, 5
};

static EmotionType weightedRandom() {
    int total = 0;
    for (int i = 0; i < EMOTION_COUNT; i++) total += EMOTION_WEIGHTS[i];
    int r = random(0, total), cum = 0;
    for (int i = 0; i < EMOTION_COUNT; i++) {
        cum += EMOTION_WEIGHTS[i];
        if (r < cum) return (EmotionType)i;
    }
    return EMOTION_NORMAL;
}

void TaskEmotion(void *pvParameters) {
    for (;;) {
        TickType_t wait = pdMS_TO_TICKS(10000)
            + (TickType_t)random(0, (long)pdMS_TO_TICKS(20000));
        vTaskDelay(wait);
        EmotionType e = weightedRandom();
        xQueueSend(xEmotionQueue, &e, 0);
        Serial.print(F("[EMOTION] -> "));
        Serial.println((int)e);
    }
}

// ═══════════════════════════════════════════════════
// TaskButton
// ═══════════════════════════════════════════════════
void TaskButton(void *pvParameters) {
    bool lastState = HIGH;
    for (;;) {
        bool curState = digitalRead(BUTTON_PIN);
        if (lastState == HIGH && curState == LOW) {
            vTaskDelay(pdMS_TO_TICKS(20));
            if (digitalRead(BUTTON_PIN) == LOW) {
                DisplayMode m = MODE_CLOCK;
                xQueueSend(xModeQueue, &m, 0);
                Serial.println(F("[BUTTON] -> Clock mode"));
            }
        }
        lastState = curState;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ═══════════════════════════════════════════════════
// TaskNTP
// ═══════════════════════════════════════════════════
void TaskNTP(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(NTP_RESYNC_MS));
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println(F("[NTP] Re-syncing..."));
            ntpSync();
        } else {
            Serial.println(F("[NTP] WiFi lost, reconnecting..."));
            connectWiFi();
        }
        vTaskDelay(pdMS_TO_TICKS(NTP_RESYNC_MS));
    }
}

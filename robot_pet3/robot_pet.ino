/**
 * robot_pet.ino
 * 自律型ロボットペット ── 全自動置き時計仕様
 *
 * 【動作仕様】
 *   通常: OLEDに目のアニメーション（まばたき・視線・自律感情変化）
 *   ボタン押下: 時刻表示に切替 → CLOCK_SHOW_MS後に目へ自動復帰
 *
 * 【FreeRTOSタスク構成】
 *   TaskDisplay  (優先度3) : 目 or 時刻を描画
 *   TaskEmotion  (優先度2) : 感情自律変化 → xEmotionQueue送信
 *   TaskButton   (優先度2) : ボタン検知  → xModeQueue送信
 *   TaskNTP      (優先度1) : 起動時・1時間ごとにNTP時刻同期
 *
 * 【配線】
 *   OLED SSD1306 : SDA=A4, SCL=A5（I2C）
 *   ボタン       : D2 → GND（INPUT_PULLUP）
 *
 * 【必要ライブラリ（ライブラリマネージャで導入）】
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *
 * 【標準内蔵（追加不要）】
 *   - Arduino_FreeRTOS / WiFiS3 / RTC（UNO R4ボードパッケージ）
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino_FreeRTOS.h>
#include <queue.h>
#include <WiFiS3.h>
#include <RTC.h>

#include "arduino_secrets.h"
#include "eye_animation.h"
#include "clock_display.h"
#include "ntp_sync.h"

// ── ハードウェア設定 ───────────────────────────────
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C
#define BUTTON_PIN    2          // INPUT_PULLUP（押すとLOW）

// 時刻表示を維持する時間（ms）
#define CLOCK_SHOW_MS 5000UL

// NTP再同期間隔（ms）
#define NTP_RESYNC_MS 3600000UL  // 1時間

// ── 表示モード ─────────────────────────────────────
typedef enum {
    MODE_EYE = 0,
    MODE_CLOCK
} DisplayMode;

// ── グローバル ─────────────────────────────────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

QueueHandle_t xEmotionQueue;  // EmotionType（感情）
QueueHandle_t xModeQueue;     // DisplayMode（画面切替）

// ── タスクプロトタイプ ─────────────────────────────
void TaskDisplay (void *pvParameters);
void TaskEmotion (void *pvParameters);
void TaskButton  (void *pvParameters);
void TaskNTP     (void *pvParameters);

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

    // OLED初期化
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println(F("OLED init failed"));
        while (true);
    }
    display.clearDisplay();
    display.display();

    // 内蔵RTC起動
    RTC.begin();

    // WiFi接続 → NTP初回同期
    if (connectWiFi()) {
        if (!ntpSync()) {
            Serial.println(F("[NTP] Initial sync failed"));
        }
    }

    // 目アニメーション初期化
    eyeInit(display);

    // FreeRTOSキュー生成
    xEmotionQueue = xQueueCreate(4, sizeof(EmotionType));
    xModeQueue    = xQueueCreate(2, sizeof(DisplayMode));
    if (!xEmotionQueue || !xModeQueue) {
        Serial.println(F("Queue creation failed"));
        while (true);
    }

    // タスク生成
    //                              関数          名前       スタック  引数  優先度  ハンドル
    xTaskCreate(TaskDisplay, "Display",  512, NULL, 3, NULL);
    xTaskCreate(TaskEmotion, "Emotion",  256, NULL, 2, NULL);
    xTaskCreate(TaskButton,  "Button",   128, NULL, 2, NULL);
    xTaskCreate(TaskNTP,     "NTP",      512, NULL, 1, NULL);

    vTaskStartScheduler();
}

void loop() {}

// ═══════════════════════════════════════════════════
// TaskDisplay : 描画管理（目 ↔ 時刻の切替）
// ═══════════════════════════════════════════════════
void TaskDisplay(void *pvParameters) {
    DisplayMode  mode          = MODE_EYE;
    EmotionType  emotion       = EMOTION_NORMAL;
    unsigned long clockShowEnd = 0;
    DisplayMode  modeMsg;
    EmotionType  emotionMsg;

    for (;;) {
        // モード切替メッセージをチェック
        if (xQueueReceive(xModeQueue, &modeMsg, 0) == pdTRUE) {
            if (modeMsg == MODE_CLOCK) {
                mode = MODE_CLOCK;
                clockShowEnd = millis() + CLOCK_SHOW_MS;
            }
        }

        // 時刻モードのタイムアウトで目に自動復帰
        if (mode == MODE_CLOCK && millis() >= clockShowEnd) {
            mode = MODE_EYE;
        }

        if (mode == MODE_EYE) {
            // 感情更新（ノンブロッキング）
            if (xQueueReceive(xEmotionQueue, &emotionMsg, 0) == pdTRUE) {
                emotion = emotionMsg;
            }
            eyeUpdate(display, emotion);
        } else {
            clockDraw(display);
        }

        vTaskDelay(pdMS_TO_TICKS(33));  // 約30fps
    }
}

// ═══════════════════════════════════════════════════
// TaskEmotion : 重み付きランダム自律感情変化
// ═══════════════════════════════════════════════════
static const uint8_t EMOTION_WEIGHTS[EMOTION_COUNT] = {
    55,  // NORMAL : 55%
    25,  // HAPPY  : 25%（たまに嬉しそう）
    10,  // SAD    : 10%
     5,  // HOT    :  5%
     5,  // ANGRY  :  5%
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
        // 10〜30秒のランダム間隔で感情変化
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
// TaskButton : ボタン検知（チャタリング除去付き）
// ═══════════════════════════════════════════════════
void TaskButton(void *pvParameters) {
    bool lastState = HIGH;  // INPUT_PULLUP: 未押下=HIGH

    for (;;) {
        bool curState = digitalRead(BUTTON_PIN);

        // 立ち下がり検出（HIGH → LOW = 押した瞬間）
        if (lastState == HIGH && curState == LOW) {
            // チャタリング除去: 20ms後に再確認
            vTaskDelay(pdMS_TO_TICKS(20));
            if (digitalRead(BUTTON_PIN) == LOW) {
                DisplayMode m = MODE_CLOCK;
                xQueueSend(xModeQueue, &m, 0);
                Serial.println(F("[BUTTON] -> Clock mode"));
            }
        }
        lastState = curState;
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms周期でポーリング
    }
}

// ═══════════════════════════════════════════════════
// TaskNTP : 1時間ごとにNTP再同期（最低優先）
// ═══════════════════════════════════════════════════
void TaskNTP(void *pvParameters) {
    // 初回同期はsetup()で完了済み。1時間後から再同期開始。
    vTaskDelay(pdMS_TO_TICKS(NTP_RESYNC_MS));

    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println(F("[NTP] Re-syncing..."));
            ntpSync();
        } else {
            // WiFi切断時は再接続を試みる
            Serial.println(F("[NTP] WiFi lost, reconnecting..."));
            connectWiFi();
        }
        vTaskDelay(pdMS_TO_TICKS(NTP_RESYNC_MS));
    }
}

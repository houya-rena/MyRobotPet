/**
 * robot_pet.ino
 * 多機能液晶ロボットガジェット メインスケッチ（Arduino IDE版）
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

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino_FreeRTOS.h>
#include <queue>
#include <WiFiS3.h>
#include <RTC.h>

// ヘッダファイル（同じフォルダに配置）
#include "eye_animation.h"
#include "clock_display.h"
#include "ntp_sync.h"
#include "arduino_secrets.h"
#include "Weather_task.h" // 追加

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

// ── WiFi接続 ──────────────────────────────────────
static bool connectWiFi() {
    if (WiFi.status() == WL_NO_MODULE) {
        Serial.println(F("[WiFi] Module not found"));
        return false;
    }
    Serial.print(F("[WiFi] Connecting"));
    for (int i = 0; i < 5; i++) {
        if (WiFi.begin(SECRET_SSID, SECRET_PASS) == WL_CONNECTED) {
            Serial.println();
            Serial.print(F("[WiFi] Connected: "));
            Serial.println(WiFi.localIP());
            return true;
        }
        Serial.print('.');
        delay(500);
    }
    Serial.println(F("\n[WiFi] Failed"));
    return false;
}

// static bool connectWiFi() {
//     // 2.4GHzのチャネルやセキュリティを明示的に指定する場合の書き方
//     // まずは既存のWiFi情報で再挑戦しますが、WiFi.beginの直前に少し待機を入れます
//     WiFi.disconnect();
//     delay(1000); 
    
//     Serial.print(F("[WiFi] Connecting"));
//     WiFi.begin(SECRET_SSID, SECRET_PASS);
    
//     // ここを少し長めに待つように変更（最大40秒）
//     int timeout = 40; 
//     while (WiFi.status() != WL_CONNECTED && timeout > 0) {
//         delay(1000);
//         Serial.print('.');
//         timeout--;
//     }

// ── setup() ──────────────────────────────────────
void setup() {
    Serial.begin(9600);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        while (true);
    }
    
    // 画面に状況を表示
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Connecting WiFi...");
    display.display();

    // 1. WiFiが繋がるまでここから出ない（最強の安定化）
    while (!connectWiFi()) {
        Serial.println("Retrying connection...");
        delay(2000);
    }

    // 2. 接続後に安定するまで少し待つ
    delay(3000); 

    // 3. NTP同期（RTCの初期化もここで行う）
    RTC.begin();
    if (!ntpSync()) {
        Serial.println(F("[NTP] Initial sync failed"));
    } else {
        Serial.println(F("[NTP] Time synced successfully!"));
    }

    // 4. ここで初めて目などのタスクを起動する
    eyeInit(display);
    
    xEmotionQueue = xQueueCreate(4, sizeof(EmotionType));
    xModeQueue    = xQueueCreate(2, sizeof(DisplayMode));

    xTaskCreate(TaskDisplay, "Display", 512, NULL, 3, NULL);
    xTaskCreate(TaskEmotion, "Emotion", 256, NULL, 2, NULL);
    xTaskCreate(TaskButton,  "Button",  128, NULL, 2, NULL);
    xTaskCreate(TaskNTP,     "NTP",     512, NULL, 1, NULL);
    xTaskCreate(TaskWeather, "Weather", 1024, NULL, 1, NULL); // 追加！メモリは少し多めに確保
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

    // ══════════════════════════════════════════════════
// TaskWeather
// ══════════════════════════════════════════════════
void TaskWeather(void *pvParameters) {
    weatherTaskInit(); // 初期化（URL生成）
    
    for (;;) {
        // 天気を取得して感情タイプを判定
        EmotionType e = weatherTaskUpdate();
        
        // 成功した場合のみ、ロボットに感情を反映させる
        if (e != EMOTION_COUNT) {
            xQueueSend(xEmotionQueue, &e, 0);
            Serial.print(F("[TaskWeather] Updated emotion: "));
            Serial.println((int)e);
        }
        
        // 10分待機（WeatherTask.hの定義を使用）
        vTaskDelay(pdMS_TO_TICKS(WEATHER_INTERVAL_MS));
    }
}

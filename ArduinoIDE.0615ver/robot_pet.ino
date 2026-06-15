/**
 * robot_pet.ino
 * 多機能液晶ロボットガジェット（Arduino IDE版）
 *
 * 【タスク構成】
 *   TaskDisplay  (優先度3) : 目 / 時刻 / 天気 を描画
 *   TaskEmotion  (優先度2) : 時間帯別・重み付きランダム感情変化
 *   TaskButton   (優先度2) : 2ボタン検知 → モード切替 + 操作音
 *   TaskWeather  (優先度2) : 天気API取得 → 感情Queue送信
 *   TaskSound    (優先度1) : 鳴き声・操作音の再生
 *   TaskNTP      (優先度1) : NTP時刻同期（起動時 + 1時間ごと）
 *
 * 【ボタン仕様】
 *   D2（ボタン1）: 押す → 時刻表示
 *                  表示中に再押し or 5秒経過 → 目に戻る
 *   D3（ボタン2）: 押す → 天気表示（天気アイコン + 気温 + 顔文字）
 *                  表示中に再押し or 5秒経過 → 目に戻る
 *
 * 【配線】
 *   OLED SSD1306 : SDA=A4, SCL=A5（I2C）
 *   ボタン1      : D2 → GND（INPUT_PULLUP）
 *   ボタン2      : D3 → GND（INPUT_PULLUP）
 *   ブザー       : D8 → ブザー(+) ／ ブザー(-) → GND
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino_FreeRTOS.h>
#include <queue>
#include <WiFiS3.h>
#include <RTC.h>

#include "eye_animation.h"
#include "clock_display.h"
#include "weather_display.h"
#include "ntp_sync.h"
#include "weather_task.h"
#include "Sound.h"
#include "arduino_secrets.h"

// ── ハードウェア設定 ───────────────────────────────
#define OLED_RESET     -1
#define OLED_ADDRESS   0x3C
#define BUTTON_CLOCK   2    // ボタン1: 時刻表示
#define BUTTON_WEATHER 3    // ボタン2: 天気表示
#define BUZZER_PIN 8 // ここを確実に定義

// 各モードの自動復帰時間（ms）
#define MODE_SHOW_MS   5000UL
#define NTP_RESYNC_MS  3600000UL

// ── 表示モード ─────────────────────────────────────
typedef enum {
    MODE_EYE = 0,
    MODE_CLOCK,
    MODE_WEATHER
} DisplayMode;

// ── 時間帯型（グローバルスコープで定義）──────────────
// ※ TimeZone はシステム予約語と衝突するため RobotTimeZone を使用
typedef enum {
    TZONE_MORNING = 0,   // 06:00〜08:59
    TZONE_WORK,          // 09:00〜11:59
    TZONE_AFTERLUNCH,    // 12:00〜14:59
    TZONE_AFTERNOON,     // 15:00〜17:59
    TZONE_EVENING,       // 18:00〜21:59
    TZONE_NIGHT,         // 22:00〜05:59
    TZONE_COUNT
} RobotTimeZone;

// ── グローバル ─────────────────────────────────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

QueueHandle_t xEmotionQueue;  // EmotionType（感情）
QueueHandle_t xModeQueue;     // DisplayMode（画面切替）
QueueHandle_t xSoundQueue;    // SoundEvent（鳴き声・操作音）

// 天気情報（TaskWeatherが書き込み、TaskDisplayが読む）
// Mutexなしでも整合性が保てるよう volatile + 簡易コピーで管理
static volatile int         g_weatherId      = 800;
static volatile float       g_weatherTemp    = 25.0f;
static volatile EmotionType g_weatherEmotion = EMOTION_NORMAL;
static char                 g_weatherDesc[32] = "clear sky";
static TickType_t lastConnectAttempt = 0; // WiFi接続試行時間を保持する変数

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
    pinMode(BUTTON_CLOCK,   INPUT_PULLUP);
    pinMode(BUTTON_WEATHER, INPUT_PULLUP);

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
    weatherTaskInit();
    // soundInit();

    xEmotionQueue = xQueueCreate(4, sizeof(EmotionType));
    xModeQueue    = xQueueCreate(4, sizeof(DisplayMode));
    // xSoundQueue   = xQueueCreate(4, sizeof(SoundEvent));
    // if (!xEmotionQueue || !xModeQueue || !xSoundQueue) {
    //     Serial.println(F("Queue creation failed"));
    //     while (true);
    // }
    xTaskCreate(TaskDisplay, "Display", 2048, NULL, 3, NULL);
    xTaskCreate(TaskEmotion, "Emotion", 256, NULL, 2, NULL);
    xTaskCreate(TaskButton,  "Button",  256, NULL, 2, NULL);
    xTaskCreate(TaskWeather, "Weather", 1024, NULL, 2, NULL);
    // xTaskCreate(TaskSound,   "Sound",   384, NULL, 1, NULL);
    xTaskCreate(TaskNTP,     "NTP",     384, NULL, 1, NULL);
        vTaskStartScheduler();
}

void loop() {}

// ═══════════════════════════════════════════════════
// TaskDisplay : 描画管理
//   MODE_EYE     → 目アニメーション
//   MODE_CLOCK   → 時刻画面（5秒 or 再押しで戻る）
//   MODE_WEATHER → 天気画面（5秒 or 再押しで戻る）
// ═══════════════════════════════════════════════════
void TaskDisplay(void *pvParameters) {
    DisplayMode   mode     = MODE_EYE;
    EmotionType   emotion  = EMOTION_NORMAL;
    unsigned long showEnd  = 0;      // タイムアウト管理
    DisplayMode   modeMsg;
    EmotionType   emotionMsg;

    for (;;) {
        // ── モード切替メッセージ受信 ──────────────
        if (xQueueReceive(xModeQueue, &modeMsg, 0) == pdTRUE) {
            if (modeMsg == MODE_EYE) {
                // 明示的な「目に戻る」
                mode = MODE_EYE;
            } else if (modeMsg == mode && mode != MODE_EYE) {
                // 同じモードのボタンを再押し → 即座に目に戻る
                mode = MODE_EYE;
                Serial.println(F("[DISPLAY] re-press -> back to eye"));
            } else {
                // 新しいモードへ切替
                mode    = modeMsg;
                showEnd = millis() + MODE_SHOW_MS;
            }
        }

        // ── タイムアウトで目に自動復帰 ────────────
        if (mode != MODE_EYE && millis() >= showEnd) {
            mode = MODE_EYE;
        }

        // ── 描画 ─────────────────────────────────
        switch (mode) {
            case MODE_EYE:
                if (xQueueReceive(xEmotionQueue, &emotionMsg, 0) == pdTRUE) {
                    emotion = emotionMsg;
                }
                eyeUpdate(display, emotion);
                break;

            case MODE_CLOCK:
                clockDraw(display);
                break;

            case MODE_WEATHER:
                weatherDraw(display,
                            (int)g_weatherId,
                            (float)g_weatherTemp,
                            g_weatherDesc,
                            (EmotionType)g_weatherEmotion);
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

// ═══════════════════════════════════════════════════
// TaskButton : 2ボタン検知（チャタリング除去付き）
//
// 【動作ロジック】
//   - 押した瞬間（立ち下がり）を検出
//   - 20ms後に再確認（チャタリング除去）
//   - 現在のモードと同じボタン → MODE_EYE（戻る）を送信
//   - 違うボタン or 目モード中 → 対応するモードを送信
//   - いずれの場合も「ピッ」という操作音をxSoundQueueへ送信
// ═══════════════════════════════════════════════════
void TaskButton(void *pvParameters) {
    bool lastClock   = HIGH;
    bool lastWeather = HIGH;

    for (;;) {
        bool curClock   = digitalRead(BUTTON_CLOCK);
        bool curWeather = digitalRead(BUTTON_WEATHER);

        // ── ボタン1（時刻）の立ち下がり検出 ──────
        if (lastClock == HIGH && curClock == LOW) {
            vTaskDelay(pdMS_TO_TICKS(20));
            if (digitalRead(BUTTON_CLOCK) == LOW) {
                // TaskDisplay側でモード判定するのでそのまま送信
                DisplayMode m = MODE_CLOCK;
                xQueueSend(xModeQueue, &m, 0);

//                 Event se = { true, EMOTION_NORMAL };
//                 xQueueSend(xSoundQueue, &se, 0);

                Serial.println(F("[BTN1] clock"));
            }
        }

        // ── ボタン2（天気）の立ち下がり検出 ──────
        if (lastWeather == HIGH && curWeather == LOW) {
            vTaskDelay(pdMS_TO_TICKS(20));
            if (digitalRead(BUTTON_WEATHER) == LOW) {
                DisplayMode m = MODE_WEATHER;
                xQueueSend(xModeQueue, &m, 0);

                // SoundEvent se = { true, EMOTION_NORMAL };
                // xQueueSend(xSoundQueue, &se, 0);

                Serial.println(F("[BTN2] weather"));
            }
        }

        lastClock   = curClock;
        lastWeather = curWeather;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ═══════════════════════════════════════════════════
// TaskEmotion : 時間帯別・重み付きランダム感情変化
// ═══════════════════════════════════════════════════

//                                NOR  HAP  SAD  HOT  ANG  SLP  CON  WOR  VOI
static const uint8_t TIME_WEIGHTS[TZONE_COUNT][EMOTION_COUNT] = {
    /* MORNING      */ { 30,  35,   5,   5,   5,  10,   5,   0,   5 },
    /* WORK         */ { 50,  15,   5,   5,   5,   5,  10,   5,   0 },
    /* AFTERLUNCH   */ { 20,  10,   5,   5,   5,  40,   5,   5,   5 },
    /* AFTERNOON    */ { 35,  15,  10,   5,  10,  10,   5,   5,   5 },
    /* EVENING      */ { 30,  35,   5,   5,   5,  10,   5,   0,   5 },
    /* NIGHT        */ { 20,   5,  10,   0,   5,  35,   5,   5,  15 },
};

static RobotTimeZone getCurrentTimeZone() {
    RTCTime now;
    RTC.getTime(now);
    int h = now.getHour();
    if      (h >= 6  && h < 9 ) return TZONE_MORNING;
    else if (h >= 9  && h < 12) return TZONE_WORK;
    else if (h >= 12 && h < 15) return TZONE_AFTERLUNCH;
    else if (h >= 15 && h < 18) return TZONE_AFTERNOON;
    else if (h >= 18 && h < 22) return TZONE_EVENING;
    else                         return TZONE_NIGHT;
}

static EmotionType weightedRandom(const uint8_t *weights) {
    int total = 0;
    for (int i = 0; i < EMOTION_COUNT; i++) total += weights[i];
    if (total == 0) return EMOTION_NORMAL;
    int r = random(0, total), cum = 0;
    for (int i = 0; i < EMOTION_COUNT; i++) {
        cum += weights[i];
        if (r < cum) return (EmotionType)i;
    }
    return EMOTION_NORMAL;
}

void TaskEmotion(void *pvParameters) {
    for (;;) {
        TickType_t wait = pdMS_TO_TICKS(10000)
            + (TickType_t)random(0, (long)pdMS_TO_TICKS(20000));
        vTaskDelay(wait);

        RobotTimeZone tz = getCurrentTimeZone();
        EmotionType e = weightedRandom(TIME_WEIGHTS[tz]);
        xQueueSend(xEmotionQueue, &e, 0);

        // // 鳴き声を再生
        // SoundEvent se = { false, e };
        // xQueueSend(xSoundQueue, &se, 0);

        Serial.print(F("[EMOTION] zone="));
        Serial.print((int)tz);
        Serial.print(F(" -> "));
        Serial.println((int)e);
    }
}

// ═══════════════════════════════════════════════════
// TaskWeather : OpenWeatherMap取得（10分周期）
//   取得結果をグローバル変数に保存
//   → ボタン2押下時に TaskDisplay が参照して天気画面を描画
// ═══════════════════════════════════════════════════
void TaskWeather(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(30000)); 

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            TickType_t currentTime = xTaskGetTickCount();
            if (currentTime - lastConnectAttempt > pdMS_TO_TICKS(60000)) {
                Serial.println(F("[WEATHER] reconnecting..."));
                connectWiFi();
                lastConnectAttempt = currentTime;
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        EmotionType e = weatherTaskUpdate();
        // ... (ここに weatherTaskUpdate の処理を続けてください) ...

        vTaskDelay(pdMS_TO_TICKS(WEATHER_INTERVAL_MS));
    } // ← for(;;) の終わり
} // ← TaskWeather 関数の終わり（重要！）
// ═══════════════════════════════════════════════════
// TaskSound : 鳴き声・操作音の再生
//
// xSoundQueue を待ち受け、SoundEventに応じて
//   isClick = true  → playClick()（ボタン操作音）
//   isClick = false → playCry(emotion)（感情の鳴き声）
// を再生する。再生中は vTaskDelay でCPUを他タスクに譲る
// ため、アニメーションやボタン監視は止まらない。
// ═══════════════════════════════════════════════════
void TaskSound(void *pvParameters) {
    SoundEvent se;

    for (;;) {
        // Queueが空のときは永久待機（CPUを消費しない）
        if (xQueueReceive(xSoundQueue, &se, portMAX_DELAY) == pdTRUE) {
            if (se.isClick) {
                playClick();
            } else {
                playCry(se.emotion);
            }
        }
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

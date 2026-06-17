#include <FreeRTOSConfig.h>
#include <Arduino_FreeRTOS.h>

#define ENABLE_SOUND   1

/**
 * robot_pet.ino
 * 多機能液晶ロボットガジェット（Arduino IDE版）
 *
 * 【タスク構成】
 * TaskDisplay  (優先度3) : 目 / 時刻 / 天気 を描画
 * TaskEmotion  (優先度2) : 時間帯別・重み付きランダム感情変化
 * TaskButton   (優先度2) : 2ボタン検知 → モード切替
 * TaskNTP      (優先度1) : NTP時刻同期（起動時 + 1時間ごと）
 * TaskSound    (優先度1) : 感情に応じた鳴き声再生
 *
 * 【ボタン仕様】
 * D2（ボタン1）: 押す → 時刻表示
 *              表示中に再押し or 5秒経過 → 目に戻る
 *
 * 【配線】
 * OLED SSD1306 : SDA=A4, SCL=A5（I2C）
 * ボタン1      : D2 → GND（INPUT_PULLUP）
 * ボタン2      : D3 → GND（INPUT_PULLUP）
 *
 * 【修正履歴 (デバッグ対応)】
 *  - configCHECK_FOR_STACK_OVERFLOW / malloc failed フックを追加し、
 *    BKPTクラッシュ時に原因（スタックオーバーフロー or ヒープ枯渇）を
 *    シリアルへ出力するようにした。
 *  - 各タスクのスタックサイズ合計が大きすぎてFreeRTOSヒープを
 *    使い切っていたため、見直して削減した。
 *    (weather_task.cpp 側の jsonBuf / JsonDocument を static 化したことで
 *     TaskWeather のスタック使用量も大きく減っている)
 *  - TIME_WEIGHTS[][] の列数が EMOTION_COUNT と不一致だったため、
 *    EMOTION_RAINY 分の重み(0)を追加して配列サイズを揃えた。
 *  - 欠落していた <WiFiUdp.h> の include を復元した。
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino_FreeRTOS.h>
#include <WiFiUdp.h>
#include <WiFiS3.h>
#include <RTC.h>

#include "eye_animation.h"
#include "clock_display.h"
#include "ntp_sync.h"
#include "Sound.h"
#include "arduino_secrets.h"

// ── ハードウェア設定 ───────────────────────────────
#define OLED_RESET     -1
#define OLED_ADDRESS   0x3C
#define BUTTON_CLOCK   2    // ボタン1: 時刻表示
#define BUZZER_PIN     5    // 圧電ブザーのピン（環境に合わせて変更してください）

// 各モードの自動復帰時間（ms）
#define MODE_SHOW_MS   5000UL
#define NTP_RESYNC_MS  3600000UL


// ── 表示モード ─────────────────────────────────────
typedef enum {
    MODE_EYE = 0,
    MODE_CLOCK,
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
// ── グローバル変数 ────────────────────────────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
QueueHandle_t xEmotionQueue, xModeQueue, xSoundQueue;
TaskHandle_t xDisplayHandle = NULL;

// ── 2. 感情重みテーブル (先に宣言) ───────────────────────
static const uint8_t TIME_WEIGHTS[TZONE_COUNT][EMOTION_COUNT] = {
    { 30, 35, 5, 5, 5, 10, 5, 0, 5}, // MORNING
    { 50, 15, 5, 5, 5, 5, 10, 5, 0}, // WORK
    { 20, 10, 5, 5, 5, 40, 5, 5, 5}, // AFTERLUNCH
    { 35, 15, 10, 5, 10, 10, 5, 5, 5}, // AFTERNOON
    { 30, 35, 5, 5, 5, 10, 5, 0, 5}, // EVENING
    { 20, 5, 10, 0, 5, 35, 5, 5, 15}  // NIGHT
};
// ── 3. WiFi接続関数 (先に宣言) ──────────────────────────
static bool connectWiFi() {

    Serial.print(F("[WiFi] Connecting to "));
    Serial.println(SECRET_SSID);

    WiFi.begin(SECRET_SSID, SECRET_PASS);

    // 諦めずにずっと待つ（ロボットが起動して目が動いている間、裏で繋がるまで待機）
    int count = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
        count++;
        // 30秒以上経過したら一度リセットを試みるなど工夫も可能
        if (count > 60) { 
            Serial.println(F("\n[WiFi] Still connecting..."));
            return false;
        }
    }

    Serial.println(F("\n[WiFi] Connected!"));
    return true;
}

// ── (1) WiFi通信専用タスク (Bus Fault回避のため完全分離) ──────

// void TaskWifiSync(void *pvParameters) {

//     vTaskDelay(pdMS_TO_TICKS(8000)); // 起動直後のノイズを避けて待機

//     Serial.println(F("[SYSTEM] WiFiSync Task starting..."));
//     if (connectWiFi()) {
//         ntpSync();
//     }
//     vTaskDelete(NULL);
// }

void TaskWifiSync(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    Serial.println(F("--- [SYSTEM] WiFiSync Task starting... ---"));
    
    // 接続の試行回数をログにだす
    int attempt = 0;
    while(WiFi.status() != WL_CONNECTED) {
        Serial.print(F("[WiFi] Attempt "));
        Serial.println(++attempt);
        if (connectWiFi()) break;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    Serial.println(F("[SYSTEM] WiFi Connected! Trying NTP..."));
    if (ntpSync()) {
        Serial.println(F("[SYSTEM] NTP Sync SUCCESS!"));
    } else {
        Serial.println(F("[SYSTEM] NTP Sync FAILED."));
    }
    vTaskDelete(NULL);
}

// ── (2) 初期化タスク (各タスクを一斉起動) ──────────────────
void TaskInitAndLaunch(void *pvParameters) {
    RTC.begin();
    soundInit();
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        display.clearDisplay();
        display.display();
        eyeInit(display);
    }
    
    xTaskCreate(TaskDisplay, "Display", 384, NULL, 3, &xDisplayHandle);
    xTaskCreate(TaskEmotion, "Emotion", 192, NULL, 1, NULL);
    xTaskCreate(TaskButton,  "Button",  128, NULL, 2, NULL);
    xTaskCreate(TaskSound,   "Sound",   352, NULL, 1, NULL);
    xTaskCreate(TaskNTP,     "NTP",     256, NULL, 1, NULL);

    vTaskDelete(NULL);
}

// ── (3) setup() (スケジューラ起動のみに集中) ──────────────
void setup() {
    Serial.begin(9600);
    pinMode(BUTTON_CLOCK, INPUT_PULLUP);
    analogWrite(BUZZER_PIN, 0);

    xEmotionQueue = xQueueCreate(4, sizeof(EmotionType));
    xModeQueue    = xQueueCreate(4, sizeof(DisplayMode));
    xSoundQueue   = xQueueCreate(4, sizeof(SoundEvent));

    xTaskCreate(TaskInitAndLaunch, "Init", 512, NULL, 4, NULL);
    xTaskCreate(TaskWifiSync, "WifiSync", 768, NULL, 1, NULL);
    
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

        unsigned long now = millis();

        // ── モード切替メッセージ受信 ──────────────
        if (xQueueReceive(xModeQueue, &modeMsg, 0) == pdTRUE) {
            if (modeMsg == MODE_EYE) {
                // 明示的な「目に戻る」
                mode = MODE_EYE;

            } else if (modeMsg == mode) {
                // 同じモードのボタンを再押し → 即座に目に戻る
                mode = MODE_EYE;

                Serial.println(F("[DISPLAY] re-press -> back to eye"));
            } else {
                // 新しいモード(時計)へ切替
                mode    = modeMsg;
                showEnd = now + MODE_SHOW_MS;
               
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
        }
        
        vTaskDelay(pdMS_TO_TICKS(66));
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
// ═══════════════════════════════════════════════════
void TaskButton(void *pvParameters) {
    bool lastClock   = HIGH;
   
    for (;;) {
        bool curClock   = digitalRead(BUTTON_CLOCK);
       
        // ── ボタン1（時刻）の立ち下がり検出 ──────
        if (lastClock == HIGH && curClock == LOW) {
            vTaskDelay(pdMS_TO_TICKS(20));
            if (digitalRead(BUTTON_CLOCK) == LOW) {
                // // TaskDisplay側でモード判定するのでそのまま送信
                // DisplayMode m = MODE_CLOCK;
                // xQueueSend(xModeQueue, &m, 0);
                // Serial.println(F("[BTN1] clock"));
                SoundEvent btnEvent;
                btnEvent.isClick = true;
                btnEvent.emotion = EMOTION_NORMAL;
                xQueueSend(xSoundQueue, &btnEvent, 0);

                DisplayMode m = MODE_CLOCK;
                xQueueSend(xModeQueue, &m, 0);
                Serial.println(F("[BTN1] clock"));
            }
        }

        lastClock   = curClock;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ═══════════════════════════════════════════════════
// TaskEmotion : 時間帯別・重み付きランダム感情変化
// ═══════════════════════════════════════════════════

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

        SoundEvent event;
        event.isClick = false;   
        event.emotion = e;       
        xQueueSend(xSoundQueue, &event, 0); 

        Serial.print(F("[EMOTION] zone="));
        Serial.print((int)tz);
        Serial.print(F(" -> "));
        Serial.println((int)e);

    }
}
// ═══════════════════════════════════════════════════
// TaskNTP
// ═══════════════════════════════════════════════════
// void TaskNTP(void *pvParameters) {
//     vTaskDelay(pdMS_TO_TICKS(NTP_RESYNC_MS));
//     for (;;) {
//         if (WiFi.status() == WL_CONNECTED) {
//             Serial.println(F("[NTP] Re-syncing..."));
//             ntpSync();
//         } else {
//             Serial.println(F("[NTP] WiFi lost, reconnecting..."));
//             connectWiFi();
//         }
//         vTaskDelay(pdMS_TO_TICKS(NTP_RESYNC_MS));
//     }
// }

void TaskNTP(void *pvParameters) {
    // 起動時の初回同期（既にWiFiは繋がっている前提）
    ntpSync();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(NTP_RESYNC_MS));
        ntpSync();
    }
}

// ═══════════════════════════════════════════════════
// TaskSound : 感情キューを受け取り、鳴き声を再生
// ═══════════════════════════════════════════════════
void TaskSound(void *pvParameters) {
    SoundEvent event;
    for (;;) {
        // キューにデータが届くまで、ここで完全に待機（CPUを他のタスクに譲る）
        if (xQueueReceive(xSoundQueue, &event, portMAX_DELAY) == pdTRUE) {
            
            // ★シリアルモニター確認用のログ！必ずこれが出ます
            Serial.print(F("[SOUND TASK] Received event! isClick="));
            Serial.print(event.isClick);
            Serial.print(F(", Emotion ID="));
            Serial.println((int)event.emotion);
            
            if (event.isClick) {
                playSwitchSound(); // ボタン操作音（ピッ）
            } else {
                playCry(event.emotion); // 感情の鳴き声（ピロリ♪など）
            }
        }
    }
}
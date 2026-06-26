#include <Arduino_FreeRTOS.h>
#include <FreeRTOSConfig.h>
#define ENABLE_SOUND   0
// #define ENABLE_WEATHER 0

/**
 * robot_pet.ino
 * 多機能液晶ロボットガジェット（Arduino IDE版）
 *
 * 【タスク構成】
 * TaskDisplay  (優先度3) : 目 / 時刻 / 天気 を描画
 * TaskEmotion  (優先度2) : 時間帯別・重み付きランダム感情変化
 * TaskButton   (優先度2) : 2ボタン検知 → モード切替
 * TaskWeather  (優先度2) : 天気API取得 → 感情Queue送信
 * TaskNTP      (優先度1) : NTP時刻同期（起動時 + 1時間ごと）
 * TaskSound    (優先度1) : 感情に応じた鳴き声再生
 *
 * 【ボタン仕様】
 * D2（ボタン1）: 押す → 時刻表示
 *              表示中に再押し or 5秒経過 → 目に戻る
 * D3（ボタン2）: 押す → 天気表示（天気アイコン + 気温 + 顔文字）
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
#include "weather_display.h"
#include "ntp_sync.h"
#include "weather_task.h"
#include "arduino_secrets.h"
#include "Sound.h"

// ── ハードウェア設定 ───────────────────────────────
#define OLED_RESET     -1
#define OLED_ADDRESS   0x3C
#define BUTTON_CLOCK   2    // ボタン1: 時刻表示
#define BUTTON_WEATHER 3    // ボタン2: 天気表示
// BUZZER_PIN は Sound.h で定義済み

// 各モードの自動復帰時間（ms）
#define MODE_SHOW_MS   5000UL
#define NTP_RESYNC_MS  3600000UL

// プロトタイプ宣言を追加
void weatherTaskInit();
EmotionType weatherTaskUpdate();
int weatherGetId();
float weatherGetTemp();
const char* weatherGetDesc();

// これにより、ヘッダーファイルがなくてもリンクエラーが解消されます

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

// 1. グローバルにタスク用の領域を確保する（ヒープを消費しない）
static StackType_t weatherTaskStack[768]; // 768 words = 3KB
static StaticTask_t weatherTaskBuffer;

QueueHandle_t xEmotionQueue;  // EmotionType（感情）
QueueHandle_t xModeQueue;     // DisplayMode（画面切替）
QueueHandle_t xSoundQueue;    // EmotionType（鳴き声・操作音）

// 天気情報（TaskWeatherが書き込み、TaskDisplayが読む）
// Mutexなしでも整合性が保てるよう volatile + 簡易コピーで管理
volatile int         g_weatherId      = 800;
volatile float       g_weatherTemp    = 25.0f;

static volatile EmotionType g_weatherEmotion = EMOTION_NORMAL;
static char                 g_weatherDesc[32] = "clear sky";

// ── WiFi接続 ──────────────────────────────────────
// 【重要】UNO R4 WiFi (ボードパッケージ 1.6.0) の既知バグ:
//   WiFi.status() は内部で firmwareVersion() を呼ぶため、
//   WiFiモジュール(ESP32-S3)の起動が完了する前に呼ぶと
//   WiFi.cpp:20 でクラッシュ(BKPT)する。
//   → setup()でdelay(3000)を入れて起動を待ってから呼ぶこと。
//   → WL_NO_MODULE チェックは begin() の戻り値で代替する。
static bool connectWiFi() {
    Serial.print(F("[WiFi] Connecting to "));
    Serial.println(SECRET_SSID);

    // テザリング環境向け：静的IP設定
    // iPhoneのテザリングは通常 172.20.10.x を使うことが多いです
    IPAddress ip(172, 20, 10, 15);
    IPAddress gateway(172, 20, 10, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.config(ip, gateway, subnet);

    // begin は最初に1回だけ呼ぶのが基本です
    int status = WiFi.begin(SECRET_SSID, SECRET_PASS);

    if (status == WL_NO_MODULE) {
        Serial.println(F("[WiFi] Module not found"));
        return false;
    }

    // 接続完了を最大20回（計10秒程度）待機する
    for (int i = 0; i < 20; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println(F("\n[WiFi] Connected!"));
            Serial.print(F("IP Address: "));
            Serial.println(WiFi.localIP());
            return true;
        }
        Serial.print('.');
        delay(500); // 1.5秒は長すぎるため0.5秒に短縮
    }

    Serial.println(F("\n[WiFi] Connection Failed"));
    return false;
}

// ═══════════════════════════════════════════════════
// デバッグ用フック
//   - スタックオーバーフロー検出
//   - ヒープ確保失敗検出（pvPortMalloc失敗時）
// 両方とも FreeRTOS.h 側で extern "C" として宣言されているため、
// 同じリンケージで定義する必要がある。
// ═══════════════════════════════════════════════════
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    Serial.print(F("!!! STACK OVERFLOW in task: "));
    Serial.println(pcTaskName);
    Serial.flush();
    while (true) { delay(100); }
}

extern "C" void vApplicationMallocFailedHook(void) {
    Serial.println(F("!!! MALLOC FAILED (FreeRTOS heap exhausted) !!!"));
    Serial.flush();
    while (true) { delay(100); }
}

// ── setup() ──────────────────────────────────────
void setup() {
    Serial.begin(9600);

    // WiFiモジュール(ESP32-S3)の起動完了を待つ。
    // UNO R4 WiFi ボードパッケージ 1.6.0 では、モジュール起動前に
    // WiFi.status() を呼ぶと firmwareVersion() 内でクラッシュする。
    // 3秒待てば確実に起動が完了する。
    delay(3000);

    // ★★★ 最優先でタスクを作成する ★★★
    // どの初期化処理よりも先にメモリを確保させます
    // BaseType_t res = xTaskCreate(TaskWeather, "Weather", 1024, NULL, 2, NULL);
    // if (res != pdPASS) {
    //     Serial.println(F("!!! TaskWeather creation FAILED !!!"));
    // }

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
            Serial.println(F("[NTP] Initial sync failed, will try in task"));
        }
    }

    eyeInit(display);
    weatherTaskInit();
    soundInit();

    xEmotionQueue = xQueueCreate(4, sizeof(EmotionType));
    xModeQueue    = xQueueCreate(4, sizeof(DisplayMode));
    xSoundQueue   = xQueueCreate(4, sizeof(EmotionType));

    if (!xEmotionQueue || !xModeQueue || !xSoundQueue) {
        Serial.println(F("Queue creation failed"));
        while (true);
    }

    // ── スタックサイズについて ─────────────────────
    // 以前は (768+512+256+1536+768+1024)=4864 words ≈ 19.4KB と
    // FreeRTOSヒープ(configTOTAL_HEAP_SIZE)を超えていたため、
    // TaskSound作成時の pvPortMalloc が失敗し BKPT クラッシュしていた。
    // weather_task.cpp 側の大きなローカル変数を static 化した上で、
    // 各タスクのスタックも見直して削減した。
    // もし vApplicationStackOverflowHook が発火する場合は、
    // 表示されたタスクのサイズを 64〜128word 単位で増やすこと。
    xTaskCreate(TaskDisplay, "Display", 512, NULL, 3, NULL);
    xTaskCreate(TaskEmotion, "Emotion", 256, NULL, 2, NULL);
    xTaskCreate(TaskButton,  "Button",  128, NULL, 2, NULL);


    xTaskCreate(TaskNTP,     "NTP",     384,  NULL, 1, NULL);
    #if ENABLE_SOUND
        xTaskCreate(TaskSound,   "Sound",   256,  NULL, 1, NULL);
    #endif
    Serial.print(F("[INFO] Free heap before scheduler: "));
    Serial.println(xPortGetFreeHeapSize());

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
            } else if (modeMsg == mode) {
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

            // robot_pet.ino の TaskDisplay 内
            case MODE_WEATHER:
                // 描画のたびに、その瞬間の最新値（volatile変数）をローカルにコピーして渡す
                {
                    int id = (int)g_weatherId;
                    float temp = (float)g_weatherTemp;
                    EmotionType emo = (EmotionType)g_weatherEmotion;
        
                    weatherDraw(display, id, temp, g_weatherDesc, emo);
                }
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
                Serial.println(F("[BTN1] clock"));
            }
        }

        // ── ボタン2（天気）の立ち下がり検出 ──────
        if (lastWeather == HIGH && curWeather == LOW) {
            vTaskDelay(pdMS_TO_TICKS(20));
            if (digitalRead(BUTTON_WEATHER) == LOW) {
                DisplayMode m = MODE_WEATHER;
                xQueueSend(xModeQueue, &m, 0);
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

//                                NOR  HAP  SAD  HOT  ANG  SLP  CON  WOR  VOI  RAI
static const uint8_t TIME_WEIGHTS[TZONE_COUNT][EMOTION_COUNT] = {
    /* MORNING      */ { 30,  35,   5,   5,   5,  10,   5,   0,   5},
    /* WORK         */ { 50,  15,   5,   5,   5,   5,  10,   5,   0},
    /* AFTERLUNCH   */ { 20,  10,   5,   5,   5,  40,   5,   5,   5},
    /* AFTERNOON    */ { 35,  15,  10,   5,  10,  10,   5,   5,   5},
    /* EVENING      */ { 30,  35,   5,   5,   5,  10,   5,   0,   5},
    /* NIGHT        */ { 20,   5,  10,   0,   5,  35,   5,   5,  15},
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
        EmotionType dummy;

        while (xQueueReceive(xSoundQueue, &dummy, 0) == pdTRUE);

        xQueueSend(xEmotionQueue, &e, 0);
        xQueueSend(xSoundQueue,   &e, 0);

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
    vTaskDelay(pdMS_TO_TICKS(30000));  // 起動30秒後から開始

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("[WEATHER] reconnecting..."));
            connectWiFi();
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        EmotionType e = weatherTaskUpdate();
        if (e != EMOTION_COUNT) {
            // グローバル天気情報を更新
            EmotionType dummy;

            g_weatherId      = weatherGetId();
            g_weatherTemp    = weatherGetTemp();
            g_weatherEmotion = e;
            strncpy(g_weatherDesc, weatherGetDesc(), sizeof(g_weatherDesc) - 1);
            g_weatherDesc[sizeof(g_weatherDesc) - 1] = '\0';

            while (xQueueReceive(xEmotionQueue, &dummy, 0) == pdTRUE);
            xQueueSend(xEmotionQueue, &e, 0);

            while (xQueueReceive(xSoundQueue, &dummy, 0) == pdTRUE);
            xQueueSend(xSoundQueue, &e, 0);

            Serial.print(F("[WEATHER] id="));
            Serial.print(g_weatherId);
            Serial.print(F(" temp="));
            Serial.print(g_weatherTemp);
            Serial.print(F(" -> emotion="));
            Serial.println((int)e);
        }

        vTaskDelay(pdMS_TO_TICKS(WEATHER_INTERVAL_MS));
    }
}

// ═══════════════════════════════════════════════════
// TaskSound : 感情キューを受け取り、鳴き声を再生
// ═══════════════════════════════════════════════════
void TaskSound(void *pvParameters) {
    EmotionType emotion;

    for (;;) {
        if (xQueueReceive(xSoundQueue, &emotion, portMAX_DELAY) == pdTRUE) {
            playCry(emotion);
        }
    }
}

// ═══════════════════════════════════════════════════
// TaskNTP : 起動1時間後から定期的にNTP再同期
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

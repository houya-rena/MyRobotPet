/**
 * robot_pet.ino
 * 自律型ロボットペット メインスケッチ（FreeRTOS版）
 *
 * 【タスク構成】
 *   TaskEyeDraw  (優先度3) : OLED描画・まばたき・視線アニメーション
 *   TaskSensor   (優先度2) : 温湿度センサー読み取り → Queueへ送信
 *   TaskSerial   (優先度2) : シリアルコマンド受信  → Queueへ送信
 *   TaskEmotion  (優先度1) : 感情ステート管理・自律変化 → Queueへ送信
 *
 * 【タスク間通信】
 *   xEmotionQueue : EmotionType を送受信する深さ4のキュー
 *                   送信側: TaskSensor / TaskSerial / TaskEmotion
 *                   受信側: TaskEyeDraw
 *
 * 【依存ライブラリ】
 *   - Adafruit SSD1306
 *   - Adafruit GFX
 *   - Arduino_FreeRTOS (UNO R4コアに標準内蔵)
 *   - DHT sensor library (温湿度センサー用)
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino_FreeRTOS.h>
#include <queue.h>
#include <semphr.h>

#include "eye_animation.h"
#include "serial_comm.h"
#include "sensor_task.h"

// ── ハードウェア設定 ───────────────────────────────
#define OLED_RESET   -1
#define OLED_ADDRESS 0x3C

// ── FreeRTOS オブジェクト（extern で各タスクから参照）──
QueueHandle_t   xEmotionQueue;   // EmotionType を渡すキュー
SemaphoreHandle_t xOledMutex;    // OLED排他制御（将来の拡張用）

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── タスク関数プロトタイプ ─────────────────────────
void TaskEyeDraw (void *pvParameters);
void TaskSerial  (void *pvParameters);
void TaskSensor  (void *pvParameters);
void TaskEmotion (void *pvParameters);

// ── setup() ──────────────────────────────────────
void setup() {
    Serial.begin(9600);

    // OLED初期化
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println(F("SSD1306 init failed"));
        while (true);
    }
    display.clearDisplay();
    display.display();

    // FreeRTOS オブジェクト生成
    xEmotionQueue = xQueueCreate(4, sizeof(EmotionType));
    xOledMutex    = xSemaphoreCreateMutex();

    if (xEmotionQueue == NULL || xOledMutex == NULL) {
        Serial.println(F("FreeRTOS object creation failed"));
        while (true);
    }

    // 目アニメーション初期化
    eyeInit(display);

    // ── タスク生成 ──────────────────────────────
    //                              関数          名前          スタック  引数   優先度  ハンドル
    xTaskCreate(TaskEyeDraw,  "EyeDraw",  512, NULL, 3, NULL);
    xTaskCreate(TaskSerial,   "Serial",   256, NULL, 2, NULL);
    xTaskCreate(TaskSensor,   "Sensor",   256, NULL, 2, NULL);
    xTaskCreate(TaskEmotion,  "Emotion",  256, NULL, 1, NULL);

    // スケジューラ開始（この行以降は実行されない）
    vTaskStartScheduler();
}

// loop() は FreeRTOS 使用時は空でよい
void loop() {}

// ═══════════════════════════════════════════════
// TaskEyeDraw : OLED描画タスク（最高優先）
// ═══════════════════════════════════════════════
void TaskEyeDraw(void *pvParameters) {
    EmotionType currentEmotion = EMOTION_NORMAL;
    EmotionType received;

    for (;;) {
        // キューに感情が届いていれば取得（ノンブロッキング）
        if (xQueueReceive(xEmotionQueue, &received, 0) == pdTRUE) {
            currentEmotion = received;
        }

        // 描画更新（まばたき・視線・モーフィングを内包）
        eyeUpdate(display, currentEmotion);

        // 約30fps（33ms待機）
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

// ═══════════════════════════════════════════════
// TaskSerial : シリアルコマンド受信タスク
// ═══════════════════════════════════════════════
void TaskSerial(void *pvParameters) {
    serialCommInit();

    for (;;) {
        EmotionType e = serialCommUpdate();
        if (e != EMOTION_COUNT) {
            // キューが満杯でも上書きしない（pdFALSE = 失敗でも無視）
            xQueueSend(xEmotionQueue, &e, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ═══════════════════════════════════════════════
// TaskSensor : 温湿度センサー読み取りタスク
// ═══════════════════════════════════════════════
void TaskSensor(void *pvParameters) {
    sensorInit();

    for (;;) {
        EmotionType e = sensorUpdate();
        if (e != EMOTION_COUNT) {
            xQueueSend(xEmotionQueue, &e, 0);
        }
        // センサーは2秒間隔で十分
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ═══════════════════════════════════════════════
// TaskEmotion : 自律感情変化タスク（最低優先）
// ═══════════════════════════════════════════════

// 重み付きランダム感情テーブル
static const uint8_t EMOTION_WEIGHTS[EMOTION_COUNT] = {
    60,  // NORMAL
    20,  // HAPPY
    10,  // SAD
     5,  // HOT
     5,  // ANGRY
};

static EmotionType weightedRandomEmotion() {
    int total = 0;
    for (int i = 0; i < EMOTION_COUNT; i++) total += EMOTION_WEIGHTS[i];
    int r = random(0, total);
    int cum = 0;
    for (int i = 0; i < EMOTION_COUNT; i++) {
        cum += EMOTION_WEIGHTS[i];
        if (r < cum) return (EmotionType)i;
    }
    return EMOTION_NORMAL;
}

void TaskEmotion(void *pvParameters) {
    // 初回は10〜30秒後にランダム変化
    const TickType_t intervalMin = pdMS_TO_TICKS(10000);
    const TickType_t intervalMax = pdMS_TO_TICKS(30000);

    for (;;) {
        TickType_t wait = intervalMin
            + (TickType_t)(random(0, (long)(intervalMax - intervalMin)));
        vTaskDelay(wait);

        EmotionType e = weightedRandomEmotion();
        xQueueSend(xEmotionQueue, &e, 0);

        Serial.print(F("[AUTO] emotion -> "));
        Serial.println((int)e);
    }
}

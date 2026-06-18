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
 * OLED SSD1306                : SDA, SCL（I2C）
 * ボタン1                      : D2 → GND（INPUT_PULLUP）
 * オーディオモジュールとスピーカー : D5（可変抵抗器を使用）
 *
 * ========================================================================
 * 【修正履歴 (デバッグ・軽量化対応)】
 * ========================================================================
 * - システムの軽量化に伴う天気タスク(TaskWeather)の削除:
 * 不要な通信とメモリ消費を抑えるため天気取得機能を撤去。
 *
 * - デバッグ機能の強化:
 * configCHECK_FOR_STACK_OVERFLOW / malloc failed フックを追加。
 * BKPT発生時に原因をシリアル出力するようにし、原因特定を迅速化。
 *
 * - FreeRTOSヒープ管理の最適化:
 * 不要タスクの削除および各タスクのスタックサイズを見直し、
 * ヒープの枯渇問題を解消。
 *
 * ========================================================================
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
#define MODE_SHOW_MS   5000UL   // ULはunsiged long（符号なし長整数）の指定
#define NTP_RESYNC_MS  3600000UL


// ── 表示モード ・列挙型 (enum) ───────────────────────────────────
// 【解説】　typedef enum: 状態を「0, 1, 2...」ではなく名前で管理する仕組み
// コードの可読性が劇的に上がる
typedef enum {
    MODE_EYE = 0,
    MODE_CLOCK,
} DisplayMode;

// ── 時間帯型（グローバルスコープで定義）─────────────────────────────
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

// ── グローバル変数 ─────────────────────────────────────────────
// 【解説】 関数の外で定義することによって、どのタスクからでもアクセス可能に
// ただし、OS環境では「キュー」を介したデータのやり取りの方が安全
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // OLEDを扱うためのインスタンス
QueueHandle_t xEmotionQueue, xModeQueue, xSoundQueue; // 「どこにキューがあるか」「誰が待っているか」という情報へのポインタ
TaskHandle_t xDisplayHandle = NULL; // 作成したタスクを後から操作するためのID

// ── 2. 感情重みテーブル (先に宣言) ───────────────────────────────
// 【解説】 static const: 「静的で変更不可能な定数」
// メモリの節約になり、プログラム実行中に値が勝手に書き換わるのを防ぐ
static const uint8_t TIME_WEIGHTS[TZONE_COUNT][EMOTION_COUNT] = { // uint8_t: 必ず8ビット（1バイト）のサイズをもつ整数型
    { 30, 35, 5, 5, 5, 10, 5, 0, 5}, // MORNING
    { 50, 15, 5, 5, 5, 5, 10, 5, 0}, // WORK
    { 20, 10, 5, 5, 5, 40, 5, 5, 5}, // AFTERLUNCH
    { 35, 15, 10, 5, 10, 10, 5, 5, 5}, // AFTERNOON
    { 30, 35, 5, 5, 5, 10, 5, 0, 5}, // EVENING
    { 20, 5, 10, 0, 5, 35, 5, 5, 15}  // NIGHT
};
// ── 3. WiFi接続関数 (先に宣言) ───────────────────────────────────
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

// ── (1) WiFi通信タスク ──────────────────────────────────────────
void TaskWifiSync(void *pvParameters) {

    // 【解説】 vTaskDelay: 単なる待機ではなく、CPUを他のタスクに明け渡すOS関数
    vTaskDelay(pdMS_TO_TICKS(5000));

    // ... WiFi接続処理 ...
    Serial.println(F("--- [SYSTEM] WiFiSync Task starting... ---"));
    
    // 接続の試行回数をログにだす
    int attempt = 0; // カウンターをゼロで初期化

    // 未接続の間繰り返し実行
    while(WiFi.status() != WL_CONNECTED) {
        Serial.print(F("[WiFi] Attempt "));
        Serial.println(++attempt); // n回目で失敗したのかを可視化

        // もし接続成功ならループを抜ける
        if (connectWiFi()) break; 

        // 接続待ちの時間もOLEDに描画を表示させるため
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    Serial.println(F("[SYSTEM] WiFi Connected! Trying NTP..."));

    // 時刻同期(NTP)だけ失敗したか、成功したかをログに残す
    if (ntpSync()) {
        Serial.println(F("[SYSTEM] NTP Sync SUCCESS!"));
    } else {
        Serial.println(F("[SYSTEM] NTP Sync FAILED."));
    }

    // 【解説】 vTaskDelete(NULL): 自分のタスクを終了し、スタックメモリを解放する
    vTaskDelete(NULL);
}

// ── (2) 初期化タスク (各タスクを一斉起動) ──────────────────
void TaskInitAndLaunch(void *pvParameters) {

    // 各ハードウェアの初期化
    RTC.begin();
    soundInit();
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        display.clearDisplay();
        display.display();
        eyeInit(display);
    }
    
    // 【解説】 xTaskCreate: OSに新しい「仕事（タスク）」を登録
    // スタックサイズ（第3引数）の決定が、メモリ枯渇を防ぐポイント
    xTaskCreate(TaskDisplay, "Display", 384, NULL, 3, &xDisplayHandle);
    xTaskCreate(TaskEmotion, "Emotion", 192, NULL, 1, NULL);
    xTaskCreate(TaskButton,  "Button",  128, NULL, 2, NULL);
    xTaskCreate(TaskSound,   "Sound",   352, NULL, 1, NULL);
    xTaskCreate(TaskNTP,     "NTP",     256, NULL, 1, NULL);

    vTaskDelete(NULL); // 自分のタスクを終了し、スタックメモリを解放する
}

// ── (3) setup() (初期化関数) ──────────────
// ここではタスク生成とOS起動（スケジューラー起動）のみに集中
void setup() {
    Serial.begin(9600);

    pinMode(BUTTON_CLOCK, INPUT_PULLUP); // ボタンピンを内蔵プルアップ抵抗に定義
    analogWrite(BUZZER_PIN, 0);          // スピーカから鳴らす音を常にオフ（0:LOW）にする

    // xQueueCreate: タスク間通信用の「伝言板」を作成

    // 感情の変化を伝える
    xEmotionQueue = xQueueCreate(4, sizeof(EmotionType)); // EmotionType（感情の種別ID）を受け取った TaskDisplay は、顔の絵を変える
    // 「ボタンが押されたからモードを切り替えて」という命令
    xModeQueue    = xQueueCreate(4, sizeof(DisplayMode)); // DisplayMode（目モードか、時計モードか）
    // 「今の感情に合わせた鳴き声を再生して」という命令
    xSoundQueue   = xQueueCreate(4, sizeof(SoundEvent));  //  SoundEvent（「ボタン音か？」「何の感情の鳴き声か？」という情報が詰まった構造体）

    xTaskCreate(TaskInitAndLaunch, "Init", 512, NULL, 4, NULL);
    xTaskCreate(TaskWifiSync, "WifiSync", 768, NULL, 1, NULL);
    
    vTaskStartScheduler();
}

void loop() {} // 空っぽ（OS環境では非使用）

// ═══════════════════════════════════════════════════
// TaskDisplay : 描画管理
//   MODE_EYE     → 目アニメーション
//   MODE_CLOCK   → 時刻画面（5秒 or 再押しで戻る）
// ═══════════════════════════════════════════════════
void TaskDisplay(void *pvParameters) {

    // 【状態管理用の変数】ローカル変数（この関数内でのみ有効）
    // 「static」をつけない場合、タスクのスタック領域に確保される
    // mode: 現在どの画面を表示しているか（目か時計か）
    // emotion: 現在の感情ID
    DisplayMode   mode     = MODE_EYE;
    EmotionType   emotion  = EMOTION_NORMAL;

    // 【タイムアウト管理】
    // 現在の時刻（now）と比較して「何時まで表示するか」を保持
    unsigned long showEnd  = 0;      // 符号なし長整数

    // 【一時保存用変数】
    // キューから届いたメッセージを受け取るための入れ物
    DisplayMode   modeMsg;
    EmotionType   emotionMsg;


    // 【無限ループ】
    // OSのタスクは一度終了すると消滅してしまうため、
    // ループ内で常に動かし続ける必要があるため
    for (;;) {

        // 【現在時刻の取得】
        // システム起動から経過時間を取得（単位：ミリ秒）
        // delay()を使用せず、時間で処理を制御するための心臓部
        unsigned long now = millis();

        // ── モード切替メッセージ受信 ──────────────────────────
        // 【キューの確認】
        // xQueueReceive: キューにデータが来るのを待つ
        // 他のタスク（ボタン等）から「モードを変えろ」という指示が来ているか確認
        // 第3引数の「0」は「データがなくても待たずにすすむ」という指定
        // 指示がなければ即座に次の処理へ
        if (xQueueReceive(xModeQueue, &modeMsg, 0) == pdTRUE) {
            if (modeMsg == MODE_EYE) {

                // 届いた指示が「目に戻れ」なら、即座に状態を変更
                // 明示的な「目に戻る」
                mode = MODE_EYE;

            } else if (modeMsg == mode) {

                // 同じモードのボタンを再押し → 即座に目に戻る
                mode = MODE_EYE;

                Serial.println(F("[DISPLAY] re-press -> back to eye"));
            } else {
                // 新しいモード(時計)へ切替
                // モードを変更し、さらに「終了時刻（showEnd）」を予約
                mode    = modeMsg;
                showEnd = now + MODE_SHOW_MS; // 現在時刻 + 5秒後
               
            }
        }

        // ── タイムアウトで目に自動復帰 ────────────────────────
        // 【非同期の強み】
        // もし「目」以外の画面を表示中(!=)で、かつ（&&）現在時刻を過ぎていたら、
        // 命令がなくても自動で「目」のモードに戻す
        if (mode != MODE_EYE && millis() >= showEnd) {
            mode = MODE_EYE;
        }

        // ── 描画の実行 ───────────────────────────────────────
        // 【描画の振り分け】
        // 現在のモードに応じて、実行すべき関数（計算処理）を切り替える
        switch (mode) {
            // 「目」モードの時、感情の変化（キュー）を確認して反映する
            case MODE_EYE:
                if (xQueueReceive(xEmotionQueue, &emotionMsg, 0) == pdTRUE) {
                    
                    // 感情の更新
                    emotion = emotionMsg;
                }

                // 感情に合わせた目をOLEDに表示する
                eyeUpdate(display, emotion);
                break;
            
            // 時計モードの時、時刻を描写
            case MODE_CLOCK:
                clockDraw(display);
                break;
        }
        
        // vTaskDelay(pdMS_TO_TICKS(66)): 約15fpsの描画周期を作成
        // 66msの間、このタスクは「CPUをOSに返却して休む」
        // これにより、他のタスク（ボタン検知等）がCPUを使えるようになる
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

    // 【前回の状態を保存】
    // 立ち下がり（押された瞬間）を検出するため、1つ前の読み取りを保持
    bool lastClock   = HIGH;
   
    // 【無限ループ】
    for (;;) {

        // 現在のボタンの状態を取得（INPUT_PULLUPなので、押すとLOWになる）
        bool curClock   = digitalRead(BUTTON_CLOCK);
       
        // ── ボタン1（時刻）の立ち下がり検出 ─────────────────────
        // 前の状態が「HIGH（離していた）」かつ（&&）、今の状態が「LOW（押された）」
        // = 「今まさに押された！」という瞬間を捉える条件
        if (lastClock == HIGH && curClock == LOW) {

            // 【チャタリング除去（重要！）】
            // 20ms待機し、再確認することでチャタリングを無視
            vTaskDelay(pdMS_TO_TICKS(20));

            // 20ms後もまだLOW（押されている）なら本当に押されていると判断
            if (digitalRead(BUTTON_CLOCK) == LOW) {
                
                // 1. 【音タスクへ指示を出す】
                // ボタンが押されたことを音タスクに通知し、「ピッ」と音を鳴らす
                SoundEvent btnEvent;
                btnEvent.isClick = true; // クリック音モード
                btnEvent.emotion = EMOTION_NORMAL; // ノーマルの際の音が鳴る
                xQueueSend(xSoundQueue, &btnEvent, 0);

                // 2. 【表示タスクへ指示を出す】
                // 画面モードを時計に戻すように指示を送信
                DisplayMode m = MODE_CLOCK;
                xQueueSend(xModeQueue, &m, 0);

                Serial.println(F("[BTN1] clock")); // ボタンが押されたことを知りあるモニタに表示
            }
        }

        // 次のループのために、今回の状態を「前回の状態」として保存
        lastClock   = curClock;

        // 【タスク制御】
        // 10ms毎に状態を確認
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ═══════════════════════════════════════════════════
// TaskEmotion : 時間帯別・重み付きランダム感情変化
// ═══════════════════════════════════════════════════

// 【時間帯判別関数】
// 現在の時刻から、ロボットにとっての「朝・昼・晩」を判定
static RobotTimeZone getCurrentTimeZone() {
    RTCTime now;
    RTC.getTime(now); // 現在時刻を取得
    int h = now.getHour();

    // 時間帯毎にIDを返すことで、「重み付けテーブル」の行を指定
    if      (h >= 6  && h < 9 ) return TZONE_MORNING;      // 6時~9時:   朝
    else if (h >= 9  && h < 12) return TZONE_WORK;         // 9時~12時:  午前
    else if (h >= 12 && h < 15) return TZONE_AFTERLUNCH;   // 12時~15時: ランチ後
    else if (h >= 15 && h < 18) return TZONE_AFTERNOON;    // 15時~18時: 午後
    else if (h >= 18 && h < 22) return TZONE_EVENING;      // 18時~22時: 夕方
    else                         return TZONE_NIGHT;       // その他:     夜
}

// 【重み付きランダム生成関数】
// 確率テーブル（TIME_WEIGHS）を元に、どの感情にするか抽選
static EmotionType weightedRandom(const uint8_t *weights) {
    int total = 0;

    // 1. 合計値を計算（例: 合計100）
    // EMOTION_COUNTより小さい場合は繰り返し、トータルを更新
    for (int i = 0; i < EMOTION_COUNT; i++) total += weights[i];
    if (total == 0) return EMOTION_NORMAL; // もし、合計が0ならば、EMOTION_NOMALを返す

    // 2. 0~ 合計値の範囲で乱数を生成
    int r = random(0, total), cum = 0;

    // 3. 「くじ引き」の要領で、どこに当たったか探す 
    for (int i = 0; i < EMOTION_COUNT; i++) {
        cum += weights[i];                      // 累計で確率枠を増やしていく
        if (r < cum) return (EmotionType)i;     // 当たった感情IDを返す
    }
    return EMOTION_NORMAL;
}

void TaskEmotion(void *pvParameters) {
    for (;;) {

        // 【不規則な待機時間（重要）】
        // 10秒 ~ 30秒の間のランダムに待機
        // いつ感情が変わるか分からないため、ロボットに人間味が出る
        TickType_t wait = pdMS_TO_TICKS(10000)
                          + (TickType_t)random(0, (long)pdMS_TO_TICKS(20000));  // 0 ~ 20000ミリ秒（ = 20秒）の間のランダムな数値を作成
        vTaskDelay(wait);

        // 1. 現在の時間帯を取得
        RobotTimeZone tz = getCurrentTimeZone();

        // ２. その時間帯に対応する「確率テーブル」から感情を抽選
        EmotionType e = weightedRandom(TIME_WEIGHTS[tz]);

        // 3. 感情決定 → 画面タスクへ通知
        xQueueSend(xEmotionQueue, &e, 0);

        // 4. 音タスクへも通知（鳴き声用）
        SoundEvent event;
        event.isClick = false;   // 操作音ではなく、感情表現だよといくフラグ
        event.emotion = e;       // 感情IDを渡す
        xQueueSend(xSoundQueue, &event, 0); 

        Serial.print(F("[EMOTION] zone="));
        Serial.print((int)tz);
        Serial.print(F(" -> "));
        Serial.println((int)e);

    }
}
// ═══════════════════════════════════════════════════
// TaskNTP : 時刻同期（NTP）を定期的に行うためのタスク
// ═══════════════════════════════════════════════════
void TaskNTP(void *pvParameters) {

    // 起動時の初回同期（既にWiFiは繋がっている前提）
    // 電源が入ってロボットが起動した直後、まず1度だけ時刻を合わせる
    ntpSync();

    // 【無限ループによる定期的更新】
    for (;;) {

        // 【待機: NTP_RESYNC_MSミリ秒】
        // ずっと同期し続けるとネットワークに負担がかかるため、決められた時間のみスリープ
        vTaskDelay(pdMS_TO_TICKS(NTP_RESYNC_MS));

        // 【再同期】
        // 時間が経つとマイコン内部の時計は少しずつズレるので、
        // 定期的にNTPサーバから時刻を取得し直し、誤差を修正
        ntpSync();
    }
}

// ═══════════════════════════════════════════════════
// TaskSound : 感情キューを受け取り、鳴き声を再生
// ═══════════════════════════════════════════════════
void TaskSound(void *pvParameters) {

    SoundEvent event;

    // 無限ループ
    for (;;) {

        // portMAX_DERAY: データが来るまで無限待機（スリープ）
        // キューにデータが届くまで、ここで完全に待機（CPUを他のタスクに譲る）
        // 無駄な計算は行わない、OS環境で最も効率的な待機方法
        if (xQueueReceive(xSoundQueue, &event, portMAX_DELAY) == pdTRUE) {
            
            // ★シリアルモニター確認用のログ
            Serial.print(F("[SOUND TASK] Received event! isClick="));
            Serial.print(event.isClick);
            Serial.print(F(", Emotion ID="));
            Serial.println((int)event.emotion);
            
            // ...再生処理...
            if (event.isClick) {
                playSwitchSound(); // ボタン操作音（ピッ）
            } else {
                playCry(event.emotion); // 感情の鳴き声（ピロリ♪など）
            }
        }
    }
}
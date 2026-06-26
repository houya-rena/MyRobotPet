#include <FreeRTOSConfig.h>
#include <Arduino_FreeRTOS.h>

#define ENABLE_SOUND   1
/**
 * main.ino
 * 多機能液晶ロボットガジェット（Arduino FreeRTOS 拡張版）
 *
 * 【タスク構成と優先度（システム・アーキテクチャ）】
 * TaskInitAndLaunch (優先度4) : [初期化] 起動時にハードウェア初期化後、他タスクを生成して自己消滅（メモリ解放）
 * TaskDisplay       (優先度3) : [描画] 目アニメーション(15fps) / 時刻画面の動的レンダリング
 * TaskButton        (優先度2) : [入力・計測] モード切替ボタンのチャタリング除去 ＆ 超音波センサーデータの安全な回収
 * TaskEmotion       (優先度1) : [思考] タイムベース管理による時間帯別・重み付きランダム感情変化 ＆ サウンド連動
 * TaskSound         (優先度1) : [出力] 感情キュー・ボタン音イベントを portMAX_DELAY で完全非同期待機・再生
 * TaskWifiSync      (優先度1) : [通信] 起動時にバックグラウンドでWiFi接続およびNTP時刻同期を実行後、自己消滅
 *
 * 【拡張仕様・ロジック】
 * 1. ボタン1（D4 / 旧D2） : 押下で時刻表示（MODE_CLOCK）。表示中に再押し、または5秒経過で自動的に目（MODE_EYE）に復帰。
 * 2. 超音波センサー（D2/D3）: 外部割り込み（ISR）を用いたノンブロッキング制御。30cm以内に接近で液晶が「SMILE（笑顔）」に変化。
 *                          手が離れると自動的に元のベース感情へ復帰（エッジ検出による状態ラッチ）。
 * 3. 感情・サウンド同期 : 20秒ごとに35%の確率で、その瞬間に液晶に表示されている感情と100%シンクロした声を自律再生。
 * 4. 共有資源の排他制御 : センサー反応中の鳴き声再生時、フラグ（isSensorReacting）を用いてボタン音等の割り込みをロック。
 *
 * 【配線・ハードウェア接続】
 * ・OLED SSD1306                : SDA, SCL（I2C通信：アドレス 0x3C）
 * ・ボタン1（時刻表示切替）        : D4 → GND（内部プルアップ INPUT_PULLUP）
 * ・超音波センサー（HC-SR04等）  : Trig → D3（パルス出力）, Echo → D2（外部割り込み入力ピン）
 * ・圧電ブザー / オーディオ       : A0 → 可変抵抗器 → スピーカー / 圧電サウンダ
 *
 * ========================================================================
 * 【修正・機能拡張 履歴】
 * ========================================================================
 * ・外部割り込みによる超音波センサー制御の追加:
 *   従来型のブロッキング処理（pulseIn）を排除。EchoピンのHIGH/LOW変化をD2ピンの外部割り込み（ISR）で
 *   マイクロ秒単位で検知。FreeRTOSのタスクスケジューリングを阻害しない完全ノンブロッキング計測を確立。
 *
 * ・クリティカルセクションによるスレッドセーフ化:
 *   ISR内で突発的に書き換わるマルチバイト距離変数に対し、TaskButton側でのデータ回収時に
 *   noInterrupts() / interrupts() による割り込み禁止区間を設け、データレース（破損）を完全に防止。
 *
 * ・1タスクマルチタイムベース（カウンタ式）設計へのリファクタリング:
 *   TaskEmotionにおいて、5秒周期のvTaskDelayをベースに内部Tickカウンタを実装。
 *  「20秒ごとの自律サウンド判定」と「3〜5分ごとのゆったりとした表情変化」を単一タスクで同居させ、
 *   FreeRTOS全体のタスク数（スタック消費）を抑えてヒープ枯渇を防止。
 *
 * ・動的タスク破棄によるメモリ最適化:
 *   初期化（TaskInitAndLaunch）および通信（TaskWifiSync）は、それぞれの役割を終えた段階で
 *    vTaskDelete(NULL) を実行し、割り当てられていた合計1,280スタックワードのRAM領域をヒープへ完全解放。
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
#include "eye_draw.h"
#include "eye_state.h"
#include "eye_params.h"
#include "clock_display.h"
#include "ntp_sync.h"
#include "sound.h"
#include "arduino_secrets.h"

// ── ハードウェア設定 ───────────────────────────────
#define OLED_RESET     -1
#define OLED_ADDRESS   0x3C
#define BUTTON_CLOCK   4     // ボタン1: 時刻表示
#define BUZZER_PIN     A0    // 圧電ブザーのピン（環境に合わせて変更してください）

// ── 超音波センサー設定（ヒープ節約のため定数定義） ──────────────────
#define ULTRA_TRIG_PIN    3     // Trigピン（パルス発射）
#define ULTRA_ECHO_PIN    2     // Echoピン（D2: AVR/ARMマイコンの外部割り込みINTO等のピン）
#define DISTANCE_THRESHOLD 30   // 反応する距離（30cm以内）

// 各モードの自動復帰時間（ms）
#define MODE_SHOW_MS   5000UL   // ULはunsiged long（符号なし長整数）の指定
#define NTP_RESYNC_MS  3600000UL

// ══════════════════════════════════════════════════
// ランダム鳴き声設定
// ══════════════════════════════════════════════════
#define CRY_INTERVAL_MIN  10000UL  // 最短インターバル（10秒）
#define CRY_INTERVAL_MAX  25000UL  // 最長インターバル（25秒）
#define CRY_CHANCE        4        // 1/4の確率で鳴く

// ── [C++技術] volatile変数とスレッド間（タスク間）共有フラグ ──────────────────
// 『volatile』: コンパイラの最適化（レジスタへのキャッシュ化）を禁止する
// 割り込みサービスルーチン（ISR）内と、メインタスク側の双方で評価される変数は、
// 常に最新状態をRAMから直接参照させるために必須
volatile EmotionType currentBaseEmotion = EMOTION_NORMAL; // 通常時の感情ベースラインを記憶
volatile bool isSensorReacting = false;                   // 超音波反応による音再生中の排他ロックフラグ
volatile bool isHandDetected   = false;                   // 手がセンサー範囲内にいるかどうか（チャタリング・連打防止用）

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

// ─── 超音波センサー外部割り込み用変数 ───────────────────
volatile unsigned long volatileEchoStartTime = 0;   // エコーパルスがHIGHになった時刻（マイクロ秒）
volatile int volatileCalculatedDistance = -1;       // 計測された距離（タスク間で回収されるまで保持）

void TaskDisplay(void *pvParameters);
void TaskWifiSync(void *pvParameters);
void TaskButton(void *pvParameters);
void TaskEmotion(void *pvParameters);
void TaskSound(void *pvParameters);

// ─── [C++ / ハードウェア特論] 外部割り込みハンドラ (ISR) ───────────────────
// Echoピンの電位が変化（CHANGE）した瞬間に、メイン処理を強制ストップしてハードウェアが直接実行
// ISR（Interrupt Service Routine）内では「delay()」や「Serial.print()」などの重い処理、
// およびFreeRTOSの通常のAPI（FromISRが付かないもの）の呼び出しは、システムクラッシュを引き起こすため絶対に厳禁
void echoInterruptHandler() {
    if (digitalRead(ULTRA_ECHO_PIN) == HIGH) {
        // 波が発射されて、Echoピンが立ち上がった（Raising）瞬間の時刻（μs）を記録
        volatileEchoStartTime = micros();
    } else {
        // 跳ね返りが戻ってきて、Echoピンが立ち下がった（Falling）瞬間の時刻を取得
        unsigned long endTime = micros();
        if (volatileEchoStartTime > 0) {
            unsigned long duration = endTime - volatileEchoStartTime;
            // 距離（cm）を計算して保存
            // 【物理距離】距離 = 時間（μs） * 音速（340m/s = 0.034cm/μs） / 往復（2）
            volatileCalculatedDistance = duration * 0.034 / 2;
            volatileEchoStartTime = 0; // 次回計測のために初期化
        }
    }
}

// ── [C++データ構造] 2次元配列とstatic const ──────────────────────────
// 『static const』: 
// constにより「書き換え不可（読み取り専用）」となり、さらにstaticをつけることで、
// このファイル内だけでアクセス可能なローカルデータ（静的記憶領域）になる
// 組み込みにおいては、RAMではなくROM（フラッシュメモリ）側に配置されるため、貴重なRAMを節約可能

// ── 2. 感情重みテーブル ──────────────────────────────────────────
// 各行の左から順に: [0]NORMAL, [1]HAPPY, [2]ANGRY, [3]SAD, [4]CONFUSED, [5]SLEEPY, 
//                  [6]EATING, [7]SLEEPING, [8]BATH, [9]SNACK, [10]SMILE の確率
// ※ イベント系感情[6]〜[10]はランダムで発生しないようすべて 0 に設定
static const uint8_t TIME_WEIGHTS[TZONE_COUNT][EMOTION_COUNT] = {
    //  NOR, HAP, ANG, SAD, CON, SLEEPY, [EAT, SLEEP, BATH, SNACK, SMILE]
    {  35,  40,   5,   5,   5,   10,     0,    0,    0,    0,    0 }, // MORNING
    {  55,  20,   5,   5,   5,    5,     0,    0,    0,    0,    0 }, // WORK
    {  30,  15,   5,   5,   5,   40,     0,    0,    0,    0,    0 }, // AFTERLUNCH
    {  40,  20,  10,   5,  15,   10,     0,    0,    0,    0,    0 }, // AFTERNOON
    {  35,  40,   5,   5,   5,   10,     0,    0,    0,    0,    0 }, // EVENING
    {  30,   5,  10,   0,   5,   50,     0,    0,    0,    0,    0 }  // NIGHT
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
// 『void *pvParameters』:
// FreeRTOSのタスク関数の決まり文句。どんな型のポインタでも受け取れる「汎用ポインタ」
void TaskWifiSync(void *pvParameters) {

    // 『pdMs_TO_TICKS(ms)』: ミリ秒単位の時間を、OSが解釈できる「Tick（カウント数）」に変換するマクロ
    // FreeRTOSのシステムロック周期がどう設定されていても、正確な時間を刻むために必須
    // vTaskDelay: 単なる待機ではなく、CPUを他のタスクに明け渡すOS関数
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
        vTaskDelay(pdMS_TO_TICKS(5000)); // 接続失敗時は5秒休止してリトライ。この間、他のタスクにCPUを引き渡す
    }
    
    Serial.println(F("[SYSTEM] WiFi Connected! Trying NTP..."));

    // 時刻同期(NTP)だけ失敗したか、成功したかをログに残す
    if (ntpSync()) {
        Serial.println(F("[SYSTEM] NTP Sync SUCCESS!"));
    } else {
        Serial.println(F("[SYSTEM] NTP Sync FAILED."));
    }

    // vTaskDelete(NULL): 引数に「NULL」を渡すと、自分のタスクを終了し、スタックメモリを解放する
    // WiFi接続とNTP同期は起動時に1度だけやればいい使い捨ての処理であるため、
    // 用が済んだらタスクごと消滅させ、割り当てられていたスタックメモリ（768バイト）をすべてヒープへ返却する
    vTaskDelete(NULL);
}

// ── (2) 初期化・タスク生成タスク ──────────────────
void TaskInitAndLaunch(void *pvParameters) {

    // 各ハードウェアの初期化
    RTC.begin();
    randomSeed(analogRead(A1));   // 未接続ピンのノイズを利用して乱数のシード（初期値）を完全位ランダム化
    soundInit();
    
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        display.clearDisplay();
        display.display();
        eyeInit(display);
    }
    
    // xTaskCreate: OSに新しい「仕事（並行処理タスク）」を登録する関数
    // 引数：(関数ポインタ, タスク識別名, スタックサイズ[ワード/バイト ※環境依存], パラメータ, 優先度, ハンドル保存先)
    // 優先度： 数値が大きいほど優先される。Display(3)は描画を滑らかにするため最優先
    // スタックサイズ（第3引数）の決定が、メモリ枯渇を防ぐポイント
    xTaskCreate(TaskDisplay, "Display", 384, NULL, 3, &xDisplayHandle);
    xTaskCreate(TaskEmotion, "Emotion", 192, NULL, 1, NULL);
    xTaskCreate(TaskButton,  "Button",  128, NULL, 2, NULL);
    xTaskCreate(TaskSound,   "Sound",   352, NULL, 1, NULL);
    // ※ NTP同期は TaskWifiSync が setup() で担当するためここでは不要

    vTaskDelete(NULL); // 自分のタスクを終了し、スタックメモリを解放する
}

// ── (3) setup() (初期化関数) ──────────────
// ここではタスク生成とOS起動（スケジューラー起動）のみに集中
void setup() {
    Serial.begin(9600);

    pinMode(BUTTON_CLOCK, INPUT_PULLUP); // ボタンピンを内蔵プルアップ抵抗に定義
    analogWrite(BUZZER_PIN, 0);          // スピーカから鳴らす音を常にオフ（0:LOW）にする

    // xQueueCreate: タスク間通信用の「伝言板」を作成、固定長のリングバッファ（メモリ領域）を確保する
    // 引数：(要素数, 1要素あたりのバイトサイズ)
    // sizeof(型)を渡すことで、C++の構造体や列挙型そのものを丸ごと格納できる領域を作成

    // 感情の変化を伝える
    xEmotionQueue = xQueueCreate(4, sizeof(EmotionType)); // EmotionType（感情の種別ID）を受け取った TaskDisplay は、顔の絵を変える
    // 「ボタンが押されたからモードを切り替えて」という命令
    xModeQueue    = xQueueCreate(4, sizeof(DisplayMode)); // DisplayMode（目モードか、時計モードか）
    // 「今の感情に合わせた鳴き声を再生して」という命令
    xSoundQueue   = xQueueCreate(4, sizeof(SoundEvent));  //  SoundEvent（「ボタン音か？」「何の感情の鳴き声か？」という情報が詰まった構造体）

    // WiFi接続という重い処理のせいで描画がフリーズするのを防ぐため、ここで実行
    // 初期化タスクの優先度を一番高く「4」に設定し、最優先でハードウェアを立ち上げる
    xTaskCreate(TaskInitAndLaunch, "Init", 512, NULL, 4, NULL);
    // wifiタスクは通信待ちでブロッキング（待機）が多いため、低い優先度「1」で裏を回す
    xTaskCreate(TaskWifiSync, "WifiSync", 768, NULL, 1, NULL);


    // ─── 超音波センサーのGPIO（汎入出力ポート）・外部割り込み登録 ───────────────────
    pinMode(ULTRA_TRIG_PIN, OUTPUT);
    pinMode(ULTRA_ECHO_PIN, INPUT);     // 外部割り込み用にINPUT
    digitalWrite(ULTRA_TRIG_PIN, LOW);

    // Echoピン（D2）の電位変化（CHANGE = HIGH/LOW両方）を監視するよう登録
    // 【解説】 attachInterrupt: 指定ピンの電位変化（CHANGE: LOW->HIGH / HIGH->LOW 両方）で ISR（割り込みサービスルーチン） を強制発動させる。
    // デジタルピン番号を割り込み番号（ベクタ）へ自動変換する digitalPinToInterrupt マクロを使用。
    attachInterrupt(digitalPinToInterrupt(ULTRA_ECHO_PIN), echoInterruptHandler, CHANGE);

    // FreeRTOSの心臓であるスケジューラーを起動
    // ここを境界に、プログラムの動きは上から下への直線的な実行から、OSによる「タスクの超高速切り替え駆動」へと変化する
    vTaskStartScheduler();
}

void loop() {} // FreeRTOSに制御が移るため、Arduino標準のloopは完全に無視される

// ═══════════════════════════════════════════════════
// TaskDisplay : 描画管理
//   MODE_EYE     → 目アニメーション
//   MODE_CLOCK   → 時刻画面（5秒 or 再押しで戻る）
// ═══════════════════════════════════════════════════
void TaskDisplay(void *pvParameters) {

    // 【状態管理用の変数】ローカル変数（この関数内でのみ有効）
    // 「static」をつけない場合、タスクのスタック領域に確保される（このタスクが持つ専用のスタック領域（384バイト内）に保持）
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
        // xQueueReceive(対象キュー, 格納先アドレス, 待機Tick数): キューにデータが来るのを待つ
        // 他のタスク（ボタン等）から「モードを変えろ」という指示が来ているか確認
        // 第3引数の「0」にすることで、「データがなくても待たずにすすむ」という指定
        // これにより、ボタンが押されていなくても、液晶の目のアニメーション描画（15fps）を止めずに回し続けられる
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
        
        // vTaskDelay(pdMS_TO_TICKS(66)): 約15fps(1000ms / 15 ≒ 66ms)の描画周期を作成
        // 66msの間、このタスクは「CPUをOSに返却して休む」
        // これにより、他のタスク（ボタン検知等）がCPUを使えるようになる
        vTaskDelay(pdMS_TO_TICKS(66));
    }
}

// ═══════════════════════════════════════════════════
// TaskButton : ボタン入力検知（チャタリング除去付き）& 外部割り込み式超音波式センサー
//
// 【動作ロジック】
//   - 押した瞬間（立ち下がり）を検出
//   - 20ms後に再確認（チャタリング除去）
//   - ボタン → MODE_EYE（戻る）を送信
//   - 目モード中 → 対応するモードを送信
// ═══════════════════════════════════════════════════

void TaskButton(void *pvParameters) {
    bool lastClock   = HIGH; // 前回のピン状態を記憶（チャタリング・エッジ検出用）
    uint8_t sensorTick = 0; 
   
    for (;;) {
        bool curClock   = digitalRead(BUTTON_CLOCK);
       
        // ── 1. ボタン（時刻）の立ち下がり（HIGHからLOWへ変化した瞬間）検出 ─────────────────────
        if (lastClock == HIGH && curClock == LOW) {

            // チャタリング対策：
            // 20ms待機して電圧が落ち着いた段階で再確認することで、誤検知を徹底的に排除
            vTaskDelay(pdMS_TO_TICKS(20));
        
            if (digitalRead(BUTTON_CLOCK) == LOW) {

                // 1. 音タスクへのメッセージ送信
                SoundEvent btnEvent;
                btnEvent.isClick = true; 
                btnEvent.emotion = EMOTION_NORMAL; 

                // xQueueSend(宛先, 送信データのポインタ, 待機時間):
                // 構造体の中身を値渡し（コピー）でキューに放り込む。第3引数0なので、キューが満杯なら諦める
                xQueueSend(xSoundQueue, &btnEvent, 0);

                // 2. 表示タスクへのメッセージ送信
                DisplayMode m = MODE_CLOCK;
                xQueueSend(xModeQueue, &m, 0);

                Serial.println(F("[BTN1] clock")); 
            }
        }

        lastClock  = curClock; // 今回の状態を次回の「過去のデータ」として保存

        // ── 2. 超音波センサー：割り込み結果の安全な回収（500msに1回） ──
        sensorTick++; // ループするたびに +1 する（10msごと）

        if (sensorTick >= 50) { // 10ms * 50 = 500ms（50回）
            sensorTick = 0;     // カウントを0にリセットし、また1から数え直す

            // 距離計測は常に行う（手がある間も離れた瞬間を検知するために必須）
            
            // ── [C++ / 組み込み特論] クリティカルセクションによる変数保護 ──
            // マイコンのCPUが多ビット変数（intやlong）を読み出す操作は、アトミック（一撃）ではない。
            // 読み出し中の割り込みによるデータ破損を防ぐため、一瞬だけ全体割り込みを禁止にする。
            noInterrupts();
            int currentDistance = volatileCalculatedDistance; // 安全にローカルへコピー
            volatileCalculatedDistance = -1;                  // バッファを未計測状態にクリア
            interrupts();                                     // 割り込み許可を即座に再開

            // ── 次の距離計測パルスを送信 (Trigピンを10μsだけHIGHに) ──
            // この一連の数マイクロ秒のパルス生成は、FreeRTOSのコンテキストスイッチに影響を与えない
            digitalWrite(ULTRA_TRIG_PIN, LOW);
            delayMicroseconds(2);
            digitalWrite(ULTRA_TRIG_PIN, HIGH);
            delayMicroseconds(10);
            digitalWrite(ULTRA_TRIG_PIN, LOW);

            // ── 取得した距離データの判定ロジック ──
            if (currentDistance > 0 && currentDistance < 400) { // 400cm以上の異常値・エラーの除外

                // ──── 【ケースA：手がしきい値内に近づいている時】 ────
                if (currentDistance <= DISTANCE_THRESHOLD && currentDistance > 2) {
                    if (!isHandDetected) {  // エッジ検出
                        // 手を検知した瞬間だけ実行（連打防止）
                        isHandDetected = true;

                        EmotionType targetSmile = EMOTION_SMILE;
                        xQueueSend(xEmotionQueue, &targetSmile, 0);  //　液晶を即座に「笑顔」にする 

                        // 音はまだ鳴っていないときだけ送る
                        if (!isSensorReacting) {
                            isSensorReacting = true;    // 音タスクが起きる前に、ボタン側で先行してロックを獲得
                            SoundEvent event;
                            event.isClick = false;
                            event.emotion = targetSmile;
                            xQueueSend(xSoundQueue, &event, 0); // SMILEに対応する音声を送信
                        }

                        Serial.println(F("[SENSOR] Hand detected -> SMILE"));
                    }
                }
                else {
                    // ──── 【ケースB：手がしきい値から離れた時】 ────
                    if (isHandDetected) {  // エッジ検出（今まさに手が離れた瞬間）
                        isHandDetected = false;

                        // TaskEmotion側が生成・更新し続けている「現在のベース感情」へ巻き戻す
                        EmotionType restoreEmotion = currentBaseEmotion;
                        xQueueSend(xEmotionQueue, &restoreEmotion, 0);

                        Serial.println(F("[SENSOR] Hand removed -> Restore Face"));
                    }
                }
            }
        }   // sensorTick >= 50 の閉じカッコ

        // 10ms毎にループを回転
        vTaskDelay(pdMS_TO_TICKS(10));
    
    } // for(;;) の閉じカッコ
}   // TaskButtonの閉じカッコ

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

// 【重み付きランダム生成関数（抽選アルゴリズム）】
// 確率テーブル（TIME_WEIGHS）を元に、どの感情にするか抽選
// 全体の確率の合計（total）を計算し、その範囲内で乱数を発生させ、累積値（cum）の境界を超える場所を探すことで
// 各感情に「35％」「50％」といった異なる当選確率を持たせる
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
    // 【タイムベース・アーキテクチャへのリファクタリング】
    // 一つのタスク内で異なる時間軸（5秒、20秒、3~5分）のイベントを制御するための独立カウンタ
    uint16_t soundTick = 0;
    uint32_t screenWaitTicks = 0;
    uint32_t targetScreenWait = 0;

    // 最初に「最初の表情が変わるまでの時間（3〜5分）」をランダムに決定
    targetScreenWait = pdMS_TO_TICKS(180000) + (uint32_t)random(0, (long)pdMS_TO_TICKS(120000));

    // ── [追加] ループ開始前に現在の感情の初期値を決めておく（音の連動用） ──
    EmotionType currentEmotion = EMOTION_NORMAL;

    for (;;) {
        // 【最重要】5秒ごとにタスクをスリープから復帰させ、内部カウンタをインクリメント（加算）
        vTaskDelay(pdMS_TO_TICKS(5000));

        // =================================================================
        // 処理A：【ランダムなサウンド処理】（5秒おきにカウントして20秒毎に判定）
        // =================================================================
        soundTick++;
        if (soundTick >= 4) { // 5秒 × 4 ＝ 20秒ごとに実行
            soundTick = 0;

            // 35%の確率で、現在の感情に合った声をランダムに鳴らす
            if (random(100) < 35) {
                SoundEvent event;
                event.isClick = false;   
                event.emotion = currentEmotion; // 表示中の感情をセット
                
                xQueueSend(xSoundQueue, &event, 0);
                Serial.print(F("[EMOTION-SOUND] Match Sound Triggered! Emotion ID="));
                Serial.println((int)currentEmotion);
            }
        }

        // =================================================================
        // 処理B：【ゆったり表情切り替え処理】（3分〜5分のカウントアップ方式）
        // =================================================================
        screenWaitTicks += pdMS_TO_TICKS(5000); // 経過時間を5秒分カウントを進める

        if (screenWaitTicks >= targetScreenWait) {
            screenWaitTicks = 0; // カウンタをリセット
            // 次の待機時間（3〜5分）を再計算して仕込む
            targetScreenWait = pdMS_TO_TICKS(180000) + (uint32_t)random(0, (long)pdMS_TO_TICKS(120000));

            // ── RTC現在時刻を取得 ────────────────────────
            RTCTime nowTime;
            RTC.getTime(nowTime);
            
            bool isTimeValid = (WiFi.status() == WL_CONNECTED) && (nowTime.getYear() >= 2024);

            // ── 感情の決定用変数 ──────────────────────────────────
            EmotionType e = EMOTION_COUNT;

            // ── RTC時刻同期が取れている場合のイベント感情の判定 ───────────
            if (isTimeValid) {
                uint8_t hour   = nowTime.getHour();
                uint8_t minute = nowTime.getMinutes();

                if (hour >= 22 || hour < 7)         e = EMOTION_SLEEPING;
                else if (hour == 7 && minute < 15)  e = EMOTION_HAPPY;
                else if (hour == 9 && minute < 15)  e = EMOTION_EATING;
                else if (hour == 12 && minute < 15) e = EMOTION_EATING;
                else if (hour == 15 && minute < 15) e = EMOTION_SNACK;
                else if (hour == 18 && minute < 15) e = EMOTION_EATING;
                else if (hour == 20 && minute < 15) e = EMOTION_BATH;
                else if (hour == 21)                e = EMOTION_SLEEPY;
            }

            // ── イベント時間外の場合は重み付きランダム抽選 ────
            if (e == EMOTION_COUNT) {
                RobotTimeZone tz = getCurrentTimeZone();
                e = weightedRandom(TIME_WEIGHTS[tz]);
                
                if (!isTimeValid) {
                    Serial.print(F("[EMOTION] NTP Not Ready -> Random Active -> "));
                } else {
                    Serial.print(F("[EMOTION] Random Active -> "));
                }
            } else {
                Serial.print(F("[EMOTION] RTC Event Triggered -> "));
            }

            // 新しく決まった感情を「現在の感情」として記憶（グローバルとタスク内のローカル双方へ蓄積）
            currentEmotion = e;
    
            // 3. 感情決定 → 画面タスクへ通知（手が検知されていなければ表情更新キューを送る）
            if (!isHandDetected) {
                xQueueSend(xEmotionQueue, &e, 0);
            } else {
                // 手を検知している間は「SMILE」画面を維持する
                Serial.print(F("[EMOTION] Screen skip (Hand detected) -> "));
            }

            Serial.print(F("zone= "));
            Serial.print((int)getCurrentTimeZone()); 
            Serial.print(F(" -> "));
            Serial.println((int)e);
        }
    }
}
// ═══════════════════════════════════════════════════
// TaskSound : 感情キューを受け取り、鳴き声を再生
// ═══════════════════════════════════════════════════
void TaskSound(void *pvParameters) {

    SoundEvent event;

    // 無限ループ
    for (;;) {

        // portMAX_DELAY: データが来るまで無限待機（スリープ）
        // キューにデータが届くまで、ここで完全に待機（CPUを他のタスクに譲る、CPU使用率0%）
        if (xQueueReceive(xSoundQueue, &event, portMAX_DELAY) == pdTRUE) {
            
            // ★シリアルモニター確認用のログ
            Serial.print(F("[SOUND TASK] Received event! isClick="));
            Serial.print(event.isClick);
            Serial.print(F(", Emotion ID="));
            Serial.println((int)event.emotion);
            
            // ── 超音波センサー反応による音再生のフラグ同期 ──
            // 超音波センサーによる笑顔（EMOTION_SMILE）の音が始まる瞬間にフラグをONにする
            // （isSensorReacting は TaskButton で既にセットされているが、コンテキストスイッチの隙間を埋めるため念のため）
            if (!event.isClick && event.emotion == EMOTION_SMILE) {
                isSensorReacting = true;
            }

            // ...再生処理...
            if (event.isClick) {
                playSwitchSound();      // ボタン操作音（ピッ）
            } else {
                playCry(event.emotion); // 感情に合わせたメロディを再生
            }

            // 音が鳴り終わったら音の排他ロックだけ解除
            // ※ isHandDetected は触らない → 手がある間はSMILE表情が継続される
            isSensorReacting = false;
        }
    } // for(;;) end
} // TaskSound end
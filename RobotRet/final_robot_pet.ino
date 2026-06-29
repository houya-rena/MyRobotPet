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
 * 1. ボタン1（D4） : 押下で時刻表示（MODE_CLOCK）。表示中に再押し、または5秒経過で自動的に目（MODE_EYE）に復帰。
 * 2. 超音波センサー（D2/D3）: 外部割り込み（ISR）を用いたノンブロッキング制御。30cm以内に接近で液晶が「SMILE（笑顔）」に変化。
 *                          手が離れると自動的に元のベース感情へ復帰（エッジ検出による状態ラッチ）。
 * 3. 感情・サウンド同期 : 20秒ごとに35%の確率で、その瞬間に液晶に表示されている感情と100%シンクロした声を自律再生。
 * 4. 共有資源の排他制御 : センサー反応中の鳴き声再生時、フラグ（isSensorReacting）を用いてボタン音等の割り込みをロック。
 *
 * 【配線・ハードウェア接続】
 * ・OLED SSD1306             : SDA, SCL（I2C通信：アドレス 0x3C）
 * ・ボタン1（時刻表示切替）      : D4 → GND（内部プルアップ INPUT_PULLUP）
 * ・超音波センサー（HC-SR04等）  : Trig → D3（パルス出力）, Echo → D2（外部割り込み入力ピン）
 * ・圧電ブザー / オーディオ      : A0 → 可変抵抗器 → スピーカー / 圧電サウンダ
 *
 * ========================================================================
 * 【修正・機能拡張 履歴】
 * ========================================================================
 * ・外部割り込みによる超音波センサー制御の追加:
 * 従来型のブロッキング処理（pulseIn）を排除。EchoピンのHIGH/LOW変化をD2ピンの外部割り込み（ISR）で
 * マイクロ秒単位で検知。FreeRTOSのタスクスケジューリングを阻害しない完全ノンブロッキング計測を確立。
 *
 * ・クリティカルセクションによるスレッドセーフ化:
 * ISR内で突発的に書き換わるマルチバイト距離変数に対し、TaskButton側でのデータ回収時に
 * noInterrupts() / interrupts() による割り込み禁止区間を設け、データレース（破損）を完全に防止。
 *
 * ・1タスクマルチタイムベース（カウンタ式）設計へのリファクタリング:
 * TaskEmotionにおいて、5秒周期のvTaskDelayをベースに内部Tickカウンタを実装。
 * 「20秒ごとの自律サウンド判定」と「3〜5分ごとのゆったりとした表情変化」を単一タスクで同居させ、
 * FreeRTOS全体のタスク数（スタック消費）を抑えてヒープ枯渇を防止。
 *
 * ・動的タスク破棄によるメモリ最適化:
 * 初期化（TaskInitAndLaunch）および通信（TaskWifiSync）は、それぞれの役割を終えた段階で
 * vTaskDelete(NULL) を実行し、割り当てられていた合計1,280スタックワードのRAM領域をヒープへ完全解放。
 * ========================================================================
 */

#include <FreeRTOSConfig.h>
#include <Arduino_FreeRTOS.h>

#define ENABLE_SOUND   1

#ifndef FPSTR
#define FPSTR(pstr) (reinterpret_cast<const __FlashStringHelper*>(pstr))
#endif

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
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
#include "web_page.h"
#include "arduino_secrets.h"

// ── ハードウェア設定 ───────────────────────────────
#define OLED_RESET     -1
#define OLED_ADDRESS   0x3C
#define BUTTON_CLOCK   4     // ボタン1: 時刻表示
#define BUZZER_PIN     A0    // 圧電ブザーのピン

// ── 超音波センサー設定（ヒープ接続のため定数定義） ──────────────────
#define ULTRA_TRIG_PIN    3     // Trigピン（パルス発射）
#define ULTRA_ECHO_PIN    2     // Echoピン（D2: AVR/ARMマイコンの外部割り込みINTO等のピン）
#define DISTANCE_THRESHOLD 30   // 反応する距離（30cm以内）

// 各モードの自動復帰時間（ms）
#define MODE_SHOW_MS   5000UL   
#define NTP_RESYNC_MS  3600000UL


// ══════════════════════════════════════════════════
// ランダム鳴き声設定
// ══════════════════════════════════════════════════
#define CRY_INTERVAL_MIN  10000UL  // 最短インターバル（10秒）
#define CRY_INTERVAL_MAX  25000UL  // 最長インターバル（25秒）
#define CRY_CHANCE        4        // 1/4の確率で鳴く

// ── グローバル共有変数 ──────────────────
// volatile: コンパイラの最適化（レジスタへのキャッシュ化）を禁止する
// 割り込みサービスルーチン（ISR）内と、メインタスク側の双方で評価される変数は、
// 常に最新の状態をRAMから直接参照させるために必須
volatile EmotionType currentBaseEmotion = EMOTION_NORMAL; // 通常時の感情のベースラインを記憶
volatile bool isSensorReacting = false;                   // 超音波反応による音再生中の排他ロックフラグ
volatile bool isHandDetected   = false;                   // 手がセンサーの範囲内にいるかどうか（チャタリング・連打防止用）

// ── 表示モード ・列挙型 (enum) ───────────────────────────────────
// 【解説】typedef enum: 状態を「0, 1, 2,....」ではなく名前で管理する仕組み
// コードの可読性が劇的に上がるためこの形式を使用
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
// 【解説】関数の外で定義することによって、どのタスクからでもアクセス可能になる
// ただし、OS環境では「キュー」を介したデータのやり取りの方が安全
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // OLEDを扱うためのインスタンス
QueueHandle_t xEmotionQueue, xModeQueue, xSoundQueue;   // 「どこにキューがあるか」「誰が待っているか」という情報へのポインタ
TaskHandle_t xDisplayHandle = NULL;     // 作成したタスクを後から操作するためのID

// ─── 超音波センサー外部割り込み用変数 ───────────────────
volatile unsigned long volatileEchoStartTime = 0; // エコーパルスがHIGHになった時刻（マイクロ秒）
volatile int volatileCalculatedDistance = -1;     // 計算された距離（タスク間で回収されるまで保持）

// 【BKPT防衛】Webサーバーオブジェクトの追加
WiFiServer server(80); 

void TaskDisplay(void *pvParameters);
void TaskWifiSync(void *pvParameters);
void TaskButton(void *pvParameters);
void TaskEmotion(void *pvParameters);
void TaskSound(void *pvParameters);

// ── 外部割り込みハンドラ (ISR) ───────────────────
// Echoピンの電位が変化（CHANGE）した瞬間に、メイン処理を強制ストップしてハードウェアが直接実行
// ISR（Interrupt Service Routine）内では「delay()」や「Serial.print()」などの重い処理
// およびFreeRTOSの通常のAPI（FromISRがつかないもの）の呼び出しはシステムのクラッシュを引き起こすため絶対に厳禁
void echoInterruptHandler() {
    if (digitalRead(ULTRA_ECHO_PIN) == HIGH) {
        // 波が発射されて、Echoピンが立ち上がった（Raising）瞬間の時刻（μs）を記録
        volatileEchoStartTime = micros();
    } else {
        // 跳ね返ってきて。Echoピンが立ち下がった（Falling）瞬間の時刻を取得
        unsigned long endTime = micros();
        if (volatileEchoStartTime > 0) {
            unsigned long duration = endTime - volatileEchoStartTime;
            // 距離（cm）を計算して保存
            // 【物理距離】距離 = 時間(μs) * 音速（340m/s = 0.034cm/μs）/ 往復(2)
            volatileCalculatedDistance = duration * 0.034 / 2;
            volatileEchoStartTime = 0; // 次回計測のため初期化
        }
    }
}

// ── 感情重みテーブル ──────────────────────────────────────────
// static const: constにより「書き換え不可（読み取り専用）」になり、さらに
// staticをつけることでこのファイル内だけでアクセス可能なローカルデータ（静的記憶領域）になる
static const uint8_t TIME_WEIGHTS[TZONE_COUNT][EMOTION_COUNT] = {
    // [0], [1], [2], [3], [4], [5],     [6],  [7],  [8],  [9],   [10],  [11]
    //  NOR, HAP, ANG, SAD, CON, SLEEPY, [EAT, SLEEP, BATH, SNACK, SMILE, FEAST]
    {  35,  40,   5,   5,   5,   10,     0,    0,    0,    0,    0,    0  }, // MORNING
    {  55,  20,   5,   5,   5,    5,     0,    0,    0,    0,    0,    0  }, // WORK
    {  30,  15,   5,   5,   5,   40,     0,    0,    0,    0,    0,    0  }, // AFTERLUNCH
    {  40,  20,  10,   5,  15,   10,     0,    0,    0,    0,    0,    0  }, // AFTERNOON
    {  35,  40,   5,   5,   5,   10,     0,    0,    0,    0,    0,    0  }, // EVENING
    {  30,   5,  10,   0,   5,   50,     0,    0,    0,    0,    0,    0  }  // NIGHT
};

// ── WiFi接続マニュアル処理 ───────────────────────────────────
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
        if (count > 60) { 
            Serial.println(F("\n[WiFi] Still connecting..."));
            return false;
        }
    }

    Serial.println(F("\n[WiFi] Connected!"));
    return true;
}

// ── (1) WiFi通信 ＆ 遠隔エサやり常駐Webサーバータスク ─────────────────────
// void *pvParameters: FreeRTOSのタスク関数の決まり文句。どんな型のポインタでも受け取れる「汎用ポインタ」
void TaskWifiSync(void *pvParameters) {
    // 起動直後の各タスク初期化ラッシュ時のメモリ衝突(BKPT)を防ぐため5秒待つ防衛策
    // pdMs_TO_TICKS(ms): ミリ秒単位の時間を、OSが解釈できる「Tick（カウント数）」に変換するマクロ
    // vTaskDelay: 単なる待機ではなく、CPUを他のタスクに受け渡すOS関数
    vTaskDelay(pdMS_TO_TICKS(5000));

    // ...WiFi接続処置...
    Serial.println(F("--- [SYSTEM] WiFiSync Task starting... ---"));
    
    // 接続の試行回数をログに出す
    int attempt = 0; // カウンターをゼロで初期化

    while(WiFi.status() != WL_CONNECTED) {
        Serial.print(F("[WiFi] Attempt "));
        Serial.println(++attempt);      // n回目で失敗したのかを可視化

        // もし接続成功ならループを抜ける
        if (connectWiFi()) break;

        // 接続待ちの時間もOLEDに描画を表示させるため
        vTaskDelay(pdMS_TO_TICKS(5000));  // 接続失敗時は5秒休止してリトライ。この間、他のタスクにCPUを引き渡す
    }
    
    // 時刻同期（NTP）だけ失敗したか、成功したかログに残す
    if (ntpSync()) {
        Serial.println(F("[SYSTEM] NTP Sync SUCCESS!"));
    } else {
        Serial.println(F("[SYSTEM] NTP Sync FAILED."));
    }

    // Webサーバーをポート80で起動
    server.begin();
    Serial.println(F("[SERVER] Web Server Started."));
    Serial.print(F("[SERVER] URL -> http://"));
    Serial.println(WiFi.localIP());

    // ── Webサーバー常駐タスク (TaskWifiSync) ─────────────────────
    // このループは一度開始されると、再起動まで永久にスマホからのアクセスを監視し続ける
    for (;;) {

        // server.available(): 接続要求（コネクション）が来ているか確認
        // クライアントがいない間は、この下のif文は無視され、CPUを明け渡す
        WiFiClient client = server.available(); 

        if (client) {
            String currentLine = "";        // 送信されてくるHTTPヘッダ行を蓄積するバッファ
            bool isFeedRequested = false;   // 餌やりボタンが押されたか判定するトリガーフラグ

            // クライアントが接続されている間、リクエスト内容を一行ずつ解析
            while (client.connected()) {
                if (client.available()) {
                    char c = client.read();

                    // HTTPリクエストの最後（空行）を検知 = ヘッダ読み込み完了
                    if (c == '\n' && currentLine.length() == 0) {
                        // 【HTTP応答ヘッダの送信】ブラウザに対してWebページを返す
                        client.println(F("HTTP/1.1 200 OK"));
                        client.println(F("Content-type:text/html; charset=utf-8"));
                        client.println(F("Connection: close"));     // 処理完了後に切断を明示
                        client.println();

                        // 【Flashメモリから送信】RAMを消費せず、プログラム領域から直接HTMLを流し込む
                        client.print(FPSTR(html_template));  // web_page.hのHTMLを送信
                        break;
                    }

                    // 改行コード(\n)が来るまでリクエスト文字列を組み立てる
                    if (c == '\n') currentLine = "";
                    else if (c != '\r') currentLine += c;

                    // 【ルーティング判定】URL末尾に /feed が含まれていたらエサやりフラグを立てる
                    // スマホからエサやりボタン判定
                    // ※ ブラウザのボタン押下により、このパスへのGETリクエストが送信される
                    if (currentLine.endsWith("GET /feed")) {
                        isFeedRequested = true;
                    }
                }
            }
            // 【通信の終了】ブラウザへのHTML送信が完了したらコネクションを切断
            // これにより、スマホ側のブラウザもローディングが完了する
            client.stop();

            // 【アクションの発行】
            // クライアントとの通信を切断した後でアクションを起こす（通信の遅延をUIに影響させない）
            if (isFeedRequested) {
                Serial.println(F("[SERVER] Remote Feast Command Received!"));

                // 【感情タスクへの通知】キュー経由でハート表示モードを要求
                EmotionType feast = EMOTION_FEAST;
                xQueueSend(xEmotionQueue, &feast, 0);

                // 【サウンドタスクへの通知】キュー経由で特別な鳴き声を要求
                SoundEvent soundEvent;
                soundEvent.isClick = false;
                soundEvent.emotion = EMOTION_FEAST;
                xQueueSend(xSoundQueue, &soundEvent, 0); // 液晶タスクへ通知
            }
        }

        // 【CPU負荷の分散】10ミリ秒の待機を入れ、他のタスク（ディスプレイ描画等）に実行権を譲る
        // このDelayがないと、Webサーバ処理がCPUを独占し、画面がカクツク
        vTaskDelay(pdMS_TO_TICKS(10));  // CPU消費を抑えるための待機

    }
}

// ── (2) 初期化・タスク生成タスク ──────────────────
void TaskInitAndLaunch(void *pvParameters) {

    // 各ハードウェアの初期化
    RTC.begin();
    randomSeed(analogRead(A1));  // 未接続ピンのノイズを利用して乱数のシード（初期値）を完全にランダム化
    soundInit();
    
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        display.clearDisplay();
        display.display();
        eyeInit(display);
    }
    
    // 各メインタスクの生成（スタックサイズはヒープを破壊しない安全値を維持）
    // xTaskCreate: OSに新しい「仕事（並行処理タスク）」を登録する関数
    // 引数: xTaskCreate（関数ポインタ, "タスク識別名", スタックサイズ[ワード/バイト ※環境依存], パラメータ, 優先度, ハンドル保存先）
    // 優先度: 数値が大きいほど優先される。Display(3)は描画を滑らかにするために最優先
    // スタックサイズ（第3引数）の決定が、メモリ枯渇を防ぐポイント
    xTaskCreate(TaskDisplay, "Display", 384, NULL, 3, &xDisplayHandle);
    xTaskCreate(TaskEmotion, "Emotion", 192, NULL, 1, NULL);
    xTaskCreate(TaskButton,  "Button",  128, NULL, 2, NULL);
    xTaskCreate(TaskSound,   "Sound",   352, NULL, 1, NULL);
    // ※ NTPタスクは TaskWifiSync がsetup() で担当するためここでは不要

    vTaskDelete(NULL); // 自分のタスクを終了して、スタックメモリを解放する
}

// ── (3) setup() （初期化関数）──────────────────────────────────
// ここではタスク生成とOS起動（スケジューラー起動）のみに集中
void setup() {
    Serial.begin(9600);

    pinMode(BUTTON_CLOCK, INPUT_PULLUP); // ボタンピンを内蔵プルアップ抵抗に定義
    analogWrite(BUZZER_PIN, 0);          // スピーカから鳴らす音を常にオフ（0:LOW）にする（ノイズ対策）

    // 通信キューの生成
    // xQueueCreate: タスク間通信用の「伝言板」を作成、固定長のリングバッファ（メモリ領域）を確保する
    // 引数: xQueueCreate(要素数, 1要素あたりのバイトサイズ)
    // sizeof(型)を渡すことで、C++の構造体や列挙列そのものを丸ごと格納できる領域を作成
    // 感情の変化を伝える
    xEmotionQueue = xQueueCreate(4, sizeof(EmotionType)); // EmotionType（感情別ID）を受け取った TaskDisplay は表情の描画を変える
    // 「物理ボタンが押されたからモードを切り替えて」という命令
    xModeQueue    = xQueueCreate(4, sizeof(DisplayMode)); // DisplayMoed（目モードか、時計モードか）
    // 「今の感情に合わせた鳴き声を再生」という命令
    xSoundQueue   = xQueueCreate(4, sizeof(SoundEvent));  // SoundEvent（「ボタンのプッシュ音」「何の感情の鳴き声か？」という情報が詰まった構造体）

    // WiFi接続という重い処理のせいで描画がフリーズするのを防ぐため、ここで実行
    // 初期化タスクの優先度を一番高い「4」に設定し、ハードウェアを最優先で立ち上げる
    xTaskCreate(TaskInitAndLaunch, "Init", 512, NULL, 4, NULL);
    
    // WiFiタスクは通信待ちでブロッキング（待機）が多いため、低い優先度「1」で裏に回す
    // 常駐タスク化に伴い、スタックサイズを768から「480」に最適化して全体の空きヒープを強固に防衛
    xTaskCreate(TaskWifiSync, "WifiSync", 480, NULL, 1, NULL);

    // ─── 超音波センサーのGPIO（汎入出力ポート）・外部割り込み登録 ───────────────────
    pinMode(ULTRA_TRIG_PIN, OUTPUT);
    pinMode(ULTRA_ECHO_PIN, INPUT);     // 外部割り込み用ににINPUT
    digitalWrite(ULTRA_TRIG_PIN, LOW);

    // Echoピン（D2）の電位変化（CHANDE = HIGH/LOW両方）を監視するように登録
    // attachInterrupt: 指定ピンの電位変化（CHANGE: LOW->HIGH / HIGH->LOW 両方）でISR（割り込みサービスルーチン）を強制発動させる
    // デジタルピン番号を割り込み番号（ベクタ）へと自動変換する digitalPinToInterrupt マクロを使用
    attachInterrupt(digitalPinToInterrupt(ULTRA_ECHO_PIN), echoInterruptHandler, CHANGE);

    // FreeRTOSの心臓であるスケジューラーを起動
    // ここを境界にプログラムの動きは上から下への直線的な実行から、OSによる「タスクの超高速切り替え駆動」へと変化する
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
    // `static` を付けない場合、タスクのスタック領域に確保される（このタスクが持つ専用のスタック領域（384バイト内）に保持）
    // mode: 現在どの画面を表示しているか（目 or 時計）
    // emotion: 現在の感情ID
    DisplayMode   mode     = MODE_EYE;
    EmotionType   emotion  = EMOTION_NORMAL;

    // 【タイムアウト管理】
    // 現在の時刻（now）と比較して「何時まで表示するかを保持」
    unsigned long showEnd  = 0;     // 符号無し長整数

    // 【一時保存用変数】
    // キューから届いたメッセージを受けとるための入れ物
    DisplayMode   modeMsg;
    EmotionType   emotionMsg;

    // フラグの管理場所をTaskDisplay内に限定して確実に制御
    bool isFeasting = false;
    unsigned long feastEnd = 0;


    // 【無限ループ】
    // OSタスクは一度終了すると消滅してしまうため、ループ内で常に動かし続ける必要がある
    for (;;) {

        // 【現在時刻の取得】
        // システム起動から経過時間を取得（単位:ミリ秒）
        // delay()を使用せず、時間で処理を制御するための心臓部
        unsigned long now = millis();

        // ── モード切替メッセージ受信 ──────────────────────────
        // 【キューの確認】
        // xQueueReceive(対象キュー, 格納先アドレス, 待機Tick数): キューにデータが来るのを待つ
        // 他のタスク（ボタン等）から「モードを変えろ」という指示が来ているかを確認
        // 第3引数を「0」にすることで、データがなくても待たずに進むという指定になる
        // これによりボタンが押されていなくても、液晶の目のアニメーション描画（15fps）を止めずに回し続けられる
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

                // 新しいモード（時計）へ切替
                // モードを変更し、さらに「終了時刻（showEnd）」を予約
                mode    = modeMsg;
                showEnd = now + MODE_SHOW_MS;  // 現在時刻 + 5秒後
            }
        }

        // ── タイムアウトで目に自動復帰 ────────────────────────
        // 【非同期の強み】
        // もし「目」以外の画面を表示中(!=)で、かつ（&&）現在時刻を過ぎていたら、
        // 命令がなくても自動で「目」のモードに戻す
        if (mode != MODE_EYE && millis() >= showEnd) {
            mode = MODE_EYE;
        }

        // 2. 感情変化の受信（ここが最重要）
        if (xQueueReceive(xEmotionQueue, &emotionMsg, 0) == pdTRUE) {
            // スマホからのエサやり信号が来た場合
            // FEAST（エサやり）時は5秒間、他の感情変更をロックしてハート演出を優先する
            if (emotionMsg == EMOTION_FEAST) {
                emotion = EMOTION_FEAST;
                feastEnd = now + 5000UL; // 5秒キープ
                isFeasting = true;
                Serial.println(F("[DISPLAY] HEART MODE ON!"));
            } 
            // 通常の感情変化
            else if (!isFeasting) {
                emotion = emotionMsg;
            }
        }

        // 3. 感情演出終了判定（タイムアウトで通常に戻る）
        if (isFeasting && now >= feastEnd) {
            isFeasting = false;
            emotion = EMOTION_NORMAL; // 基本の顔に戻す
            Serial.println(F("[DISPLAY] HEART MODE OFF"));
        }

        // 4. 描画の実行（modeによって時計と目を選択）
        if (mode == MODE_EYE) {
            // 💡 ここで確実に「現在のemotion」を渡す
            eyeUpdate(display, emotion);
        } else {
            if (now >= showEnd) mode = MODE_EYE;
            clockDraw(display);
        }

        vTaskDelay(pdMS_TO_TICKS(66));
    }
}
void TaskButton(void *pvParameters) {
    bool lastClock   = HIGH;
    uint8_t sensorTick = 0;  
   
    for (;;) {
        bool curClock   = digitalRead(BUTTON_CLOCK);
        if (lastClock == HIGH && curClock == LOW) {
            vTaskDelay(pdMS_TO_TICKS(20));
            if (digitalRead(BUTTON_CLOCK) == LOW) { 
                SoundEvent btnEvent;
                btnEvent.isClick = true; 
                btnEvent.emotion = EMOTION_NORMAL; 
                xQueueSend(xSoundQueue, &btnEvent, 0);

                DisplayMode m = MODE_CLOCK;
                xQueueSend(xModeQueue, &m, 0);
                Serial.println(F("[BTN1] clock")); 
            }
        }
        lastClock  = curClock;

        sensorTick++;
        if (sensorTick >= 50) { // 500ms周期
            sensorTick = 0;

            noInterrupts();
            int currentDistance = volatileCalculatedDistance; 
            volatileCalculatedDistance = -1;
            interrupts();

            digitalWrite(ULTRA_TRIG_PIN, LOW);
            delayMicroseconds(2);
            digitalWrite(ULTRA_TRIG_PIN, HIGH);
            delayMicroseconds(10);
            digitalWrite(ULTRA_TRIG_PIN, LOW);

            if (currentDistance > 0 && currentDistance < 400) { 
                if (currentDistance <= DISTANCE_THRESHOLD && currentDistance > 2) {
                    if (!isHandDetected) {  
                        isHandDetected = true;
                        EmotionType targetSmile = EMOTION_SMILE;
                        xQueueSend(xEmotionQueue, &targetSmile, 0);  

                        if (!isSensorReacting) {
                            isSensorReacting = true;
                            SoundEvent event;
                            event.isClick = false;
                            event.emotion = targetSmile;
                            xQueueSend(xSoundQueue, &event, 0); 
                        }
                        Serial.println(F("[SENSOR] Hand detected -> SMILE"));
                    }
                }
                else {
                    if (isHandDetected) {  
                        isHandDetected = false;
                        EmotionType restoreEmotion = currentBaseEmotion;
                        xQueueSend(xEmotionQueue, &restoreEmotion, 0);
                        Serial.println(F("[SENSOR] Hand removed -> Restore Face"));
                    }
                }
            }
        }   
        vTaskDelay(pdMS_TO_TICKS(10));
    } 
}

static RobotTimeZone getCurrentTimeZone() {
    RTCTime now;
    RTC.getTime(now); 
    int h = now.getHour();
    if      (h >= 6  && h < 9 ) return TZONE_MORNING;
    else if (h >= 9  && h < 12) return TZONE_WORK;
    else if (h >= 12 && h < 15) return TZONE_AFTERLUNCH;
    else if (h >= 15 && h < 18) return TZONE_AFTERNOON;
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
    uint16_t soundTick = 0;
    uint32_t screenWaitTicks = 0;
    uint32_t targetScreenWait = 0;

    targetScreenWait = pdMS_TO_TICKS(180000) + (uint32_t)random(0, (long)pdMS_TO_TICKS(120000));
    EmotionType currentEmotion = EMOTION_NORMAL;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        soundTick++;
        if (soundTick >= 4) { 
            soundTick = 0;
            if (random(100) < 35) {
                SoundEvent event;
                event.isClick = false;   
                event.emotion = currentEmotion; 
                xQueueSend(xSoundQueue, &event, 0);
                Serial.print(F("[EMOTION-SOUND] Match Sound Triggered! ID="));
                Serial.println((int)currentEmotion);
            }
        }

        screenWaitTicks += pdMS_TO_TICKS(5000);
        if (screenWaitTicks >= targetScreenWait) {
            screenWaitTicks = 0;
            targetScreenWait = pdMS_TO_TICKS(180000) + (uint32_t)random(0, (long)pdMS_TO_TICKS(120000));

            RTCTime nowTime;
            RTC.getTime(nowTime);
            bool isTimeValid = (WiFi.status() == WL_CONNECTED) && (nowTime.getYear() >= 2024);
            EmotionType e = EMOTION_COUNT;

            if (isTimeValid) {
                uint8_t hour   = nowTime.getHour();
                uint8_t minute = nowTime.getMinutes();
                if (hour >= 22 || hour < 7)         e = EMOTION_SLEEPING;
                else if (hour == 7  && minute < 15) e = EMOTION_HAPPY;
                else if (hour == 9  && minute < 15) e = EMOTION_EATING;
                else if (hour == 12 && minute < 15) e = EMOTION_EATING;
                else if (hour == 15 && minute < 15) e = EMOTION_SNACK;
                else if (hour == 18 && minute < 15) e = EMOTION_EATING;
                else if (hour == 20 && minute < 15) e = EMOTION_BATH;
                else if (hour == 21)                e = EMOTION_SLEEPY;
            }

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

            currentEmotion = e;
            if (!isHandDetected) {
                xQueueSend(xEmotionQueue, &e, 0);
            } else {
                Serial.print(F("[EMOTION] Screen skip (Hand detected) -> "));
            }

            Serial.print(F("zone= "));
            Serial.print((int)getCurrentTimeZone()); 
            Serial.print(F(" -> "));
            Serial.println((int)e);
        }
    }
}

void TaskSound(void *pvParameters) {
    SoundEvent event;
    for (;;) {
        if (xQueueReceive(xSoundQueue, &event, portMAX_DELAY) == pdTRUE) {
            Serial.print(F("[SOUND TASK] Received event! isClick="));
            Serial.print(event.isClick);
            Serial.print(F(", Emotion ID="));
            Serial.println((int)event.emotion);
            
            if (!event.isClick && event.emotion == EMOTION_SMILE) {
                isSensorReacting = true;
            }

            if (event.isClick) {
                playSwitchSound();
            } else {
                playCry(event.emotion);
            }
            isSensorReacting = false;
        }
    } 
}
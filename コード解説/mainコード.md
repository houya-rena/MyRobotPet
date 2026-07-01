# main.ino

## 全体の設計思想

- このコードはArduinoの通常の `loop()` を**使用していない**
- 代わりにFreeRTOSというリアルタイムOSを乗せて、機能ごとに独立した「タスク」を並行動作させている

```text
通常のArduino:  setup() → loop() → loop() → loop() …（1本道）

このコード:     FreeRTOSが6つのタスクを超高速で切り替え
                → 見かけ上、全部が同時に動いているように見える
```

## ① 起動前の準備（グローバル定義）1〜140行目

- コードの冒頭はタスクが動き始める前の「設計図」にあたる部分

- **ライブラリの読み込み**（48〜72行目）

```cpp
#include <Arduino_FreeRTOS.h>   // OSの本体
#include <Adafruit_SSD1306.h>   // OLEDディスプレイ
#include <WiFiS3.h>             // WiFi通信
#include "eye_animation.h"      // 目の動き（自作）
#include "sound.h"              // 鳴き声（自作）
```

- **ピン番号・定数の定義**（74〜95行目）

```cpp
#define BUTTON_CLOCK      4    // ボタンはD4ピン
#define ULTRA_TRIG_PIN    3    // 超音波センサーのTrigはD3
#define ULTRA_ECHO_PIN    2    // EchoはD2（割り込み対応ピン）
#define DISTANCE_THRESHOLD 30  // 30cm以内で反応
#define MODE_SHOW_MS   5000UL  // 時計表示は5秒
```

- **タスク間で共有する変数**（101〜134行目）
- 複数のタスクから同時に読み書きされる変数には `volatile` をつけている。これはコンパイラが「最適化でレジスタにキャッシュしてしまう」のを防ぐためのキーワード

```cpp
volatile bool isHandDetected   = false;  // 手を検知中か
volatile bool isSensorReacting = false;  // 音の排他ロック
volatile int  volatileCalculatedDistance = -1;  // ISRが書き込む距離値
```

- **タスク間通信の「伝言板」（キュー）**（129行目）

```cpp
QueueHandle_t xEmotionQueue;  // 「表情を変えろ」命令の通路
QueueHandle_t xModeQueue;     // 「時計/目を切り替えろ」命令の通路
QueueHandle_t xSoundQueue;    // 「この音を鳴らせ」命令の通路
```

## ② 超音波センサーの割り込み処理（echoInterruptHandler）149〜164行目

- 普通のセンサー読み取りは `pulseIn()` という関数でやるが、これはCPUを占有して他の処理を止めてしまうため、このコードでは**ハードウェア割り込み**を使っている

```text
通常: pulseIn() → CPUが待ちっぱなし（他のタスクが止まる）

このコード: Echoピンの電圧変化 → ISRが自動実行 → CPUは別の作業を続けられる
```

```cpp
void echoInterruptHandler() {
    if (digitalRead(ULTRA_ECHO_PIN) == HIGH) {
        volatileEchoStartTime = micros();      // 立ち上がり時刻を記録
    } else {
        unsigned long duration = endTime - volatileEchoStartTime;
        volatileCalculatedDistance = duration * 0.034 / 2;  // 距離を計算して保存
    }
}
```

- ISRの中では `delay()` や `Serial.print()` を使用。やることを最小限（時刻記録と距離計算だけ）にして即座に終了させる

## ③ 感情の重みテーブル（TIME_WEIGHTS）169〜178行目

- 時間帯ごとにどの感情が出やすいかを数値で定義したテーブル

```cpp
//                  普通  嬉  怒  悲  集中  眠
{ 35, 40,  5,  5,  5, 10, ... },  // 朝（MORNING）
{ 55, 20,  5,  5,  5,  5, ... },  // 午前（WORK）
{ 30, 15,  5,  5,  5, 40, ... },  // 昼食後（眠気50%近い）
{ 30,  5, 10,  0,  5, 50, ... },  // 深夜（NIGHT）
```

- 合計が100になるように設計されているので、そのままパーセントとして読むことができる。昼食後の「眠い」が40%、深夜の「眠い」が50%など、時間帯ごとに合った感情を表している

## ④ setup() / TaskInitAndLaunch — 起動シーケンス（341〜337行目）

- `setup()` は**タスクを登録してOSを起動するだけ**で終了

```cpp
void setup() {
    // キューを3本作る
    xEmotionQueue = xQueueCreate(4, sizeof(EmotionType));
    xModeQueue    = xQueueCreate(4, sizeof(DisplayMode));
    xSoundQueue   = xQueueCreate(4, sizeof(SoundEvent));

    // 初期化担当タスクとWiFiタスクを登録
    xTaskCreate(TaskInitAndLaunch, "Init",     512, NULL, 4, NULL);
    xTaskCreate(TaskWifiSync,      "WifiSync", 480, NULL, 1, NULL);

    // ISRを登録（Echoピンの電圧変化でechoInterruptHandlerを呼ぶ）
    attachInterrupt(digitalPinToInterrupt(ULTRA_ECHO_PIN), echoInterruptHandler, CHANGE);

    vTaskStartScheduler();  // OSを起動。以降はOSがタスクを切り替え続ける
}
```

- `TaskInitAndLaunch`（優先度4・最高）はOLED・RTC・スピーカーを初期化してから残り4タスクを生成し、`vTaskDelete(NULL)` で自分自身を削除する。512ワードのスタックがヒープに返却されるため、メモリの無駄がない

## ⑤ TaskWifiSync — WiFi + Webサーバー常駐タスク（205〜309行目）

- 2段構成

- **前半：接続 → NTP同期（使い捨て）**

```cpp
vTaskDelay(5000);        // 他タスクの初期化が落ち着くまで5秒待つ
while (!connectWiFi()) { ... }  // 繋がるまでリトライ
ntpSync();               // NTPで正確な時刻をRTCに書き込む
server.begin();          // ポート80でWebサーバーを起動
```

- **後半：Webサーバーとして永久ループ**

```cpp
for (;;) {
    WiFiClient client = server.available();
    if (client) {
        // HTTPリクエストを読む
        if (currentLine.endsWith("GET /feed")) {
            // スマホの「エサやり」ボタンが押された
            xQueueSend(xEmotionQueue, &feast, 0);   // ハート表情を送信
            xQueueSend(xSoundQueue,   &soundEvent, 0);  // 特別な鳴き声を送信
        }
        client.print(FPSTR(html_template));  // HTMLページを返す
    }
    vTaskDelay(10);  // 10msごとにCPUを他タスクに渡す
}
```

- スマホのブラウザから `http://<IPアドレス>/feed` にアクセスすると `EMOTION_FEAST`（ハート）がキュー経由でディスプレイタスクに届く

## ⑥ TaskDisplay — 描画タスク（389〜503行目）

- 66ms（≈15fps）ごとに回るループで、受信したキューの内容に応じて表示を切り替える

```text
毎ループの処理順:
  1. xModeQueue 確認 → モード切替 or 再押しで戻る
  2. タイムアウト確認 → 5秒経過でMODE_EYEへ自動復帰
  3. xEmotionQueue 確認 → 表情更新（FEASTなら5秒ロック）
  4. 描画実行
       MODE_EYE   → eyeUpdate(emotion)   目アニメーション
       MODE_CLOCK → clockDraw()          時計
```

- ポイントは `xQueueReceive()` の第3引数がすべて `0`（ノーウェイト）である。キューが空でもブロックせず即次の処理に進むので、15fpsの描画が止まらない

## ⑦ TaskButton — ボタン + センサー監視タスク（515〜622行目）

- 10msごとに2つの仕事をしている

- **機能1：ボタン監視（毎ループ）**
  - 「今押した瞬間」だけを検出する**立ち下がりエッジ検出**を使用

```cpp
if (lastClock == HIGH && curClock == LOW) {  // 今まさに押された瞬間
    vTaskDelay(20);  // 20ms待ってチャタリングが消えるのを待つ
    if (digitalRead(BUTTON_CLOCK) == LOW) {  // まだ押されていれば本物
        xQueueSend(xSoundQueue, &btnEvent, 0);  // ピッ音を鳴らす
        xQueueSend(xModeQueue,  &m, 0);          // 時計モードへ切替
    }
}
```

- **機能2：センサー判定（500msごと = sensorTick が50になるたびに）**

```cpp
noInterrupts();                         // 割り込みを一瞬禁止
int d = volatileCalculatedDistance;     // ISRが書いた距離を安全に取得
volatileCalculatedDistance = -1;        // バッファをクリア
interrupts();                           // 割り込み再開

// Trigパルスを送って次の計測をトリガー
digitalWrite(ULTRA_TRIG_PIN, HIGH);
delayMicroseconds(10);
digitalWrite(ULTRA_TRIG_PIN, LOW);

// 距離で判定
if (d <= 30) {       // 手がある → SMILE + 鳴き声
    isHandDetected = true;
    xQueueSend(xEmotionQueue, &smile, 0);
} else {             // 手が離れた → 元の表情に戻す
    isHandDetected = false;
    xQueueSend(xEmotionQueue, &currentBaseEmotion, 0);
}
```

- マルチバイト変数（int = 2バイト）の読み出しはCPU的に「一撃」ではないため、途中でISRが割り込むとデータが壊れる可能性があるため`noInterrupts()` / `interrupts()` で囲む

---

## ⑧ TaskEmotion — 感情タイマータスク（666〜771行目）

- 5秒ごとに起きて、2種類の時間軸を1タスクで管理する「**マルチタイムベース設計**」

```text
5秒ごとに起きる
  │
  ├─ soundTick をカウント
  │    4回たまったら（= 20秒）→ 35%の確率でxSoundQueueへ
  │
  └─ screenWaitTicks に5000ms加算
       3〜5分たまったら表情を変更する
            │
            ├─ RTCイベント時刻なら固定感情（例: 12時 → EATING）
            └─ それ以外 → TIME_WEIGHTSから重み付きランダム抽選
```

- 本来ならタイマータスクを2本立てるところを、カウンタで「5秒×4=20秒」「5秒×36=3分」を数えることでタスクを1本に節約し、スタック消費とヒープ消費を削減している

## ⑨ TaskSound — 鳴き声再生タスク（776〜812行目）

- 最もシンプルな構造で、キューが来るまで完全にスリープしているだけ

```cpp
for (;;) {
    xQueueReceive(xSoundQueue, &event, portMAX_DELAY);  // 来るまで無限待機・CPU使用率0%

    if (event.isClick) {
        playSwitchSound();       // ボタン音（ピッ）
    } else {
        playCry(event.emotion);  // 感情の鳴き声
    }

    isSensorReacting = false;    // 音が終わったら排他ロックを解除
}
```

- `portMAX_DELAY` により、音がない間はCPUを一切使用しない
- 音が鳴っている最中は `isSensorReacting = true` になっているので他のタスクからの音の割り込みをブロック可能

## まとめ：タスク間の情報の流れ

```text
[超音波ISR] ──距離を書き込む──→ volatileCalculatedDistance
                                        ↓ TaskButtonが500msごとに回収
[ボタン D4] ──────────────────→ TaskButton ──→ xModeQueue  ──→ TaskDisplay
                                        └──→ xEmotionQueue ──→ TaskDisplay
                                        └──→ xSoundQueue   ──→ TaskSound

[スマホWeb] ──GET /feed────────→ TaskWifiSync ─→ xEmotionQueue + xSoundQueue

[タイマー]  ──5秒周期──────────→ TaskEmotion  ─→ xEmotionQueue + xSoundQueue

                                   TaskDisplay ──→ OLED描画
                                   TaskSound   ──→ スピーカー出力
```

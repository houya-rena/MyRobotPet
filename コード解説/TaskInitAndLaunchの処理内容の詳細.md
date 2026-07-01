# `TaskInitAndLaunch`の処理内容の詳細

- `TaskInitAndLaunch` は**優先度4（最高）で一度だけ走り、終わったら自滅する「コンストラクタ専用タスク」**
- 処理は大きく2段階に分かれる

## 段階1：ハードウェア初期化（順番が重要）

```cpp
RTC.begin();
randomSeed(analogRead(A1));  // 未接続A1ピンのノイズ → 真のランダムシード
soundInit();

if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    display.clearDisplay();
    display.display();
    eyeInit(display);        // 目の描画パラメータ初期化
}
```

- スケジューラー起動後（`vTaskStartScheduler()` 以降）でないと `vTaskDelay()` などFreeRTOS APIが使えないため、`setup()` 直接ではなく**タスクの中でやる実行する**
- **メリット：**
  - 重いWiFi処理（`TaskWifiSync`）と並行して初期化を進めることができる

## 段階2：残り4タスクを生成

| タスク | スタック | 優先度 | 役割 |
|---|---|---|---|
| `TaskDisplay` | 384 | 3 | OLED描画（最高優先） |
| `TaskEmotion` | 192 | 1 | 感情変化タイマー |
| `TaskButton` | 128 | 2 | ボタン＋センサー |
| `TaskSound` | 352 | 1 | 鳴き声再生 |

- スタックサイズはヒープ枯渇（BKPTエラー）を防ぐため、実測で最小限に絞った値
- `TaskDisplay` の384は描画ライブラリ（Adafruit SSD1306）のスタック消費が大きいため大きめに確保

## 段階3：`vTaskDelete(NULL)` で自滅 → メモリ解放

```cpp
vTaskDelete(NULL);  // NULL = 自分自身を削除
```

- `TaskInitAndLaunch` のスタック（512ワード）がここで完全に解放されてヒープに戻る
- 一度しか使わない初期化コードがRAMを占有し続けないための**使い捨て設計**

## なぜ `setup()` に直接書かなかったか

```text
setup() の中でできないこと
  → vTaskDelay() などFreeRTOS APIはスケジューラー起動前に呼べない
  → WiFiSync（優先度1）より先に確実に終わらせたい処理がある

解決策
  → 優先度4のTaskInitAndLaunchを先に走らせ、
    終わり次第 vTaskDelete で解放
```

- この設計により、`TaskWifiSync` のWiFi接続待ち（最大30秒）の間もOLEDの目アニメーションが止まらず動き続けることができる

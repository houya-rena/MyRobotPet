# 自律型ロボットペット開発プロジェクト

## 概要

- Arduino UNO R4 WiFiをベースとした、自律的に表情を変え、時刻を表示し、環境音を再生するロボットペットのプロジェクト。
- FreeRTOSを用いたマルチタスク制御により、各機能の並行動作を実現しています。

## プロジェクトの目的

- **組み込みスキルの可視化**: FreeRTOSを用いたタスク制御、メモリ制限下での幾何学描画など、低レイヤ技術の統合的な証明。
- **表現力の追求**: 数式による描画ロジックと状態遷移を用いた、愛着の湧くインタラクティブな表現。

## 技術的な見どころ

- **FreeRTOS によるマルチタスク**: 描画（高優先度）・UI監視・通信・音響を分離し、堅牢な動作を実現。
- **数式によるモーフィング描画**: ビットマップを使わず幾何学関数で描画し、SRAM を節約。
- **非同期状態遷移**: `delay()` を排除し、`millis()` ベースの感情管理による生命感の演出。
- **外部割り込みによる超音波センサー**: Echo ピンの立ち上がり/立ち下がりを割り込みで計測し、メインタスクをブロックしない設計。
- **2フラグによる状態分離**: 「手の検知状態（`isHandDetected`）」と「音再生の排他ロック（`isSensorReacting`）」を分離し、センサー反応中も正確に手の離脱を検知可能。

## 完成イメージ

<img width="1380" height="752" alt="image" src="https://github.com/user-attachments/assets/5918699d-3ab9-430a-b0ba-d5d94294a610" />

## 回路図

<img width="879" height="591" alt="スクリーンショット 2026-06-25 15 35 54" src="https://github.com/user-attachments/assets/4247ec28-a2e0-467e-987a-c237de24c126" />

## 動作仕様
 
### 表示モード
 
| モード | 内容 |
|---|---|
| `MODE_EYE` | 感情に応じた目アニメーションを表示（デフォルト） |
| `MODE_CLOCK` | 時刻を表示。ボタン再押しまたは5秒で自動復帰 |
 
### 感情・時間帯
 
時間帯ごとに重み付きランダムで感情が変化します。
 
| 時間帯 | 時刻 |
|---|---|
| 朝（MORNING） | 06:00〜08:59 |
| 午前（WORK） | 09:00〜11:59 |
| 昼（AFTERLUNCH） | 12:00〜14:59 |
| 午後（AFTERNOON） | 15:00〜17:59 |
| 夕方（EVENING） | 18:00〜21:59 |
| 夜（NIGHT） | 22:00〜05:59 |
 
### 超音波センサー（手かざし検知）
 
- 30cm 以内に手が検知されると、即座に `EMOTION_SMILE` へ切り替え＋鳴き声を1回再生
- 手が離れると `currentBaseEmotion`（現在の基本感情）に自動復帰
- 計測はフラグに依存せず常時実行されるため、手の離脱を確実に検知
```
手をかざす  → isHandDetected=true  → SMILE 表情 + 鳴き声（1回）
音が終わる  → isSensorReacting=false のみリセット / 表情は維持
手を離す   → isHandDetected=false → 元の表情へ復帰
```
 
### ボタン
 
| ピン | 操作 | 動作 |
|---|---|---|
| D4 | 短押し | 時刻表示（`MODE_CLOCK`）へ切替。再押しまたは5秒で復帰 |
 
---
 
## タスク構成
 
| タスク | 優先度 | スタックサイズ | 役割 |
|---|---|---|---|
| `TaskDisplay` | 3（最高） | 384 | OLED への目・時刻描画 |
| `TaskButton` | 2 | 128 | ボタン入力・超音波センサー読み取り |
| `TaskEmotion` | 1 | 192 | 時間帯別ランダム感情変化 |
| `TaskSound` | 1 | 352 | 感情に応じた鳴き声再生 |
| `TaskWifiSync` | 1 | — | NTP 時刻同期（起動時 + 1時間ごと） |
 
> **注:** `TaskNTP` は `TaskWifiSync` に統合されており、`TaskInitAndLaunch` からは呼び出しません。

## 配線
 
| デバイス | ピン |
|---|---|
| OLED SSD1306（I2C） | SDA, SCL |
| ボタン（時刻表示） | D4 → GND（INPUT_PULLUP） |
| 超音波センサー Trig | D3 |
| 超音波センサー Echo | D2（外部割り込み） |
| 圧電ブザー / スピーカー | A0 |

 
## メモリ管理状況

- システム安定化のため、ヒープ領域を以下の通り設定・管理しています。

- **ヒープ設定値**: `0x2E00` (11,776バイト)
- **最大許容値**: `0x2F80` (12,160バイト)
- **安全バッファ**: `0x180` (384バイト)
- **設計方針**: システム停止（BKPTエラー）を未然に防ぐため、あえて最大値ギリギリを使用せず、余裕を持たせる「防衛的運用」を採用。

## フォルダ構成

- 本プロジェクトは、Arduino IDE環境とPlatformIO環境の両方に対応可能です。

### 1. Arduino IDE

- `src/` フォルダ内のすべての `.cpp` / `.ino` ファイルを1つのフォルダにまとめて読み込む必要があります。
- Arduino IDEでは「include」フォルダの自動認識が弱いため、パスの管理には注意してください。
- 構成は以下の通りです。

```text

RobotPet/               # スケッチフォルダ（この名前のフォルダにまとめる）
├── main.ino        # メインプログラム（フォルダ名と同じ名前にする）
├── clock_display.cpp
├── clock_display.h
├── eye_animation.cpp
├── eye_animation.h
├── eye_draw.cpp
├── eye_draw.h
├── eye_params.cpp
├── eye_params.h
├── eye_state.cpp
├── eye_state.h
├── ntp_sync.cpp
├── ntp_sync.h
├── sound.cpp
├── sound.h
├── pitches.h
├── arduino_secrets.h
├── config.h
└── README.md
```

#### Arduino IDE 運用のコツ

Arduino IDEで開発を続ける場合は、「すべてのファイルを1つのフォルダ（スケッチフォルダ）に集める」のが基本です。

1. 全てのファイルを一つのフォルダに集約してください。
2. ヘッダーのインクルードは `#include "filename.h"` の形式で記述してください（フォルダ階層を含めない）

### 2. PlatformIO (VS Code)

- プロジェクトルートに `platformio.ini` を配置することで、依存ライブラリやビルド設定を自動管理できます。
- 構成は以下の通りです。

```text
.
├── platformio.ini             # PlatformIO設定ファイル
├── src/
│   ├── robot_pet.ino          # メインプログラム
│   ├── clock_display.cpp
│   ├── eye_animation.cpp
│   ├── eye_draw.cpp
│   ├── eye_parms.cpp
│   ├── eye_state.cpp
│   ├── sound.cpp
│   └── ntp_sync.cpp
├── include/
│   ├── clock_display.h
│   ├── eye_animation.h
│   ├── eye_draw.h
│   ├── eye_parms.h
│   ├── eye_state.h
│   ├── sound.h
│   ├── pitches.h
│   ├── ntp_sync.h
│   ├── arduino_secrets.h
│   └── config.h
└── README.md

```

#### PlatformIO移行用：`platformio.ini` の作成

PlatformIO版を作成する場合、プロジェクトルートに `platformio.ini` を作成し、以下を記述してください。これでメモリ設定やライブラリの依存関係が安定します。

```ini
[env:uno_r4_wifi]
platform  = renesas-ra
board     = uno_r4_wifi
framework = arduino

; ... 既存の設定 ...
check_flags =
  cppcheck: --suppress=unusedFunction

; シリアルモニタ速度
monitor_speed = 9600

upload_speed = 115200

; ポートを自動検知ではなく「強制指定」します
upload_protocol = dfu
; デバッグインターフェースを明示的に指定
;debug_tool = cmsis-dap

;upload_flags = --timeout=60

; ここを追加：OpenOCDに「Renesas RA4M1」であることを明示する
board_upload.maximum_size = 262144
board_upload.maximum_ram_size = 32768

; 書き込み失敗を防ぐためのフラグ
upload_flags =
    -c
    "program .pio/build/uno_r4_wifi/firmware.bin verify reset exit 0x00000000"
    
; ── 使用ライブラリ ────────────────────────────
; PlatformIOが自動でダウンロード・管理してくれる
lib_deps =
    adafruit/Adafruit SSD1306 @ ^2.5.7
    adafruit/Adafruit GFX Library @ ^1.11.9
    bblanchon/ArduinoJson @ ^7.0.0

; ── インクルードパス ──────────────────────────
; include/ フォルダを自動認識させる
build_flags =
    -I include

board_upload.use_1200bps_touch = yes

```

## トラブルシューティング履歴

### BKPTエラーの解決（ヒープ枯渇）
- **事象**: 初期設定の8KBではタスクのメモリ確保が追いつかず、システム保護機能（BKPT）によりCPUが強制停止。
- **対策**: ヒープを11KB（0x2E00）へ拡張し、各タスクのスタックサイズを最適化。

- **安定動作の維持**:
  - 静的領域（`static`）の活用によるヒープ消費の抑制。
  - 動的なタスク生成・削除を避け、メモリ断片化（フラグメンテーション）を回避。

### 手を離しても表情が戻らない
 
- **事象**: `isHandDetected=true` の間、センサー計測ブロック全体をスキップしていたため、手の離脱を検知できなかった。
- **対策**: 距離計測（パルス発射・読み取り）を常時実行するよう変更し、距離の値だけで手あり/なしを判断するシンプルな構造に修正。

### ノイズ対策

本プロジェクトでは、電子工作特有の「ノイズ（ジーーーという雑音）」や「動作の不安定さ」を排除するために、ハード・ソフト両面で以下の対策を実施しています。

#### 1. ハードウェア対策（ノイズ発生源の抑制）

- 音質をクリアにし、デジタル回路からの干渉を防ぐための物理的な配線対策です。

- **電源の分離**: OLEDディスプレイ（5V）とオーディオモジュール（3.3V）の電源ラインを分離し、電力干渉を回避しています。
- **一点アースの徹底**: OLEDとスピーカーのGND線をブレッドボードでまとめず、Arduinoの各GNDピンへ個別接続することで、回路同士の電位の引っ張り合い（GNDループノイズ）を防止しています。
- **信号線の隔離**: OLEDの通信線（SDA/SCL）から発生するデジタルノイズをスピーカー線が拾わないよう、ワイヤーの配置を物理的に離しています。

#### 2. ソフトウェア対策（信号のクリーン化）

プログラムの暴走による不快音や、DAC出力のノイズを完全にシャットアウトします。

- **ピンの完全シャットダウン**: 無音時にはオーディオピンを `INPUT` モードへ切り替えることで、待機時の不快な「ジーーー」というホワイトノイズを物理的に遮断しています。
- **排他制御とクジ引き暴走の防止**: 感情抽選タイマーを「鉄壁の構造」で管理し、10〜25秒のインターバルを確実に守ることで、音が重なってバリバリというノイズが発生する現象を根絶しました。

#### 3. 動作の仕組みと特徴

- **生命感あふれる挙動**: 感情に合わせたピコピコ音が、気まぐれかつ絶妙なタイミングで再生されます。
- **静寂の確保**: 鳴き声以外の時は、画面の描画処理がどれほど激しくても、スピーカーは完璧な静寂を保ちます。
- **リアルタイム制御**: 超音波センサー等の割り込み処理を最適化したことで、描画・計算・音声再生が互いに干渉することなく、安定したパフォーマンスを実現しています。

## 使用技術・環境
 
| 分野 | 技術 / ツール |
|---|---|
| **ハードウェア** | Arduino UNO R4 WiFi（RA4M1）, SSD1306 OLED, プッシュボタン, 超音波センサー HC-SR04, 圧電ブザー / スピーカー |
| **OS / Middleware** | FreeRTOS, WiFiS3, ArduinoJson |
| **開発環境** | Arduino IDE（PlatformIO への移行も可能） |
| **管理** | Git / GitHub / Sourcetree |
 

## [完成イメージフォルダまとめ-初期画面などもあり](https://github.com/houya-rena/MyRobotPet/tree/main/%E5%AE%8C%E6%88%90%E3%82%A4%E3%83%A1%E3%83%BC%E3%82%B8)

- [完成イメージ動画](https://github.com/houya-rena/MyRobotPet/blob/main/%E5%AE%8C%E6%88%90%E3%82%A4%E3%83%A1%E3%83%BC%E3%82%B8/%E5%AE%9F%E9%9A%9B%E3%81%AE%E5%8B%95%E4%BD%9C%E5%8B%95%E7%94%BB.md)
- [時間帯別表情](https://github.com/houya-rena/MyRobotPet/blob/main/%E5%AE%8C%E6%88%90%E3%82%A4%E3%83%A1%E3%83%BC%E3%82%B8/%E6%99%82%E9%96%93%E5%B8%AF%E5%88%A5%E3%82%A4%E3%83%99%E3%83%B3%E3%83%88%E8%A1%A8%E6%83%85.md)
- [通常の表情バリエーション](https://github.com/houya-rena/MyRobotPet/blob/main/%E5%AE%8C%E6%88%90%E3%82%A4%E3%83%A1%E3%83%BC%E3%82%B8/%E8%A1%A8%E6%83%85%E7%94%BB%E5%83%8F.md)

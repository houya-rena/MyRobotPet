# 自律型ロボットペット開発プロジェクト

## 概要

- Arduino UNO R4 WiFiをベースとした、自律的に表情を変え、時刻を表示し、環境音を再生するロボットペットのプロジェクト。
- FreeRTOSを用いたマルチタスク制御により、各機能の並行動作を実現しています。

## プロジェクトの目的

- **組み込みスキルの可視化**: FreeRTOSを用いたタスク制御、メモリ制限下での幾何学描画など、低レイヤ技術の統合的な証明。
- **表現力の追求**: 数式による描画ロジックと状態遷移を用いた、愛着の湧くインタラクティブな表現。

## 技術的な見どころ

- **FreeRTOSによるマルチタスク**: 描画（低優先度）、UI監視（高優先度）、通信（中優先度）を分離し、堅牢なUIを実現。
- **数式によるモーフィング描画**: ビットマップを使わず、幾何学関数による描画でSRAMを節約。
- **非同期状態遷移**: `delay()` を排除し、`millis()` ベースの感情管理による生命感の演出。

## 完成イメージ

<img width="1380" height="752" alt="image" src="https://github.com/user-attachments/assets/5918699d-3ab9-430a-b0ba-d5d94294a610" />


## メモリ管理状況

- システム安定化のため、ヒープ領域を以下の通り設定・管理しています。

- **ヒープ設定値**: `0x2E00` (11,776バイト)
- **最大許容値**: `0x2F80` (12,160バイト)
- **安全バッファ**: `0x180` (384バイト)
- **設計方針**: システム停止（BKPTエラー）を未然に防ぐため、あえて最大値ギリギリを使用せず、余裕を持たせる「防衛的運用」を採用。

## 使用技術・環境

| 分野 | 技術 / ツール |
| --- | --- |
| **ハードウェア** | Arduino UNO R4 WiFi (RA4M1), SSD1306 OLED |
| **OS / Middleware** | FreeRTOS, WiFiS3, ArduinoJson |
| **開発環境** | ArduinoIDE（platform IOへの移行も可能） |
| **管理** | Git / GitHub /Sourcetree|

## フォルダ構成

- 本プロジェクトは、Arduino IDE環境とPlatformIO環境の両方に対応可能です。

### 1. Arduino IDE

- `src/` フォルダ内のすべての `.cpp` / `.ino` ファイルを1つのフォルダにまとめて読み込む必要があります。
- Arduino IDEでは「include」フォルダの自動認識が弱いため、パスの管理には注意してください。
- 構成は以下の通りです。

```text

RobotPet/               # スケッチフォルダ（この名前のフォルダにまとめる）
├── RobotPet.ino        # メインプログラム（フォルダ名と同じ名前にする）
├── clock_display.cpp
├── clock_display.h
├── eye_animation.cpp
├── eye_animation.h
├── ntp_sync.cpp
├── ntp_sync.h
├── sound.cpp
├── sound.h
├── TaskSound.cpp
├── TaskWifiSync.cpp
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
│   ├── ntp_sync.cpp
│   ├── sound.cpp
│   ├── TaskSound.cpp
│   └── TaskWifiSync.cpp
├── include/
│   ├── clock_display.h
│   ├── eye_animation.h
│   ├── ntp_sync.h
│   ├── sound.h
│   ├── arduino_secrets.h
│   └── config.h
└── README.md

```

#### PlatformIO移行用：`platformio.ini` の作成

PlatformIO版を作成する場合、プロジェクトルートに `platformio.ini` を作成し、以下を記述してください。これでメモリ設定やライブラリの依存関係が安定します。

```ini
[env:uno_r4_wifi]
platform = renesas-ra
board = uno_r4_wifi
framework = arduino
monitor_speed = 115200

# ライブラリの依存関係（例）
lib_deps = 
    adafruit/Adafruit SSD1306 @ ^2.5.0
    adafruit/Adafruit GFX Library @ ^1.11.0

# メモリ設定（安定化のため）
build_flags = 
    -D configTOTAL_HEAP_SIZE=11776

```

## 動作仕様

- **アニメーションモード**: 感情に応じた目・視線の自律移動
- **天気連動**: 気温・天気コードに応じて、「暑い」「悲しい（雨）」等の感情が自動でモーフィング変化。
- **時刻表示モード**: ボタン操作で即座に切り替え、5秒後に自動復帰。

```text
my-robot-pet/
├── platformio.ini        # ボード設定・依存ライブラリ管理
├── src/
│   ├── main.cpp          # 各タスクの起動・FreeRTOSスケジューラ制御
│   ├── display.cpp       # 幾何学描画ロジック・感情表現補間
│   └── network.cpp       # WiFi通信・天気API解析ロジック
└── include/              # 各種ヘッダー定義

```

## トラブルシューティング履歴

- **BKPTエラーの解決**:
  - **事象**: 初期設定の8KBではタスクのメモリ確保が追いつかず、システム保護機能（BKPT）によりCPUが強制停止。
  - **対策**: ヒープを11KB（0x2E00）へ拡張し、各タスクのスタックサイズを最適化。

- **安定動作の維持**:
  - 静的領域（`static`）の活用によるヒープ消費の抑制。
  - 動的なタスク生成・削除を避け、メモリ断片化（フラグメンテーション）を回避。

## 今後の拡張計画

- **人感検知**: 超音波センサ（HC-SR04）を追加し、人の接近をトリガーに画面をON/OFFする機能の実装。
- **表現力強化**: 虚無顔時の目玉ランダム移動アニメーションの追加。

## [完成イメージフォルダ](https://github.com/houya-rena/MyRobotPet/tree/main/%E5%AE%8C%E6%88%90%E3%82%A4%E3%83%A1%E3%83%BC%E3%82%B8)

- [表示されるテキスト](URL)

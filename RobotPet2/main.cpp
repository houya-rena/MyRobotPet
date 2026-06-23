/**
 * robot_pet.ino (シリアル入力・手動デバッグ版)
 * シリアルモニタから数字（0〜5）を入力して、感情と音を切り替えます。
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "eye_animation.h"      // 目のアニメーション制御用ヘッダー
#include "sound.h"              // 鳴き声・サウンド制御用ヘッダー

// ── OLEDディスプレイの宣言 ──────────────────────────────────────
// 画面の幅、高さ、通信方式（I2C）、リセットピン（-1はリセットピンなし）を指定して初期化
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── 状態管理用の変数 ────────────────────────────────────────────
// 現在のロボットの感情を保持する変数。最初は「EMOTION_NORMAL（通常）」からスタート
EmotionType currentEmotion = EMOTION_NORMAL;

// ランダム鳴き声用のタイマー変数
unsigned long lastCryTime = 0;           // 最後にランダムに鳴いた時間
unsigned long randomCryInterval = 10000; // 次に鳴くまでの最小間隔（10秒）

// ══════════════════════════════════════════════════
// セットアップ処理
// ══════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);

    // 起動直後、シリアルモニタが開くのを最大2秒間だけ待つ処理（これがないと最初の文字が消えることがある）
    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 2000)); 
    
    // シリアルモニタに使い方の案内を表示
    Serial.println("====== Robot Pet Debug Studio ======");
    Serial.println("[使い方] シリアルモニタに「0〜5」の数字を入力して送信してください。");
    Serial.println("  0: NORMAL   (通常 / ピピッ)");
    Serial.println("  1: HAPPY    (嬉しい / ピロリロリン♪)");
    Serial.println("  2: ANGRY    (怒り / プンプン！)");
    Serial.println("  3: SAD      (悲しい / ホーー…)");
    Serial.println("  4: CONFUSED (困惑 / ピョコピョコ)");
    Serial.println("  5: SLEEPY   (ウトウト / ふぁ〜あ)");
    Serial.println("=====================================");

    // サウンド機能の初期化(A0ピンを出力モードに変更し、ポップノイズを防止)
    soundInit();
    Serial.println("[Init] Sound module ready.");

    // OLEDディスプレイの初期化 (I2Cアドレス: 0x3C)
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("[ERROR] SSD1306 allocation failed"));
        for (;;);   
    }
    
    // 画面を一度真っ黒にクリアして反映
    display.clearDisplay();
    display.display();
    Serial.println("[Init] OLED Display ready.");

    // 起動時のチャイム音（ポチッ）
    playSwitchSound();
    Serial.println("====== Setup Completed! ======");
}

// ══════════════════════════════════════════════════
// ループ処理
// ══════════════════════════════════════════════════
void loop() {
    // ── シリアルモニタからの入力を監視する処理 ──
    if (Serial.available() > 0) {
        char inputChar = Serial.read();

        if (inputChar >= '0' && inputChar <= '5') {
            int emotionIndex = inputChar - '0';
            currentEmotion = (EmotionType)emotionIndex;

            Serial.print("\n[Command Received] Switch to Emotion ID: ");
            Serial.println(emotionIndex);
            
            // 感情が切り替わった瞬間に音を鳴らす
            playCry(currentEmotion);
            
            // 手動操作されたらタイマーをリセット
            lastCryTime = millis();
        }
    }

    // ── 暴走しないたまごっち風ランダム鳴き声処理  ──
    // 前回の鳴き声から、指定時間が経過した「その一瞬のみ」をチェック（スリープ時以外）
    if (currentEmotion != EMOTION_SLEEPY) {
        if (millis() - lastCryTime > randomCryInterval) {
        
            // 判定に飛び込んだら、鳴く・鳴かないに関わらず【即座に】タイマーをリセット！
            // これにより、loopが超高速回転しても連続で鳴り続ける（ノイズになる）のを100%防止
            lastCryTime = millis();
            randomCryInterval = random(10000, 25000); // 次のチェックまでの時間を10〜25秒後に設定
            
            // 4回に1回（25%）の確率で気まぐれに鳴く
            if (random(0, 4) == 0) { 
                Serial.println("[Random Cry] 気まぐれに鳴いたよ♪");
                playCry(currentEmotion); // 今の感情の声を鳴らす
            }
        }
    } else {
        // ロボットがスリープ状態の時は、ランダムタイマーの基準点を「常に現在時刻」に更新し続ける
        // これにより、眠っている間は絶対に勝手に鳴かなくなり、
        // さらに起きた（感情がNORMALなどに戻った）瞬間から、正確にタイマーが「0秒」からカウントダウンを始める

        lastCryTime = millis();
        }
    // ── 目のアニメーションと描画の毎フレーム更新 ──
    eyeUpdate(display, currentEmotion);

    // ループ全体の過剰な超高速回転を防ぐためのわずかなウェイト
    delay(16); 
}
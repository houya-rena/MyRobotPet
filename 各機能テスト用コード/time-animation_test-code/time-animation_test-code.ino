/**
 * robot_pet.ino
 * 描画テスト専用・時間ワープ機能付き
 *
 * 【テスト方法】
 * シリアルモニタ（115200bps）から以下を入力して送信してください。
 *
 * [時間操作]
 *   w : 時間を 1時間 進める
 *   m : 時間を 1分 進める
 *
 * [表情テスト（通常時間帯のみ有効）]
 *   0: NORMAL  / 1: HAPPY  / 2: ANGRY
 *   3: SAD     / 4: CONFUSED / 5: SLEEPY
 *
 * 【イベント時間帯】（eyeUpdateRTC と完全一致）
 *   07:00〜07:14  起床   HAPPY
 *   12:00〜12:29  昼食   EATING
 *   15:00〜15:14  おやつ SNACK
 *   18:00〜18:29  夕食   EATING
 *   20:00〜20:29  お風呂 BATH
 *   22:00〜06:59  就寝   SLEEPING
 *   上記以外      通常   s_baseEmotion（0〜5キーで切替）
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "eye_animation.h"
#include "sound.h"
#include "eye_draw.h"
#include "eye_state.h"
#include "eye_params.h"

// ── OLEDディスプレイ ─────────────────────────────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// 状態管理用の変数
EmotionType currentEmotion = EMOTION_NORMAL;

// ── テスト用の時刻変数（起動は 11:55 スタート） ─────────────────────────────────────────────
static uint8_t s_testHour   = 11;
static uint8_t s_testMinute = 55;
static EmotionType s_baseEmotion = EMOTION_NORMAL; // 通常時間帯のデフォルト表情

static bool isManualOverride = false; // loop関数の外に配置

// ─────────────────────────────────────────────────────
// 前方宣言
// ─────────────────────────────────────────────────────
void printCurrentTime();
void printHelp();

unsigned long s_emotionStartTime = 0;

// ───　初期化関数　──────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // シリアル接続待機時間（最大2秒）
    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 2000)); 
    
    printHelp();
    printCurrentTime();
    Serial.println("=======================================================");

    // サウンド初期化処理
    soundInit();

    // OLED初期化
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("[ERROR] SSD1306 allocation failed"));
        for (;;);   // 初期化失敗時は停止
    }
    
    display.clearDisplay();
    display.display();

    // アニメーション内部状態を初期化
    eyeStateInit();

    playSwitchSound();
}
// ─────────────────────────────────────────────────────
// メインループ（これ一つだけが存在するようにしてください）
// ─────────────────────────────────────────────────────
void loop() {
    bool timeChanged = false; // 時刻が変わったかどうかのフラグ

    // 1. 【シリアル入力処理】
    if (Serial.available() > 0) {
        char inputChar = Serial.read();
        
        switch (inputChar) {
            case 'w': case 'W':
                s_testHour = (s_testHour + 1) % 24;
                isManualOverride = false; 
                timeChanged = true; // 時刻が変わったことを記録
                printCurrentTime();
                break;
            case 'm': case 'M':
                s_testMinute = (s_testMinute + 1) % 60;
                if (s_testMinute == 0) s_testHour = (s_testHour + 1) % 24;
                isManualOverride = false;
                timeChanged = true; // 時刻が変わったことを記録
                printCurrentTime();
                break;
            case '0': case '1': case '2':
            case '3': case '4': case '5': 
            {
                int idx = inputChar - '0';
                currentEmotion = (EmotionType)idx;
                isManualOverride = true;
                playCry(currentEmotion); // 手動時は即座に鳴らす
                Serial.print("[手動モード] ");
                Serial.println(idx);
                break;
            }
        }
    }

    // 2. 状態管理（自動モード時）
    if (!isManualOverride) {
        EmotionType targetEmotion = getCurrentEventEmotion(s_testHour, s_testMinute);
        
        // 感情が変化した時（NORMALへの復帰も含む）
        if (targetEmotion != currentEmotion) {
            
            // 状態を更新
            currentEmotion = targetEmotion;

            // 変化した内容に応じて音を鳴らす
            // 全てのイベント切り替わりで鳴らしたい場合は条件を緩和します
            if (currentEmotion != EMOTION_NORMAL) {
                // 何らかのイベントに入った時
                playCry(currentEmotion);
            } else {
                // NORMALに戻った時（必要であればここでも音を鳴らす）
                playSwitchSound(); 
            }
        }
    }

    // 3. 【描画処理】
    display.clearDisplay();
    if (isManualOverride) {
        eyeUpdate(display, currentEmotion); 
    } else {
        eyeUpdateRTC(display, currentEmotion, s_testHour, s_testMinute);
    }
    display.display();
    delay(16);
}
// ─────────────────────────────────────────────────────
// 現在のテスト時刻とイベント状態をシリアル出力
// ─────────────────────────────────────────────────────
void printCurrentTime() {
    Serial.print("[現在のテスト時刻] ");
    if(s_testHour < 10) Serial.print("0");
    Serial.print(s_testHour);
    Serial.print(":");
    if(s_testMinute < 10) Serial.print("0");
    Serial.println(s_testMinute);
}
// ─────────────────────────────────────────────────────
// ヘルプメッセージ出力
// ─────────────────────────────────────────────────────
void printHelp() {
    Serial.println("====== [描画確認用] RTCイベント・テストスタジオ ======");
    Serial.println("[時間操作]");
    Serial.println("  w : 1時間進める  /  m : 1分進める");
    Serial.println("[表情テスト（通常時間帯のみ）]");
    Serial.println("  0:NORMAL  1:HAPPY  2:ANGRY  3:SAD  4:CONFUSED  5:SLEEPY");
    Serial.println("[その他]");
    Serial.println("  h または ? : このヘルプを再表示");
    Serial.println("-------------------------------------------------------");
}

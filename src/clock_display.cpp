#include "clock_display.h"
#include <Arduino.h>

/**
 * clock_display.cpp
 * ボタン押下時に表示する時刻画面
 *
 * 【レイアウト】
 *   ┌─────────────────┐
 *   │   HH : MM : SS  │  ← 大きめフォント（size=3）
 *   │   YYYY/MM/DD    │  ← 小さめフォント（size=1）
 *   │   [曜日]        │
 *   └─────────────────┘
 *
 * 【使用ライブラリ】
 *   RTC.h : UNO R4ボードパッケージ標準内蔵
 */

// static = 外部への遮断（=private化）
// このファイル以外からは "WEEKDAYS" という名前は勝手に使わせない！
// 曜日表示用の配列(0:Sun , 1:Mon ... 6:Sat に対応)
static const char* WEEKDAYS[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

// staticなのでこのファイル以外からは "printTwoDigit" という関数を勝手に呼ばせない！
// ゼロ埋め2桁表示ヘルパー
// 1桁の数字を01, 02のように2桁にするための関数
static void printTwoDigit(Adafruit_SSD1306 &dsp, int val) {
    if (val < 10) dsp.print('0'); // 10未満なら'0'を先頭につける
    dsp.print(val);               // Sirial.print()とほぼ同じ
                                  // 数値を表示する     
}

void clockDraw(Adafruit_SSD1306 &display) {
    RTCTime now;        // 現在時刻を保持するための変数構造体を作成
    RTC.getTime(now);   // UNO R4の内蔵RTCから現在時刻を取得してnowに格納

    display.clearDisplay();         // 画面を一度真っ黒に消去（描画の準備）
    display.setTextColor(WHITE);    // 文字色を白に設定

    // ── 時刻（HH:MM:SS）大きく中央に ──────────────
    display.setTextSize(3);     // フォントサイズを3に拡大
    // 文字幅: size3 = 1文字18px, コロン=18px
    // "HH:MM:SS" = 8文字 × 18px = 144px … 128pxに収まらないので
    // "HH:MM" のみ大きく、":SS" は小さくする
    int timeX = 4;                          // 横位置の開始座標
    int timeY = 8;                          // 縦位置の開始座標
    display.setCursor(timeX, timeY);        // 描画開始位置を指定
    printTwoDigit(display, now.getHour());  // 時を2桁で表示
    display.print(':');                     // コロンを表示
    printTwoDigit(display, now.getMinutes());// 分を2桁で表示

    // -------秒を小さく右寄せ----------------------
    display.setTextSize(2);     // 秒は少し小さめのサイズ2に設定
    display.setCursor(100, 16); // 右側の少し下がった位置に移動
    display.print(':');         // 区切りのコロンを表示
    printTwoDigit(display, now.getSeconds());// 秒を2桁で表示

    // ── 区切り線 ───────────────────────────────────
    // 画面いっぱい（128px）に横線を引き、上下を視覚的に分ける
    display.drawFastHLine(0, 40, 128, WHITE);

    // ── 日付・曜日（小さく下段）────────────────────
    display.setTextSize(1);        // 最も小さいフォントサイズ1
    display.setCursor(4, 46);      // 花壇の左側付近にカーソルを移動
    display.print(now.getYear());  // 年を表示
    display.print('/');            // スラッシュを表示
    printTwoDigit(display, Month2int(now.getMonth())); // 月を2桁表示
    display.print('/');            // スラッシュを表示
    printTwoDigit(display, now.getDayOfMonth());       // 日を2桁表示

    // -- 曜日を右寄せ -------------------------------
    display.setCursor(92, 46); // 下段の右側へ移動
    display.print('[');        // 飾りカッコを表示
    // 取得した曜日番号(0-6)をインデックスにして配列から英単語を取り出す
    display.print(WEEKDAYS[static_cast<int>(now.getDayOfWeek())]);
    display.print(']');        // 飾りカッコ（閉じる）を表示

    display.display();         // これまでに計算した描画内容を一気に画面へ反映！
}

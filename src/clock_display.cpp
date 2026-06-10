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
    RTC.getTime(now);

    display.clearDisplay();
    display.setTextColor(WHITE);

    // ── 時刻（HH:MM:SS）大きく中央に ──────────────
    display.setTextSize(3);
    // 文字幅: size3 = 1文字18px, コロン=18px
    // "HH:MM:SS" = 8文字 × 18px = 144px … 128pxに収まらないので
    // "HH:MM" のみ大きく、":SS" は小さくする
    int timeX = 4;
    int timeY = 8;
    display.setCursor(timeX, timeY);
    printTwoDigit(display, now.getHour());
    display.print(':');
    printTwoDigit(display, now.getMinutes());

    // 秒を小さく右寄せ
    display.setTextSize(2);
    display.setCursor(100, 16);
    display.print(':');
    printTwoDigit(display, now.getSeconds());

    // ── 区切り線 ───────────────────────────────────
    display.drawFastHLine(0, 40, 128, WHITE);

    // ── 日付・曜日（小さく下段）────────────────────
    display.setTextSize(1);
    display.setCursor(4, 46);
    display.print(now.getYear());
    display.print('/');
    printTwoDigit(display, Month2int(now.getMonth()));
    display.print('/');
    printTwoDigit(display, now.getDayOfMonth());

    // 曜日を右寄せ
    display.setCursor(92, 46);
    display.print('[');
    display.print(WEEKDAYS[static_cast<int>(now.getDayOfWeek())]);
    display.print(']');

    display.display();
}

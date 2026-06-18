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


// 文字列の配列
// 曜日リスト
static const char* WEEKDAYS[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

// ゼロ埋め2桁表示ヘルパー
// 1桁の数字」を「2桁の表示（05など）」に変換する整形ツール
static void printTwoDigit(Adafruit_SSD1306 &dsp, int val) {
    if (val < 10) dsp.print('0'); // valが10未満なら、先に'0'を表示
    dsp.print(val);               // すぐ後ろに数字（'val'）を置く
}

// ===========================================================
// clockDraw : 時刻と日付を画面に描画する関数
// display : OLEDディスプレイのインスタンスへの参照
// ===========================================================

void clockDraw(Adafruit_SSD1306 &display) {

    // 【時刻取得】
    // RTC（リアルタイムクロック）から現在の時刻をコピー
    RTCTime now;
    RTC.getTime(now);

    // 【描画準備】
    // 画面を一度クリアし、文字色を白に設定（OLEDなので基本は白）
    display.clearDisplay();
    display.setTextColor(WHITE);

    // ── 時刻（HH:MM:SS）大きく中央に ──────────────────────────
    display.setTextSize(3);
    // 文字幅: size3 = 1文字18px, コロン=18px
    // "HH:MM:SS" = 8文字 × 18px = 144px … 128pxに収まらないので
    // "HH:MM" のみ大きく、":SS" は小さくする
    int timeX = 4;
    int timeY = 8;
    display.setCursor(timeX, timeY);    // 画面表示の座標

    // 自作のヘルパーで「9時」を「09」のように0埋めして表示
    printTwoDigit(display, now.getHour());
    display.print(':');
    printTwoDigit(display, now.getMinutes());

    // ── 秒（SS）を小さく表示 ─────────────────────────────────
    // setTextSize(2)で少し小さくし、右端(100px目)に配置
    display.setTextSize(2);
    display.setCursor(100, 16);
    display.print(':');
    printTwoDigit(display, now.getSeconds());

    // ── 区切り線 ───────────────────────────────────────────
    // Y=40の位置に横線を引き、上部（時刻）と下部（日付）の視覚的な境界を作成
    display.drawFastHLine(0, 40, 128, WHITE);

    // ── 日付・曜日(YYYY/MM/DD)表示（小さく下段）────────────────
    display.setTextSize(1);     // 一番小さいサイズ（約6x8px）
    display.setCursor(4, 46);
    display.print(now.getYear());
    display.print('/');

    // Month2int: RTCの月定数を整数に変更するヘルパー
    printTwoDigit(display, Month2int(now.getMonth()));
    display.print('/');
    printTwoDigit(display, now.getDayOfMonth());

    // ── 曜日表示 ────────────────────────────────────────────
    // 配列WEEEKDAYから現在時刻の曜日に対する文字列を取り出す
    display.setCursor(92, 46);
    display.print('[');
    display.print(WEEKDAYS[static_cast<int>(now.getDayOfWeek())]);
    display.print(']');

    // 【画面反映】
    // ここまでメモリ上で組み立てた情報を、実際の物理ディスプレイに転送
    display.display();
}

#include <Arduino.h>
#include <RTC.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── OLEDディスプレイの設定 ───────────────────────────────────────────
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── 曜日表示用の配列（コード内で使用されているため定義） ──────────────────────
const char* const WEEKDAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// ── 2桁のゼロ埋め表示ヘルパー関数（displayオブジェクトを渡す形式） ───────────────
void printTwoDigit(Adafruit_SSD1306 &d, int number) {
    if (number < 10) {
        d.print('0');
    }
    d.print(number);
}

// ── OLEDに指定の可愛いレイアウトで時間を描画する関数 ───────────────────────
void drawClock() {
    
    // 【時刻取得】
    // RTC（リアルタイムクロック）から現在の時刻をコピー
    RTCTime now;
    RTC.getTime(now);

    // 【描画準備】
    // 画面を一度クリアし、文字色を白に設定（OLEDなので基本は白）
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE); // ※環境に合わせてWHITEからSSD1306_WHITEに安全対策

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
    display.setTextSize(1);
    display.setCursor(100, 27);
    printTwoDigit(display, now.getSeconds());

    // ── 区切り線 ───────────────────────────────────────────
    // Y=40の位置に横線を引き、上部（時刻）と下部（日付）の視覚的な境界を作成
    display.drawFastHLine(0, 40, 128, SSD1306_WHITE);

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

// ── 初期化 ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    RTC.begin(); 

    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("OLED allocation failed"));
        for(;;);
    }
    
    display.clearDisplay();
    display.display();

    // ⏱ 初期時刻セット（ダミー：2026/06/24 11:35:00 WED）
    // ライブラリ固有のエラーを回避するため、完全に安全なUnixタイム秒数で初期化
    RTCTime testTime(1782272100UL);
    RTC.setTime(testTime);
}

// ── メインループ ─────────────────────────────────────────────────────
void loop() {
    drawClock();   
    delay(1000);   // 1秒ごとに更新して時計を進める
}

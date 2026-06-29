#include "ntp_sync.h"
#include <Arduino.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <RTC.h>
#include <Arduino_FreeRTOS.h>

/**
 * ntp_sync.cpp
 * NTPサーバーからUNO R4内蔵RTCへの時刻同期
 *
 * 【タイムゾーン】
 *   JST (UTC+9) を適用。NTPはUTC取得のため +9時間オフセット。
 *
 * 【使用ライブラリ】
 *   WiFiS3.h / WiFiUdp.h : UNO R4コア標準内蔵（Wi-Fi通信用）
 *   RTC.h                : UNO R4コア標準内蔵（リアルタイムクロック制御用）
 *
 * 【NTPパケット仕様】
 *   RFC 4330 SNTPv4 に準拠。
 *   UDPポート123へ48バイトのリクエストを送信し、
 *   レスポンスの40〜43バイト目（Transmit Timestamp 秒部）を取得。
 */


// ── 設定項目 ─────────────────────────────────────────────────────────
static const char NTP_SERVER[]  = "ntp.nict.jp";  // 日本標準時(NICT：情報通信研究機構)
static const int  NTP_PORT      = 123;            // NTP通信が使用する世界共通のポート番号
static const int  TZ_OFFSET_SEC = 9 * 3600;       // JST: UTC+9（9時間分を秒に変換）
static const int  NTP_TIMEOUT   = 3000;           // サーバからの返事を待つ上限時間（3秒）

// ── NTPパケット（通信データ）の準備 ────────────────────────────────────
static const int  NTP_PACKET_SIZE = 48;           // NTP通信に必要なデータの塊のサイズ（48バイト固定）
static byte       s_packetBuf[NTP_PACKET_SIZE];   // データを一時的に格納するバッファ（メモリの引き出し）

// ═══════════════════════════════════════════════════════════════════
// buildNtpRequest: NTPサーバーに送る「リクエストデータ」を組み立てる
// ═══════════════════════════════════════════════════════════════════
static void buildNtpRequest() {

    // まず48バイトの引き出しの中身を全て0でクリアする
    memset(s_packetBuf, 0, NTP_PACKET_SIZE);

    // 先頭の1バイト目に「クライアントとして最新バージョン（v4）でデータを要求します」という設定を書き込む
    // 2進数の「11（うるう秒警告なし） 100（バージョン4） 011（クライアントモード）」を合算した値
    s_packetBuf[0] = 0b11100011;  // LI=3, VN=4, Mode=3(client)
    s_packetBuf[1] = 0;           // 自分の階層（Stratum: 未定義の時は0）
    s_packetBuf[2] = 6;           // 問い合わせの間隔（Polling interval）
    s_packetBuf[3] = 0xEC;        // 時計の精度（Clock precision）
    // その他フィールド（44バイト）は0のままサーバー側が受け取ってくれるのでOK（SNTP簡易実装）
}

// ═══════════════════════════════════════════════════════════════════
// applyTimestamp: サーバから届いた「1900年からの秒数」を日本時間に直して時計にセット
// Unix時間(1900年基準 → 1970年基準変換) をRTCTimeにセット
// ═══════════════════════════════════════════════════════════════════
static void applyTimestamp(unsigned long secsSince1900) {

    // NTPは「1900年1月1日」が基準。Unix時間（Unix Epoch）は「1970年1月1日」が基準
    // この間にある「70年分の秒数」が 2,208,988,800秒
    const unsigned long SEVENTY_YEARS = 2208988800UL;

    // 【時間の変換計算】
    // 1900年基準の秒数から70年分を引いて「1970年基準（Unix時間）」に変換
    // そこに「日本時間の9時間」を足す
    unsigned long epoch = secsSince1900 - SEVENTY_YEARS + TZ_OFFSET_SEC;

    // epoch → 年月日時分秒 に分解
    // Arduino RTCTime は setUnixTime() で一発設定可能
    RTCTime t;
    // 計算した秒数をそのままセット（ライブラリが内部で自動的に年・月・日・時・分・秒に分解）
    t.setUnixTime((time_t)epoch);
    // 実際のハードウェア時計（RTC）の針を動かして現在時刻を確定させる
    RTC.setTime(t);
}

// ═══════════════════════════════════════════════════════════════════
// ntpSync: NTP同期処理のメイン関数
// 成功したらtrue、失敗したらfalseを返す
// ═══════════════════════════════════════════════════════════════════
bool ntpSync() {
    WiFiUDP udp;
    // UDP通信の受付口をポート123番で開く
    udp.begin(NTP_PORT);

    // 送信するパケット（48バイト）を作成
    buildNtpRequest();

    // NTPサーバーへUDP送信
    udp.beginPacket(NTP_SERVER, NTP_PORT);
    udp.write(s_packetBuf, NTP_PACKET_SIZE);  // 48バイトのデータを書き込む
    udp.endPacket();                          // 送信完了！

    // ── レスポンス（返事）の待機 ────────────────────────────────────────
    unsigned long start = millis();           // 待機を始めた現在のシステム時刻を記録
    while (udp.parsePacket() == 0) {          // サーバーがらデータパケットが届くまでループ

        // もし、設定したタイムアウト時間（3秒）を過ぎても返事が来なければタイムアウト
        if (millis() - start > NTP_TIMEOUT) {
            Serial.println(F("[NTP] Timeout"));
            udp.stop();    // 通信を終了して片付ける
            return false;
        }
        delay(10);  // マイコンが全力で回り続けないように10m秒の待機時間
    }

    // ── パケット読み取り ───────────────────────────────────────────────
    // 届いたデータをバッファ（s_packetBuf）に丸ごと読み込む
    udp.read(s_packetBuf, NTP_PACKET_SIZE);
    udp.stop();  // 通信口を閉じる

    // 【重要】サーバが「データを送信した時刻（Transmit Timestamp）」は40~43バイト目に隠れている
    // Transmit Timestamp: バイト40〜43（ビッグエンディアン）
    // ネットワークデータは「ビックエディアン（上の桁から順に届く）」ので、シフト演算子（`<<`）を使用し
    // 4バイト（32ビット）の一つの大きな数字`secsSince1900`に復元する
    unsigned long hi = ((unsigned long)s_packetBuf[40] << 8) | s_packetBuf[41]; // 上位16ビット分
    unsigned long lo = ((unsigned long)s_packetBuf[42] << 8) | s_packetBuf[43]; // 下位16ビット分
    unsigned long secsSince1900 = (hi << 16) | lo;    // 合体させて「1900年からの秒数」が完成

    // もしデータが0（サーバーの不具合などで空っぽ）だったらエラーとする
    if (secsSince1900 == 0) {
        Serial.println(F("[NTP] Invalid timestamp"));
        return false;
    }

    // 正しく取得出来た秒数を内蔵時計に反映させる
    applyTimestamp(secsSince1900);

    // ── ログ出力 ──────────────────────────────────────────────────
    // 時計がきちんと設定されたかを、RTCがら「今設定された時間」をもう一度読み出し
    // シリアルモニターに表示
    RTCTime now;
    RTC.getTime(now);
    Serial.print(F("[NTP] Synced: "));
    Serial.print(now.getYear()); Serial.print('/');
    Serial.print(Month2int(now.getMonth())); Serial.print('/');  // Month型を数値（int）に変換して表示
    Serial.print(now.getDayOfMonth()); Serial.print(' ');
    Serial.print(now.getHour()); Serial.print(':');
    Serial.print(now.getMinutes()); Serial.print(':');
    Serial.println(now.getSeconds());

    return true;  // 同期成功！
}
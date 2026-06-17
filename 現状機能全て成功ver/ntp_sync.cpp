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
 *   WiFiS3.h / WiFiUdp.h : UNO R4コア標準内蔵
 *   RTC.h                : UNO R4コア標準内蔵
 *
 * 【NTPパケット仕様】
 *   RFC 4330 SNTPv4 に準拠。
 *   UDPポート123へ48バイトのリクエストを送信し、
 *   レスポンスの40〜43バイト目（Transmit Timestamp 秒部）を取得。
 */


// ── 設定 ──────────────────────────────────────────
static const char NTP_SERVER[]  = "ntp.nict.jp";  // 日本標準時(NICT)
static const int  NTP_PORT      = 123;
static const int  TZ_OFFSET_SEC = 9 * 3600;       // JST: UTC+9
static const int  NTP_TIMEOUT   = 3000;            // ms

// ── NTPパケット ────────────────────────────────────
static const int  NTP_PACKET_SIZE = 48;
static byte       s_packetBuf[NTP_PACKET_SIZE];

static void buildNtpRequest() {
    memset(s_packetBuf, 0, NTP_PACKET_SIZE);
    s_packetBuf[0] = 0b11100011;  // LI=3, VN=4, Mode=3(client)
    s_packetBuf[1] = 0;           // Stratum
    s_packetBuf[2] = 6;           // Polling interval
    s_packetBuf[3] = 0xEC;        // Clock precision
    // その他フィールドは0でOK（SNTP簡易実装）
}

// Unix時間(1900年基準 → 1970年基準変換) をRTCTimeにセット
static void applyTimestamp(unsigned long secsSince1900) {
    const unsigned long SEVENTY_YEARS = 2208988800UL;
    unsigned long epoch = secsSince1900 - SEVENTY_YEARS + TZ_OFFSET_SEC;

    // epoch → 年月日時分秒 に分解
    // Arduino RTCTime は setUnixTime() で一発設定可能
    RTCTime t;
    t.setUnixTime((time_t)epoch);
    RTC.setTime(t);
}
bool ntpSync() {
    WiFiUDP udp;
    udp.begin(NTP_PORT);

    buildNtpRequest();

    // NTPサーバーへUDP送信
    udp.beginPacket(NTP_SERVER, NTP_PORT);
    udp.write(s_packetBuf, NTP_PACKET_SIZE);
    udp.endPacket();

    // レスポンス待機
    unsigned long start = millis();
    while (udp.parsePacket() == 0) {
        if (millis() - start > NTP_TIMEOUT) {
            Serial.println(F("[NTP] Timeout"));
            udp.stop();
            return false;
        }
        delay(10);
    }

    // パケット読み取り
    udp.read(s_packetBuf, NTP_PACKET_SIZE);
    udp.stop();

    // Transmit Timestamp: バイト40〜43（ビッグエンディアン）
    unsigned long hi = ((unsigned long)s_packetBuf[40] << 8) | s_packetBuf[41];
    unsigned long lo = ((unsigned long)s_packetBuf[42] << 8) | s_packetBuf[43];
    unsigned long secsSince1900 = (hi << 16) | lo;

    if (secsSince1900 == 0) {
        Serial.println(F("[NTP] Invalid timestamp"));
        return false;
    }

    applyTimestamp(secsSince1900);

    RTCTime now;
    RTC.getTime(now);
    Serial.print(F("[NTP] Synced: "));
    Serial.print(now.getYear()); Serial.print('/');
    Serial.print(Month2int(now.getMonth())); Serial.print('/');
    Serial.print(now.getDayOfMonth()); Serial.print(' ');
    Serial.print(now.getHour()); Serial.print(':');
    Serial.print(now.getMinutes()); Serial.print(':');
    Serial.println(now.getSeconds());

    return true;
}
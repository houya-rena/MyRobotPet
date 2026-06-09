/**
 * weather_task.cpp
 * OpenWeatherMap Current Weather API v2.5 取得・解析
 *
 * 【エンドポイント】
 *   GET https://api.openweathermap.org/data/2.5/weather
 *       ?q={city},{country}&units=metric&appid={apikey}
 *
 * 【依存ライブラリ】
 *   - WiFiS3        (UNO R4コア標準内蔵)
 *   - ArduinoJson   (Arduino IDEライブラリマネージャから導入)
 *
 * 【FreeRTOS上の注意】
 *   - WiFiSSLClient の接続・受信はブロッキング処理。
 *     TaskWeather は専用タスクで動かし、他タスクへの影響を防ぐ。
 *   - JSON解析用バッファ(StaticJsonDocument)はスタック上に確保。
 *     タスクスタックサイズを768byte以上に設定すること。
 *   - WiFi.begin() はsetup()で完了済みであること。
 */

#include "weather_task.h"
#include "arduino_secrets.h"
#include <Arduino.h>
#include <WiFiS3.h>
#include <ArduinoJson.h>

// ── OWM API設定 ────────────────────────────────────
static const char OWM_HOST[] = "api.openweathermap.org";
static const int  OWM_PORT   = 443;  // HTTPS

// クエリURL（arduino_secrets.hの値で構築）
// 例: /data/2.5/weather?q=Tokyo,JP&units=metric&appid=xxxxx
static char s_url[128];

// ── 最後の取得結果 ─────────────────────────────────
static int   s_weatherId   = 800;          // デフォルト: 快晴
static float s_temp        = 25.0f;
static char  s_desc[32]    = "clear sky";

// ── 内部関数：天気IDと気温から感情を決定 ──────────
static EmotionType classifyWeather(int id, float temp) {
    // 気温による上書き（最優先）
    if (temp >= 33.0f) return EMOTION_HOT;
    if (temp <=  5.0f) return EMOTION_SAD;

    // 天気コードによる分類
    if (id >= 200 && id < 300) return EMOTION_ANGRY;  // 雷雨
    if (id >= 300 && id < 400) return EMOTION_SAD;    // 霧雨
    if (id >= 500 && id < 600) return EMOTION_SAD;    // 雨
    if (id >= 600 && id < 700) return EMOTION_SAD;    // 雪
    if (id == 800)              return EMOTION_HAPPY;  // 快晴
    // 801〜804: 曇り / 700〜799: 霧など → NORMAL
    return EMOTION_NORMAL;
}

// ── 内部関数：HTTPSリクエストを送信してレスポンスBody取得 ──
// 成功時 true、失敗時 false
static bool fetchWeatherJson(char *outBuf, size_t bufSize) {
    WiFiSSLClient client;

    Serial.print(F("[WEATHER] Connecting to "));
    Serial.println(OWM_HOST);

    if (!client.connect(OWM_HOST, OWM_PORT)) {
        Serial.println(F("[WEATHER] Connection failed"));
        return false;
    }

    // GETリクエスト送信
    client.print(F("GET "));
    client.print(s_url);
    client.println(F(" HTTP/1.1"));
    client.print(F("Host: "));
    client.println(OWM_HOST);
    client.println(F("Connection: close"));
    client.println();

    // レスポンスのヘッダをスキップしてBodyだけ取得
    bool headerDone = false;
    size_t pos = 0;
    unsigned long timeout = millis() + 8000UL;  // 8秒タイムアウト

    while (client.connected() || client.available()) {
        if (millis() > timeout) {
            Serial.println(F("[WEATHER] Timeout"));
            client.stop();
            return false;
        }
        if (!client.available()) {
            delay(10);
            continue;
        }
        if (!headerDone) {
            // ヘッダ終端（空行）を検出
            String line = client.readStringUntil('\n');
            if (line == "\r") headerDone = true;
        } else {
            // Body読み取り
            while (client.available() && pos < bufSize - 1) {
                outBuf[pos++] = (char)client.read();
            }
        }
    }
    outBuf[pos] = '\0';
    client.stop();

    if (pos == 0) {
        Serial.println(F("[WEATHER] Empty response"));
        return false;
    }
    return true;
}

// ── 公開関数 ──────────────────────────────────────

void weatherTaskInit() {
    // URLを一度だけ構築
    snprintf(s_url, sizeof(s_url),
             "/data/2.5/weather?q=%s,%s&units=metric&appid=%s",
             OWM_CITY, OWM_COUNTRY, OWM_API_KEY);
    Serial.print(F("[WEATHER] URL: "));
    Serial.println(s_url);
}

EmotionType weatherTaskUpdate() {
    // JSON受信バッファ（512Bはスタック上。TaskWeatherのスタックサイズ>=768B必要）
    char jsonBuf[512];

    if (!fetchWeatherJson(jsonBuf, sizeof(jsonBuf))) {
        return EMOTION_COUNT;  // 取得失敗 → 変化なし
    }

    Serial.print(F("[WEATHER] JSON: "));
    Serial.println(jsonBuf);

    // JSON解析
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, jsonBuf);
    if (err) {
        Serial.print(F("[WEATHER] JSON parse error: "));
        Serial.println(err.c_str());
        return EMOTION_COUNT;
    }

    // 天気ID・気温・説明文を取得
    // OWM レスポンス構造:
    //   { "weather": [{ "id": 800, "description": "clear sky" }],
    //     "main": { "temp": 28.3 }, ... }
    int   id   = doc["weather"][0]["id"]          | 800;
    float temp = doc["main"]["temp"]              | 25.0f;
    const char *desc = doc["weather"][0]["description"] | "unknown";

    s_weatherId = id;
    s_temp      = temp;
    strncpy(s_desc, desc, sizeof(s_desc) - 1);
    s_desc[sizeof(s_desc) - 1] = '\0';

    Serial.print(F("[WEATHER] id="));
    Serial.print(id);
    Serial.print(F("  temp="));
    Serial.print(temp, 1);
    Serial.print(F("C  desc="));
    Serial.println(s_desc);

    return classifyWeather(id, temp);
}

int         weatherGetId()   { return s_weatherId; }
float       weatherGetTemp() { return s_temp; }
const char* weatherGetDesc() { return s_desc; }

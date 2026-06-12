/**
 * src/weather_task.cpp
 * OpenWeatherMap Current Weather API v2.5 取得・解析
 *
 * 【PlatformIO対応の変更点】
 *   - #include <Arduino.h> を追加
 *   - ArduinoJson は platformio.ini の lib_deps から自動解決
 *   - ヘッダーパスを include/ から自動解決
 */
 
#include <WiFiS3.h>
#include <ArduinoJson.h>
#include "weather_task.h"
#include "arduino_secrets.h"
 
static const char OWM_HOST[] = "api.openweathermap.org";
static const int  OWM_PORT   = 443;
 
static char  s_url[128];
static int   s_weatherId = 800;
static float s_temp      = 25.0f;
static char  s_desc[32]  = "clear sky";
 
static EmotionType classifyWeather(int id, float temp) {
    if (temp >= 33.0f) return EMOTION_HOT;
    if (temp <=  5.0f) return EMOTION_SAD;
    if (id >= 200 && id < 300) return EMOTION_ANGRY;
    if (id >= 300 && id < 400) return EMOTION_SAD;
    if (id >= 500 && id < 600) return EMOTION_SAD;
    if (id >= 600 && id < 700) return EMOTION_SAD;
    if (id == 800)              return EMOTION_HAPPY;
    return EMOTION_NORMAL;
}
 
static bool fetchWeatherJson(char *outBuf, size_t bufSize) {
    WiFiSSLClient client;
 
    Serial.print(F("[WEATHER] Connecting..."));
    if (!client.connect(OWM_HOST, OWM_PORT)) {
        Serial.println(F(" failed"));
        return false;
    }
    Serial.println(F(" ok"));
 
    client.print(F("GET ")); client.print(s_url);
    client.println(F(" HTTP/1.1"));
    client.print(F("Host: ")); client.println(OWM_HOST);
    client.println(F("Connection: close"));
    client.println();
 
    bool headerDone = false;
    size_t pos = 0;
    unsigned long timeout = millis() + 8000UL;
 
    while (client.connected() || client.available()) {
        if (millis() > timeout) {
            Serial.println(F("[WEATHER] Timeout"));
            client.stop();
            return false;
        }
        if (!client.available()) { delay(10); continue; }
        if (!headerDone) {
            String line = client.readStringUntil('\n');
            if (line == "\r") headerDone = true;
        } else {
            while (client.available() && pos < bufSize - 1)
                outBuf[pos++] = (char)client.read();
        }
    }
    outBuf[pos] = '\0';
    client.stop();
    return pos > 0;
}
 
void weatherTaskInit() {
    snprintf(s_url, sizeof(s_url),
             "/data/2.5/weather?q=%s,%s&units=metric&appid=%s",
             OWM_CITY, OWM_COUNTRY, OWM_API_KEY);
}
 
EmotionType weatherTaskUpdate() {
    char jsonBuf[512];
    if (!fetchWeatherJson(jsonBuf, sizeof(jsonBuf))) return EMOTION_COUNT;
 
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonBuf);
    if (err) {
        Serial.print(F("[WEATHER] JSON error: "));
        Serial.println(err.c_str());
        return EMOTION_COUNT;
    }
 
    int   id   = doc["weather"][0]["id"]              | 800;
    float temp = doc["main"]["temp"]                  | 25.0f;
    const char *desc = doc["weather"][0]["description"] | "unknown";
 
    s_weatherId = id;
    s_temp = temp;
    strncpy(s_desc, desc, sizeof(s_desc) - 1);
    s_desc[sizeof(s_desc) - 1] = '\0';
 
    Serial.print(F("[WEATHER] id=")); Serial.print(id);
    Serial.print(F(" temp=")); Serial.print(temp, 1);
    Serial.print(F("C  ")); Serial.println(s_desc);
 
    return classifyWeather(id, temp);
}
 
int         weatherGetId()   { return s_weatherId; }
float       weatherGetTemp() { return s_temp; }
const char* weatherGetDesc() { return s_desc; }
 
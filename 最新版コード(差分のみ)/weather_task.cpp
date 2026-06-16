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

static StaticJsonDocument<256> doc;
 
static char  s_url[128];
static int   s_weatherId = 800;
static float s_temp      = 25.0f;
// 1. グローバル領域（変更なし）
static const char* s_desc = "clear sky"; // 文字列の参照を保持

// weather_task.cpp の上部に以下を追加
extern volatile int g_weatherId;
extern volatile float g_weatherTemp;

static EmotionType classifyWeather(int id, float temp) {
    if (temp >= 33.0f) return EMOTION_HOT;
    if (temp <=  5.0f) return EMOTION_NORMAL;
    if (id >= 200 && id < 300) return EMOTION_ANGRY;
    if (id >= 300 && id < 400) return EMOTION_NORMAL;
    if (id >= 500 && id < 600) return EMOTION_NORMAL;
    if (id >= 600 && id < 700) return EMOTION_NORMAL;
    if (id == 800)              return EMOTION_HAPPY;
    return EMOTION_NORMAL;
}
 
// 修正案：通信バッファを先に閉じてからパースする（メモリ断片化対策）
static bool fetchWeatherJson(JsonDocument &doc) {
    WiFiClient client; // WiFiClient (HTTP) に変更
    if (!client.connect(OWM_HOST, 80)) return false;

    // ...GETリクエスト送信...
    // GETリクエスト（メモリを節約するため短く）
    client.print(F("GET ")); client.print(s_url);
    client.println(F(" HTTP/1.0")); // 1.1より1.0の方がヘッダー管理が楽
    client.print(F("Host: ")); client.println(OWM_HOST);
    client.println(F("Connection: close"));
    client.println();
    // ★フィルタを定義：必要なフィールドだけを指定する
    StaticJsonDocument<128> filter;
    filter["weather"][0]["id"] = true;
    filter["weather"][0]["description"] = true;
    filter["main"]["temp"] = true;

    // 読み飛ばし
    if (!client.find("\r\n\r\n")) { client.stop(); return false; }

    // ★重要：一度全部メモリに入れてしまう（または制限する）
    // ArduinoJson はストリームパースより、静的メモリパースの方が
    // ストレスが少ない場合があります。
    // まずは doc サイズを 256 程度まで落として試してください
    DeserializationError err = deserializeJson(doc, client);
    client.stop(); // パース前に通信を切る
    
    return (err == DeserializationError::Ok);
}

void weatherTaskInit() {
    // 必要であればここでシリアル初期化や内部変数の初期設定を行う
    Serial.println(F("[WEATHER] Initialized"));
}
// weatherTaskUpdate 内
EmotionType weatherTaskUpdate() {
    // 毎回新しい URL を生成してキャッシュを無効化
    snprintf(s_url, sizeof(s_url),
             "/data/2.5/weather?q=%s,%s&units=metric&appid=%s&t=%lu",
             OWM_CITY, OWM_COUNTRY, OWM_API_KEY, millis());

    static JsonDocument doc;
    doc.clear();
    
    if (!fetchWeatherJson(doc)) return EMOTION_COUNT;

    int   id   = doc["weather"][0]["id"]              | 800;
    float temp = doc["main"]["temp"]                  | 25.0f;

    // 文字列は .as<const char*>() で取得し、nullptr なら "unknown" とする
    const char* desc = doc["weather"][0]["description"].as<const char*>();
    s_desc = (desc != nullptr) ? desc : "unknown";

    s_weatherId = id;
    s_temp = temp;
    // ★ここに追加
    g_weatherId = id;
    g_weatherTemp = temp;

 
    Serial.print(F("[WEATHER] id=")); Serial.print(id);
    Serial.print(F(" temp=")); Serial.print(temp, 1);
    Serial.print(F("C  ")); Serial.println(s_desc);
 
    return classifyWeather(id, temp);
}
// weather_task.cpp に追加して確認
void debugWeather() {
    Serial.print("Current global weatherId: ");
    Serial.println(g_weatherId); // robot_pet.ino の g_weatherId を参照
}
 
int         weatherGetId()   { return s_weatherId; }
float       weatherGetTemp() { return s_temp; }
const char* weatherGetDesc() { return s_desc; }


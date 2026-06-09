#ifndef NTP_SYNC_H
#define NTP_SYNC_H

// NTPサーバーからUNO R4内蔵RTCに時刻をセットする
// 成功: true / 失敗: false
// WiFi接続済みの状態で呼ぶこと
bool ntpSync();

#endif // NTP_SYNC_H

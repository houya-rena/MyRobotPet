#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include <Arduino.h>


// HTML全体をひとまとめにして定義（R"rawliteral(...)" で改行を含めた記述が可能）
const char html_template[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <link href='https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap' rel='stylesheet'>
    <style>
        body { margin:0; height:100vh; display:flex; flex-direction:column; justify-content:center; align-items:center;
               background: linear-gradient(135deg, #74ebd5 0%, #9face6 100%); font-family: 'Press Start 2P', cursive; }
        .glass-btn { background: rgba(255, 255, 255, 0.2); backdrop-filter: blur(10px); -webkit-backdrop-filter: blur(10px);
                     border: 2px solid rgba(255, 255, 255, 0.5); border-radius: 15px; padding: 20px 30px; color: white;
                     text-decoration: none; font-size: 12px; text-shadow: 2px 2px #333; transition: 0.2s; }
        .glass-btn:active { background: rgba(255, 255, 255, 0.4); transform: scale(0.95); }
        .msg { color: white; margin-top: 30px; font-size: 10px; text-shadow: 1px 1px #333; }
    </style>
</head>
<body>
    <h1>PET FEEDER</h1>
    <a href='/feed' class='glass-btn' id='feed-btn'>FEED ME!</a>
    <p class='msg' id='status-msg'>TOUCH TO FEED</p>
    <script>
        // ボタンが押されたらメッセージを変える処理
        document.getElementById('feed-btn').addEventListener('click', function() {
            document.getElementById('status-msg').innerText = 'SENT YUMMY FEED!';
        });
    </script>
</body>
</html>
)rawliteral";

#endif
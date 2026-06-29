#include "sound.h"
#include "pitches.h"
#include "eye_animation.h" // 既存の定義コンパイルを通すため残しています
#include <Arduino.h>

#define DAC_PIN A0  // 音を出力するアナログピン（A0）

// =================================================================================
// [1] 調整用パラメータ
// =================================================================================
// 【音色の丸み・音量調整】
// 0(無音) 〜 255(最大電圧) の間で設定可能
// 数値を「8」や「12」などの低い値に抑えることで、スピーカーの音割れを防ぎ、
// たまごっち特有のポコポコとした角の丸い、耳に優しいピコピコ音を作成
#define DAC_VOLUME_LEVEL  12

// 【音と音の間のすき間（デフォルト値）】
// 1つの音が鳴り終わってから、次の音が鳴るまでの「無音時間（ミリ秒）」
// この数値を大きくすると「ピ・ピ・ピ」とハッキリ切れ味が出て、反対に小さくすると音が滑らかに繋がる
#define DEFAULT_GAP_MS    25

// =================================================================================
// [2] 電子音生成関数：音を実際に作る仕組み（FreeRTOS非依存・テスト用軽量版）
// =================================================================================

// 引数の見方：r4_dac_tone( 鳴らす周波数, 鳴らす長さms, 次の音までの無音ms );
// 3番目の「無音ms」は記載しなくても、自動的に上記の「DEFARUT_GAP_MS(25ms)」が使用される
static void r4_dac_tone(uint32_t freq, uint32_t durationMs, uint32_t gapMs = DEFAULT_GAP_MS) {

    // ── 休符（無音）の処理 ──
    if (freq == 0) {
        // 休符処理
        analogWrite(DAC_PIN, 0); // 電圧を完全に0（ゼロ）にする
        delay(durationMs);       // 指定された時間だけ待つ（お休み）
    } 
    
    // ── 音を鳴らす処理 ──
    else {
        // 周波数（Hz）から、波が1往復する時間（周期：マイクロ秒）を計算
        uint32_t periodUs = 1000000UL / freq;
        uint32_t halfPeriodUs = periodUs / 2; // 波の「山」と「谷」の半分の時間
        uint32_t startMs = millis();          // 鳴らし始めた時刻を記録  

        // 指定された「durationMs（ミリ秒）」が経過するまで、超高速で「山と谷」をループする（矩形波の合成）
        while ((millis() - startMs) < durationMs) {
            analogWrite(DAC_PIN, DAC_VOLUME_LEVEL); // 波の「山」：設定した音量分の電圧をかける
            delayMicroseconds(halfPeriodUs);
            analogWrite(DAC_PIN, 0);                // 波の「谷」：電圧を完全にゼロ(0V)にする
            delayMicroseconds(halfPeriodUs);
        }
    }

    // ── 【超重要】ノイズ完全抹殺処理 ──
    // 音が鳴り終わった「一瞬の無音時」にアンプがノイズを拾わないための処理
    analogWrite(DAC_PIN, 0);   // 電圧を完全に0にする
    pinMode(DAC_PIN, INPUT);   // ピンを「INPUT」にして電気の通り道を物理的に遮断

    // 次の音へ移る前の無音時間
    if (gapMs > 0) {
        delay(gapMs); 
    }
}

// =================================================================================
// [3] サウンド制御関数（7つの感情と可愛い音階アレンジ）
// =================================================================================
void soundInit() {
    analogWriteResolution(8); // Arduino UNO R4のDACを8ビット(0~255段階)に設定
    pinMode(DAC_PIN, OUTPUT);
    analogWrite(DAC_PIN, 0);  // 起動時のポップノイズ防止
}

void playCry(EmotionType emotion) {
    // 演奏開始
    pinMode(DAC_PIN, OUTPUT);
    // 各感情ごとのメロディデータ（この部分を変更し、音を作成）
    switch (emotion) {

        case EMOTION_NORMAL: // ノーマル
            // 音階（NOTE_XX）を細かく階段状に並べることで、生き物らしいしゃべり方の抑揚を再現
            // 音の長さ（25〜80）を全体的に短くすると「早口」に、長くすると「のんびり」喋る
            // 【ピカチュウ風】「ピカァ〜チュゥ♪」
            r4_dac_tone(NOTE_C6, 40);  // ピ
            r4_dac_tone(NOTE_E6, 40);  // カ
            r4_dac_tone(NOTE_G6, 40);  // ァ
            delay(10);                 // （一瞬のタメ）
            r4_dac_tone(NOTE_A6, 50);  // チュ
            r4_dac_tone(NOTE_F6, 70);  // ゥ
            r4_dac_tone(NOTE_D6, 100);  // ぅ…
            break;

        case EMOTION_HAPPY:
        case EMOTION_BATH:
            // 【たまごっち風ご機嫌音】「ピロリロリン♪」
            // カスタマイズのコツ：
            // ド・ミ・ソ・ド・ミ・ソ と綺麗な和音を超スピード（30ms）で駆け上がらせるのがおもちゃ感の秘訣
            // 最後の一音（G7）だけ「80ms」と長めに残すことで、音が綺麗に響いて「決定！」というスッキリ感が出る
            r4_dac_tone(NOTE_C6, 30);  // ピ
            r4_dac_tone(NOTE_E6, 30);  // ロ
            r4_dac_tone(NOTE_G6, 30);  // リ
            r4_dac_tone(NOTE_C7, 30);  // ロ
            r4_dac_tone(NOTE_E7, 40);  // リ
            r4_dac_tone(NOTE_G7, 80);  // ン♪
            break;

        case EMOTION_SAD: 
            // 【悲しみ・メソメソ】「ぴえぇぇん…」
            // カスタマイズのコツ：
            // 音階を「ド → シ → シ♭(AS) → ラ」と、半音ずつ（ピアノの隣の鍵盤へ）滑り落ちるように
            // 低くしていくことで、マイナー（短調）な寂しいトーンが強調され、しょんぼりした雰囲気になる
            r4_dac_tone(NOTE_C7, 50);  
            r4_dac_tone(NOTE_B6, 50);  
            r4_dac_tone(NOTE_AS6, 50); 
            r4_dac_tone(NOTE_A6, 150); // 最後を長めにして引きずる
            break;
        
        case EMOTION_CONFUSED:
            // 【混乱・はてな？】「ピョコピョコ（あれれ？）」
            // カスタマイズのコツ：
            // 「高い音 → 低い音 → 高い音」と、音階を上下に激しくジャンプさせるのがポイント
            // 規則性のない動きをさせることで、ロボットが首を傾げて戸惑っているコミカルな感じが出る
            r4_dac_tone(NOTE_CS6, 50);  // 高いド#
            r4_dac_tone(NOTE_F5,  50);  // 低いファ
            r4_dac_tone(NOTE_CS6, 50);  // 高いド#
            break;

        case EMOTION_ANGRY:
            // 【怒り・ぷんぷん！】「プンプンッ！！」
            // カスタマイズのコツ：
            // 第3引数に「10」を指定し、音と音の間に強制的に100分の1秒の鋭い無音を挟む
            // これにより音が「プッ！プッ！」と尖って、怒って頬を膨らませているようなトゲトゲしさが出る
            r4_dac_tone(NOTE_F6, 50, 10);  // プンッ（音を途切れさせて尖らせる）
            r4_dac_tone(NOTE_F6, 50, 10);  // プンッ
            r4_dac_tone(NOTE_G6, 120);     // ッサーー！（最後は少し高い音をキツめに長く鳴らす）
            break;
        
        case EMOTION_SLEEPY:
            // 【ウトウトあくび】
            // カスタマイズのコツ：
            // 前半（300ms）を思いっきり長く伸ばして意識が遠のく様子を表し、
            // その後「delay(80)」で一瞬完全に息が止まるようなタメを作ってから、最後に力尽きたように
            // 低い音を「40ms」だけポツンと短く鳴らして消える、ストーリー性のある鳴き声
            r4_dac_tone(NOTE_G5, 150); // ふ
            r4_dac_tone(NOTE_E5, 300); // ぁ〜〜（眠気で音が長く伸びる）
            delay(80);                 // （うとうとして一瞬息が止まる「タメ」）
            r4_dac_tone(NOTE_C5, 40);  // ぅ （最後はベッドに倒れ込むように短く消える）
            break;

        case EMOTION_EATING:
        case EMOTION_SNACK:
            // 【おやつ・もぐもぐ】
            // カスタマイズのコツ：
            // 短く鋭い音（40ms）を刻んで「噛んでいる感」を出し、
            // 最後に高い音（C7）を少し長めに鳴らして「美味しかった！」という満足感を表現
            r4_dac_tone(NOTE_A5, 40);  
            delay(30);
            r4_dac_tone(NOTE_AS5, 40); 
            delay(30);
            r4_dac_tone(NOTE_A5, 40);  
            delay(50);
            r4_dac_tone(NOTE_C7, 100); // 満足げにペロッと一鳴き
            break;
        
        case EMOTION_SMILE:
            // 【人が近づいた！・超ニコニコ】
            // 短くハッピーな高音（50ms）をリズミカルにトコトコと刻み、
            // 最後に突き抜ける最高音（E7）を長めに鳴らして「見つけて大喜び」を表現！
            r4_dac_tone(NOTE_C6, 40);  
            delay(30);
            r4_dac_tone(NOTE_E6, 40); 
            delay(30);
            r4_dac_tone(NOTE_G6, 40);  
            delay(30);
            r4_dac_tone(NOTE_C7, 40);
            delay(40);
            r4_dac_tone(NOTE_E7, 120); // 嬉しさ全開の超高音ジャンプ！
            break;
        
        case EMOTION_FEAST:
            // ⭐ [追加] 歓喜のごちそう専用サウンド
            // 前半：激しいもぐもぐラッシュ（音程を上げながら4回高速パルス）
            for(int i=0; i<4; i++) {
                r4_dac_tone(NOTE_F5 + (i*20), 30);  
                delay(15);
                r4_dac_tone(NOTE_A5 + (i*20), 30); 
                delay(15);
            }
            delay(40);
            
            // 中半：おいしくて頭を振っているようなトレモロ音
            for(int i=0; i<3; i++) {
                r4_dac_tone(NOTE_C6, 25);
                r4_dac_tone(NOTE_E6, 25);
            }
            delay(60);

            // 後半：嬉しさ大爆発の最高音グリッサンドファンファーレ！
            r4_dac_tone(NOTE_C6, 20);
            delay(10);
            r4_dac_tone(NOTE_E6, 20);
            delay(10);
            r4_dac_tone(NOTE_G6, 20);
            delay(10);
            r4_dac_tone(NOTE_C7, 20);
            delay(10);
            r4_dac_tone(NOTE_E7, 30);
            delay(10);
            r4_dac_tone(NOTE_G7, 180); // 突き抜ける超高音でフィニッシュ！
            break;
        
        default:
            // 想定外の感情のセーフティ（プッ）
            r4_dac_tone(NOTE_A5, 100);
            break;
    }

    // ── 演奏終了後の完全消音ダメ押し処理 ──
    // ループを出た後、スピーカーに一切の電気が残らないように完全にピンを眠らせる
    noTone(DAC_PIN);
    analogWrite(DAC_PIN, 0);
    pinMode(DAC_PIN, INPUT); // これが次のplayCryが呼ばれるまでの「無音ノイズゼロ」を維持
}

void playSwitchSound() {
    // 2500Hz の高音を 30ms だけ短く鳴らす（ボタン決定音）
    r4_dac_tone(2500, 30); 
}
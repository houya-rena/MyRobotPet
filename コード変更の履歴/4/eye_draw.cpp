/**
 * eye_draw.cpp
 * 目・鼻・口・装飾アイテムの描画実装
 *
 * 【修正点】
 * - drawOneEye: static ローカル変数を引数経由の参照に変更し、
 *   左右の状態が混在するバグを解消（呼び出し元で管理）
 * - drawMouth: isMouthOpen の条件式バグを修正
 *     旧: (emotion == EMOTION_EATING && EMOTION_SNACK) → 常にtrue
 *     新: (emotion == EMOTION_EATING || emotion == EMOTION_SNACK)
 */

#include "eye_draw.h"

// ══════════════════════════════════════════════════
// ほっぺ（照れ線）
// ══════════════════════════════════════════════════
void drawCheek(Adafruit_SSD1306 &dsp, int cx, int cy, int r) {
    int cheekY = cy + r + 2;
    if (cheekY + 4 >= SCREEN_HEIGHT) return;

    int offset  = (cx < SCREEN_WIDTH / 2) ? -8 : 8;
    int centerX = cx + offset;

    dsp.drawLine(centerX - 4, cheekY,     centerX - 3, cheekY + 3, WHITE);
    dsp.drawLine(centerX - 3, cheekY,     centerX - 2, cheekY + 3, WHITE);
    dsp.drawLine(centerX - 1, cheekY - 1, centerX,     cheekY + 2, WHITE);
    dsp.drawLine(centerX,     cheekY - 1, centerX + 1, cheekY + 2, WHITE);
    dsp.drawLine(centerX + 2, cheekY,     centerX + 3, cheekY + 3, WHITE);
    dsp.drawLine(centerX + 3, cheekY,     centerX + 4, cheekY + 3, WHITE);
}

// ══════════════════════════════════════════════════
// 1つの目を描画
// currentPgy: 左右それぞれの黒目Y位置追従値（呼び出し元で管理）
// ══════════════════════════════════════════════════
void drawOneEye(Adafruit_SSD1306 &dsp,
                int cx, int cy, int r,
                int8_t gx, int8_t gy,
                float lidClose, float lidCut,
                bool isLeft, EmotionType emotion)
{
    float effectiveLid = constrain(lidClose + lidCut, 0.0f, 1.0f);
    int   drawR        = r - 4;

    // ─── 【状態A】まぶた全閉 ───
    if (effectiveLid > 0.9f) {
        if (emotion == EMOTION_SLEEPY) {
            int x_offset = r - 4;
            int sleepY   = cy + (drawR / 2) - 1;
            dsp.drawLine(cx - x_offset,     sleepY - 4, cx - x_offset + 3, sleepY - 1, WHITE);
            dsp.drawLine(cx - x_offset + 3, sleepY - 1, cx - x_offset + 7, sleepY + 1, WHITE);
            dsp.drawFastHLine(cx - x_offset + 7, sleepY + 1, (x_offset - 7) * 2 + 1, WHITE);
            dsp.drawLine(cx + x_offset - 7, sleepY + 1, cx + x_offset - 3, sleepY - 1, WHITE);
            dsp.drawLine(cx + x_offset - 3, sleepY - 1, cx + x_offset,     sleepY - 4, WHITE);
        } else {
            dsp.drawLine(cx - r + 2, cy, cx,         cy - 2, WHITE);
            dsp.drawLine(cx,         cy - 2, cx + r - 2, cy, WHITE);
        }
        return;
    }

    // ─── 【状態B】目が開いている ───

    // 1. 白目ベース
    dsp.fillCircle(cx, cy, drawR, WHITE);

    // 2. 感情別白目カット
    if (emotion == EMOTION_ANGRY && effectiveLid < 0.45f) {
        if (isLeft) {
            dsp.fillRect(cx - drawR - 1, cy - drawR - 2, drawR * 2 + 3, 2, BLACK);
            dsp.fillTriangle(cx - drawR - 1, cy - drawR,
                             cx + drawR + 2, cy - drawR,
                             cx + drawR + 2, cy - (drawR / 3), BLACK);
        } else {
            dsp.fillRect(cx - drawR - 1, cy - drawR - 2, drawR * 2 + 3, 2, BLACK);
            dsp.fillTriangle(cx - drawR - 1, cy - drawR,
                             cx - drawR - 1, cy - (drawR / 3),
                             cx + drawR + 2, cy - drawR, BLACK);
        }
    }
    else if (emotion == EMOTION_SAD && effectiveLid < 0.45f) {
        int shortOffset = drawR / 3;
        if (isLeft) {
            dsp.fillRect(cx - drawR - 1, cy - drawR - 2, drawR * 2 + 3, 2, BLACK);
            dsp.fillTriangle(cx - drawR - 1, cy - (drawR / 2.2f),
                             cx - drawR - 1, cy - drawR,
                             cx + drawR + 2 - shortOffset, cy - drawR, BLACK);
        } else {
            dsp.fillRect(cx - drawR - 1, cy - drawR - 2, drawR * 2 + 3, 2, BLACK);
            dsp.fillTriangle(cx - drawR - 1 + shortOffset, cy - drawR,
                             cx + drawR + 2, cy - drawR,
                             cx + drawR + 2, cy - (drawR / 2.2f), BLACK);
        }
    }

    // 3. 黒目（瞳）
    int iR  = max(2, drawR - 8);
    int pgx = constrain((int)gx, -(drawR - iR - 1), (drawR - iR - 1));
    int pgy = constrain((int)gy, -(drawR - iR - 1), (drawR - iR - 1));

    // 悲しい時は黒目を下寄りに
    if (emotion == EMOTION_SAD) {
        pgy = constrain(pgy + (drawR / 4), -(drawR - iR - 1), (drawR - iR - 1));
    }

    int pupilX = cx + pgx;
    int pupilY = cy + pgy;
    dsp.fillCircle(pupilX, pupilY, iR, BLACK);

    // 4. ハイライト
    if (emotion == EMOTION_SAD) {
        dsp.fillCircle(pupilX + (iR / 2), pupilY - (iR / 2), 3, WHITE);
        dsp.fillCircle(pupilX - (iR / 2), pupilY + (iR / 2), 1, WHITE);
        dsp.fillCircle(pupilX + (iR / 4) - 3, pupilY + (iR / 2), 1, WHITE);
    }
    else if (emotion == EMOTION_CONFUSED) {
        dsp.fillCircle(pupilX + (iR / 3), pupilY + (iR / 4), 2, WHITE);
        dsp.fillCircle(pupilX - (iR / 2), pupilY + (iR / 2), 1, WHITE);
    }
    else {
        int openRy = max(1, (int)(r * (1.0f - effectiveLid)));
        if (effectiveLid < 0.7f && openRy >= 4) {
            if (emotion == EMOTION_HAPPY) {
                dsp.fillCircle(cx + (r / 4),     cy - (openRy / 4), 2, WHITE);
                dsp.fillCircle(cx - (r / 6),     cy + (openRy / 6), 1, WHITE);
                dsp.fillCircle(cx + (r / 4) - 3, cy + (openRy / 4), 1, WHITE);
                dsp.fillCircle(cx - (r / 6),     cy - (openRy / 4) + 1, 1, WHITE);
            } else {
                dsp.fillCircle(cx + (iR / 4), pupilY - (iR / 2), 2, WHITE);
                dsp.fillCircle(cx - (iR / 6), pupilY + (iR / 2), 1, WHITE);
            }
        }
    }

    // ─── 5. まぶた・フチ線の最終処理 ───
    if (emotion == EMOTION_CONFUSED) {
        // CONFUSED時の基準シャッターライン（0.25f はgetEyeParamのlidCloseと一致させること）
        float confLid = max(effectiveLid, 0.25f);
        int   lidH    = max(1, (int)(r * 2.0f * confLid));
        dsp.fillRect(cx - r - 1, cy - r - 1, r * 2 + 3, lidH + 1, BLACK);
        if (cy - r - 1 + lidH < SCREEN_HEIGHT) {
            dsp.drawFastHLine(cx - r, cy - r - 1 + lidH, r * 2, WHITE);
        }
    }
    else if (emotion == EMOTION_ANGRY && effectiveLid < 0.45f) {
        if (isLeft) {
            dsp.drawLine(cx - drawR - 1, cy - drawR,
                         cx + drawR + 2, cy - (drawR / 3), WHITE);
        } else {
            dsp.drawLine(cx - drawR - 1, cy - (drawR / 3),
                         cx + drawR + 2, cy - drawR, WHITE);
        }
    }
    else if (emotion == EMOTION_SAD && effectiveLid < 0.45f) {
        if (isLeft) {
            dsp.drawLine(cx - drawR - 1, cy - (drawR / 2.2f),
                         cx + drawR + 2, cy - drawR, WHITE);
        } else {
            dsp.drawLine(cx - drawR - 1, cy - drawR,
                         cx + drawR + 2, cy - (drawR / 2.2f), WHITE);
        }
    }
    else if (effectiveLid > 0.02f) {
        int lidH = max(1, (int)(r * 2.0f * effectiveLid));
        dsp.fillRect(cx - r - 1, cy - r - 1, r * 2 + 3, lidH + 1, BLACK);
        if (cy - r - 1 + lidH < SCREEN_HEIGHT) {
            dsp.drawFastHLine(cx - r, cy - r - 1 + lidH, r * 2, WHITE);
        }
    }
}

// ══════════════════════════════════════════════════
// 鼻
// ══════════════════════════════════════════════════
void drawNose(Adafruit_SSD1306 &dsp) {
    int nx = SCREEN_WIDTH / 2;
    int ny = EYE_CENTER_Y + 12;
    if (ny < SCREEN_HEIGHT) {
        dsp.fillCircle(nx, ny, 1, WHITE);
    }
}

// ══════════════════════════════════════════════════
// 口
// 【修正】isMouthOpen の条件式バグを修正
//   旧: (emotion == EMOTION_EATING && EMOTION_SNACK) → 常にtrue（&&がポインタ評価）
//   新: (emotion == EMOTION_EATING || emotion == EMOTION_SNACK) → 正しいOR条件
// ══════════════════════════════════════════════════
void drawMouth(Adafruit_SSD1306 &dsp, EmotionType emotion, int frame) {
    const int mx = SCREEN_WIDTH / 2;
    const int my = 50;

    if (my >= SCREEN_HEIGHT) return;

    // もぐもぐ判定：EATING / SNACK のみ frame で口を動かす
    bool isMouthOpen = ((emotion == EMOTION_EATING || emotion == EMOTION_SNACK))
                       ? (frame == 0)
                       : true;

    switch (emotion) {
        case EMOTION_HAPPY:
            dsp.drawPixel(mx - 5, my,     WHITE);
            dsp.drawPixel(mx - 4, my + 2, WHITE);
            dsp.drawPixel(mx - 3, my + 3, WHITE);
            dsp.drawPixel(mx - 2, my + 3, WHITE);
            dsp.drawPixel(mx - 1, my + 1, WHITE);
            dsp.drawPixel(mx,     my,     WHITE);
            dsp.drawPixel(mx + 5, my,     WHITE);
            dsp.drawPixel(mx + 4, my + 2, WHITE);
            dsp.drawPixel(mx + 3, my + 3, WHITE);
            dsp.drawPixel(mx + 2, my + 3, WHITE);
            dsp.drawPixel(mx + 1, my + 1, WHITE);
            break;

        case EMOTION_ANGRY:
        case EMOTION_SAD:
            dsp.drawLine(mx - 5, my + 3, mx,     my,     WHITE);
            dsp.drawLine(mx,     my,     mx + 5, my + 3, WHITE);
            dsp.drawPixel(mx,    my + 1, WHITE);
            break;
          
        case EMOTION_CONFUSED:
        case EMOTION_SLEEPY:
        case EMOTION_SLEEPING:
        case EMOTION_BATH:
            dsp.drawCircle(mx, my + 1, 1, WHITE);
            break;

        case EMOTION_EATING:
        case EMOTION_SNACK:
            if (isMouthOpen) {
                dsp.drawRect(mx - 3, my, 6, 4, WHITE);   // 口を開けている
            } else {
                dsp.drawFastHLine(mx - 3, my + 2, 6, WHITE); // もぐもぐ（閉じ）
            }
            break;

        default:
            dsp.drawFastHLine(mx - 3, my, 6, WHITE);
            break;
    }
}

// ══════════════════════════════════════════════════
// 🍙 ごはんアイテムのドット絵
// ══════════════════════════════════════════════════
void drawFoodItem(Adafruit_SSD1306 &dsp, int frame) {
    const int bx = 104;
    const int by = 48;

    dsp.drawFastHLine(bx + 2, by + 8, 8, WHITE);
    dsp.drawLine(bx + 2, by + 8, bx,      by + 4, WHITE);
    dsp.drawLine(bx + 9, by + 8, bx + 11, by + 4, WHITE);
    dsp.drawFastHLine(bx, by + 4, 12, WHITE);

    if (frame == 0) {
        dsp.fillCircle(bx + 6, by + 2, 3, WHITE);
        dsp.fillCircle(bx + 3, by + 3, 2, WHITE);
        dsp.fillCircle(bx + 9, by + 3, 2, WHITE);
    } else {
        dsp.fillRect(bx + 1, by + 2, 10, 2, WHITE);
        dsp.drawPixel(bx + 3, by - 2, WHITE);
        dsp.drawPixel(bx + 8, by - 1, WHITE);
    }
}
// ══════════════════════════════════════════════════
// 🍪 クッキーアイテムのドット絵（アニメーション対応）
// ══════════════════════════════════════════════════
void drawSnackItem(Adafruit_SSD1306 &dsp, int frame) {
    const int bx = 104; // X座標
    const int by = 48;  // Y座標
    const int r = 7;    // 半径

    // frame 0: まるごとのクッキー
    // frame 1: 少し欠けたクッキー
    // frame 2: 半分になったクッキー
    // frame 3: ほぼ完食

    if (frame >= 3) return; // 3以上は描画しない

    // 1. クッキーのベースを描画
    dsp.fillCircle(bx, by, r, WHITE);

    // 2. 欠けさせる（frameが進むほど黒い長方形で右側を削る）
    if (frame > 0) {
        int biteWidth = frame * 4; // frameが大きいほど削る幅を増やす
        // bx - r から右方向へ biteWidth 分だけ黒く塗りつぶす
        dsp.fillRect(bx - r, by - r, biteWidth, r * 2 + 1, BLACK);
      }

    // 3. チョコチップ（frame 0〜1のときだけ描画）
    if (frame < 2) {
        dsp.drawPixel(bx - 2, by - 2, BLACK);
        dsp.drawPixel(bx + 3, by + 1, BLACK);
        dsp.drawPixel(bx - 1, by + 3, BLACK);
    }
}
// ══════════════════════════════════════════════════
// 💤 Zzz... アニメーション
// ══════════════════════════════════════════════════
void drawSleepZzz(Adafruit_SSD1306 &dsp, int frame) {
    const int zx = 104;
    const int zy = 12;

    dsp.setTextSize(1);
    dsp.setTextColor(WHITE);

    if (frame == 0) {
        dsp.setCursor(zx,     zy + 6); dsp.print("z");
        dsp.setCursor(zx + 8, zy);     dsp.print("Z");
    } else {
        dsp.setCursor(zx + 4,  zy + 3); dsp.print("z");
        dsp.setCursor(zx + 12, zy - 3); dsp.print("Z");
    }
}

// ══════════════════════════════════════════════════
// 🛁 お湯の波線（画面下部）
// ══════════════════════════════════════════════════
void drawWaterLine(Adafruit_SSD1306 &dsp, int frame) {
    const int wy = SCREEN_HEIGHT - 3;

    for (int x = 0; x < SCREEN_WIDTH; x += 8) {
        if (frame == 0) {
            dsp.drawFastHLine(x,     wy,     4, WHITE);
            dsp.drawFastHLine(x + 4, wy + 1, 4, WHITE);
        } else {
            dsp.drawFastHLine(x,     wy + 1, 4, WHITE);
            dsp.drawFastHLine(x + 4, wy,     4, WHITE);
        }
    }
}

// ══════════════════════════════════════════════════
// 🪖 頭のタオル
// ══════════════════════════════════════════════════
void drawHeadTowel(Adafruit_SSD1306 &dsp) {
    const int tx = (SCREEN_WIDTH / 2) - 10;
    const int ty = 2;

    dsp.fillRect(tx, ty, 20, 5, WHITE);
    dsp.drawFastHLine(tx + 2, ty + 2, 16, BLACK);
}
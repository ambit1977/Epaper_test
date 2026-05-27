# U8g2 日本語フォントメモ

`U8g2_for_Adafruit_GFX` を使う際の日本語フォント命名と収録範囲（実機検証済み）。

## 命名規則

```
u8g2_font_<size>_b_t_japanese<N>
                ^   ^
                |   transparent mode
                bold (必須)
```

- `b` (bold) は **必須**。漏らすと `not declared in this scope` でコンパイルエラー
- `t` は transparent モード対応
- `<N>` は漢字収録レベル: 1 / 2 / 3

## サイズの有効値

| サイズ指定 | 状態 |
|-----------|------|
| `b10` | ✓ 使える |
| `b12` | ✓ 使える |
| `b14` | ✗ 存在しない (compile error) |
| `b16` | ✓ 使える |
| `b18` | ✓ 使える |

## 漢字収録の実態

**`japanese1` で表示できる文字（実機検証）：**
- 晴 / 雲 / 最高 / 最低 / 今日 / 明日 / 明後日

**`japanese1` で「□」になる文字：**
- 雨 ← 1 年生で習うのに **含まれない**
- 湿
- 更
- 曇
- 霧
- 凍
- 雷

「教育漢字 ＝ japanese1」とは **限らない**。`japanese1` は教育漢字の完全サブセットでは
ない（命名から推測すべきでない）。実機で確認するのが確実。

## サイズ目安（実コンパイル時）

| フォント | フラッシュ追加量 |
|---------|----------------|
| `japanese1` | ~100KB |
| `japanese2` | ~300KB |
| `japanese3` | ~500KB |

ESP32 (1.3MB program area) では `japanese2` まで余裕、`japanese3` はリスク高。

## 実用的なパターン

容量が厳しいときは `japanese1` + 英語ラベル混在：

| 表示したい | 漢字版 (japanese2 必要) | 代替 (japanese1 で OK) |
|-----------|----------------------|----------------------|
| 更新 12:00 | 更新 12:00 | Update 12:00 |
| 雨 50% | 雨 50% | Rain 50% |
| 湿度 60% | 湿度 60% | RH 60% |

## フォントモードの注意

```cpp
u8g2.setFontMode(1);                       // 1 = transparent
u8g2.setBackgroundColor(GxEPD_WHITE);      // これも明示しないと文字裏が黒くなる
u8g2.setForegroundColor(GxEPD_BLACK);
```

`setFontMode(1)` だけだと、3 色 E-Paper で文字裏に黒い矩形が出ることがある。
`setBackgroundColor()` を毎フレーム明示するのが安全。

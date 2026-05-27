# パネル自動識別

`sketches/04_weather_display_dual` は WeAct 2.9" と 4.2" の **どちらが接続されているかを起動時に自動判別**して、サイズに応じたレイアウトを描画する。

## 仕組み

WeAct の JST 8-pin ケーブルには **MISO ラインがない**ため、SPI 経由でコントローラ
ID を読み取ることはできない。そこで「**正しいドライバと間違ったドライバで Full Update の所要時間が
明確に違う**」性質を利用して判定する。

### 実測値

ESP32 + WeAct パネルでの計測結果：

| 接続パネル | 4.2"ドライバ所要時間 | 2.9"ドライバ所要時間 |
|-----------|---------------------|---------------------|
| **4.2"パネル** | 約 10.0 秒 ✓ | 約 20.0 秒 (タイムアウト) |
| **2.9"パネル** | 約 5.8 秒 (異常早期完了) | 約 18.7 秒 |

ポイント：
- 4.2" ドライバを使った時の所要時間が **8 秒以上 → 4.2"パネル**
- 4.2" ドライバを使った時の所要時間が **8 秒未満 → 2.9"パネル**
- 4.2" ドライバ片方だけで判別できるので、判定は **約 10 秒**で完了

### コード

```cpp
int detectPanel() {
  unsigned long t0 = millis();
  d42.init(115200, true, 50, false);
  d42.setRotation(0);
  d42.setFullWindow();
  d42.firstPage();
  do { d42.fillScreen(GxEPD_WHITE); } while (d42.nextPage());
  unsigned long elapsed = millis() - t0;
  return (elapsed >= 8000) ? PANEL_42 : PANEL_29;
}
```

## RTC RAM キャッシュ

判定には毎回 10 秒かかるので、結果を ESP32 の RTC RAM に保存して、
**Deep Sleep からの復帰では判定スキップ**する。

```cpp
RTC_DATA_ATTR int detected_panel = PANEL_UNKNOWN;
```

`RTC_DATA_ATTR` 変数は：
- Deep Sleep を跨いで保持される
- Power-On Reset でクリアされる
- なので**モジュールを差し替えたら USB を一度抜く** → 次回起動で再判定

## 制限

- 起動直後の **最初の Full Update** が必ず白塗りになる（判定処理のため）
- 4.2" / 2.9" 以外のパネルは判別できない（他のサイズを使う場合は閾値を再調整）
- 4.2" ドライバの所要時間で判定しているので、`GxEPD2_420c_GDEY042Z98` が `GxEPD2_3C` で
  正常動作する必要がある

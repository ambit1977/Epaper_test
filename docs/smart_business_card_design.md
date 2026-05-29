# 電子ペーパー名刺デバイス 仕様書（PN532 統合版）

E-Paper + PN532 + NTAG215 を ESP32 で統合した名刺デバイス。
既存の NFC 名刺サイト `smart_business_card`（固定 URL タグ）と並行運用しつつ、
本デバイスは **動的な vCard 配信 + 相手の NFC スキャン** ができる「動く名刺」を目指す。

## 1. 役割の整理

| 要素 | 役割 |
|------|------|
| **既存 NFC ステッカー** | 固定 URL `https://ambit.go2020.tokyo/card/`、変更不要、ばら撒き用 |
| **名刺サイト (さくらVPS)** | プロフィール、vCard、`now.json` で「今ここ」を動的反映 |
| **電子ペーパー名刺デバイス** ⭐ 本書 | E-Paper 表示 + PN532 で動的 NDEF 書き込み + 相手 NFC 読み取り |

3 つの動作モードを持つ：

| モード | トリガー | 内容 |
|--------|---------|------|
| **A. 通常更新** | 10 分タイマー | now.json + 天気を取得して E-Paper 更新（PN532 は OFF） |
| **B. 名刺交換** | ボタン押下 | PN532 起動 → 相手 NFC 読み取り → NTAG215 書き換え → "Ready to tap!" |
| **C. パッシブ受信** | 電源 OFF 時 | NTAG215 だけが反応、最後に書き込まれた NDEF を返す |

```
[ボタン押下]
   │
   ▼
[E-Paper: "Preparing..." → WiFi 接続 → now.json + 天気取得]
   │
   ▼
[PN532 RST=HIGH → 起動]
   ├──[Phase 1] Reader: 5秒間スキャン → 相手 NFC ID/vCard を読み取り
   └──[Phase 2] Writer: 隣の NTAG215 に最新 NDEF 書き込み
   │
   ▼
[E-Paper: "Ready to tap! #N" 表示]
[PN532 RST=LOW → OFF]
[30 秒待機（相手のスマホタッチ受付）]
[Deep Sleep]
```

---

## 2. ハードウェア構成

```
USB-C ──→ [TP4056] ──→ [Li-Po 1000mAh]
                            │
                            ▼
                       [MCP1700 LDO 3.3V]
                            │
            ┌───────────────┼──────────────────┐
            ▼               ▼                  ▼
      ┌──────────┐   ┌──────────────┐   ┌──────────┐
      │ E-Paper  │SPI│   ESP32      │I2C│  PN532   │
      │  4.2"    │◀─▶│  WROOM-32    │◀─▶│  Module  │
      │  (前面)  │   │              │   │          │
      └──────────┘   └──────┬───────┘   └────┬─────┘
                            │                 │
                          GPIO33              │ アンテナ
                            │                 ▼
                            ▼          近接(< 1cm)
                       [ボタン]               │
                       (強制 wake)            ▼
                                       [NTAG215 裏面]
                                         (パッシブ)
                                              ↑
                                         スマホタッチ
```

### GPIO アサイン一覧

| 用途 | GPIO | 備考 |
|------|------|------|
| E-Paper BUSY | 4 | |
| E-Paper SCL | 5 | SPI クロック |
| E-Paper D/C | 16 | |
| E-Paper RES | 17 | |
| E-Paper SDA | 18 | SPI MOSI |
| E-Paper CS | 23 | |
| PN532 SDA | 21 | I2C |
| PN532 SCL | 22 | I2C |
| PN532 IRQ | 13 | カード検出割込み |
| PN532 RST | 14 | 電源 / リセット制御 |
| ボタン | 33 | RTC GPIO、ext0 wake |

ESP32 DevKit V1 の GPIO はこれで 11 ピン消費、まだ余裕あり。

### PN532 ディップスイッチ

ELECHOUSE 系 PN532 モジュールには SET0 / SET1 スライドスイッチがある：

| SET0 | SET1 | モード |
|------|------|-------|
| L | L | UART (HSU) |
| **H** | **L** | **I2C** ⭐ |
| L | H | SPI |

I2C モードに設定する。

### PN532 省電力制御（RST ピン）

通常時は PN532 を OFF にしておく：

```cpp
const int PN532_RST = 14;

void pn532PowerOn() {
  digitalWrite(PN532_RST, HIGH);
  delay(50);
  nfc.begin();
  nfc.SAMConfig();
}

void pn532PowerOff() {
  digitalWrite(PN532_RST, LOW);  // パワーダウン状態（消費 < 1mA）
}
```

完全に切りたければ P-MOSFET（AO3401）を VDD に挟む選択肢もあるが、
RST 制御で十分なケースが多い。

---

## 3. ソフトウェア構成

### 3.1 起動シーケンス

```
[Deep Sleep wake]
   │
   ├── 原因: タイマー (10分) → 通常更新
   ├── 原因: ボタン (任意) → 強制更新
   │
   ▼
[WiFi 接続] (静的IP + BSSID キャッシュで高速)
   │
   ▼
[NTP 同期] (1日1回のみ、それ以外は RTC RAM の時刻を使用)
   │
   ▼
[API 取得を並列で]
   ├── 天気 (Open-Meteo)
   └── 状況 (ambit.go2020.tokyo/card/now.json)
   │
   ▼
[E-Paper 描画]
   ├── 名前 / 肩書き
   ├── 場所 / イベント / トピック (now.json から)
   ├── 天気アイコン + 気温
   └── QR コード (https://ambit.go2020.tokyo/card/)
   │
   ▼
[WiFi OFF, Deep Sleep N 分]
```

### 3.2 ESP32 が POST するパターン（任意）

ボタン押下時など「今この瞬間に発信したい」場合のみ、ESP32 が直接 `/api/set.php` を叩く：

```cpp
JsonDocument doc;
doc["current"]["place"]  = "ESP32 自動更新";
doc["current"]["topic"]  = "デバイスから発信中";
doc["current"]["public"] = true;
// POST
```

ただし通常運用では、状況の入力は **秋山スマホの /admin から** 行い、
ESP32 は **取得側に徹する** のがシンプル。

---

## 4. E-Paper の表示内容

```
┌────────────────────────────────────────────────┐
│  秋山 大志 / Akiyama Taishi                     │ ← 既存プロフィール
│  AMBIT / Data Gov・Web Analytics                │
├────────────────────────────────────────────────┤
│                                                │
│  📍 渋谷オフィス                                │ ← now.json
│  🎫 IoT Conference 2026                         │
│  💬 "Edge AI × E-Paper の話をします"             │
│                                                │
├────────────────────────────────────────────────┤
│  ☀️ 23°C / 雲   高:25° 低:18°  RH:55%           │ ← 天気 (Open-Meteo)
├────────────────────────────────────────────────┤
│                                                │
│  [QR]   https://ambit.go2020.tokyo/card/        │ ← 名刺サイトへ
│                                                │
│  Updated 14:23                                  │
└────────────────────────────────────────────────┘
```

### QR コードについて

- 4.2インチパネルなら 100×100 px 程度で十分読み取れる
- ライブラリ: `QRCode` (Project Nayuki) などで動的生成
- 内容: 名刺サイトの URL（固定）

---

## 5. now.json データモデル

[business_card_site_design.md](business_card_site_design.md) の §4 を参照。

```json
{
  "current": {
    "place": "渋谷",
    "venue": "WeWork 渋谷スクランブルスクエア",
    "event": "IoT Conference 2026",
    "topic": "Edge AI × E-Paper の話",
    "public": true
  }
}
```

---

## 6. 部品リスト（PN532 統合版）

| 部品 | 個数 | 入手 | 価格目安 |
|------|------|------|---------|
| ESP32 開発ボード（or WROOM-32 ベアモジュール） | 1 | Amazon | 800-1,500円 |
| WeAct 4.2" BWR E-Paper | 1 | (既に所有) | - |
| **PN532 NFC Module V3** | 1 | (既に購入済み) | ~1,800円 |
| **NTAG215 ステッカー**（パッシブタグ用、デバイス内蔵） | 1 | Amazon | 100-200円 |
| LiPo 1000mAh + JST 2.0mm | 1 | Amazon | 1,200円 |
| TP4056 Type-C 充電基板 | 1 | Amazon | 200円 |
| MCP1700-3302E LDO | 1 | 秋月 / DigiKey | 100円 |
| NFC 用フェライトシート（E-Paper との干渉対策） | 1 | Amazon | 500-800円 |
| 抵抗・コンデンサ・タクトスイッチ等 | 一式 | 電子部品キット | 含む |

### 既存 NFC ステッカー（別途、ばら撒き用）

iPhone NFC Tools で `https://ambit.go2020.tokyo/card/` を書いた NTAG213/215 ステッカーは
**そのまま並行運用**。シールとして配ったり、デスクや手帳に貼ったり。

NTAG215（デバイス内蔵）は動的書き換え用、外付けステッカーは固定 URL 用、と役割分担。

---

## 7. 実装フェーズ

### Phase 1: 既存サイトに `/admin` と `/api/set.php` を追加
- smart_business_card リポジトリで作業
- `pages/admin.jsx` 追加
- `api/set.php` 追加
- VPS で PHP 有効化、トークン配置
- `deploy.sh` を拡張して api/ も同期

### Phase 2: ESP32 で now.json を取得 → E-Paper 表示
- 既存 `weather_display_lp` をベースに
- HTTPClient で `https://ambit.go2020.tokyo/card/now.json` を GET
- レイアウトを「天気」中心から「名刺＋天気＋状況」に再構成

### Phase 3: QR コードの描画
- QRCode ライブラリの統合
- 固定 URL を QR にして E-Paper に描画

### Phase 4: 強制更新ボタン（任意）
- GPIO33 のタクトスイッチ
- ext0 wake で即時更新
- E-Paper に「Sending...」「Updated」のフィードバック

### Phase 5: ベアモジュール化 + ケース印刷
- 電力測定 → 低消費電力対策が効いてるか確認
- 3D プリントケースで仕上げ

---

## 8. 関連ドキュメント

- 既存名刺サイトとの連携: [business_card_site_design.md](business_card_site_design.md)
- ハードウェア: [bare_module_design.md](bare_module_design.md)
- ケース: [case_design_spec.md](case_design_spec.md)
- 低消費電力対策: [low_power_optimization.md](low_power_optimization.md)
- 既存リポジトリ: https://github.com/ambit1977/smart_business_card
- 既存 NFC タグ書き込みガイド: https://github.com/ambit1977/smart_business_card/blob/main/NFC_WRITE_iPhone.md

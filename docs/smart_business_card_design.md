# 電子ペーパー名刺デバイス 仕様書（NFC 名刺サイト連携版）

E-Paper を搭載した「名刺を渡す本人が持つ」デバイス。
NFC タグは別途用意し、**固定 URL を 1回だけ書き込む既存運用**を踏襲する。
このデバイスは「動的な内容」を E-Paper に表示し、サーバの `now.json` も更新する役割。

## 1. 役割の整理

| 要素 | 役割 |
|------|------|
| **NFC タグ (NTAG213 等)** | 固定 URL `https://ambit.go2020.tokyo/card/` のみ。スマホタッチで Web ページが開く |
| **名刺サイト (さくらVPS)** | プロフィール、vCard、`now.json` で「今ここ」を動的反映 |
| **電子ペーパー名刺デバイス** ⭐ 本書 | 名刺の表面として "今これしてます" を表示、サーバへ状況発信 |

NFC 関連の動的書き換えは **行わない**。
スマホタッチで開いた Web ページが、サーバの `now.json` を読んで動的にステータスを表示する。

```
[NFC タグ] ── 固定 URL ─→ [スマホブラウザ] ── fetch ─→ [now.json]
                                                          ↑
                                                          │
                                                       [ESP32 デバイス]
                                                          │ POST
                                                          │
                                                       [秋山スマホ /admin]
                                                          ↑
                                                          │ 場所/イベント入力
```

---

## 2. ハードウェア構成

```
              USB-C (5V 充電)
                  │
            ┌──────────┐
            │  TP4056  │ ──── [Li-Po 1000mAh]
            └──────────┘            │
                  │                  │
                  ▼                  ▼
            ┌──────────┐
            │ MCP1700  │
            │ LDO 3.3V │
            └──────────┘
                  │
        ┌─────────┴─────────┐
        ▼                   ▼
  ┌──────────┐         ┌──────────────┐
  │ E-Paper  │  SPI    │   ESP32      │
  │ 4.2" BWR │◀───────▶│  WROOM-32    │
  │ (前面)   │         │              │
  └──────────┘         └──────┬───────┘
                              │ WiFi
                              ▼
                       https://ambit.go2020.tokyo/card/now.json
                       (GET / POST)
```

### 配線（ベアモジュール構成と同一）

E-Paper への配線は [bare_module_design.md](bare_module_design.md) と
[pin_mapping.md](pin_mapping.md) を参照。NFC 関連の配線は一切なし。

### オプション：強制更新ボタン

- タクトスイッチを GPIO33 (ext0 wake)
- 押下で wake → `now.json` を即時取得 + E-Paper 即時更新
- POST も同時に行いたい時は `/api/set.php` に「自動測定値」を送る

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

## 6. 部品リスト（NFC 関連簡素化版）

| 部品 | 個数 | 入手 | 価格目安 |
|------|------|------|---------|
| ESP32 開発ボード（or WROOM-32 ベアモジュール） | 1 | Amazon | 800-1,500円 |
| WeAct 4.2" BWR E-Paper | 1 | (既に所有) | - |
| LiPo 1000mAh + JST 2.0mm | 1 | Amazon | 1,200円 |
| TP4056 Type-C 充電基板 | 1 | Amazon | 200円 |
| MCP1700-3302E LDO | 1 | 秋月 / DigiKey | 100円 |
| **NTAG213 ステッカー**（1枚で OK） | 1 | Amazon | 50-100円 |
| 抵抗・コンデンサ・タクトスイッチ等 | 一式 | 電子部品キット | 含む |

### 不要になったもの
- ~~PN532 NFC モジュール~~
- ~~フェライト NFC シート~~
- ~~P-MOSFET（PN532 電源切替用）~~
- ~~書き換えトリガー用の特別な配線~~

**合計コスト削減: 約 3,000-4,000 円**

NFC タグへの書き込みは [既存ガイド](https://github.com/ambit1977/smart_business_card/blob/main/NFC_WRITE_iPhone.md) に従って iPhone の NFC Tools で 1回だけ。

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

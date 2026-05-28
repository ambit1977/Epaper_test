# スマートビジネスカード 仕様書（PN532 + NTAG215 構成）

E-Paper（表面：名刺情報、日替わりトピック）+ NFC（裏面：vCard、URL、交換時の状況メモ）を
持ったデジタル名刺デバイス。

## 1. コンセプト

```
┌──────────────────────────┐   ←  表面（E-Paper 4.2"）
│   Taishi Akiyama         │       名前、肩書き、会社
│   ezmag                  │       連絡先 QR コード
│   Engineer / Designer    │       「今日のトピック」「最近の活動」
│   [QR]  "今日は..."       │
└──────────────────────────┘

┌──────────────────────────┐   ←  裏面（NTAG215 + アンテナ）
│   📡 NFC                  │       スマホタッチで:
│                          │       - vCard を連絡先に追加
│                          │       - ポートフォリオ URL を開く
│                          │       - 受け取り時の日時 / 場所が
│                          │         vCard NOTE に自動記入される
└──────────────────────────┘
```

### 設計のキー

| 役割 | デバイス | 配置 |
|------|---------|------|
| スマホからの読み取り対象 | **NTAG215**（パッシブタグ） | ケース裏面 |
| ESP32 から書き込み（NDEF 生成） | **PN532**（リーダー / ライター） | ケース内 |

PN532 が NTAG215 を内側から書き換える、変態的な構成。

利点：
- **スマホは NTAG215 を直接読む** → ESP32/PN532 の電源が切れていても OK
- **ESP32 + PN532 で内容を書き換えられる** → 動的更新可能
- **PN532 は書き込み時のみ電源 ON** → 省電力（GPIO で電源制御）

---

## 2. 動作モード

```
┌──────────────────────────────────────────────────────────┐
│  通常時（待機）                                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐               │
│  │  ESP32   │  │  PN532   │  │  NTAG215 │ ◀── スマホで読 │
│  │ DeepSleep│  │   OFF    │  │ パッシブ │     み取り可能 │
│  └──────────┘  └──────────┘  └──────────┘               │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│  10 分ごとの更新時（数秒〜15秒）                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐               │
│  │  ESP32   │  │  PN532   │  │  NTAG215 │               │
│  │   wake   │─▶│   ON     │─▶│ 書き換え │               │
│  └──────────┘  └──────────┘  └──────────┘               │
│       │             │                                     │
│       └─ WiFi で API、E-Paper 更新も並行                  │
└──────────────────────────────────────────────────────────┘
```

---

## 3. ハードウェア構成

```
                  USB-C (5V 充電のみ)
                       │
                  ┌──────────┐
                  │  TP4056  │ ──── [Li-Po 1000mAh]
                  └──────────┘            │
                       │                  │
                       ▼                  ▼
                ┌──────────┐
                │ MCP1700  │
                │  LDO 3.3V│
                └──────────┘
                       │
        ┌──────────────┼──────────────────────┐
        ▼              ▼                      ▼
  ┌──────────┐    ┌──────────────┐        ┌──────────┐
  │ E-Paper  │SPI │   ESP32      │  GPIO  │ P-MOSFET │
  │ 4.2" BWR │◀──▶│  WROOM-32    │───────▶│ 電源 SW  │
  │ (前面)   │    │              │        └────┬─────┘
  └──────────┘    │              │             │ 3.3V
                  │              │             ▼
                  │              │I2C    ┌──────────┐
                  │              │◀─────▶│  PN532   │
                  │              │       │ モジュール│
                  └──────────────┘       └────┬─────┘
                                              │ アンテナ
                                              ▼
                                        近接(< 1cm)
                                              │
                                              ▼
                                        ┌──────────┐
                                        │ NTAG215  │ ◀─スマホ
                                        │(裏面パッシブ)│
                                        └──────────┘
```

### 配線（既存ベアモジュール + NFC 追加分）

| 信号 | ESP32 GPIO | PN532 |
|------|-----------|-------|
| VCC | P-MOSFET 経由 3.3V | VCC |
| GND | GND | GND |
| SDA | GPIO21 | SDA |
| SCL | GPIO22 | SCL |
| IRQ | GPIO13 | IRQ（任意） |
| RST | GPIO14 | RSTPDN（任意） |
| PWR_CTRL | GPIO27 | P-MOSFET ゲートへ |

GPIO27 で P-MOSFET を制御して PN532 の電源を ON/OFF できるようにする。

---

## 4. PN532 → NTAG215 への書き込み手順

PN532 を **リーダーモード** にして、近接する NTAG215 に対して `WRITE` コマンドで NDEF を書き込む。

### PN532 ライブラリ
- **Adafruit_PN532**（一般的、リーダー / ライター対応）

### 疑似コード

```cpp
#include <Adafruit_PN532.h>
#include "NDEFBuilder.h"  // 自作 or 既存ライブラリ

Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);

void updateNTAG() {
  digitalWrite(PWR_CTRL, HIGH);   // PN532 電源 ON
  delay(50);

  nfc.begin();
  nfc.SAMConfig();

  // NDEF メッセージ構築
  uint8_t buf[504];
  NDEFBuilder ndef(buf, sizeof(buf));
  ndef.addVCard(...);
  ndef.addUri("https://...");

  // NTAG215 を待機
  uint8_t uid[7], uidLen;
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 1000)) {
    // ページ単位（4byte）で書き込み
    for (int page = 4; page < 130; page++) {
      nfc.mifareultralight_WritePage(page, &buf[(page - 4) * 4]);
    }
  }

  digitalWrite(PWR_CTRL, LOW);    // PN532 電源 OFF
}
```

---

## 5. アンテナ配置と物理設計

### 二つのアンテナの干渉問題

- PN532 のアンテナと NTAG215 のアンテナが両方 13.56 MHz で共振
- PN532 が ON のとき NTAG215 も励起されてしまう
- スマホタッチ時に PN532 が OFF なら NTAG215 だけ反応する

**→ PN532 電源を OFF にしている限り、スマホは NTAG215 と通信可能**

### 配置案

```
┌────────────────────────────────────┐ ← ケース裏面（断面図）
│   [スマホタッチ面]                  │
│                                    │
│   [NTAG215 ステッカー]              │ ← 一番外側
│   ─────────────────────             │
│   [フェライトシート (NTAG用)]       │ ← E-Paper 干渉対策
│                                    │
│   [PN532 モジュール]                │ ← NTAG215 と < 1cm
│   (内蔵、アンテナを NTAG に向ける)  │
│                                    │
│   ─────────────────────             │
│   [ESP32 ベアモジュール基板]        │
│   [LiPo]                            │
│                                    │
│   [E-Paper]                         │ ← 表面 (反対側)
│                                    │
└────────────────────────────────────┘ ← ケース表面
```

NTAG215 → PN532 → ESP32 基板 → LiPo → E-Paper の順で重ねる。

### NTAG215 ステッカーの選定

NTAG215 は NFC ステッカー / カードの形で安価に入手可能：
- **NTAG215 NFC ステッカー**（25mm 円形 or 25×25 角型、Amazon で 10枚 500-800円）
- **白紙 NFC カード**（クレジットカード型、NTAG215 内蔵）
- 容量: 504 byte (vCard 数百文字、URL × 2-3 個 余裕)

### スマホ読み取り距離

- 通常 NTAG はスマホから 0-3cm
- 名刺の薄いケース構造なら問題なし

---

## 6. NDEF メッセージ仕様（変更なし）

```
[NDEF Message]
├── Record 1: vCard 3.0
│   BEGIN:VCARD
│   VERSION:3.0
│   FN:Taishi Akiyama
│   ORG:ezmag
│   TITLE:Engineer
│   EMAIL:ambit.akiyama@gmail.com
│   TEL:+81-90-XXXX-XXXX
│   URL:https://example.com
│   NOTE:2026-05-27 17:30 / 渋谷 - "IoTについて話しました"
│   END:VCARD
│
├── Record 2: URI (Portfolio)
│   https://portfolio.example.com
│
└── Record 3: URI (LinkedIn)
  https://linkedin.com/in/...
```

NOTE フィールドは ESP32 起動時に動的生成。

---

## 7. ESP32 動作フロー

```
[Power on / Deep Sleep wake]
       │
       ▼
[WiFi 接続]
       │
       ├──→ SSID 判定 → 場所決定
       │
       ▼
[NTP 同期] (必要時のみ)
       │
       ▼
[API 取得]
   ├── 天気 (Open-Meteo)
   └── 今日のトピック (自分のクラウド)
       │
       ▼
[E-Paper 描画]
       │
       ▼
[NDEF 構築]
       │
       ▼
[GPIO27 = HIGH → PN532 電源 ON]
       │
       ▼
[PN532 経由で NTAG215 書き換え]
       │
       ▼
[GPIO27 = LOW → PN532 電源 OFF]
       │
       ▼
[WiFi OFF, Deep Sleep N 分]
```

PN532 の通電時間は実質 **5 秒程度**。

---

## 8. 部品リスト

### NFC 関連（追加分）

| 部品 | 検索ワード / 型番 | Amazon 入手 | 価格目安 |
|------|-----------------|-----------|---------|
| **PN532 NFC モジュール V3** | `PN532 NFC モジュール ELECHOUSE V3` | ✓ | 1,500-2,500円 |
| **NTAG215 NFC ステッカー** | `NTAG215 NFC ステッカー 10枚` | ✓ | 500-800円 |
| **P-MOSFET（電源スイッチ用）** | `AO3401 SOT-23` または `IRLML6402` | ✓ | 500-800円 / 10個 |
| **NFC 用フェライトシート** | `NFC アンチメタル シート` | ✓ | 500-800円 |
| **抵抗 10kΩ**（P-MOSFET プルアップ用） | (キットに含む想定) | - | - |

### 合計 NFC 関連: 約 3,000-4,500円

---

## 9. PN532 のメリット（PN532 を選んだ補足）

- **Amazon ですぐ入手可能**（ELECHOUSE V3 が定番）
- 価格安め（1,500-2,500円）
- 豊富なライブラリ（Adafruit_PN532）
- **将来的に拡張可能**：
  - リーダーモードで他人の NFC カードを読み取り（FeliCa含む）
  - カードエミュレーション（PN532 自体がタグになる）
  - P2P 通信（古い Android スマホとの通信）

NTAG I²C plus を諦めた代わりに、PN532 で**さらに多機能な使い方** ができるようになる。

---

## 10. リスクと対策

| リスク | 対策 |
|-------|------|
| PN532 と NTAG215 のアンテナ干渉でスマホが読めない | PN532 を完全に電源 OFF（P-MOSFET 経由） |
| PN532 から NTAG215 への書き込みが届かない | アンテナ距離 < 1cm を確保、アンテナを向き合わせ |
| NTAG215 が他のタグと混信 | ケース内には NTAG は 1個だけ |
| NDEF 容量超過（504B 超） | URL を短縮、NOTE を短く |
| P-MOSFET の選定ミスで PN532 起動不可 | ピンチェック、ゲート電圧確認 |
| 書き込み中に電源を切るとデータ破損 | 書き込み完了確認後に電源 OFF |

---

## 10b. 書き換えトリガーボタン併用（推奨）

10 分タイマーだけでは「名刺交換の瞬間」と更新タイミングが最大 10 分ずれる。
**側面に書き換えトリガーボタン**を設けることで、交換直前に「今この瞬間」のデータを
NTAG215 に書き込める。

### ハードウェア
- タクトスイッチ 6×6mm
- ESP32 GPIO33（RTC GPIO、ext0 wake 対応）に接続、もう一端 GND
- 内蔵プルアップ使用、外付け抵抗不要

### 動作分岐
ESP32 setup で wake 原因を判定し、ボタン経由なら「今すぐ書き換え」モードに：

```cpp
esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
if (cause == ESP_SLEEP_WAKEUP_EXT0) {
  handleManualExchange();   // 即時 NTAG 書き換え + E-Paper "Ready to tap!"
} else {
  handleScheduledUpdate();  // 10分タイマー: 通常更新
}

// 次回 wake は両方有効化
esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0);
esp_sleep_enable_timer_wakeup(SLEEP_MINUTES * 60ULL * 1000000ULL);
esp_deep_sleep_start();
```

### 名刺交換シナリオ
1. ユーザー: 渡す直前にボタン押下
2. ESP32: wake、E-Paper 表面に "Sending..." 表示
3. WiFi → NTP → 「今この瞬間」の時刻 / 場所 / 第N回交換 を組み立て
4. PN532 経由で NTAG215 書き換え
5. E-Paper: "Ready to tap! #5" 表示
6. ユーザーがカードを渡し、相手がスマホタッチ → vCard 取得
7. 30 秒後に通常表示に戻り Deep Sleep

### 「交換カウント」を RTC RAM に保持
```cpp
RTC_DATA_ATTR int  exchange_count_today = 0;
RTC_DATA_ATTR int  last_exchange_date   = 0;
```
日付が変わったらリセット。E-Paper にも「今日 N 人」と表示できる。

### ボタンの拡張余地
| 操作 | 想定動作 |
|------|---------|
| 短押し (< 1秒) | NFC 書き換え + 即時更新 |
| 長押し (3-5秒) | WiFi 設定リセット等 |
| ダブルクリック | 「今日のトピック」を強制再取得 |

Phase 1 は短押しのみ。

## 11. 実装段階

### Phase 1: PN532 単体動作確認
- ESP32 + PN532 で既存 NFC カード（Suica, MIFARE）が読めるか確認
- I2C 通信、ライブラリ動作確認

### Phase 2: NTAG215 への書き込み
- NTAG215 ステッカーを購入
- PN532 経由で固定 NDEF を書き込み、スマホで読めるか確認

### Phase 3: 動的 NDEF
- 時刻 / 場所 / トピックを組み合わせた NDEF
- 毎回 wake で書き換え

### Phase 4: P-MOSFET 電源制御
- 通常時 OFF、書き込み時のみ ON
- 電池寿命の計測

### Phase 5: ケース実装
- PN532 と NTAG215 を物理配置
- スマホ読み取り距離の調整

---

## 12. 関連ドキュメント

- 配線：[bare_module_design.md](bare_module_design.md) を更新
- ケース：[case_design_spec.md](case_design_spec.md) を更新

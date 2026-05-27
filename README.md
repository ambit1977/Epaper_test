# ESP32 × WeAct 2.9" E-Paper 天気予報ディスプレイ

ESP32 と WeAct Studio 2.9" BWR E-Paper モジュールを使った、Wi-Fi 経由で天気情報を表示する電子ペーパー端末。Open-Meteo API から現在気温・湿度・降水確率・3日間予報を取得して表示し、10 分ごとに Deep Sleep で自動更新する。

![preview](docs/preview.png)

## 機能

- 現在気温（℃）・天気アイコン・天気名（晴/雲/雨など）
- 最高/最低気温
- 降水確率（10% 刻みに丸め）
- 相対湿度
- 3 日間予報（今日 / 明日 / 明後日）
- NTP 同期で更新時刻表示（API 時刻フォールバックあり）
- Deep Sleep による省電力（10 分間隔更新）

## ハードウェア

- ESP32 DevKit V1（ESP32-WROOM-32）
- WeAct Studio 2.9" BWR E-Paper Module（128×296、SSD1680 コントローラ）

### ⚠️ 重要：このモジュール固有の落とし穴

WeAct 製の付属 JST ケーブルは **基板シルク表記とケーブル色配置が一致していません**。
色から信号を推測してはいけない。実機で導通を確認して下さい。

このリポジトリのコードは以下の物理配線で動作確認済み：

| 信号 | ESP32 GPIO | ケーブル色 | E-Paper JST PIN |
|------|-----------|-----------|-----------------|
| BUSY | GPIO4 | 紫 | 1 |
| RES | GPIO17 | オレンジ | 2 |
| D/C | GPIO16 | 白 | 3 |
| CS | GPIO23 | 青 | 4 |
| SCL | GPIO5 | 緑 | 5 |
| SDA | GPIO18 | 黄 | 6 |
| GND | GND | 黒 | 7 |
| VCC | 3.3V | 赤 | 8 |

詳しくは [`docs/pin_mapping.md`](docs/pin_mapping.md) を参照。

## 必要なライブラリ

Arduino IDE / arduino-cli でインストール：

```bash
arduino-cli lib install "GxEPD2"
arduino-cli lib install "Adafruit GFX Library"
arduino-cli lib install "U8g2_for_Adafruit_GFX"
arduino-cli lib install "ArduinoJson"
```

ボード定義：

```bash
arduino-cli core install esp32:esp32
```

## セットアップ

### 1. リポジトリをクローン

```bash
git clone https://github.com/ambit1977/Epaper_test.git
cd Epaper_test
```

### 2. Wi-Fi 設定ファイルを作成

`sketches/03_weather_display/config.h.example` を `config.h` にコピーして編集：

```bash
cd sketches/03_weather_display
cp config.h.example config.h
```

`config.h` を編集して自分の Wi-Fi の SSID とパスワードを設定する。
（`config.h` は `.gitignore` で除外されるので、コミットに含まれない）

### 3. 都市を変更したい場合（任意）

`weather_display.ino` 内の `LAT` / `LON` / `CITY` を編集：

```cpp
const float LAT = 35.6895;     // 緯度
const float LON = 139.6917;    // 経度
const char* CITY = "Tokyo";    // 表示名
```

### 4. コンパイル & アップロード

```bash
arduino-cli compile --upload \
  -p /dev/cu.usbserial-XXX \
  --fqbn esp32:esp32:esp32:UploadSpeed=115200 \
  sketches/03_weather_display
```

> **注意**: `UploadSpeed=115200` の指定が必須。デフォルト 921600 だと USB ケーブル品質によって `Chip stopped responding` エラーが頻発する。

## ディレクトリ構成

```
.
├── README.md
├── docs/
│   └── pin_mapping.md          物理配線の詳細とトラブルシュート
├── sketches/
│   ├── 01_basic_test/          自前ドライバの初期テスト（参考）
│   ├── 02_hello_world/         GxEPD2 で Hello World 表示（最小サンプル）
│   ├── 03_weather_display/     ★ 天気予報メイン
│   │   ├── weather_display.ino
│   │   ├── config.h.example
│   │   └── (config.h)          各自で作成、gitignored
│   └── diagnostic/             診断ツール（配線疑い時に使う）
│       ├── loopback_test.ino   GPIO ループバック確認
│       ├── signal_check.ino    信号 HIGH/LOW 制御（マルチメーター用）
│       └── epaper_autotest.ino 複数の GxEPD2 クラスを順番に試行
```

## トラブルシュート

### 表示が更新されない

1. `diagnostic/loopback_test.ino` で ESP32 と配線そのものの導通を確認
2. それが OK なら `diagnostic/signal_check.ino` で各信号を HIGH/LOW 切り替えてマルチメーターで E-Paper 側のピンに信号が届いているか確認
3. JST ケーブル色は信用しないこと（このモジュールは色と信号が一致していない）

### アップロード時 `Chip stopped responding`

- USB ケーブルがデータ転送非対応の可能性 → 別ケーブルを試す
- `UploadSpeed=115200` を必ず FQBN に追加

### 日本語が「□」になる

- u8g2 のフォント命名規則は `u8g2_font_<size>_b_t_japanese<N>` で `b`（bold）が必須
- `japanese1` には「雨」「湿」「更」などの基本漢字すら含まれないことがある
- 詳細は [`docs/u8g2_japanese_notes.md`](docs/u8g2_japanese_notes.md)

## ライセンス

MIT

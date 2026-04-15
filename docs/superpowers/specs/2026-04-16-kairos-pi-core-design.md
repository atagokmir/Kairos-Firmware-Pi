# Kairos Pi 5 Core — Design Spec

**Date:** 2026-04-16  
**Status:** Approved

---

## Context

Kairos bir endüstriyel IoT sistemidir. Pico 2W, MZ80 IR sensörü ile cycle time ölçer ve USB CDC üzerinden `CYCLE <microseconds>\n` formatında Raspberry Pi 5'e gönderir. Bu spec, Pi 5 C++ uygulamasının ilk fazını kapsar: serial okuma, UCL/LCL istatistik hesabı, anomaly detection ve logging. Sistem ileride LVGL (fiziksel dokunmatik ekran — DSI/HDMI, framebuffer) ve Paho MQTT thread'leriyle genişletilecektir.

---

## Pico Serial Protocol

- **Format:** `CYCLE <uint32_t microseconds>\n`
- **Baud:** USB CDC (sanal — baud rate önemsiz)
- **Port:** `/dev/ttyACM0`
- **Edge cases:**
  - İlk rising edge'de output yok (timer başlatma)
  - Startup/error mesajı yok
  - Sadece `CYCLE` satırları üretilir

---

## Architecture: Pipeline (Option B)

```
SerialThread → CycleQueue (mutex+condvar) → StatsThread → SharedState (shared_mutex)
                                                                ↑          ↑
                                                       (future) DisplayThread  MQTTThread
```

**Neden Pipeline seçildi:**
- Cycle time saniyeler mertebesinde → yüksek frekans baskısı yok
- `condition_variable` ile StatsThread gerçekten uyuyor, CPU boşa yanmıyor
- Her thread tek sorumluluk taşıyor
- LVGL/MQTT gelince sadece `display_thread.{hpp,cpp}` veya `mqtt_thread.{hpp,cpp}` ekleniyor — mevcut koda dokunulmaz

---

## File Structure

```
Kairos-Firmware-Pi/
├── CMakeLists.txt
├── kairos.conf
├── src/
│   ├── main.cpp
│   ├── config.hpp
│   ├── config.cpp
│   ├── cycle_queue.hpp
│   ├── shared_state.hpp
│   ├── rolling_window.hpp
│   ├── rolling_window.cpp
│   ├── logger.hpp
│   ├── logger.cpp
│   ├── serial_thread.hpp
│   ├── serial_thread.cpp
│   ├── stats_thread.hpp
│   └── stats_thread.cpp
└── tests/
    ├── CMakeLists.txt
    ├── test_config.cpp
    ├── test_rolling_window.cpp
    ├── test_logger.cpp
    └── test_cycle_queue.cpp
```

---

## Config

```cpp
struct Config {
    std::string port              = "/dev/ttyACM0";
    std::size_t window_size       = 100;
    std::size_t min_samples       = 50;        // UCL/LCL aktif olmadan önce gereken minimum örnek
    std::size_t summary_interval  = 50;        // her N cycle'da özet log
    std::string log_path          = "/var/log/kairos/kairos.log";
    double      anomaly_threshold = 3.0;       // UCL = mean + threshold*σ
};
```

**Yükleme sırası:** `kairos.conf` (INI formatı) → CLI args override

**CLI flags:** `--port`, `--window`, `--min-samples`, `--summary-interval`, `--log`, `--threshold`, `--config`

---

## Log Format

```
[2026-04-16 14:30:00] [INFO]    Warming up... samples=12/50
[2026-04-16 14:32:01] [ANOMALY] cycle=1250000µs  mean=1000000µs  UCL=1120000µs  LCL=880000µs  sigma=40000µs
[2026-04-16 14:32:51] [SUMMARY] count=150  mean=1000200µs  sigma=39800µs  UCL=1119600µs  LCL=880800µs  anomalies=2
[2026-04-16 14:33:00] [WARN]    Serial port lost. Retrying in 4s...
```

---

## Error Handling

| Durum | Davranış |
|---|---|
| Port açılamıyor | `[WARN]` + exponential backoff (1→2→4→...→30s) |
| Port bağlantı kesilmesi | Aynı retry, thread çökmez |
| Malformed satır (`CYCLE abc`) | Skip + `[WARN]` log |
| Boş satır / bilinmeyen prefix | Sessizce skip |
| `cycle_time == 0` | Geçersiz ölçüm, skip |
| Pencere dolmadan anomali sorgusu | `[INFO] Warming up...`, anomali flaglenmez |
| `SIGINT` / `SIGTERM` | `atomic<bool> running=false` + `queue.stop()` → temiz çıkış |
| Log dizini yoksa | `create_directories()` dener; başarısız → sadece stdout |

---

## SharedState (future-proof)

```cpp
struct SharedState {
    mutable std::shared_mutex mtx;
    double   mean          = 0.0;
    double   sigma         = 0.0;
    double   ucl           = 0.0;
    double   lcl           = 0.0;
    uint32_t last_cycle    = 0;
    uint64_t cycle_count   = 0;
    uint64_t anomaly_count = 0;
    bool     warming_up    = true;
};
```

`DisplayThread` ve `MQTTThread` `std::shared_lock` ile okur — birbirini bloklamaz.

---

## Design Decisions (Rationale)

| Karar | Seçenek | Neden |
|---|---|---|
| Thread iletişimi | Queue + condvar | Polling yerine event-driven; CPU yakmaz |
| SharedState kilidi | `std::shared_mutex` | Okuyucular (LVGL, MQTT) eş zamanlı okuyabilir |
| UCL/LCL yöntemi | Rolling window | Hat değişimlerine adapte olur; kümülatif kirletiyor |
| Pencere boyutu | Configurable (default 100) | Makineye/ürüne göre ayarlanabilir, derleme gerektirmez |
| Anomaly threshold | Configurable sigma çarpanı | 3σ sabit kodlamak field'da kullanışsız |
| Config yöntemi | INI dosyası + CLI override | Servis olarak çalışırken SSH'sız düzenlenebilir |
| Log hedefi | stdout + dosya | Debug (stdout) + headless monitoring (dosya) |
| Summary interval | Her 50 cycle | Audit trail + log boyutu dengesi |
| min_samples | window/2 = 50 | Warm-up periyodunda yanlış alarm önleme |
| Hata durumu | Retry, çökme değil | 7/24 çalışan üretim sistemi için kritik |
| Test framework | Google Test (FetchContent) | Standart C++ test framework, CMake entegrasyonu kolay |

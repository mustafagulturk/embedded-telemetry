# Paket formatı (v1)

Tüm çok baytlı alanlar **little-endian**dır. Her çerçeve senkron baytlarıyla başlar,
CRC ile biter. CRC, `ver` alanından payload'un sonuna kadar olan baytlar üzerinden hesaplanır.

## Çerçeve

| Ofset | Boyut | Alan      | Açıklama                                             |
|------:|------:|-----------|------------------------------------------------------|
| 0     | 1     | `sync0`   | `0xA5`                                               |
| 1     | 1     | `sync1`   | `0x5A`                                               |
| 2     | 1     | `ver`     | Protokol sürümü, şu an `0x01`                        |
| 3     | 1     | `type`    | `0x01` telemetri, `0x02` istatistik                  |
| 4     | 2     | `seq`     | Paket sayacı (u16, her gönderilen çerçevede +1, sarar) |
| 6     | 1     | `len`     | Payload uzunluğu (bayt)                              |
| 7     | len   | `payload` | Tipine göre aşağıda                                  |
| 7+len | 2     | `crc`     | CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, refin/refout yok, xorout 0) |

`seq` tüm paket tipleri için ortak tek sayaçtır; yer istasyonu boşluklardan kayıp sayar.
CRC kontrol değeri: `"123456789"` → `0x29B1`.

## Tip 0x01: Telemetri (32 bayt, 100 Hz)

| Ofset | Tip | Alan           | Birim                         |
|------:|-----|----------------|-------------------------------|
| 0     | u32 | `t_ms`         | açılıştan beri ms             |
| 4     | i16 | `ax`           | mg                            |
| 6     | i16 | `ay`           | mg                            |
| 8     | i16 | `az`           | mg                            |
| 10    | i16 | `gx`           | 0.1 °/s                       |
| 12    | i16 | `gy`           | 0.1 °/s                       |
| 14    | i16 | `gz`           | 0.1 °/s                       |
| 16    | i16 | `temp`         | 0.01 °C (BMP280)              |
| 18    | u32 | `pressure`     | Pa                            |
| 22    | i32 | `alt_raw`      | cm (barometrik, filtresiz)    |
| 26    | i32 | `alt_filt`     | cm (filtreli)                 |
| 30    | u8  | `mode`         | 0 NORMAL, 1 DEGRADED, 2 FAILSAFE |
| 31    | u8  | `faults`       | bit0 IMU, bit1 BARO, bit2 kuyruk taşması, bit3 watchdog reset |

Toplam çerçeve: 7 + 32 + 2 = **41 bayt**. 100 Hz'de ≈ 4100 B/s, 115200 baud (≈11520 B/s) yeterli.

## Tip 0x02: İstatistik (18 bayt, 1 Hz)

| Ofset | Tip | Alan              | Birim / açıklama                      |
|------:|-----|-------------------|---------------------------------------|
| 0     | u32 | `uptime_ms`       | ms                                    |
| 4     | u16 | `cpu_load`        | binde (‰), idle sayacından            |
| 6     | u16 | `sensor_max_us`   | sensör görevinin en uzun çalışma süresi (µs) |
| 8     | u16 | `packer_max_us`   | paketleme görevi en uzun süre (µs)    |
| 10    | u16 | `tx_max_us`       | gönderme görevi en uzun süre (µs)     |
| 12    | u16 | `queue_drops`     | dolu kuyruk yüzünden atılan örnek     |
| 14    | u16 | `i2c_errors`      | toplam I2C hatası                     |
| 16    | u8  | `reset_cause`     | 0 güç açılışı, 1 watchdog, 2 yazılım, 3 diğer |
| 17    | u8  | `mode`            | telemetrideki ile aynı                |

# Renode simülasyonu (NUCLEO-F446RE + MPU6050 + BMP280)

Bu dizin, telemetri firmware'ini donanım olmadan [Renode](https://renode.io) 1.15.3 içinde
çalıştırmak için gereken her şeyi içerir.

| Dosya | Açıklama |
|---|---|
| `telemetry.resc` | Ana betik: modelleri derler, platformu yükler, ELF'i yükler, USART2'yi TCP 3456'ya bağlar, uçuş profilini açar |
| `platforms/nucleo_f446re.repl` | STM32F446RE platformu (512 KB flash, 128 KB SRAM, 16 MHz HSI), LD2 (PA5), I2C1 üzerinde sensörler |
| `models/MPU6050.cs` | MPU6050 IMU modeli (I2C 0x68) |
| `models/BMP280.cs` | BMP280 barometre modeli (I2C 0x76), Bosch kompanzasyonunun tersini alarak ham değer üretir |
| `models/STM32F4_ResetFlags.cs` | RCC_CSR reset bayraklarını gerçekten modelleyen RCC + bunu besleyen IWDG (Renode'un kendi modellerinin kopyası) |
| `tests/telemetry.robot` | `renode-test` ile uçtan uca testler (gerçek firmware ELF'i ile) |
| `tests/telemetry_frames.py` | Robot kütüphanesi + komut satırı çerçeve çözücü (ikili UART için) |
| `tests/model_selftest.resc`, `tests/check_models.py` | Firmware'siz model öz-testi (I2C yolu, NACK, BMP280 kompanzasyonu) |

## Gereksinimler

- Renode 1.15.3 (dotnet portable), örn. `/opt/renode-dl/renode_1.15.3-dotnet_portable`
  (`renode`, `renode-test` PATH'te olsun ya da tam yolla çağırın).
- Firmware ELF'i: `build/fw/firmware/firmware.elf`
  ```bash
  cmake -S . -B build/fw -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=MinSizeRel
  cmake --build build/fw
  ```
- Testler için Robot Framework (Renode'un `tests/requirements.txt` dosyası):
  ```bash
  python3 -m venv .venv-renode && . .venv-renode/bin/activate
  pip install robotframework==6.1 robotframework-retryfailed==0.2.0 psutil pyyaml
  ```
- İnternet gerekmez. (Upstream `stm32f4.repl` açılışta `dl.antmicro.com`'dan SVD indirir;
  bizim platform dosyası bunu yapmaz.)

## Simülasyonu çalıştırma

Depo kökünden (betik yolları `$ORIGIN` ile kendi konumuna göre çözülür, her yerden çalışır):

```bash
renode --disable-gui --console renode/telemetry.resc -e start
```

Farklı bir ELF veya port:

```bash
renode --disable-gui --console \
  -e '$elf=@/tam/yol/firmware.elf; $uart_port=4000; include @renode/telemetry.resc; start'
```

Notlar:
- `--console` modunda Renode stdin'den monitör komutu okur; stdin kapanırsa (ör. `< /dev/null`)
  Renode hemen çıkar. Arka planda çalıştırırken stdin'i açık tutun (`sleep infinity | renode ...`)
  veya monitörü porta açın: `renode --disable-gui -P 1234 renode/telemetry.resc` + `telnet localhost 1234`.
- `machine Reset` / IWDG reset sonrasında `reset` makrosu ELF'i yeniden yükler.
- Sanal zaman, bu makinede gerçek zamanın ~%65'i hızında ilerler (CPU 16 MIPS'e ayarlı:
  `sysbus.cpu PerformanceInMips 16`, gerçek kartla aynı döngü süreleri/CPU yükü için).
  Yer istasyonu ~65 çerçeve/s görür; `t_ms` ve `seq` yine de tutarlıdır (kayıp 0).

## Yer istasyonunu bağlama

USART2, TCP sunucu soketine (port 3456, telnet müzakeresi yok, ikili güvenli) bağlıdır:

```bash
cd ground_station
python -m telemetry_gs --tcp localhost:3456            # canlı grafik
python -m telemetry_gs --tcp localhost:3456 --no-plot --duration 20
```

Soket terminali, istemci bağlanmadan önce gönderilen baytları **saklamaz**; ilk çerçeveleri
görmek için yer istasyonunu `start`'tan önce bağlayın (Renode istemci beklemez).

Ham yakalamayı çözmek için: `python3 renode/tests/telemetry_frames.py kayit.bin [her_n_cercevede_bir]`.

## Monitörden sensör ayarı ve hata enjeksiyonu

```
# Sensör değerleri
sysbus.i2c1.mpu6050 AccelX 0.0          # g
sysbus.i2c1.mpu6050 AccelZ 1.0
sysbus.i2c1.mpu6050 GyroZ 45            # °/s
sysbus.i2c1.mpu6050 Temperature 30
sysbus.i2c1.mpu6050 AccelNoiseStdDev 0.01
sysbus.i2c1.mpu6050 GyroNoiseStdDev 0.1
sysbus.i2c1.bmp280 FlightProfile false
sysbus.i2c1.bmp280 Pressure 95000       # Pa
sysbus.i2c1.bmp280 Temperature 18.25    # °C
sysbus.i2c1.bmp280 PressureNoiseStdDev 2

# Uçuş profili (sanal zamana bağlı, deterministik)
sysbus.i2c1.bmp280 FlightProfileDelay 2     # s yerde bekle
sysbus.i2c1.bmp280 FlightProfileApogee 120  # m
sysbus.i2c1.bmp280 FlightProfile true
sysbus.i2c1.bmp280 ProfileAltitude          # o anki profil irtifası (salt okunur)

# Sensör kopması (I2C adresine NACK)
sysbus.i2c1.mpu6050 Disconnected true
sysbus.i2c1.mpu6050 Disconnected false
sysbus.i2c1.bmp280 Disconnected true
sysbus.i2c1.bmp280 Disconnected false

# Watchdog / reset
sysbus.cpu IsHalted true       # CPU'yu dondur -> ~500 ms sonra IWDG reset
sysbus.cpu IsHalted false
sysbus.iwdg ResetCount         # IWDG kaynaklı reset sayısı
sysbus.iwdg Running
sysbus.rcc ResetFlags          # RCC_CSR[31:25] ham hali (firmware RMVF ile temizler)

# Doğrudan register erişimi (firmware'siz deneme)
sysbus.i2c1.mpu6050 ReadRegister 0x75
sysbus.i2c1.mpu6050 WriteHex "3B"
sysbus.i2c1.mpu6050 ReadHex 14
sysbus.i2c1.bmp280 ReadRegister 0xD0
```

### `Disconnected = true` olduğunda firmware ne görür

Model kendini I2C1 denetleyicisinin adres tablosundan çıkarır (monitörde adıyla görünmeye devam
eder). Renode'un `STM32F4_I2C` modeli bu durumda adres fazında:

- `SR1.SB` temizlenir, `SR1.ADDR` **hiç set edilmez**,
- `SR1.AF` (bit 10, 0x400) **set edilir**, `ITERREN` açıksa ER kesmesi (IRQ 32) gelir,
- iç durum `Idle`'a döner; firmware STOP üretmeli ve AF'yi temizlemelidir.

Dikkat: Renode'da `AF` **okununca temizlenir** (ReadToClear + WriteZeroToClear). ADDR'yi
beklerken her döngüde `SR1`'i bir kez okuyup hem ADDR hem AF'yi aynı okunan değerde kontrol edin;
aksi halde AF bir okumada kaybolur ve sürücü ADDR için zaman aşımına kadar bekler (yine hata olarak
sonuçlanır ama daha geç). Mevcut firmware bunu doğru yapıyor: test sırasında `mode=1 (DEGRADED)`,
`faults=0x01`, `i2c_errors` artıyor; yeniden bağlanınca `mode=0`.

## Testler

```bash
# Uçtan uca (gerçek firmware ile), sonuçlar renode/tests/results/ altına
renode-test -r renode/tests/results renode/tests/telemetry.robot
renode-test -r renode/tests/results --variable ELF:$PWD/build/fw/firmware/firmware.elf renode/tests/telemetry.robot

# Firmware'siz model öz-testi (WHO_AM_I, burst, NACK, BMP280 kompanzasyonu Python ile doğrulanır)
python3 renode/tests/check_models.py
```

`-r` verilmezse `renode-test` çıktıları (log.html, report.html, snapshots/, logs/) bulunduğunuz
dizine yazar.

Robot testleri:

1. **Firmware Should Send Valid Telemetry Frames** – 3 s: ≥250 telemetri, ≥2 istatistik çerçevesi,
   CRC hatası 0, `seq` kesintisiz, mod hep NORMAL, az≈1000 mg, p≈101325 Pa, T=22.50 °C.
2. **Barometric Altitude Should Follow The Flight Profile** – 13 s sonra `alt_filt` ≈ 120 m.
3. **MPU6050 Disconnect Should Degrade The Mode** – kopma sonrası mod 1/2, `faults` bit0,
   `i2c_errors` artar; yeniden bağlanma sonrası mod sadece loglanır.
4. **BMP280 Disconnect Should Set The Baro Fault** – mod 1/2, `faults` bit1.
5. **Watchdog Should Not Reset The Firmware In A Normal Run** – 5 s: IWDG çalışıyor,
   `ResetCount`=0, `t_ms` monoton, `reset_cause`=0.

Renode 1.15.3'ün UART test aracı satır/metin tabanlıdır (ikili mod veya `Wait For Bytes On Uart`
yok). Bu yüzden testler USART2'yi bir TCP sunucu soketine (port 34560) bağlar ve
`telemetry_frames.py` çerçeveleri oradan okur. Sanal zaman `emulation RunFor "0.25"` dilimleriyle
ilerletilir ve her dilimden sonra soket boşaltılır.

## Model ayrıntıları ve basitleştirmeler

### Yükleme sırası (önemli)

1. `include $ORIGIN/models/MPU6050.cs`, `BMP280.cs`, `STM32F4_ResetFlags.cs` (çalışma anında C#
   derlemesi bu dotnet sürümünde çalışıyor)
2. `mach create`
3. `machine LoadPlatformDescription $ORIGIN/platforms/nucleo_f446re.repl`
4. `sysbus LoadELF $elf`

- Her `include` edilen `.cs` ayrı bir assembly'ye derlenir ve bunlar **birbirine referans
  veremez**; bu yüzden RCC ve IWDG aynı dosyadadır.
- `.resc` içinde `@$ORIGIN/...` çalışmaz; `$ORIGIN/...` (başında `@` olmadan) kullanın.
- REPL'de `using "platforms/cpus/stm32f4.repl"` ile var olan bir girdinin **tipini** değiştirmek
  (`rcc`, `iwdg`) veya yapıcı parametresini (`nvic systickFrequency`) güncellemek mümkün değil
  (E02/E35). Bu nedenle `nucleo_f446re.repl` upstream dosyanın tam bir kopyasıdır.

### MPU6050

- Registerlar: `0x19 SMPLRT_DIV`, `0x1A CONFIG`, `0x1B GYRO_CONFIG (FS_SEL 4:3)`,
  `0x1C ACCEL_CONFIG (AFS_SEL 4:3)`, `0x3B..0x48` veri (big-endian), `0x6B PWR_MGMT_1`
  (reset 0x40, bit7 cihaz reseti), `0x75 WHO_AM_I = 0x68`. Diğerleri basit R/W bayt.
- Ölçek: ivme 16384/8192/4096/2048 LSB/g, gyro 131/65.5/32.8/16.4 LSB/(°/s), sıcaklık
  `raw = (T − 36.53)·340`.
- Basitleştirme: SLEEP modunda da veri registerları canlı değer döndürür; DLPF/örnekleme hızı
  çıktıyı etkilemez, her okuma yeni örnektir. Gürültü sabit tohumlu (`NoiseSeed`) → tekrarlanabilir.

### BMP280

- Kalibrasyon: datasheet örnek değerleri (dig_T1=27504 … dig_P9=6000).
- Ham değerler, datasheet tamsayı kompanzasyonunun (`compensate_T_int32` +
  `compensate_P_int64`, Q24.8) ikili arama ile tersi alınarak üretilir: firmware'in 64-bit formülü
  ayarlanan basıncı ±0.1 Pa, sıcaklığı 0.01 °C ile geri verir. 32-bit formül (`compensate_P_int32`)
  kullanan bir firmware ±5 Pa sapma görebilir.
- Mod: sleep → son ölçüm kalır (reset sonrası 0x80000); forced → `ctrl_meas` yazılınca bir ölçüm
  alınır ve mod 00'a döner; normal → her I2C okuma işlemi yeni ölçüm. `osrs_x = 0` olan kanal
  0x80000 okur. Çözünürlük her zaman 20 bit; `config` (t_sb, filtre) saklanır ama etkisizdir.
  `status` (0xF3) her zaman 0 (hazır).
- Uçuş profili: `FlightProfileDelay` s yerde, 10 s'de `FlightProfileApogee` m'ye S-eğrisiyle tırmanış,
  10 s bekleme, 20 s iniş; `p = 101325·(1 − h/44330)^5.255`. Zaman kaynağı makinenin sanal zamanıdır.

### STM32F4_I2C (Renode modeli) – firmware sürücüsünün uyması gerekenler

- START (CR1.START) → `SR1.SB` hemen set. DR'ye adres yazılınca:
  - ACK: `ADDR` set; yazmada `TXE`, okumada `RXNE` + `BTF` set. `ADDR`, **SR2 okunarak**
    temizlenir (SR1 okuması gerekmez ama zararsız).
  - NACK (cihaz yok / `Disconnected`): `AF` set, `ADDR` yok (bkz. yukarı).
- Yazma: DR'ye yazılan baytlar biriktirilir ve cihaza **STOP veya tekrarlı START** anında tek
  seferde `[reg, data...]` olarak verilir. Her bayttan sonra `TXE`/`BTF` bir sonraki senkron noktada
  set edilir (anında değil) → `BTF`/`TXE`'yi döngüyle bekleyin.
- Okuma: adres fazında cihazdan **tüm** veri bir kerede alınır ve bir FIFO'ya konur. `ACK`/`POS`
  bitleri ve STOP zamanlaması **yok sayılır**; 1, 2 veya N bayt okuma için HAL tarzı herhangi bir
  sıra çalışır. FIFO'da veri kaldıkça `RXNE`=1 ve `BTF`=1'dir.
- Okuma işlemi bittikten sonra FIFO'da kalan baytlar bir sonraki okuma adres fazına kadar durur:
  `SR1.RXNE` (ve bazen `BTF`) STOP'tan sonra da 1 görünebilir. Sürücü "boşta" durumunu RXNE'den
  çıkarmamalı; sonraki işlemlerde zararsızdır.
- FIFO boşken DR okunursa 0 döner (log uyarısı).
- `SR2.BUSY` modellenmez (hep 0); `MSL` STOP'ta temizlenmez. CCR/TRISE/F/S (400 kHz) değerleri
  saklanır ama zamanlamayı etkilemez: bir I2C işlemi sanal zamanda neredeyse anında biter.

### RCC ve IWDG

- Renode'un `STM32F4_RCC` modelinde `RCC_CSR` reset bayrakları yalnızca "tag"dir (RMVF çalışmaz,
  IWDGRSTF/SFTRSTF hiç set olmaz). `STM32F4_RCC_ResetFlags` bunu düzeltir:
  açılışta `0x0E000000` (PORRSTF|PINRSTF|BORRSTF), IWDG reseti → `IWDGRSTF|PINRSTF`, diğer her
  makine reseti (AIRCR SYSRESETREQ, `machine Reset`) → `SFTRSTF|PINRSTF`, `RMVF` yazımı hepsini
  temizler. Bayraklar sistem resetinde korunur.
- RCC'de özel bir başlatma gerekmez: saat etkinleştirme bitleri (AHB1ENR/APB1ENR) sadece saklanır,
  hiçbir çevre birimi saati kapalı diye durmaz; `HSIRDY` = `HSION` (reset değeri 1).
- IWDG: 32 kHz LSI, `KR=0x5555` kilit açma, `PR`, `RLR`, `KR=0xCCCC` başlatma, `KR=0xAAAA`
  yenileme – hepsi modellenir. `SR.PVU/RVU` her zaman 0 (beklemek gerekmez, ama beklemek de
  sorun değil). Kilit açılmadan `PR`/`RLR` yazılırsa yok sayılır (uyarı).
- Saat ağacı: SysTick 16 MHz, TIM2 16 MHz (PSC=15 → 1 MHz), USART2 16 MHz (BRR doğru yorumlanır).

### Bilinen zararsız uyarılar

- `nvic: Trying to set the priority for interrupt 16 to 0xFF, but it should be maskable with 0xF0`
  (FreeRTOS, SysTick/PendSV önceliğini 0xFF yazar; STM32 yalnızca üst 4 biti tutar).
- `gpioPortB: Unhandled write to offset 0x4 ... OT8/OT9` (açık-drenaj I2C pinleri; Renode GPIO
  modeli OTYPER'ı tag olarak tutar).

# Firmware <-> Renode sözleşmesi

- MCU: STM32F446RE (Renode `platforms/cpus/stm32f4.repl` temelli), sistem saati 16 MHz HSI (PLL yok, Renode ve gerçek kartta aynı).
- I2C1: PB8 = SCL, PB9 = SDA (Nucleo Arduino D15/D14), 100 kHz.
  - MPU6050 @ 0x68 (WHO_AM_I 0x75 = 0x68)
  - BMP280  @ 0x76 (chip id 0xD0 = 0x58)
- USART2: PA2 TX / PA3 RX, 115200 8N1 (Nucleo ST-LINK sanal COM portu). İkili telemetri burada.
- Teşhis metni yok; USART2 yalnızca ikili çerçeve taşır (docs/packet_format.md).
- IWDG: ~500 ms zaman aşımı.
- Sensör kopma testi: Renode'da sensör modeli I2C'den ayrılır (veya "Disconnected" özelliği true yapılır), firmware birkaç ardışık hatadan sonra DEGRADED/FAILSAFE moduna geçmeli.

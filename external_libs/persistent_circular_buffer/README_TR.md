# STM32WLE5 RAM-backed Persistent Circular Buffer — v1.0

Bu ilk sürüm, çalışma sırasında **tamamen RAM üzerinde** çalışan 100 kayıtlık
bir circular buffer sağlar. Flash yalnızca kalıcılık katmanıdır:

- `pcb_init()` → en yeni geçerli Flash snapshot'ını RAM'e yükler.
- Normal API çağrıları → yalnızca RAM ile çalışır.
- `pcb_sync()` → RAM'in o anki görüntüsünü Flash'a kaydeder.
- `pcb_deinit()` → veri değişmişse `Sync` yapar, başarılıysa RAM handle'ını temizler.

## Kayıt biçimi

Her kayıt tam 16 byte'tır:

| Alan | Boyut |
|---|---:|
| Unix timestamp (saniye) | 4 byte |
| UUID | 7 byte |
| UUID uzunluğu | 1 byte |
| Kütüphane record ID | 2 byte |
| Durum | 1 byte |
| Reserved | 1 byte |

UUID yalnızca 4 veya 7 byte olabilir. Dört byte UUID kullanılırsa kalan üç byte
sıfırlanır.

## Bellek kullanımı

RAM kayıt alanı:

```text
100 × 16 byte = 1600 byte
```

Flash:

```text
Page 126 = 0x0803F000 - 0x0803F7FF = 2 KiB
Page 127 = 0x0803F800 - 0x0803FFFF = 2 KiB
Toplam = 4 KiB
```

Bir snapshot:

```text
64 byte header
100 × 16 byte kayıt = 1600 byte
Toplam = 1664 byte
```

## Neden iki Flash sayfası var?

A/B snapshot düzeni kullanılır. Yeni snapshot pasif sayfaya yazılır. Header ve
payload doğrulandıktan sonra commit marker en son yazılır. Yeni sayfa commit
edilene kadar eski sayfa geçerli kalır.

Sync sırası:

1. Pasif sayfayı sil.
2. Header'ın ilk 56 byte'ını yaz; commit alanını silinmiş bırak.
3. RAM kayıtlarını en eskiden en yeniye yaz.
4. Readback ve payload CRC32 kontrolü yap.
5. Commit marker'ı son 8 byte'a yaz.
6. Yeni sayfayı aktif kabul et.

## Desteklenen işlemler

- `pcb_add()`
- `pcb_get_latest()`
- `pcb_get_last_n()`
- `pcb_get_recent_slice()`
- `pcb_get_by_timestamp()`
- `pcb_get_by_status()`
- `pcb_set_status_by_id()`
- `pcb_set_status_by_key()`
- `pcb_clear()`
- `pcb_sync()`
- `pcb_deinit()`

Sorgular seçilen sonuç kümesi içinde **en eskiden en yeniye** sıralıdır.

### Son X ve önceki Y kayıt

```c
/* En yeni 10 kayıt. */
pcb_get_recent_slice(&buffer, 0, 10, out, 10, &count);

/* En yeni 10 kaydı atla, önceki 10 kaydı getir. */
pcb_get_recent_slice(&buffer, 10, 10, out, 10, &count);
```

## CubeIDE entegrasyonu

1. `Inc` klasöründeki dosyaları include path'e ekleyin.
2. `Src/persistent_circular_buffer.c` ve `Src/pcb_flash_stm32wl.c` dosyalarını
   projeye ekleyin.
3. Linker script'te `FLASH LENGTH` değerini `256K` yerine `252K` yapın.
4. `pcb_config.h` içindeki adreslerin hedef parçayla uyuştuğunu kontrol edin.
5. Handle'ı static/global veya `{0}` ile başlatın.
6. Başlangıçta `pcb_init()` çağırın.
7. Çalışma sırasında normal RAM API'lerini kullanın.
8. Kontrollü aralıklarda `pcb_sync()` çağırın.
9. Kontrollü kapanışta `pcb_deinit()` çağırın.

## Çok önemli: DeInit güç kesilince çalışmaz

Ani enerji kesilmesi, watchdog reset, brownout veya HardFault durumunda
`pcb_deinit()` çağrılma garantisine sahip değildir. Yalnızca DeInit'e güvenilirse
son açılıştan beri RAM'de biriken kayıtlar kaybolabilir.

Örnek persistence politikası:

- Her 10 yeni kayıtta `pcb_sync()`
- Her 5 dakikada `pcb_sync()`
- LoRaWAN TX/RX işlemleri tamamlandıktan sonra `pcb_sync()`
- Kontrollü kapanışta `pcb_deinit()`

## Çalışma bağlamı

`pcb_sync()`:

- ISR içinden çağrılmamalı.
- LoRaWAN RX1/RX2 pencerelerinde çağrılmamalı.
- Aynı handle için iki task tarafından eşzamanlı çağrılmamalı.
- FreeRTOS kullanılıyorsa uygulama seviyesinde mutex ile korunmalı.

Kütüphanedeki `busy` bayrağı iç içe çağrıları engeller; tam RTOS thread-safety
sağlamaz.

## All-ones double-word neden programlanmıyor?

STM32WL Flash portu `0xFFFFFFFFFFFFFFFF` değerindeki 8-byte blokları atlar.
Kullanıcı verisi değişmemiş görünse bile all-ones programlama ECC bitlerini
programlayabilir. Commit double-word'ünün gerçekten silinmiş kalması için bu
bloklar yazılmaz.

## İlk sürümün sınırları

- Dinamik bellek kullanmaz.
- Flash işlemleri bloklayıcıdır.
- FreeRTOS mutex'i içermez.
- CRC32 yazılımla hesaplanır.
- Flash sayfaları compile-time sabittir.
- Eski snapshot formatından migration henüz yoktur.
- İlk açılışta iki snapshot da geçersizse buffer boş başlatılır.
\n\n## v1.0 ekleri\n\n- `pcb_get_all()`: init sonrasında tüm RAM kayıtlarını eskiden yeniye alma\n- `pcb_get_failed()`: gönderilememiş kayıtları doğrudan filtreleme\n- `pcb_count_by_status()`: kopyalama yapmadan durum sayısını alma\n- `Examples/example_usage.c`: `USER_LOG` ile kayıt döküm örneği\n\nKütüphane doğrudan UART veya logger bağımlılığı taşımaz. Döküm formatı uygulama\nkatmanında yapılır; bu sayede kütüphane farklı projelerde taşınabilir kalır.\n

## Init sonrası kontrol ve filtreleme

- `pcb_get_all()` RAM buffer içindeki bütün kayıtları en eskiden en yeniye döndürür.
- `pcb_get_failed()` yalnızca `PCB_STATUS_FAILED` kayıtlarını döndürür.
- `pcb_count_by_status()` veri kopyalamadan durum sayısını verir.
- `Examples/main_integration_example.c`, init sonrasında UART dump ve failed filtreleme kullanımını gösterir.

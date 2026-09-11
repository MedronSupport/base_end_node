# LW RFID Base End Node

STM32WL tabanlı, RFID kart okuyup LoRaWAN üzerinden sunucuya ileten, düşük güç tüketimli ve **offline dayanıklı** bir uç düğüm (end node) firmware'i. Kart okunduğunda ACK alınamazsa (join yok, duty-cycle, ağ kapalı vb.) kayıt flash'a yazılır ve ağ tekrar erişilebilir olduğunda otomatik olarak boşaltılır — manuel reset gerekmez.

## Donanım

- **MCU:** STM32WLE5JCIx (Cortex-M4 + entegre SubGHz radyo)
- **RFID okuyucu:** MFRC522 (SPI2), GPIO ile anahtarlanabilir güç hattı üzerinden
- **Bölge/Profil:** EU868, LoRaWAN Class A, **OTAA** (`LoRaWAN/App/lora_app.h`)
- **Uygulama portu:** `LORAWAN_USER_APP_PORT = 2`

## Mimari

Klasik CubeMX iskeleti + kooperatif, run-to-completion bir sequencer (`Utilities/sequencer`, `UTIL_SEQ_*`) üzerine kurulu — RTOS yok. `main.c`'deki tek süperdöngü `MX_LoRaWAN_Process()` üzerinden sequencer'ı sürüyor; sequencer boşta kaldığında düşük güç yöneticisi (`Utilities/lpm`, `Core/Src/stm32_lpm_if.c`) cihazı **Stop2** moduna sokuyor.

Uygulama mantığının tamamı `LoRaWAN/App/lora_app.c`'de: RFID okuma tetikleyicisi (buton/GPIO wake-up), LoRaWAN gönderim/ACK/retry zincirleri, periyodik durum (STATUS) mesajı, otomatik rejoin, sunucudan gelen genel komut protokolü (zaman senkronu, buzzer/LED, tarih aralığı sorgusu, STATUS aralığı değişikliği).

CubeMX'in dokunmadığı, projeye özel her şey **`external_libs/`** altında kendi `Inc/`+`Src/` klasörleriyle tutulur — bu sayede CubeMX ile kod yeniden üretimi (regenerate) bu modülleri asla etkilemez:

| Modül | Görev |
|---|---|
| `MFRC522/` | MFRC522 RFID okuyucu sürücüsü (SPI, register erişimi, anticollision) |
| `Wake_Up_Button/` | Buton ile uyandırma GPIO/EXTI kurulumu |
| `adc_bat_meas/` | Pil voltajı ve sıcaklık ölçümü (dahili ADC) |
| `persistent_circular_buffer/` | Flash destekli dairesel arabellek — ACK alınamayan RFID kayıtlarını saklar, bkz. `README_TR.md` içinde |
| `lora_app_auxilary/` | `lora_timesync` — sunucudan gelen zaman senkron downlink'ini işleyip cihaz saatini yöneten bağımsız kütüphane |
| `watchdog/` | `app_watchdog` — IWDG (bağımsız donanım watchdog) sarmalayıcısı |

## LoRaWAN Payload Formatları

Tüm mesajlar `LORAWAN_USER_APP_PORT` (2) üzerinden, uplink'lerde `Buffer[0]` = uplink counter, `Buffer[1]` = mesaj tipi ile başlar. Çok baytlı sayısal alanlar **big-endian**. Tam byte-seviyesi tablolar, örnek payload'lar ve sunucu tarafı davranış kuralları (hata/red senaryoları, retry, MQTT topic'leri dahil) için bkz. **`docs/server-gelistirici-rehberi.md`** — burada sadece özet veriliyor.

**Canlı RFID okuma** (`LORA_RFID_MSG_TYPE_LIVE_UID = 0x45`, 18 byte) ve **buffer'dan tekrar gönderim** — aynı format, confirmed:
| Byte | Alan |
|---|---|
| 0 | uplink counter |
| 1 | tip (0x45) |
| 2:5 | kart okunduğu andaki timestamp (Unix epoch, sn) |
| 6 | UID uzunluğu |
| 7:13 | UID (7 byte, kullanılmayan baytlar 0x00) |
| 14:17 | gönderim anındaki güncel epoch tahmini (retry'lerde [2:5]'ten farklı olabilir) |

**Durum (STATUS) mesajı** (`LORA_RFID_MSG_TYPE_STATUS = 0x27`, 14 byte, confirmed — varsayılan periyot 1 saat, uzaktan komutla ayarlanabilir):
| Byte | Alan |
|---|---|
| 0 | uplink counter |
| 1 | tip (0x27) |
| 2:3 | pil ADC değeri (mV) |
| 4:5 | sıcaklık (Q8.8) |
| 6:9 | status sayacı — sunucunun zaman senkron yanıtında **aynen** geri göndermesi gereken tazelik anahtarı |
| 10:13 | gönderim anındaki güncel epoch tahmini |

**IND ping** (`LORA_RFID_MSG_TYPE_IND = 0x22`, 6 byte, unconfirmed): veri taşımayan, ek bir RX penceresi açmak için gönderilen yoklama mesajı. Bir STATUS'un ACK sonucu belli olduktan ~3 sn sonra ve boş kalan (kart bulunamayan) bir okuma denemesinin ardından tetiklenir — sunucunun kuyrukladığı bir downlink'e (zaman senkronu, komut) ekstra teslim fırsatı sağlar.
| Byte | Alan |
|---|---|
| 0 | uplink counter |
| 1 | tip (0x22) |
| 2:5 | gönderim anındaki epoch |

**Tarih aralığı sorgu sonucu** (`LORA_RFID_MSG_TYPE_QUERY_RESULT = 0x46`, değişken uzunluk, unconfirmed, çoklu batch): bir sorgu komutuna (aşağıda) yanıt olarak, radyo veri hızına (DR) göre dinamik boyutlu batch'ler halinde gönderilir. Header (6 byte: counter, tip, bu mesajdaki kayıt sayısı, o ana kadarki toplam, batch index, kırpıldı-mı bayrağı) + ardışık TLV kayıt blokları (`[uuid_uzunluk][uuid][timestamp]`). Detay: `LoRaWAN/App/lora_app.h`.

**Zaman senkron downlink'i** (sunucudan cihaza, tip `0x01`, 9 byte — bkz. `external_libs/lora_app_auxilary/Inc/lora_timesync.h`):
| Byte | Alan |
|---|---|
| 0 | tip (0x01) |
| 1:4 | Unix epoch (sn) |
| 5:8 | sunucunun bu yanıtı hesapladığı andaki status sayacı |

Cihaz, gelen sayacı kendi son gönderdiği status sayacıyla **birebir eşleşmiyorsa** ya da taşınan zaman izin verilen toleranstan fazla geriye sıçratıyorsa yanıtı sessizce reddeder (gecikmiş/stale downlink koruması, ölçeklenmiş tolerans) — bkz. `lora_timesync.c`.

**Genel komut downlink'i** (sunucudan cihaza, tip `LORA_COMMAND_DOWNLINK_TYPE = 0x02`, `Buffer[1]` = komut ID): tek bir dispatch mekanizması altında üç komut tanımlı — bkz. `LoRaWAN/App/lora_app.h` ve `docs/server-gelistirici-rehberi.md` bölüm 5.2:
| Komut ID | İsim | Boyut | Özet |
|---|---|---|---|
| `0x01` | Buzzer/LED | 6 byte | Sesli/görsel uyarıyı uzaktan tetikler (kayıp cihaz bulma); süre saniye cinsinden, cihaz tarafında azami 3 dakikaya sessizce kırpılır |
| `0x02` | Tarih aralığı sorgusu | 10 byte | Kalıcı bellekteki, verilen `[start,end]` epoch aralığına giren tüm kayıtları `0x46` ile geri raporlatır |
| `0x03` | STATUS aralığı değişikliği | 4 byte | Periyodik STATUS gönderim sıklığını uzaktan ayarlar (30 sn – 24 saat arası, 30 sn'lik çarpanlarla); RTC yedek register'ında (`RTC_BKP_DR4`) kalıcı |

Geçersiz parametreli komutlar (aralık dışı çarpan, start>end, çakışan sorgu) **sessizce reddedilir** — cihaz hiçbir hata uplink'i göndermez, sadece UART logu tutar.

## Güç Yönetimi ve Watchdog

Cihaz çoğu zamanını Stop2 modunda geçirir; RFID okuma (~5-6 sn, timer ile sınırlı) ve status hazırlığı (ADC okuma, çok kısa) dışında Stop modu kilitlenmez.

**IWDG (bağımsız watchdog)** `external_libs/watchdog/` üzerinden elle bağlandı — CubeMX `.ioc`'ta hiç etkinleştirilmedi, bilinçli bir tercih (regenerate tetiklenmesin diye):
- Zaman aşımı: ~30 sn (LSI /256 prescaler, reload 3749) — donanım tavanına (~32,768 sn) ~2,77 sn pay bırakır.
- **İki refresh noktası:** (1) `Core/Src/stm32_lpm_if.c` → `PWR_EnterStopMode()`, her Stop2 girişinde; (2) `external_libs/watchdog/`'un kendi kurduğu **bağımsız periyodik "kick" görevi** (~20 sn, `CFG_SEQ_Task_WatchdogKickEvent` — enerji tasarrufu için zaman aşımının ~2/3'ü kadar seyrek tutuluyor). İkincisi zorunlu: IWDG, Stop2 uykusu sırasında da (LSI'ye bağlı olduğu için) saymaya devam eder — cihaz bir sonraki olaya kadar (ör. periyodik status turu) zaman aşımından uzun kesintisiz uyursa, sadece Stop2-girişi refresh'i bir daha hiç çağrılmaz ve IWDG cihazı **gerçek bir donma olmadan** resetler (sahada gözlemlenen, düzeltilen gerçek bir bug — `SendBufferedRfidLogHandler`'ın ardından ~26 sn'de tekrarlayan reboot).
- Periyodik kick görevi gerçek bir donmayı (ör. MFRC522 SPI'da `HAL_MAX_DELAY` ile kilitlenme) yakalama özelliğini bozmaz: RTC ISR'i sadece bir sequencer görevi bayrağı koyar, asıl `HAL_IWDG_Refresh()` çağrısı ancak sequencer bu görevi gerçekten çalıştırabilirse olur — donmuş bir sequencer bu görevi hiç çalıştıramaz, IWDG yine de resetler.
- `Core/Src/main.c`'deki `Error_Handler()`'a **bilerek** refresh eklenmedi — kurtarılamaz bir hata artık sessizce sonsuza dek asılı kalmak yerine IWDG tarafından yakalanıp resetlenir.
- **Kırılganlık notu:** CubeMX `.ioc` üzerinden "Generate Code" çalıştırılırsa (IWDG orada kapalı göründüğü için) `Core/Inc/stm32wlxx_hal_conf.h`'deki `HAL_IWDG_MODULE_ENABLED` satırı tekrar yorum satırına dönüp build'i kırabilir — regenerate edilecekse önce IWDG'yi `.ioc`'ta da işaretlemek gerekir.

## Zaman Senkronizasyonu

Cihaz saati, STM32CubeWL'in `SysTimeSet()`/`SysTimeGet()` (RTC yedek registerları, `RTC_BKP_DR0/DR1`) altyapısına dayanır — bu registerlar **watchdog/yazılımsal reset'lerde silinmez**, sadece gerçek güç kaybında sıfırlanır. Ayrıca "gerçekten en az bir kez senkron olundu mu" bilgisini ayrı bir yedek register'da (`RTC_BKP_DR3`, `lora_timesync.c`) tutar. Detay için `external_libs/lora_app_auxilary/Inc/lora_timesync.h`'deki kullanım sırası yorumuna bakın.

## Kalıcı Buffer / Offline Dayanıklılık

ACK alınamayan her RFID kaydı `PersistFailedRfidSend()` (`lora_app.c`) üzerinden `persistent_circular_buffer`'a (flash) yazılır. Ağ tekrar erişilebilir olduğunda (bir sonraki başarılı ACK, ya da rejoin) buffer en yeniden en eskiye doğru (LIFO) otomatik boşaltılır. Detaylar ve senaryo bazlı testler için `docs/test-plan-lora_app.md` bölüm A-D, K.

> **Bilinen davranış (bug değil):** Uygulama kodu sektörünü silmeden yeniden flaşlamak buffer'ı korur; **tam chip erase** (ör. reflash sırasında) PCB'nin sakladığı sayfaları da siler, bu yüzden `pcb_count()` reset sonrası 0 görünür — bu beklenen bir durumdur.

## Derleme

STM32CubeIDE projesi (`.cproject`/`.project`). `external_libs/` altındaki her modül `.cproject`'e elle eklenmiş include-path + source-path girdileriyle derlemeye dahil edilir (CubeMX'in bilmediği, dokunmadığı dosyalar). Yeni bir `external_libs/<modül>` eklerken aynı iki satırlık `.cproject` düzenlemesi (include path + sourcePath) gerekir — mevcut girişler örnek alınabilir.

## Dokümanlar

`docs/` klasörü konuya göre alt klasörlere ayrılmıştır:

**Protokol ve süreç (`docs/` kökü):**
- `docs/server-gelistirici-rehberi.md` — **sunucu geliştirici rehberi:** MQTT topic'leri, tüm uplink/downlink mesajlarının byte-seviyesi payload tabloları ve örnekleri, zaman senkron mekanizması, hata/red davranışları, buffer semantiği — kod referansı olmadan, saf protokol dokümantasyonu
- `docs/test-plan-lora_app.md` — senaryo bazlı test planı (buffer, RFID/status ACK zincirleri, rejoin, LPM, zaman senkronu, refactor regresyonu)
- `docs/Ürünleştirmeye Yönelik Eylem-Plani_UYGULANDI.md` — ürüne dönüşme yol haritası ve buradan çıkan somut eylem maddeleri (komut protokolü, boş basımda RX penceresi, STATUS interval komutu, buzzer/LED komutu, tarih aralığı sorgusu) — **tümü uygulandı** (madde 1-5 ✅); her madde için değişen dosyalar ve test önerileri; ayrıca üretime çıkmadan önce çözülmesi gereken kritik riskler (paylaşılan anahtar, RDP, OTA/provisioning eksikliği, vb.)
- `docs/LW_RFID_infografik.html` — ürünün çalışma prensibini özetleyen HTML infografik

**`docs/Hardware/`** — donanım referansları: BOM (`LOW_POWER_RFID_BOM.xlsx`), MFRC522/BC337/BS250P/NDS7002A-D datasheet'leri, güç tüketim ölçüm fotoğrafı (`power_test_lwrfid.png` — bkz. Güç Tüketim Testi bölümü)

**`docs/E5 Mini Module/`** — LoRa-E5 mini modülünün şematiği, özellik dokümanı ve pinout görseli

**`docs/stm32wle5/`** — STM32WLE5 MCU datasheet'i ve RM0461 referans kılavuzu

**`docs/Sorunlar/`** — sahada/geliştirmede karşılaşılan sorunların analiz kayıtları:
- `lorawan-devicetimereq-reference.md` — LoRaWAN DeviceTimeReq referansı
- `milesight-ug63-lorawan-version-mismatch.md` — Milesight UG63 network server ile gözlemlenen LoRaWAN sürüm uyuşmazlığı (üretici cevabı bekleniyor)
- `LoRaWAN RX Penceresi Sorunu.pdf`, `ack_alinamama_sorunu_ozet.txt` — RX penceresi/ACK alınamama sorunlarının log bazlı kök neden analizleri

**Diğer:**
- `external_libs/persistent_circular_buffer/README_TR.md` — kalıcı buffer'ın kendi detaylı dokümantasyonu

## Güç Tüketim Testi

Cihazın Stop2 modu ve periyodik uyanma döngüsüne dayalı güç tüketimi, `docs/LOW POWER LORAWAN RFID READER GÜÇ TESTİ v2.docx` içinde ölçüm sonuçlarıyla birlikte raporlanmıştır. Ölçüm düzeneğinin fotoğrafı `docs/Hardware/power_test_lwrfid.png` dosyasındadır.

## Bilinen Açık Konular

- **Flash senkron sırasında RX penceresi stall riski** — STM32WLE5 tek banklı flash (`stm32wlxx_hal_flash.h`: `FLASH_EraseInitTypeDef`'te bank alanı yok, 128×2KB düz sayfa uzayı, `FLASH_TIMEOUT_VALUE=1000` ms), yani bir sayfa erase/program sırasında CPU — interrupt dahil — tamamen duruyor. Mimari risk gerçek ve kalıcı, ama güncel koddaki karşılıklı dışlama bayrakları (`rfid_data_pending_on_lora`, `buffered_rfid_data_wait_for_ack`, `status_data_pending_on_lora`) aynı anda yalnızca tek bir confirmed uplink'e izin verdiği için, gözlemlenebilir bir tetikleyici yolu şu an kapalı görünüyor. Yoğun trafik altında istatistiksel doğrulama (ACK başarı oranı) yine de faydalı olur, ama düşük olasılıklı bir risk olarak değerlendirilmeli — bkz. `docs/test-plan-lora_app.md` bölüm I.
- LoRaWAN sürüm uyuşmazlığı (Milesight UG63) — üretici cevabı bekleniyor, bkz. `docs/Sorunlar/milesight-ug63-lorawan-version-mismatch.md`.



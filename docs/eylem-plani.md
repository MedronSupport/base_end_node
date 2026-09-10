# Eylem Planı — 2026-09-07'den İtibaren

Bu dosya, `docs/urun-yol-haritasi.md`'deki tartışmadan çıkan somut adımları takip eder. Her madde: **Amaç → Teknik notlar/riskler → Yapılacaklar → Durum**.

---

## 1. STATUS mesajını parçalı gönderip RX penceresini uzatma — ✅ UYGULANDI (2026-09-10)

**Amaç:** Bir STATUS turu sırasında birden fazla uplink göndererek, o an için daha fazla RX (downlink alma) fırsatı yaratmak.

**Uygulanan tasarım:**
- Mevcut STATUS mesajı (confirmed, 14 byte) **hiç değişmedi** — zaman senkron mekanizması dahil, sıfır risk.
- STATUS'un ACK sonucu (başarılı YA DA başarısız) belli olduktan **3 saniye sonra** (`STATUS_PART2_DELAY_MS`), yeni bir oneshot timer (`StatusPart2DelayTimer`) ikinci, hafif bir uplink tetikliyor: `SendStatusPart2Handler()`.
- İkinci parça **unconfirmed** (`LORAMAC_HANDLER_UNCONFIRMED_MSG`) — ACK beklemiyor, sadece TX+RX1/RX2 ile bir fırsat daha açıyor. Zaten dokümante edilmiş ama hiç kullanılmamış `LORA_RFID_MSG_TYPE_IND` (0x22) tipini kullanıyor.
- Payload minimal (6 byte): `[0]=uplink counter, [1]=tip(0x22), [2:5]=gönderim anı epoch`.
- Mevcut mutual-exclusion guard'larına uyuyor (`rfid_data_pending_on_lora`/`buffered_rfid_data_wait_for_ack`/`status_data_pending_on_lora` devam ederken atlanır) ve join kontrolü var.
- Hem ACK-başarılı hem ACK-başarısız dalından tetikleniyor (`OnTxData()`), böylece STATUS'un kendisi başarısız olsa bile ek bir fırsat kaçırılmıyor.

**Değişen dosyalar:** `Core/Inc/utilities_def.h` (yeni `CFG_SEQ_Task_StatusPart2Event`), `LoRaWAN/App/lora_app.c` (timer, binding, handler, iki tetikleme noktası).

**Bilinçli sınırlamalar (madde 2/3 ile tamamlanacak):** Bu, sadece STATUS turunun ANINDAKİ fırsat sayısını 1'den 2'ye çıkarıyor — STATUS turları arası (~1 saat) hâlâ sıfır fırsat var, en kötü durum gecikmesi değişmedi. Asıl gecikme iyileştirmesi madde 2 (boş basımda RX penceresi) ve madde 3'ten (komut protokolü) gelecek.

**Test edilmesi gereken:** Sahada bir STATUS turunda loglarda `"STATUS PART2 (IND) gonderildi"` satırının STATUS'un ACK/fail sonucundan ~3 sn sonra göründüğünü doğrula; duty-cycle/pil etkisini birkaç gün gözlemle.

---

## 2. Boş buton basımında da RX penceresi açma

**Amaç:** Kart okunamayan buton basımlarını da bir uplink fırsatına çevirmek (aktif kullanım saatlerinde komut gecikmesini azaltır, ekstra timer gerektirmez).

**Teknik notlar (önceki değerlendirmeden):**
- **Unconfirmed** mesaj olarak gönderilmeli — amaç ACK almak değil, sadece RX penceresi açmak; confirmed olursa gereksiz 80 sn'lik ACK kilidi (`RfidAckTimeoutTimer`) devreye girer.
- Mevcut mutual-exclusion guard'larına (status/buffer-drain devam ederken göndermeme) uymalı.
- Sahadaki gerçek "kart okunamadı" sıklığı önce mevcut UART loglarından çıkarılmalı — pil/duty-cycle maliyetini gerçek sayılarla tahmin etmek için.
- Doc yorumlarında bahsi geçen ama hiç implemente edilmemiş `0x22 IND` mesaj tipi bunun için doğal bir taşıyıcı olabilir — hem RX penceresi açar hem "okuyucu arızalı mı" diye ayrı bir tanı sinyali taşır.

**Yapılacaklar:**
- [ ] Mevcut loglardan "kart okunamadı" olayının günlük/saatlik sıklığını çıkar.
- [ ] `0x22 IND` payload formatını tanımla (muhtemelen sadece `[0]=tip, [1:4]=epoch` gibi minimal).
- [ ] `SendRFID_Data()`'nın "kart okunamadı, buffer boş" dalına unconfirmed gönderim ekle.

**Durum:** Onaylandı, tasarım detayları netleşince uygulanabilir.

---

## 3. Uzaktan komutla STATUS mesaj aralığını değiştirme (RTC yedek register'da kalıcı)

**Amaç:** Sunucudan bir komutla `STATUS_MSG_TIMEOUT`'u dinamik olarak değiştirebilmek, ve bu değeri reset'ler arası koruyabilmek.

**Teknik notlar:**
- Bu, `docs/urun-yol-haritasi.md` madde 1'deki (genel komut protokolü) ÖN KOŞULU — önce komut kanalı olmalı, sonra bu komut o kanaldan gelir.
- `StatusMessageTimeoutTimer` şu an `UTIL_TIMER_PERIODIC` olarak SADECE oluşturulma anında verilen periyotla çalışıyor (`lora_app.c:538`). Çalışırken periyodunu değiştirmek için `UTIL_TIMER` API'sinde bunu destekleyen bir fonksiyon var mı (`UTIL_TIMER_SetPeriod` benzeri) yoksa timer'ı `Stop` edip yeni `ReloadValue` ile yeniden `Create`/`Start` etmek mi gerekiyor — **doğrulanmalı**.
- Kalıcılık için `lora_timesync`'in kullandığı desenle aynı şekilde bir RTC yedek register'ı (örn. `RTC_BKP_DR4`, DR0-DR3 zaten kullanımda) kullanılabilir.
- **Güvenlik/sağlamlık sınırı şart:** komutla gelen değere min/max sınır konmalı (örn. 5 dk - 24 saat arası) — yoksa hatalı/kötü niyetli bir komut cihazı ya sürekli uplink göndererek pil tüketimine ("DoS") ya da hiç status göndermeyecek kadar seyrekleştirmeye zorlayabilir.

**Yapılacaklar:**
- [ ] `UTIL_TIMER` API'sinde çalışan bir periyodik timer'ın periyodunu değiştirme yolunu doğrula.
- [ ] Yeni RTC yedek register'ı ayır, `lora_timesync` deseniyle tutarlı bir modül/fonksiyon tasarla.
- [ ] Min/max sınırları belirle.
- [ ] Madde 1 (komut protokolü) tamamlanınca komut ID'si ata.

**Durum:** Madde 1'in (genel komut kanalı) tamamlanmasını bekliyor.

---

## 4. Uzaktan komutla buzzer/LED uyarı — MUTLAKA azami süre sınırlı

**Amaç:** Sunucudan bir komutla, cihazın buzzer'ını ve/veya LED'ini belirli bir süre/pattern ile (örn. bip bip) çalıştırmak.

**Teknik notlar:**
- Senin belirttiğin risk tamamen doğru ve kritik: **komutun kendi taşıdığı süre değerine güvenilmemeli**. Uygulama: cihaz, komuttaki süreyi uygular AMA asla belirlenmiş bir **donanımsal/yazılımsal azami sınırı** (örn. 30 sn) aşmaz — sunucu/yazılım hatası ya da kötü niyetli bir komut `duration=0xFFFFFFFF` gönderse bile cihaz en fazla o azami süre kadar öter, sonrasında KOŞULSUZ susar.
- Mevcut `BuzzerNotify_init()`/`_deinit()` (RFID okumalarında zaten kullanılıyor) bu işin altyapısını sağlıyor, üzerine inşa edilebilir.
- Bu işlem sırasında Stop moduna girilip girilmeyeceği (RfidPreventStopMode benzeri bir kilit gerekip gerekmediği) netleştirilmeli — büyük ihtimalle kısa süreli olduğu için mevcut RFID kilit deseniyle aynı yaklaşım yeterli.
- Azami süre sabiti, `AppWatchdog` timeout'undan (30 sn) kısa tutulmalı ki bu işlem sırasında bir başka görev IWDG'yi resetleyemez hale gelmesin (yani bu blok da "Stop moduna girilemez" pencerelerden biri olacak, watchdog marjını yeniden gözden geçirmek gerekebilir).

**Yapılacaklar:**
- [ ] Komut payload formatı: `[tip][komut ID][pattern][süre (ms, 2 byte)]`.
- [ ] Azami sınır sabiti belirle (örn. `MAX_BUZZER_LED_DURATION_MS = 30000`).
- [ ] `UTIL_TIMER_ONESHOT` ile "azami süre dolunca zorla kapat" güvenlik timer'ı tasarla — komutun kendi süresi ne olursa olsun bu timer her zaman kurulur.
- [ ] Madde 1 (komut protokolü) tamamlanınca komut ID'si ata.

**Durum:** Madde 1'i bekliyor; tasarım net, güvenlik sınırı önceden kararlaştırıldı.

---

## 5. Buffer'dan tarih aralığı sorgulama komutu

**Amaç:** Sunucudan "şu iki tarih arasındaki kayıtları gönder" komutu.

**Teknik notlar — iyi haber:**
- `persistent_circular_buffer.h`'de bu sorgu için gereken alt katman **zaten var**: `pcb_get_by_timestamp(handle, start_timestamp, end_timestamp, ...)`. Yani depolama tarafında sıfırdan bir şey yazmaya gerek yok.
- Eksik olan: (a) bu sorguyu tetikleyecek bir downlink komutu (madde 1'in parçası), (b) eşleşen (potansiyel olarak birden fazla) kaydı sunucuya GERİ göndermek için bir "raporlama" akışı — muhtemelen mevcut `SendBufferedRfidLogHandler`'ın gönderim mantığı (LIFO değil bu sefer tarih sırasıyla) yeniden kullanılabilir.
- Çok kayıt eşleşirse (örn. geniş bir tarih aralığı), bunları tek seferde değil, mevcut buffer-drain deseniyle (`BufferedDrainDelayTimer`, art arda göndermeyi engelleyen 10 sn bekleme) birer birer göndermek gerekir — pil/duty-cycle açısından bu akışın ne kadar süreceği önceden tahmin edilmeli.

**Yapılacaklar:**
- [ ] Komut payload formatı: `[tip][komut ID][start_timestamp][end_timestamp]`.
- [ ] `pcb_get_by_timestamp` sonuçlarını sırayla gönderecek bir "rapor modu" state machine'i tasarla (mevcut buffer-drain akışından türetilebilir).
- [ ] Çok sonuçlu sorgularda toplam süre/pil maliyetini tahmin et, gerekirse üst sınır koy (örn. "en fazla N kayıt bir seferde raporlanır").
- [ ] Madde 1 (komut protokolü) tamamlanınca komut ID'si ata.

**Durum:** Madde 1'i bekliyor; depolama alt yapısı zaten hazır olduğu için görece düşük riskli.

---

## Genel Bağımlılık Sırası

```
Madde 1 (genel komut protokolü, urun-yol-haritasi.md'den)
   ├── Madde 3 (status interval komutu)
   ├── Madde 4 (buzzer/LED komutu)
   └── Madde 5 (tarih aralığı sorgu komutu)

Madde 2 (boş basımda RX penceresi) — bağımsız, hemen başlanabilir
Madde 1'deki "STATUS'u parçalama" — bağımsız ama düşük öncelik (madde 2/3 daha etkili)
```

**Önerilen başlangıç sırası:** Önce **genel komut protokolü** (madde 1'in temeli), çünkü 3/4/5 hepsi buna bağımlı. Paralel olarak **madde 2** (bağımsız, düşük risk) başlanabilir.

> **Not:** Flash yazım sıklığını azaltma (pcb_sync) maddesi bilerek bu plandan çıkarıldı — şimdilik üzerinde değişiklik yapılmayacak.

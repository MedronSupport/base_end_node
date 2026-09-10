# Eylem Planı — 2026-09-07'den İtibaren

Bu dosya, `docs/urun-yol-haritasi.md`'deki tartışmadan çıkan somut adımları takip eder. Her madde: **Amaç → Teknik notlar/riskler → Yapılacaklar → Durum**.

---

## 1. STATUS mesajını parçalı gönderip RX penceresini uzatma — ✅ UYGULANDI (2026-09-10)

**Amaç:** Bir STATUS turu sırasında birden fazla uplink göndererek, o an için daha fazla RX (downlink alma) fırsatı yaratmak.

**Uygulanan tasarım:**
- Mevcut STATUS mesajı (confirmed, 14 byte) **hiç değişmedi** — zaman senkron mekanizması dahil, sıfır risk.
- STATUS'un ACK sonucu (başarılı YA DA başarısız) belli olduktan **3 saniye sonra** (`IND_PING_DELAY_MS`), yeni bir oneshot timer (`IndPingDelayTimer`) ikinci, hafif bir uplink tetikliyor: `SendIndPingHandler()` (madde 2'de bu mekanizma genelleştirilip ikinci bir tetikleyiciyle de paylaşıldı, aşağıya bkz.).
- İkinci parça **unconfirmed** (`LORAMAC_HANDLER_UNCONFIRMED_MSG`) — ACK beklemiyor, sadece TX+RX1/RX2 ile bir fırsat daha açıyor. Zaten dokümante edilmiş ama hiç kullanılmamış `LORA_RFID_MSG_TYPE_IND` (0x22) tipini kullanıyor.
- Payload minimal (6 byte): `[0]=uplink counter, [1]=tip(0x22), [2:5]=gönderim anı epoch`.
- Mevcut mutual-exclusion guard'larına uyuyor (`rfid_data_pending_on_lora`/`buffered_rfid_data_wait_for_ack`/`status_data_pending_on_lora` devam ederken atlanır) ve join kontrolü var.
- Hem ACK-başarılı hem ACK-başarısız dalından tetikleniyor (`OnTxData()`), böylece STATUS'un kendisi başarısız olsa bile ek bir fırsat kaçırılmıyor.

**Değişen dosyalar:** `Core/Inc/utilities_def.h` (yeni `CFG_SEQ_Task_IndPingEvent`), `LoRaWAN/App/lora_app.c` (timer, binding, handler, iki tetikleme noktası).

**Bilinçli sınırlamalar (madde 2/3 ile tamamlanacak):** Bu, sadece STATUS turunun ANINDAKİ fırsat sayısını 1'den 2'ye çıkarıyor — STATUS turları arası (~1 saat) hâlâ sıfır fırsat var, en kötü durum gecikmesi değişmedi. Asıl gecikme iyileştirmesi madde 2 (boş basımda RX penceresi) ve madde 3'ten (komut protokolü) gelecek.

**Test edilmesi gereken:** Sahada bir STATUS turunda loglarda `"IND ping kuyruga alindi"` satırının STATUS'un ACK/fail sonucundan ~3 sn sonra göründüğünü doğrula; duty-cycle/pil etkisini birkaç gün gözlemle.

---

## 2. Boş buton basımında da RX penceresi açma — ✅ UYGULANDI (2026-09-10)

**Amaç:** Kart okunamayan buton basımlarını da bir uplink fırsatına çevirmek (aktif kullanım saatlerinde komut gecikmesini azaltır, ekstra timer gerektirmez).

**Uygulanan tasarım — madde 1'in altyapısıyla birleştirildi:**
- Madde 1 için yazılan `SendIndPingHandler()`/`IndPingDelayTimer` mekanizması genel bir "RX penceresi açmak için hafif IND ping'i" altyapısına dönüştürüldü (isimler `SendStatusPart2Handler` → `SendIndPingHandler`, `StatusPart2DelayTimer` → `IndPingDelayTimer` olarak genelleştirildi) — iki ayrı olay aynı mekanizmayı paylaşıyor.
- `SendRFID_Data()`'nın "kart okunamadı" dalına, buffer kontrolünü tetikleyen satırın hemen ardına `UTIL_TIMER_Start(&IndPingDelayTimer);` eklendi.
- **Çakışma yönetimi bilinçli tasarım:** Buffer'da gönderilecek bir kayıt VARSA, `SendBufferedRfidLogHandler` zaten bir gönderim başlatıp `buffered_rfid_data_wait_for_ack`'i true yapıyor — 3 sn sonra çalışan IND ping bunu görüp kendiliğinden atlanıyor (mutual-exclusion guard'ı zaten böyle tasarlanmıştı). Buffer BOŞSA (asıl hedeflenen senaryo), hiçbir şey çakışmıyor ve IND ping gerçekten gönderiliyor.
- `0x22 IND` mesaj tipi kullanıldı — madde 1'deki ile aynı 6 byte'lık minimal payload (`[0]=counter, [1]=0x22, [2:5]=epoch`).

**Değişen dosyalar:** `LoRaWAN/App/lora_app.c` (`SendRFID_Data()`'ya 1 satır + yorum, ayrıca madde 1'in isimlendirmesi genelleştirildi).

**Ertelenen/yapılmayan:** "Kart okunamadı" olayının gerçek sıklığını loglardan çıkarıp pil maliyetini ölçme adımı — bu, sahadan yeni loglar geldikçe ayrıca değerlendirilecek, şimdilik mekanizma zaten var olan mutual-exclusion ile kendini sınırlıyor (aynı anda sadece bir IND ping denemesi olabilir, buffer/RFID/status meşgulken atlanıyor).

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

## 4. Uzaktan komutla buzzer/LED uyarı — MUTLAKA azami süre sınırlı — ✅ UYGULANDI (2026-09-10)

**Amaç:** Sunucudan bir komutla, cihazın buzzer'ını ve/veya LED'ini belirli bir süre/pattern ile (örn. bip bip) çalıştırmak.

**Uygulanan tasarım:**
- **Genel komut protokolü de bu maddeyle birlikte, minimal şekilde kuruldu** (madde 1/3/5'in de üzerine oturacağı temel): `LORA_COMMAND_DOWNLINK_TYPE=0x02`, `[0]=0x02, [1]=komut ID, [2:N]=parametreler`. `OnRxData()`'da zaman senkron kontrolü `false` dönerse `LoraCommand_HandleDownlink()`'e düşüyor.
- Buzzer/LED komutu (`LORA_CMD_ID_BUZZER_LED=0x01`, 6 byte): `[2]=hedef (bit0:buzzer, bit1:led), [3]=pattern (0:sürekli, 1:bip-bip), [4:5]=istenen süre (ms, büyük-endian)`.
- **MUTLAK sınır garantisi:** `effectiveMs = min(istenen_sure, MAX_BUZZER_LED_DURATION_MS)` — bu hesap HER ZAMAN yapılıyor, istenen değer ne olursa olsun (0xFFFF dahil). Güvenlik timer'ı (`BuzzerLedSafetyTimer`) HER komutta, İSTENEN değil UYGULANAN (sınırlanmış) süreyle kuruluyor — komutun kendi mantığı bypass edilse bile bu son kontrol atlanamaz.
- `MAX_BUZZER_LED_DURATION_MS = 15000` (15 sn) seçildi — hem watchdog timeout'undan (30 sn) hem kick periyodundan (20 sn) rahat marjlı, hem de **bloklamayan bir tasarımla** birleşince bu marj daha da rahat (aşağıya bkz).
- **Bloklamayan tasarım (kritik karar):** Mevcut `Buzzer_Alert_Process(ms)` fonksiyonu `HAL_Delay` ile BLOKLUYOR — 15 saniyelik bir komut için bu, sequencer'ı o kadar süre kilitler ve watchdog kick görevinin çalışmasını engelleyip cihazın KENDİ KENDİNİ resetlemesine yol açabilirdi. Bunun yerine buzzer/LED pini `UTIL_TIMER` ile yönetiliyor: sürekli pattern'de pin sabit açık kalıyor (GPIO çıkışı Stop2'de de korunuyor, CPU'nun uyanık kalmasına bile gerek yok), bip-bip pattern'inde 300 ms'lik periyodik bir timer pini değiştiriyor — sequencer hiçbir zaman bloklanmıyor, watchdog'la sıfır etkileşim riski.
- RFID okuma/gönderimiyle aynı GPIO'ları (buzzer/awake LED) paylaştığı için, tam olarak aynı anda bir RFID okuması da oluyorsa küçük bir görsel/işitsel çakışma olabilir — kritik değil, bilinçli olarak ele alınmadı (dokümante edildi).

**Değişen dosyalar:** `LoRaWAN/App/lora_app.h` (yeni komut sabitleri), `Core/Inc/utilities_def.h` (2 yeni görev), `LoRaWAN/App/lora_app.c` (2 timer, 2 binding, 4 fonksiyon, `OnRxData()` bağlantısı).

**Test edilmesi gereken:** Sunucudan `[0x02, 0x01, hedef, pattern, süre_MSB, süre_LSB]` formatında bir downlink gönderip loglarda `"Buzzer/LED komutu alindi"` ve süresi dolunca `"Buzzer/LED komutu suresi doldu"` satırlarını doğrula; özellikle `istenen > 15000` gönderip `uygulanan` alanının hep 15000'de sabit kaldığını teyit et.

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

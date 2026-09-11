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

## 3. Uzaktan komutla STATUS mesaj aralığını değiştirme (RTC yedek register'da kalıcı) — ✅ UYGULANDI (2026-09-11)

**Amaç:** Sunucudan bir komutla `STATUS_MSG_TIMEOUT`'u dinamik olarak değiştirebilmek, ve bu değeri reset'ler arası koruyabilmek.

**Uygulanan tasarım:**
- **Komut** (`LORA_CMD_ID_SET_STATUS_INTERVAL=0x03`, 4 byte): `[0]=0x02 [1]=0x03 [2:4]=carpan (2 byte, büyük-endian)`. Gerçek aralık = **çarpan × 30 sn** — çarpan yaklaşımı hem payload'ı küçültüyor (4 byte ham saniye yerine 2 byte) hem de "ondalıklı/kesirli değer" sınıfını yapısal olarak imkânsız kılıyor (tamsayı alan).
- **Sınırlar:** çarpan `[1, 2880]` (30 sn - 24 saat). Aralık dışı HER değer (0 dahil) **reddedilir** — kırpma yok, mevcut ayar değişmeden kalır, sadece UART'a loglanır.
- **`UTIL_TIMER` azami süre kontrolü — uygulamadan önce doğrulandı:** `timer_if.c`'deki gerçek dönüşüm formülü (`RTC_N_PREDIV_S=10`, 1024 tik/sn) ile hesaplandı — 24 saat ≈ 88,47 milyon tik, `uint32_t` sınırının (~4,29 milyar) sadece %2'si; gerçek azami temsil edilebilir süre ~48,5 gün. 24 saatlik sınır bu tavandan ~48 kat uzakta, taşma riski yok.
- **Çalışma zamanı uygulama:** Buzzer komutunda kanıtlanmış desen — `UTIL_TIMER_Stop(&StatusMessageTimeoutTimer)` → `UTIL_TIMER_Create` (yeni periyot) → `UTIL_TIMER_Start`. Bir sonraki STATUS, komutun geldiği andan itibaren yeni süre kadar sonra gelir.
- **Kalıcılık:** Çarpan değeri (saniyeye çevrilmiş hali değil, doğrudan çarpan) **RTC yedek register DR4**'e yazılıyor (DR0-DR3 zaten kullanımda, DR4'ün boş olduğu grep ile doğrulandı). Açılışta DR4 okunur; `[1,2880]` aralığında değilse (ilk açılışta fabrika sıfırı olduğu için otomatik geçersiz) varsayılan **120** (=1 saat) kullanılır — ayrı bir "ayarlandı mı" bayrağına gerek kalmadı.

**Değişen dosyalar:** `LoRaWAN/App/lora_app.h` (yeni komut sabiti), `LoRaWAN/App/lora_app.c` (`rtc.h`/`hrtc` eklendi, 4 sabit, 2 yardımcı fonksiyon, komut dispatch dalı, `LoRaWAN_Init()`'te kalıcı değeri yükleme), `docs/test_codes/lw_lora_rif_decoder.py` (`interval` komutu — sunucu tarafında da aynı sınırlarla ön-doğrulama yapıp geçersiz komutu hiç göndermiyor).

**Test edilmesi gereken:** `interval 3600` gibi geçerli bir değer gönderip loglarda `"Status araligi degistirildi: carpan=120 -> 3600 sn"` satırını doğrula; ardından cihazı resetleyip (watchdog ya da manuel) STATUS turlarının hâlâ yeni aralıkta geldiğini teyit et (kalıcılık testi). `interval 90000` gibi sınır dışı bir değer de göndererek `"REDDEDILDI"` logunu ve mevcut ayarın değişmediğini doğrula.

---

## 4. Uzaktan komutla buzzer/LED uyarı — MUTLAKA azami süre sınırlı — ✅ UYGULANDI (2026-09-10)

**Amaç:** Sunucudan bir komutla, cihazın buzzer'ını ve/veya LED'ini belirli bir süre/pattern ile (örn. bip bip) çalıştırmak.

**Uygulanan tasarım:**
- **Genel komut protokolü de bu maddeyle birlikte, minimal şekilde kuruldu** (madde 1/3/5'in de üzerine oturacağı temel): `LORA_COMMAND_DOWNLINK_TYPE=0x02`, `[0]=0x02, [1]=komut ID, [2:N]=parametreler`. `OnRxData()`'da zaman senkron kontrolü `false` dönerse `LoraCommand_HandleDownlink()`'e düşüyor.
- Buzzer/LED komutu (`LORA_CMD_ID_BUZZER_LED=0x01`, 6 byte): `[2]=hedef (bit0:buzzer, bit1:led), [3]=pattern (0:sürekli, 1:bip-bip), [4:5]=istenen süre (SANİYE, büyük-endian)`.
- **Süre birimi bilerek SANİYE, ms değil:** İlk tasarımda ms kullanılmıştı ama 2 byte'lık alana ms cinsinden sığan azami değer ~65,5 saniye — "kayıp cihaz bulma" senaryosu (kullanıcı talebiyle 3 dakikaya çıkarıldı) için yetersiz kalıyordu. Saniyeye geçilince aynı 2 byte ile ~18,2 saate kadar yer açıldı.
- **MUTLAK sınır garantisi:** `effectiveMs = min(istenen_sure_sn × 1000, MAX_BUZZER_LED_DURATION_MS)` — bu hesap HER ZAMAN yapılıyor, istenen değer ne olursa olsun (0xFFFF saniye dahil). Güvenlik timer'ı (`BuzzerLedSafetyTimer`) HER komutta, İSTENEN değil UYGULANAN (sınırlanmış) süreyle kuruluyor — komutun kendi mantığı bypass edilse bile bu son kontrol atlanamaz.
- `MAX_BUZZER_LED_DURATION_MS = 180000` (**3 dakika**) — kullanıcı talebiyle güncellendi: amaç, kayıp/kaybolmuş bir cihazı sesle bulabilmek. Bu değer **watchdog timeout'uyla (30 sn) SINIRLI DEĞİL** — aşağıdaki bloklamayan tasarım sayesinde süre uzunluğunun watchdog'la hiçbir etkileşimi yok.
- **Bloklamayan tasarım (kritik karar, 3 dakikayı güvenli kılan asıl sebep):** Mevcut `Buzzer_Alert_Process(ms)` fonksiyonu `HAL_Delay` ile BLOKLUYOR — 3 dakikalık bir komut için bunu kullansaydık sequencer o kadar süre kilitlenir, watchdog kick görevi çalışamaz, cihaz KENDİ KENDİNİ resetlerdi. Bunun yerine buzzer/LED pini `UTIL_TIMER` ile yönetiliyor: sürekli pattern'de pin sabit açık kalıyor (GPIO çıkışı Stop2'de de korunuyor, CPU'nun uyanık kalmasına bile gerek yok), bip-bip pattern'inde 300 ms'lik periyodik bir timer pini değiştiriyor — sequencer hiçbir zaman bloklanmıyor, süre ne kadar uzun olursa olsun watchdog'la sıfır etkileşim riski.
- RFID okuma/gönderimiyle aynı GPIO'ları (buzzer/awake LED) paylaştığı için, tam olarak aynı anda bir RFID okuması da oluyorsa küçük bir görsel/işitsel çakışma olabilir — kritik değil, bilinçli olarak ele alınmadı (dokümante edildi).

**Değişen dosyalar:** `LoRaWAN/App/lora_app.h` (yeni komut sabitleri), `Core/Inc/utilities_def.h` (2 yeni görev), `LoRaWAN/App/lora_app.c` (2 timer, 2 binding, 4 fonksiyon, `OnRxData()` bağlantısı), `docs/test_codes/lw_lora_rif_decoder.py` (interaktif `buzzer` komutu, saniye birimine güncellendi).

**Test edilmesi gereken:** Sunucudan `[0x02, 0x01, hedef, pattern, süre_sn_MSB, süre_sn_LSB]` formatında bir downlink gönderip loglarda `"Buzzer/LED komutu alindi"` ve süresi dolunca `"Buzzer/LED komutu suresi doldu"` satırlarını doğrula; özellikle `istenen > 180` (saniye) gönderip `uygulanan` alanının hep `180000 ms`'de sabit kaldığını teyit et.

---

## 5. Buffer'dan tarih aralığı sorgulama komutu — ✅ UYGULANDI (2026-09-11)

**Amaç:** Sunucudan "şu iki tarih arasındaki kayıtları gönder" komutu.

**Uygulanan tasarım:**
- **Komut** (`LORA_CMD_ID_QUERY_BY_DATE=0x02`, 10 byte): `[0]=0x02 [1]=0x02 [2:6]=start_timestamp [6:10]=end_timestamp` (ikisi de büyük-endian uint32).
- **Depolama:** `pcb_get_by_timestamp()` zaten hazırdı — tek çağrıda tüm eşleşen kayıtlar (`QUERY_MAX_RESULTS=30` kapasiteli, RAM'de 480 byte, 96KB'lik bütçede önemsiz) statik bir diziye çekiliyor; `PCB_TRUNCATED` dönerse not ediliyor.
- **Raporlama — DR'ye duyarlı, çoklu-kayıt/mesaj (batching):** Önceki "1 kayıt = 1 mesaj" fikri yerine, `LmHandlerGetTxDatarate()` ile o anki DR öğrenilip (EU868, `RegionEU868.h`'deki gerçek tablo: DR0-2→51 byte, DR3→115 byte, DR4+→242 byte) her mesaja **sığdığı kadar** kayıt paketleniyor — kötü sinyalde bile birkaç kayıt, iyi sinyalde 15+ kayıt tek mesajda gidebiliyor. Basitlik için batch hesabı hep en kötü durumu (7-byte UID, 12 byte/kayıt) varsayıyor.
- **Kayıt kodlaması — ayraçsız, kendi kendini sınırlayan (TLV):** `[uuid_uzunluk(1)][uuid(4/7 byte)][timestamp(4 byte BE)]` — uzunluk ön eki sayesinde ayraç byte'ına gerek yok.
- **Yeni uplink tipi** (`LORA_RFID_MSG_TYPE_QUERY_RESULT=0x46`): mevcut `LIVE_UID` (0x45) tipinden bilerek ayrı — sunucu bunu "yeni bir olay" değil "geçmişten tekrar raporlanan kayıt" olarak ayırt edebilsin diye. Header: `[2]=bu mesajdaki kayıt, [3]=şu ana kadar toplam, [4]=batch index, [5]=kırpıldı mı`.
- **0 sonuç:** Özel bir dal gerekmedi — paketleme döngüsü doğal olarak 0 kayıt paketler, `[2]=0,[3]=0` içeren tek bir header mesajı gider, sunucu sessizce beklemez.
- **Eşzamanlılık:** `g_queryInProgress` bayrağı — rapor sürerken yeni bir sorgu komutu reddediliyor; mevcut RFID/status/buffer-drain guard'larına da uyuyor (meşgulse rapor ERTELENİYOR, kaybedilmiyor — STATUS/RFID'nin aksine bu veri kritik kabul edildi).
- **Zincirleme:** `QueryReportDelayTimer` (10 sn, `BufferedDrainDelayTimer` ile aynı desen) her batch sonrası kendini yeniden başlatıp bir sonraki batch'i tetikliyor, ta ki tüm kayıtlar gidene kadar.

**Değişen dosyalar:** `LoRaWAN/App/lora_app.h` (yeni komut/mesaj tipi sabitleri), `Core/Inc/utilities_def.h` (1 yeni görev), `LoRaWAN/App/lora_app.c` (1 timer, 1 binding, durum değişkenleri, 3 fonksiyon: `GetMaxAppPayloadForCurrentDR`, `SendQueryBatchHandler`, komut dispatch dalı), `docs/test_codes/lw_lora_rif_decoder.py` (`query` komutu + 0x46 çözümleme + `_fmt_epoch` ortak yardımcıya çıkarıldı).

**Test edilmesi gereken:** `query <start> <end>` ile bir sorgu gönderip loglarda `"Tarih araligi sorgusu alindi"`, ardından `"Sorgu raporu batch #0: N kayit..."` satırlarını doğrula; Python tarafında her batch'in `📦 QUERY_RESULT` olarak doğru ayrıştırıldığını, `total_so_far` değerinin batch'ler arasında doğru arttığını kontrol et. Geniş bir aralık isteyip `PCB_TRUNCATED` durumunu (kırpıldı uyarısı) da bir kez tetiklemek faydalı olur.

---

## Genel Durum

**Madde 1, 2, 3, 4, 5 — hepsi uygulandı.** Genel komut protokolü (`LORA_COMMAND_DOWNLINK_TYPE=0x02`, `LoraCommand_HandleDownlink()`) artık 3 komutu destekliyor: `0x01` buzzer/LED, `0x02` tarih aralığı sorgusu, `0x03` STATUS aralığı değiştirme. Python test script'i (`docs/test_codes/lw_lora_rif_decoder.py`) her üçü için de interaktif komutlar (`buzzer`, `query`, `interval`) ve karşılık gelen uplink çözümleme desteği içeriyor.

> **Not:** Flash yazım sıklığını azaltma (pcb_sync) maddesi bilerek bu plandan çıkarıldı — şimdilik üzerinde değişiklik yapılmayacak.

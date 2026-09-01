# Test Planı — `lora_app.c` LoRaWAN/RFID Buffer Akışı

**Tarih:** 2026-08-28
**Kapsam:** Bu oturumda geliştirilen/düzeltilen özellikler (buffered resend zinciri, ACK-hata yönetimi, otomatik rejoin mekanizmaları, LPM kilit ayrımı, volatile/dead-code düzeltmeleri) ve derin kod incelemesinde bulunan noktalar.
**Ortam notu:** EU868, Milesight UG63 private network server (LoRaWAN 1.0.2 rev B profili — bkz. `docs/milesight-ug63-lorawan-version-mismatch.md`).

Her test: **Ön Koşul → Adımlar → Beklenen Sonuç**. UART log çıktısı (`APP_LOG`) ana doğrulama aracı; bazı testler için gateway'in web arayüzü/network server logu da gerekiyor.

---

## A. Persistent Buffer Temel Davranışı

### A1 — Buffer'a kayıt ekleme ve `pcb_count` doğruluğu
- **Ön koşul:** Cihaz joined, buffer boş (`pcb_count(&eventBuffer) == 0`).
- **Adımlar:** Gateway'i geçici kapat → bir kart okut → `SendRFID_Data` ACK alamayacak.
- **Beklenen:** `"###### RFID ACK ALINAMADI, depoya yaziliyor"` logu + `"Record added to RAM. ID: X, ... Count: 1"` logu. `pcb_count` bir artmış olmalı.

### A2 — Flash reset sonrası kayıtların korunması
- **Ön koşul:** Buffer'da en az 1 `FAILED` kayıt var, `pcb_sync` başarıyla çalışmış.
- **Adımlar:** Cihazı **sadece uygulama kodu sektörünü** silerek yeniden flaşla (PCB'nin Page A/Page B bölgesine dokunmadan) → resetle.
- **Beklenen:** Açılışta `pcb_init` aynı kayıtları geri yükler, `SendBufferedRfidLogHandler` ilk tetiklendiğinde bu kayıtları bulur. *(Not: tam chip erase yaparsan kayıtlar kaybolur — bu beklenen davranış, bug değil.)*

---

## B. RFID Canlı Gönderim → Buffer'a Düşme Yolları

### B1 — Join yokken kart okutma
- **Adımlar:** Cihaz join olmamışken kart okut.
- **Beklenen:** `"Henuz join olunmadi, gonderim yerine rejoin deneniyor"` + kayıt FAILED olarak buffera yazılır + `CFG_SEQ_Task_LoRaRejoinEvent` tetiklenir.

### B2 — Duty-cycle kısıtlıyken kart okutma
- **Adımlar:** Kısa aralıklarla art arda birçok kart okutarak duty-cycle limitini zorla, limit aşılınca bir kart daha okut.
- **Beklenen:** `"Next Tx in : ~X second(s)"` logu + kayıt FAILED olarak buffera yazılır, `rfid_data_pending_on_lora=false` kalır.

### B3 — Gönderim kabul edilir ama ACK gelmez (NACK)
- **Adımlar:** Gateway'i kapat, kart okut. `LmHandlerSend` SUCCESS döner ama `OnTxData` `AckReceived=0` ile çağrılır.
- **Beklenen:** `"###### RFID ACK ALINAMADI, depoya yaziliyor"` + kayıt buffera yazılır + `un_successfull_ack_response` bir artar.

### B4 — ACK hiç gelmez, 80 sn timeout devreye girer
- **Adımlar:** B3 senaryosunda `OnTxData` callback'inin **hiç gelmediği** bir durum simüle et (mümkünse) veya gerçek şartlarda 80 sn bekle.
- **Beklenen:** `RfidAckTimeoutHandler` çalışır, `"###### RFID ACK timeout - OnTxData gelmedi"` logu, kayıt buffera yazılır, `rfid_data_pending_on_lora=false` olur.

### B5 — RFID okuma sırasında önceki gönderim hâlâ ACK bekliyorsa
- **Adımlar:** Bir kart okut, ACK gelmeden (80 sn içinde) tekrar butona bas.
- **Beklenen:** `"###### Onceki RFID gonderimi hala ACK bekliyor, yeni okuma reddedildi"` — okuma başlamaz.

---

## C. Karşılıklı Dışlama (Mutual Exclusion) Guard'ları — bu oturumun 8. madde düzeltmesi

### C1 — Status ACK bekliyorken kart okutma
- **Ön koşul:** `status_data_pending_on_lora == true` (saatlik status mesajı gönderilmiş, ACK bekleniyor — test için `STATUS_MSG_TIMEOUT`'u geçici olarak küçültüp tetikleyebilirsin).
- **Adımlar:** Tam bu pencerede bir kart okut.
- **Beklenen:** `SendRFID_Data`, `LmHandlerSend`'i **hiç çağırmadan** kaydı direkt FAILED olarak buffera yazıp `return` eder — ikinci bir confirmed mesaj denenmemeli (log akışında iki ayrı `LmHandlerSend`/`SEND REQUEST` görülmemeli).

### C2 — RFID ACK bekliyorken buffer drain tetiklenmesi
- **Ön koşul:** `rfid_data_pending_on_lora == true`.
- **Adımlar:** Bu sırada `SendBufferedRfidLogHandler`'ın tetiklenmesine yol açacak bir olay simüle et (örn. eş zamanlı bir `OnTxData` başarı olayı başka bir yoldan).
- **Beklenen:** `"###### Baska bir gonderim ACK bekliyor, buffer denemesi ertelendi"` logu — buffer denemesi o an yapılmaz, ertelenir.

### C3 — Status ACK bekliyorken buffer drain tetiklenmesi
- **Adımlar:** `status_data_pending_on_lora == true` iken `SendBufferedRfidLogHandler` tetiklenmeye çalışılsın.
- **Beklenen:** Aynı erteleme logu, `LmHandlerSend` çağrılmaz.

---

## D. Buffered Resend (Backlog Drain) Zinciri

### D1 — Tek kayıtlı buffer'ın başarıyla drain edilmesi
- **Ön koşul:** Buffer'da 1 `FAILED` kayıt var, gateway şimdi ulaşılabilir.
- **Adımlar:** Herhangi bir confirmed mesaj (RFID/status) ACK alsın → zincir tetiklensin.
- **Beklenen:** `SendBufferedRfidLogHandler` çalışır → `pcb_get_latest_by_status` `PCB_OK` döner → gönderim yapılır → ACK gelince `pcb_set_status_by_id(...,SENT)` + `pcb_sync` başarılı olur → `"ALL DATA STATUS IS SENT"` logu (buffer'da başka FAILED kalmadığı için).

### D2 — Çoklu kayıtlı buffer'ın LIFO sırayla drain edilmesi
- **Ön koşul:** Buffer'da 3+ `FAILED` kayıt var (farklı kartlar okutularak oluşturulmuş).
- **Adımlar:** Zinciri tetikle, tüm ACK'lerin geldiğini varsay.
- **Beklenen:** Kayıtlar **en yeniden en eskiye** doğru gönderilir (`recordid` logları azalan sırada görünmeli — örn. 8,7,6,5...), her biri ACK alınca `SENT` işaretlenip bir sonrakine geçilir, buffer boşalana kadar zincir kendini tekrar tetikler.

### D3 — Buffered gönderim NACK alırsa
- **Adımlar:** D1 senaryosunda gönderim kabul edilir ama ACK gelmez.
- **Beklenen:** `buffered_rfid_data_wait_for_ack=false` olur, `un_successfull_ack_response` artar, kayıt buffer'da FAILED olarak kalır (SENT işaretlenmemiş) — bir sonraki fırsatta tekrar denenebilir olmalı.

### D4 — Buffered gönderim 80 sn içinde hiç ACK/NACK almazsa
- **Adımlar:** `OnTxData` callback'i hiç gelmesin.
- **Beklenen:** `BufferAckTimeoutHandler` çalışır, `"###### Buffer ACK timeout - OnTxData gelmedi"` logu, `buffered_rfid_data_wait_for_ack=false` olur — kayıt buffer'da kalır, sistem kilitlenmez.

### D5 — Status ACK alınca da drain tetiklenmeli (bu oturumda eklendi)
- **Ön koşul:** Buffer'da en az 1 FAILED kayıt var.
- **Adımlar:** Saatlik status mesajının ACK almasını sağla (kart okutmadan).
- **Beklenen:** Status ACK'i sonrası `SendBufferedRfidLogHandler` otomatik tetiklenmeli (önceden bu eksikti, eklendi) — status başarısının hemen ardından buffer drain loglarını görmelisin.

---

## E. Status Mesajı Retry Zinciri

### E1 — Status ACK alınamazsa 15 sn sonra tekrar denenmesi
- **Adımlar:** Gateway kapalıyken saatlik status mesajını tetikle (test için `STATUS_MSG_TIMEOUT`'u küçültebilirsin).
- **Beklenen:** NACK sonrası `RetryStatusTimer` kurulur, ~15 sn sonra `OnStatusMessageHandler` tekrar çalışır, güncel batarya/sıcaklık verisiyle yeni bir deneme yapar (eski veri değil).

### E2 — 5 art arda status/RFID hatası sonrası zorla rejoin
- **Adımlar:** Gateway'i kapalı tutup E1'in 5 kez art arda başarısız olmasını sağla (~75 sn).
- **Beklenen:** `un_successfull_ack_response` 5'e ulaşınca sıfırlanır, `JoinRetryCount=0` + `CFG_SEQ_Task_LoRaRejoinEvent` tetiklenir.

### E3 — Status ACK başarılı olunca sayaç sıfırlanmalı
- **Adımlar:** Art arda 2-3 status hatası oluştur, sonra gateway'i aç ve bir status/RFID gönderiminin ACK almasını sağla, ardından tekrar 2-3 hata oluştur.
- **Beklenen:** Sayaç aradaki başarıda sıfırlanmalı — ardışık olmayan toplam 5 hataya ulaşsa bile (aralarda başarı varsa) rejoin **tetiklenmemeli**. *(Bu, ACK-başarı dalındaki `un_successfull_ack_response=0;` satırının doğrulanmasıdır.)*

### E4 — Join yokken status kontrolü
- **Adımlar:** Cihazı join olmamış duruma getir (örn. yanlış anahtarlarla geçici test), saatlik status'u tetikle.
- **Beklenen:** `"###### Status mesaji icin join yok, gonderim atlandi"` + anlık rejoin tetiklenir. `RetryStatusTimer` bu dalda **kurulmamalı** (bilinçli tasarım kararı — sadece eski `JoinRetryTimer` 3x cascade'i devreye girmeli).

---

## F. Otomatik Rejoin Mekanizmaları

### F1 — Join başarısız olunca 3 kere, 3 sn arayla otomatik deneme
- **Adımlar:** Cihazı resetle, gateway kapalıyken ilk join'in başarısız olmasını izle.
- **Beklenen:** `"Rejoin deneme 1/3, 3 sn sonra"` → `"2/3"` → `"3/3"` logları ~3 sn arayla görülür, sonra `"Rejoin denemeleri tukendi..."` logu ile durur (sürekli tekrarlanan bir fırtınaya dönüşmemeli).

### F2 — Join başarılı olunca otomatik status tetiklenmesi (bu oturumda eklendi)
- **Adımlar:** Cihazı resetle veya bir rejoin'in başarılı olmasını sağla.
- **Beklenen:** `"###### = JOINED ="` logundan hemen sonra `OnStatusMessageHandler` tetiklenmeli (yeni eklenen `UTIL_SEQ_SetTask(CFG_SEQ_Task_StatusMSGEvent,...)` satırı) — join sonrası kısa süre içinde bir status mesajı denemesi görülmeli, 1 saat beklenmemeli.

### F3 — Gateway kapatılıp açılma senaryosu (uçtan uca, bu araştırmanın orijinal motivasyonu)
- **Adımlar:** Gateway'i kapat → birkaç kart okut (hepsi buffera düşsün) → gateway'i aç → **cihazı resetlemeden** bekle.
- **Beklenen:** En kötü ihtimalle ~75 sn içinde (E2 mekanizması) veya bir sonraki kart okutmada (B1/C1 guard'ları sayesinde temiz bir deneme) otomatik rejoin gerçekleşmeli, buffer kendiliğinden boşalmalı — **manuel reset gerekmemeli**. Bu, en başta araştırdığımız "sadece reset ACK'i düzeltiyor" sorununun artık var olmadığının nihai kanıtı.

---

## G. Düşük Güç Modu (LPM) Kilit Ayrımı

### G1 — RFID okuma ve status gönderimi birbirinin LPM kilidini bozmuyor (kod incelemesi + statik doğrulama)
- **Not:** Bu, run-to-completion sequencer modeli nedeniyle UART logu ile doğrudan gözlemlenmesi zor bir senaryo. Önerilen doğrulama: kod incelemesi ile `RfidPreventStopMode`/`RfidAllowStopMode`'un yalnızca `RfidStopLockActive`'i, `StatusMsgPreventStopMode`/`StatusMsgAllowStopMode`'un yalnızca `StatusStopLockActive`'i kullandığını teyit et (✅ bu oturumda düzeltildi).
- **İleri seviye doğrulama (opsiyonel, ekipman gerektirir):** Bir GPIO'yu `RfidPreventStopMode`/`StatusMsgPreventStopMode` içinde toggle ettirip osiloskop/logic analyzer ile iki kilidin bağımsız çalıştığını gözle.

---

## H. Kod Sağlamlığı / Regresyon Testleri

### H1 — `stop_read_rfid` volatile düzeltmesi, Release derlemede hang testi
- **Ön koşul:** Projeyi **Release/optimize edilmiş** konfigürasyonda derle (Debug'da bu sorun zaten görünmeyebilir).
- **Adımlar:** RFID okumasını tetikle, **hiç kart okutma** (MFRC522'ye hiç kart yaklaştırma), `RFID_TIMEOUT` süresinin dolmasını bekle.
- **Beklenen:** `OnRfidReadTimeoutEvent` ateşlenip `stop_read_rfid=true` yapınca `ReadRFIDCard()`'ın `while` döngüsü **gerçekten sona ermeli** — "RFID Read task has ending..." logu görülmeli, cihaz asılı kalmamalı. *(Bu test, `volatile` eklenmeden önce Release derlemede teorik olarak başarısız olabilirdi.)*

### H2 — Ölü kod temizliği doğrulaması
- **Adımlar:** Projeyi temiz derle (`Build All`).
- **Beklenen:** `StopJoin`/`StopJoinTimer`/`OnStopJoinTimerEvent` ile ilgili hiçbir referans/uyarı kalmamalı (bu oturumda tamamen kaldırıldı) — derleme uyarısız/hatasız tamamlanmalı.

### H3 — `pcb_sync` retry sayısı anomalisi (doğrulanması gereken açık nokta)
- **Gözlem:** `OnTxData`'nın ACK-başarı dalında (`buffered_rfid_data_wait_for_ack` bloğu), `pcb_set_status_by_id` 5 kez retry ediyor ama **`pcb_sync` artık sadece 1 kez deneniyor** (`pcb_sync_try_count<1`, önceden `<5` idi).
- **Yapılacak:** Bu değişikliğin bilinçli mi olduğunu teyit et. Değilse `<5`'e geri al — flash senkron hatası tek denemede pes ederse, `PCB_STATUS_SENT` RAM'de işaretlenmiş ama flash'a hiç yazılmamış bir kayıt oluşabilir (bir sonraki resette bu kayıt hâlâ FAILED görünüp gereksiz yere tekrar gönderilir — veri kaybı değil ama duplicate gönderim riski artar).

---

## I. Bilinen Açık Konular (bu test planının kapsamı dışında, takip gerektirir)

- **[KAPATILDI] TryJoin() reentrancy koruması yok** (derin incelemenin 5. maddesi) — yeniden incelendi (bu oturumda): `join_in_progress` guard'ı (`lora_app.c:1103-1140`, giriş kilidi + `LmHandlerStop()` başarısızlığında geri alma) ve 35 sn'lik `JoinTimeoutHandler` güvenlik supabı bunu zaten önlüyor. Ayrıca 7 tetikleyicinin (RFID/status join-yok dalları, 5-strike, buffer handler, `JoinRetryTimer`) hepsi `TryJoin`'i doğrudan çağırmak yerine `UTIL_SEQ_SetTask` ile bayrak koyuyor; sequencer run-to-completion olduğu için gerçek reentrancy zaten mimari olarak mümkün değil.
  - **I1 — Doğrulama testi (önerilir, henüz koşulmadı):** RFID + status + 5-strike tetikleyicilerinin çok kısa aralıkla (aynı sequencer tick'i içinde) üst üste rejoin tetiklemesini kurgula (örn. gateway kapalıyken art arda birkaç kart okut + status timeout'u aynı ana denk getir). **Beklenen:** loglarda `"TryJoin zaten surüyor, atlandi"` mesajı görülmeli (guard çalışıyor), `LmHandlerJoin()` asla üst üste iki kez çağrılmamalı, cihaz kalıcı olarak "rejoin yapamaz" duruma düşmemeli.

- **Flash senkron sırasında RX penceresi stall riski** (derin incelemenin 3. maddesi) — **mimari soru netleşti** (bu oturumda): `stm32wlxx_hal_flash.h` incelendi, `FLASH_EraseInitTypeDef`'te bank alanı yok, 128×2KB düz sayfa uzayı (`FLASH_PAGE_NB=128`, `FLASH_PAGE_SIZE=0x800`) — yani **STM32WLE5 tek banklı flash**, read-while-write desteği yok, bir sayfa erase/program sırasında CPU (interrupt dahil) tamamen duruyor (`FLASH_TIMEOUT_VALUE=1000` ms, HAL'in kabul ettiği azami süre). Mimari risk gerçek ve kalıcı.
  - Ancak güncel kodda `pcb_sync`/`pcb_add`'in her çağrı noktası izlendi: hepsi bir gönderim denemesinden ÖNCE ya da bir ACK/timeout kesinleştikten SONRA çalışıyor; proje genelindeki karşılıklı dışlama bayrakları (`rfid_data_pending_on_lora`, `buffered_rfid_data_wait_for_ack`, `status_data_pending_on_lora`) aynı anda tek bir confirmed uplink'e izin verdiği için, "bir mesajın RX penceresi açıkken başka bir flash yazımı araya girsin" senaryosunun şu an canlı/gözlemlenebilir bir tetikleyici yolu yok.
  - **I2 — İstatistiksel doğrulama (hâlâ önerilir, ama artık düşük öncelik):** yoğun buffer-drain trafiği sırasında (D2 senaryosu, 3+ art arda kayıt) ACK başarı oranının izole gönderimlere göre gözle görülür şekilde düştüğü gözlemlenirse, bu maddeye geri dönülmeli — LoRaMAC'in kendi iç housekeeping görevinin (`CFG_SEQ_Task_LmHandlerProcess`) bir flash yazımıyla art arda gelmesi hâlâ birkaç on ms'lik bir gecikmeye yol açabilir.

- **LoRaWAN sürüm uyuşmazlığı** (`docs/milesight-ug63-lorawan-version-mismatch.md`) — üretici cevabı bekleniyor, değişiklik yok.

---

## J. Zaman Senkronizasyonu (`lora_timesync` kütüphanesi, bu oturumda eklendi)

**Kapsam notu:** `g_deviceUnixEpoch`/`g_deviceEpochSetAtMs` yerine STM32CubeWL'in RTC yedek registerlarına dayanan `SysTimeSet()`/`SysTimeGet()` + kendi "SYNC" işareti (`RTC_BKP_DR3`) kullanılıyor — bkz. `external_libs/lora_app_auxilary/`.

### J1 — İlk açılışta/senkron öncesi `GetCurrentUnixTime()` 0 dönmeli
- **Ön koşul:** Cihaz gerçek bir power-on sıfırlaması geçirmiş (backup domain temiz), hiçbir zaman senkron downlink'i alınmamış.
- **Adımlar:** Join ol, sunucudan zaman senkron downlink'i gelmeden bir STATUS veya RFID mesajı gönder.
- **Beklenen:** Payload'daki epoch alanı (statusNowEpoch / liveNowEpoch) 0; `LoraTimeSync_IsSynced()` false.

### J2 — Doğru sayaçla gelen zaman senkron downlink'i kabul edilmeli
- **Ön koşul:** Cihaz join olmuş, en az bir STATUS mesajı gönderilmiş (log'da "sayac=X" görülmüş).
- **Adımlar:** Sunucudan `LORAWAN_USER_APP_PORT`'a 9 byte'lık downlink gönder: `[0]=0x01`, `[1:4]=epoch`, `[5:8]=X` (cihazın son gönderdiği status sayacı).
- **Beklenen:** `"###### TIME SYNC alindi: epoch=..."` logu; sonraki `GetCurrentUnixTime()` çağrıları bu epoch'tan itibaren gerçek zamanlı ilerler.

### J3 — Yanlış/eski sayaçla gelen downlink reddedilmeli (stale koruması)
- **Adımlar:** J2 ile aynı downlink'i, `[5:8]` alanına bilinçli yanlış bir sayaç koyarak gönder.
- **Beklenen:** Hiçbir log basılmaz (tasarım gereği sessiz red), epoch/`IsSynced()` değişmez.

### J4 — Reset sonrası senkron bilgisinin korunması (bu modülün asıl motivasyonu)
- **Ön koşul:** J2 ile senkron olunmuş.
- **Adımlar:** Cihazı `NVIC_SystemReset()` veya watchdog ile resetle (**VDD kesme değil**).
- **Beklenen:** Reset sonrası `IsSynced()` true kalır; ilk STATUS/RFID payload'ındaki epoch 0 DEĞİL, gerçek zamana yakın bir değerdir. *(Eski RAM-only `g_deviceUnixEpoch` yaklaşımında bu test başarısız olurdu — asıl regresyon noktası burası.)*

### J5 — Gerçek güç kaybında senkron bilgisi kaybolmalı (beklenen davranış, bug değil)
- **Adımlar:** Backup domain dahil VDD'yi kes, tekrar ver.
- **Beklenen:** `IsSynced()` false'a döner, yeni senkron döngüsü gerekir — bkz. [[pcb-flash-erase-resets-count]] ile aynı kategoriden "beklenen sıfırlanma".

### J6 — ACK alınamayan STATUS retry'ında sayaç ilerlememeli
- **Ön koşul:** STATUS gönderildi, ACK gelmedi (bkz. E1, `RetryStatusTimer` devrede).
- **Adımlar:** Retry denemesindeki `"sayac=X"` log değerini ilk denemeyle karşılaştır.
- **Beklenen:** İkisi de AYNI X değerini taşır — sunucudan gecikmeli gelen bir J2 yanıtı bile retry denemesini "taze" kabul edebilmeli.

---

## K. Modülerleştirme Refactor Regresyon Testleri (`PersistFailedRfidSend` / `OnTimerFiresSetTask` / `WriteU32BE`)

### K1 — Derleme kontrolü (ön koşul, en ucuz/en kritik test)
- **Adımlar:** `Build All`, tüm warning çıktısını incele.
- **Beklenen:** Sıfır hata, yeni warning yok — özellikle "unused variable" (eski `bufferResult` kopyaları) ve `OnTimerFiresSetTask`'ın `(void*)&kXxxBinding` cast'leri için pointer tipi uyarısı olmamalı.

### K2 — 7 timer'ın doğru task+priority ile tetiklenmesi
- **Not:** Ayrı bir test gerekmez — A-F bölümündeki senaryolar (B4, D4, E1, F1 vb.) zaten her timer'ı en az bir kez tetikliyor; o senaryoların beklenen logları aynen çıkıyorsa binding tablosu doğru çalışıyor demektir.
- **Ek doğrulama:** E1 (periyodik `StatusMessageTimeoutTimer`) ve retry (`RetryStatusTimer`) denemelerinin ikisinde de RFID gönderimiyle aynı önceliği (`CFG_SEQ_Prio_status_1`) paylaştığını — yani status işleminin RFID görevleriyle çakıştığında beklenen sırayı koruduğunu — gözle.

### K3 — `PersistFailedRfidSend`'in `pcb_sync` davranış değişikliği (RfidAckTimeoutHandler'a özel regresyon)
- **Arka plan:** Refactor öncesi `RfidAckTimeoutHandler`'da `pcb_sync` SADECE `pcb_add` başarılıysa çağrılıyordu; artık (diğer 7 site ile tutarlı olacak şekilde) koşulsuz çağrılıyor.
- **Ön koşul:** Buffer'ı doldur (`pcb_add` `PCB_FULL`/hata dönene kadar; gerekirse test için PCB kapasitesini geçici düşür).
- **Adımlar:** Buffer doluyken B4 senaryosunu (RFID ACK timeout) tetikle.
- **Beklenen:** `"Record add error: ..."` logu + hemen ardından bir flash sync denemesi de yapılır (yeni davranış). Fonksiyonel hata beklenmez; sadece dolu-buffer durumunda ekstra bir flash yazımı olduğunu doğrula.

### K4 — `WriteU32BE` payload doğruluğu (saha/network server ile doğrulama)
- **Ön koşul:** Network server'da (Milesight UG63) ham hex payload görüntüleme açık.
- **Adımlar:** Bir STATUS ve bir canlı RFID mesajı gönder, ham payload'ı yakala.
- **Beklenen:** STATUS: `byte[6:9]`=sayaç, `byte[10:13]`=epoch; RFID: `byte[2:5]`=timestamp, `byte[14:17]`=epoch — hepsi big-endian, refactor öncesiyle birebir aynı offset/format (bu oturumdaki değişiklik sadece yazım şeklini kısalttı, hiçbir byte konumunu değiştirmedi).

### K5 — Duplicate flag düzeltmesinin doğrulanması (`SendRFID_Data` DUTYCYCLE_RESTRICTED dalı)
- **Arka plan:** Refactor öncesi bu dalda `rfid_data_pending_on_lora = false;` iki kez yazılıyordu (zararsız ama gereksiz); tekilleştirildi.
- **Adımlar:** B2 senaryosunu (duty-cycle kısıtlaması) tetikle.
- **Beklenen:** Davranışta gözle görülür bir fark olmamalı — bu test sadece "hiçbir yan etki yok" doğrulaması içindir, ayrı bir log beklenmez.

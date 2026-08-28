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

- **TryJoin() reentrancy koruması yok** (derin incelemenin 5. maddesi) — henüz düzeltilmedi. Test için: birden fazla tetikleyicinin (RFID + status + 5-strike) çok kısa aralıkla üst üste rejoin tetiklediği bir senaryo kurgulanabilir, `LmHandlerStop()`'un "on going" logunun ardından yine de `Configure`+`Join` çağrıldığını gözlemleyip stack davranışını izlemek gerekir.
- **Flash senkron sırasında RX penceresi stall riski** (derin incelemenin 3. maddesi) — STM32WL referans kılavuzundan flash bank mimarisi teyit edilmeli; ardından yoğun buffer-drain trafiği sırasında ACK başarı oranının izole gönderimlere göre düştüğü istatistiksel olarak test edilebilir.
- **LoRaWAN sürüm uyuşmazlığı** (`docs/milesight-ug63-lorawan-version-mismatch.md`) — üretici cevabı bekleniyor.

# Kalıcı `SEND FAILED (-2)` ve Rejoin Olmaması — RX Pencere Zamanlayıcı Underflow Sorunu

**Durum: çözüldü** (iki saha logu + 1 saatlik bekleme testiyle doğrulandı).

## Belirti
Cihaz bir süre sonra her gönderimde `SEND FAILED (-2)` (`LORAMAC_STATUS_BUSY`) veriyor, hiç RX penceresi açılmıyor ve rejoin tetiklenmiyordu. Cihaz yeniden başlatılana kadar düzelmiyordu.

## Kök neden
1. RFID okuma görevi bloklayıcıdır (~2-5 sn, `HAL_Delay` tabanlı). Sequencer kooperatif olduğu için bu sürede diğer görevler çalışamaz.
2. Okuma, bir TX'in bitişine denk gelirse `LmHandlerProcess` görevi içindeki `ProcessRadioTxDone()` gecikir.
3. `ProcessRadioTxDone()` RX1/RX2 zamanlayıcılarını `RxWindowDelay - offset` olarak kurar. Gecikme `RxWindowDelay` değerini aşınca `uint32_t` çıkarma **taşar** (~4,29 milyar ms) ve pencereler hiç açılmaz.
4. MAC, RX penceresi bekleyerek meşgul kalır. `LmHandlerSend` hep `-2` döner.
5. Rejoin yolu da `LmHandlerStop()` (`LoRaMacDeInitialization()`) çağırır; MAC meşgulken bu başarısız olduğu için `TryJoin()` rejoin yapamaz. Onay (ACK) sayacı yalnızca tamamlanan confirmed TX'leri saydığından `MAX_UNSUCCESS_ACK_COUNT` yolu da tetiklenmez.

## Çözüm
| Dosya | Değişiklik |
|---|---|
| `LoRaWAN/App/lora_app.c` | `ReadRFIDCard()` başında `LoRaMacIsBusy()` kontrolü: MAC meşgulse okuma `RfidDeferTimer` ile 300 ms ertelenir (en fazla 40 kez ≈ 12 sn), sonra yine de başlar. |
| `Middlewares/Third_Party/LoRaWAN/Mac/LoRaMac.c` | `RxWindowDelayAfterOffset()` yardımcı fonksiyonu: `delay > offset ? delay - offset : 1`. `ProcessRadioTxDone()` içindeki dört `TimerSetValue(RxWindowTimer1/2)` çağrısında kullanılıyor. |

> **Uyarı:** `LoRaMac.c` değişikliği bir proje yamasıdır. CubeMX/CubeWL ile middleware yeniden üretilirse tekrar uygulanmalıdır (dosyadaki yorum bu yamayı işaretler).

## Yan düzeltme: her okumada batarya ADC ölçümü
Düşük batarya koruması (`RFID_READ_MIN_BATTERY_MV = 2800`) her kart okumasında ADC ölçümü yapıyordu. Artık son ölçüm (`g_lastBatMv`, STATUS ölçümü de dahil) **10 dakika** önbelleklenir (`RFID_BAT_CACHE_MAX_AGE_MS`). Ölçüm 0 dönerse (hata) okuma engellenmez.

## Doğrulama (log)
- Her TX'i `RX_1`/`RX_2` (veya `rxDone`) ve `Tx Data Rate` satırı izliyor.
- Yoğun buton testinde (≈18 uplink) tümünde `ACK alindi`, `SEND FAILED` yok; kaçırılan RX penceresinde MAC kendi retry'ıyla ACK aldı.
- STATUS aralığı ≈ 1 saat (`sayac=0` → `sayac=1`); ADC yalnızca STATUS'ta ve önbellek süresi dolduktan sonraki ilk okumada çalışıyor.

## lw_ping / canlı RFID gönderimi çakışması (düzeltildi)
Kart okunamayan bir okumadan sonra kurulan `LwPingDelayTimer` (3 sn), bir sonraki (bloklayıcı) okuma sırasında dolup ping görevi bekliyor; okuma bitince ping, canlı RFID gönderiminden önce çalışıp MAC'i meşgul ediyor ve canlı gönderim `SEND FAILED (-2)` alıyordu (veri RAM buffer'a düşüp ~16 sn sonra gidiyordu).

Çözüm (`lora_app.c`): lw_ping **aynen gönderilir** (kart okunmayan okumalarda sunucuya ek RX penceresi sağlama amacı korunur). Bunun yerine `SendRFID_Data()` canlı gönderimden hemen önce `LoRaMacIsBusy()` kontrolü yapar; MAC meşgulse gönderimi `RfidSendDeferTimer` ile 200 ms aralıkla (en çok 40 kez ≈ 8 sn) erteler ve `-2` ile buffer'a düşmesini önler. Ertelenen gönderimde kartın okunma zamanı (`g_lastSentTimestamp`) korunur; bekleme sırasında yeni okuma `ReadRFIDCard()` tarafından reddedilir (kart üzerine yazılmasın). Beklenen log: `MAC mesgul, canli RFID gonderimi ertelendi` → `Canli RFID gonderimi N kez ertelendi, simdi gonderiliyor`.

## Test edilen ve bilinen davranış
Erteleme dalı (`MAC mesgul ... ertelendi` → `RFID okumasi N kez ertelendi, simdi basliyor`) 10:31'deki buton yağmuru testinde doğrulandı (N = 6–15, sınır 40'ın altında). Ping ile çakışan canlı gönderim artık ~2 sn gecikmeyle (ping bitince) doğrudan gider, buffer'a düşmez.

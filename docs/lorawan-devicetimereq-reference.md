# LoRaWAN DeviceTimeReq / DeviceTimeAns — Referans Doküman

**Tarih:** 2026-08-28
**Amaç:** MQTT tabanlı özel zaman senkronizasyon çözümümüze (bkz. `test_codes/lw_lora_rif_decoder.py` + `lora_app.c`'deki `LORA_DOWNLINK_MSG_TYPE_TIME_SYNC`) alternatif olarak değerlendirilen, LoRaWAN standardının **kendi yerleşik** zaman senkronizasyon mekanizmasının tam referansı.

---

## 1. Resmi ad ve standart

- **Resmi ad:** `DeviceTimeReq` (uplink MAC komutu) / `DeviceTimeAns` (downlink MAC komutu)
- **Komut kimliği (CID):** `0x0D`
- **Standart:** LoRa Alliance, **TS001 — LoRaWAN® L2 1.0.x Specification** (Layer 2 / MAC katmanı spesifikasyonu — eski adıyla "LoRaWAN Specification")
- **Tanıtıldığı sürüm:** **LoRaWAN 1.0.3** — 1.0.4 ve 1.1.x'te de aynen korunuyor. **1.0.2 ve öncesinde YOK.**
- **İlişkili, daha kapsamlı ayrı bir standart:** LoRa Alliance ayrıca **"LoRaWAN® Application Layer Clock Synchronization Specification v1.0.0"** adında, uygulama katmanında (fPort tabanlı, periyodik ve driftölçen) daha gelişmiş bir senkronizasyon paketi de tanımlamış — bu, `DeviceTimeReq`'in üzerine inşa edilen ayrı/isteğe bağlı bir TS-package, temel ihtiyaç için gerekli değil.

## 2. Genel tanım — nasıl çalışır

1. Cihaz, bir **MLME (MAC Layer Management Entity) isteği** olarak `DeviceTimeReq`'i kuyruğa alır (payload'sız, sadece 1 byte'lık CID).
2. Bu komut, **ayrı bir uplink gerektirmez** — cihazın bir sonraki göndereceği HERHANGİ bir uplink'in (confirmed/unconfirmed, uygulama verisi olsun ya da boş olsun fark etmez) `FOpts` alanına otomatik olarak eklenir (piggyback).
3. Ağ sunucusu bu komutu görünce, cevabı (`DeviceTimeAns`) **aynı uplink'in RX1/RX2 downlink penceresinin `FOpts` alanına** ekler — yani **ayrı bir uygulama-katmanı downlink'i, ayrı bir fPort, ayrı bir dış sistem (MQTT/script) gerekmez.**
4. `DeviceTimeAns` payload'ı: **4 byte GPS epoch saniyesi + 1 byte kesirli saniye (1/256 sn çözünürlükte)** = toplam 5 byte, ama bu bizim uygulama payload'ımızın DIŞINDA, MAC katmanında taşınır — uygulama veri bütçemizi (14 byte'lık RFID payload'ı gibi) hiç etkilemez.
5. Cihazın LoRaMAC stack'i bu cevabı otomatik olarak **GPS epoch'tan Unix epoch'a çevirir** (`UNIX_GPS_EPOCH_OFFSET` sabiti ile) ve TX-anından-cevap-anına geçen süreyi telafi ederek sistem saatini ayarlar — uygulama kodunun hiçbir dönüşüm yapmasına gerek yok.

## 3. Bu projede mevcut durum — KOD İLE DOĞRULANMIŞ, ZATEN HAZIR

Middleware'i (`Middlewares/Third_Party/LoRaWAN`) inceledim — bu mekanizma **tamamen implemente edilmiş ve projeye zaten bağlı**, sadece uygulama tarafında kullanılmıyor:

| Parça | Konum | Durum |
|---|---|---|
| İsteği tetikleyen API | `LmHandlerDeviceTimeReq(void)` — `LmHandler.h:791`, `LmHandler.c:796` | Hazır, çağrılmayı bekliyor |
| Cevap geldiğinde tetiklenen callback | `OnSysTimeUpdate()` — `LmHandlerCallbacks.OnSysTimeUpdate`, `LmHandler.c:1079-1082` | **Zaten `lora_app.c`'de kayıtlı ama gövdesi boş!** |
| GPS→Unix dönüşümü + TX-gecikme telafisi | `LoRaMac.c:3128-3140` (`SysTimeSet`, `LoRaMacClassBDeviceTimeAns`) | Otomatik, stack içinde |
| Senkronize saati okuma API'si | `SysTimeGet(void)` → `SysTime_t {uint32_t Seconds; int16_t SubSeconds;}` — `Utilities/misc/stm32_systime.h:197` | Hazır, proje derlemesine zaten dahil (`.o` dosyası mevcut) |

**Entegrasyon için gereken minimum kod** (mevcut MQTT çözümünün YERİNE, ya da onunla birlikte yedek olarak):
```c
// 1) Join başarılı olduğunda veya her status gönderiminden önce bir kere:
LmHandlerDeviceTimeReq();

// 2) OnSysTimeUpdate() callback'inin (şu an bos govdeli) icine:
static void OnSysTimeUpdate(void)
{
    SysTime_t syncedTime = SysTimeGet();
    APP_LOG(TS_OFF, VLEVEL_M, "###### DeviceTimeAns alindi: epoch=%u\r\n", (unsigned int)syncedTime.Seconds);
    // istenirse g_deviceUnixEpoch/g_deviceEpochSetAtMs mekanizmamiza da yazilabilir,
    // ya da dogrudan SysTimeGet() her ihtiyac aninda cagrilabilir.
}
```
Bizim `SendRFID_Data`'daki `GetCurrentUnixTime()` fonksiyonu yerine doğrudan `SysTimeGet().Seconds` de kullanılabilir — ikisi paralel de çalışabilir (MQTT çözümü yedek, DeviceTimeReq birincil kaynak gibi).

## 4. Özellikler

- **Ayrı fPort/downlink gerektirmez** — MAC seviyesinde, `FOpts` üzerinden gider, uygulama veri bütçesini kullanmaz.
- **Class A, B, C hepsinde çalışır** — Class A'da bile bir sonraki normal uplink'in RX penceresinde cevap gelir (bizim mevcut MQTT çözümümüzün yaşadığı "ilk birkaç deneme kayboluyor" sorunu burada MAC seviyesinde, protokolün kendi güvencesiyle çözülüyor — ayrı bir dış script'e, insan/ağ gecikmesine bağımlı değil).
- **Class B için ZORUNLU önkoşul** — beacon edinimi öncesi cihazın `DeviceTimeReq` ile zaman senkronize etmesi spec gereği şart (bu proje Class A kullanıyor, bu maddeyi ilgilendirmiyor ama bilgi amaçlı).
- **Sadece zaman, saat dilimi değil** — Milesight'ın kendi dokümantasyonunda da özellikle belirtilmiş: "This only supports to get the time but not time zone." Saat dilimi ayrı bir mekanizma (ToolBox app veya özel downlink) gerektiriyor.
- **Periyodik tekrar önerilir** — cihaz saati zamanla driftedebileceği için, Milesight dokümantasyonu **~5 günde bir** tekrar `DeviceTimeReq` gönderilmesini öneriyor.

## 5. Kullanım alanları

- Pil/RTC yedeklemesi olmayan ya da (bizim projemizdeki gibi) takvim modunda çalışmayan RTC'ye sahip cihazlarda zaman damgası ihtiyacı
- Erişim kontrolü / olay kaydı (log) cihazları — tam bizim RFID senaryomuz
- Varlık takibi (asset tracking), zaman damgalı sensör verisi
- LoRaWAN Class B cihazlarında beacon senkronizasyonunun ön koşulu

## 6. Ticari üründe destek durumu (doğrulanmış)

| Ürün / Platform | Destek | Kaynak |
|---|---|---|
| **ChirpStack** (açık kaynak NS) | ✅ Destekleniyor, gateway'in sağladığı RX zaman damgasını kullanarak en doğru sonucu veriyor; gateway zaman senkronize değilse sunucu saatine düşüyor. | [ChirpStack — Device time](https://www.chirpstack.io/docs/chirpstack/features/device-time.html) |
| **The Things Stack / The Things Network** | ✅ Prensipte destekleniyor, ama tarihsel olarak bazı sürümlerde/durumlarda hatalı davranış bildirilmiş (bazı kullanıcılar ChirpStack'te sorunsuz, TTN'de sorunlu bulmuş). | [TTN forum — DeviceTimeAns tartışması](https://www.thethingsnetwork.org/forum/t/understanding-specification-v1-0-3-and-v1-0-4-at-devicetimeans-mac-command/53592), [TTS GitHub issue #187](https://github.com/TheThingsNetwork/lorawan-stack/issues/187) |
| **Milesight gateway embedded NS (UG6x serisi — bizim kullandığımız!)** | ✅ **Açıkça destekleniyor**, resmi dokümantasyonda "Milesight gateway embedded NS" uyumlu sunucu örneği olarak veriliyor. | [Milesight — Time Synchronization](https://www.milesight.com/products/docs/en/ts201/steps/time-sync.html) |

## 7. ⚠️ Sahaya özel kritik nokta — bizim kurulumumuzla doğrudan çakışıyor

`docs/milesight-ug63-lorawan-version-mismatch.md`'de belgelediğimiz sorunu hatırla: **UG63'teki cihaz profilimiz "LoRaWAN 1.0.2 rev B" olarak ayarlı**, ama `DeviceTimeReq` **1.0.3'te tanıtıldı**. Milesight'ın kendi dokümantasyonu da bunu doğruluyor: *"Devices supporting LoRaWAN V1.0.3 can request time from a network server that provides this capability."*

**Sonuç:** Bu mekanizmayı kullanabilmemiz için, cihaz profilini 1.0.3'e yükseltmemiz (ya da UG63'ün bunu desteklemediğini görürsek harici bir LNS'e geçmemiz) — yani zaten çözmemiz gereken versiyon uyuşmazlığı sorunu — **önkoşul**. İki konu birbirine bağlı: versiyon sorunu çözülmeden `DeviceTimeReq` denemesi muhtemelen hiç cevap almaz (NS, 1.0.2 profili için bu MAC komutunu tanımayabilir/cevaplamayabilir).

## 8. Sınırlamalar / dikkat edilecekler

- Sadece Unix zaman damgası verir, saat dilimi (timezone) bilgisi vermez.
- Doğruluk, ağ sunucusunun/gateway'in kendi saat kaynağının doğruluğuna bağlı (ChirpStack, gateway'in gerçek RX zaman damgasını kullanıyor — bu en doğru yöntem; bazı basit private NS'ler sadece "sunucu şu an ne zaman" der, gateway/RF gecikmesini telafi etmeyebilir).
- LoRaWAN 1.0.3+ gerektirir — 1.0.2 profillerde çalışmaz (bkz. madde 7).
- Periyodik tekrar (Milesight önerisi: ~5 günde bir) gerekiyor, drift'i önlemek için.

## Kaynaklar
- [LoRa Alliance — LoRaWAN Application Layer Clock Synchronization Specification v1.0.0](https://resources.lora-alliance.org/technical-specifications/lorawan-application-layer-clock-synchronization-specification-v1-0-0)
- [ChirpStack — Device time](https://www.chirpstack.io/docs/chirpstack/features/device-time.html)
- [Milesight — Time Synchronization](https://www.milesight.com/products/docs/en/ts201/steps/time-sync.html)
- [The Things Network forum — DeviceTimeAns v1.0.3/v1.0.4 tartışması](https://www.thethingsnetwork.org/forum/t/understanding-specification-v1-0-3-and-v1-0-4-at-devicetimeans-mac-command/53592)
- Proje içi kod referansları: `Middlewares/Third_Party/LoRaWAN/LmHandler/LmHandler.c` (satır 796, 1079), `Middlewares/Third_Party/LoRaWAN/Mac/LoRaMac.c` (satır 3128-3140), `Utilities/misc/stm32_systime.h`

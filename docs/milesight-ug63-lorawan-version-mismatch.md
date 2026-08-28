# Bilgi Notu: Cihaz Firmware'i ile UG63 Network Server Profili Arasında LoRaWAN Sürüm Uyuşmazlığı

**Tarih:** 2026-08-28
**Konu:** Uç cihazın (base_end_node) gerçekte konuştuğu LoRaWAN MAC sürümü ile Milesight UG63 gateway'in dahili (private) network server'ında cihaz için tanımlı profilin beklediği sürüm birbirini tutmuyor.

## 1. Ortam / bileşenler

- **Uç cihaz:** STM32WL tabanlı RFID base end node (bu proje)
- **LoRaWAN middleware:** ST STM32CubeWL, `MW_LORAWAN_VERSION: V2.5.0` (cihaz boot logunda görünüyor)
- **Bölge/frekans planı:** EU868
- **Gateway:** Milesight UG63, kendi üzerindeki **private (dahili) network server** modu kullanılıyor (packet forwarder olarak değil)
- **Örnek test cihazı DevEUI:** `00:80:E1:15:00:3E:1F:6F` (kontrol ederken hangi cihazın/profilin incelendiğini teyit etmek için)

## 2. Tespit edilen uyuşmazlık

- Cihaz firmware'inin derleme zamanı ayarı: `LoRaWAN/Target/lorawan_conf.h` içinde
  ```c
  #define LORAMAC_SPECIFICATION_VERSION   0x01000300   // = LoRaWAN 1.0.3
  ```
  Yani cihaz fiilen **LoRaWAN 1.0.3** kurallarıyla konuşuyor.
- Kullanılan STM32CubeWL middleware paketinde (`Middlewares/Third_Party/LoRaWAN/**`) sadece şu üç sürüm için derleme desteği var: **1.0.3**, **1.0.4**, **1.1.1**. Kod içinde `LORAMAC_VERSION == 0x01000200` (1.0.2) şartına bağlı **hiçbir dal bulunmuyor** — yani bu middleware ile cihazın gerçekten 1.0.2 konuşacak şekilde derlenmesi mümkün değil.
- UG63'ün web arayüzünde, bu cihaz için tanımlı **Network Server > Device Profile** ayarında "LoRaWAN Version" alanı **"1.0.2 rev B"** olarak ayarlı/seçili, ve dropdown listesinde **1.0.3 seçeneği bulunmuyor**.

## 3. Neden önemli

LoRaWAN ağ sunucuları, cihaz profilinde tanımlı MAC sürümüne göre:
- MAC komutlarının (`FOpts` alanı) nasıl şifrelenip çözüleceğini,
- ADR/regional parametre varsayılanlarını (RX2 default datarate/frekans, dwell time limitleri, ADR_ACK_LIMIT/DELAY gibi sabitler),
- Join Accept çerçevesindeki `CFList` formatını,

farklı yorumlayabilir/oluşturabilir. Temel join/uplink/downlink akışı genelde 1.0.2 ↔ 1.0.3 arasında çalışsa da (bu iki sürüm arası fark çoğunlukla errata/netleştirme seviyesinde), versiyon-spesifik ince noktalarda (özellikle MAC komutu taşıyan downlink çerçevelerinde) sessiz/aralıklı uyumsuzluklara yol açabilecek bilinen bir LoRaWAN interoperability sorun sınıfıdır.

*(Not: Bu proje sırasında gözlemlenen aralıklı ACK kaybı sorununun TEK nedeninin bu olduğu kanıtlanmadı — başka katkıda bulunan etkenler de tespit edildi (ör. ADR datarate geri yükleme eksikliği). Ama bu versiyon uyuşmazlığı, bağımsız olarak da düzeltilmesi gereken gerçek bir yapılandırma sorunu.)*

## 4. Şu ana kadar denenen / değerlendirilen çözümler

- Firmware'i 1.0.2'ye "indirmek" **mümkün değil** — STM32CubeWL middleware'i (WL55/WLE5 SoC ailesi için) hiçbir sürümünde 1.0.2'yi desteklemiyor. 1.0.2 desteği sadece ST'nin çok daha eski/farklı bir SoC ailesi (STM32L0) için kullanılan I-CUBE-LRWAN paketinde vardı; bu projeyi o stack'e taşımak pratik değil.
- UG63'ün private network server'ı yerine gateway'i **packet forwarder** moduna alıp arkasına 1.0.3 destekleyen harici bir LNS (ör. ChirpStack, self-hosted) bağlamak teknik olarak mümkün, ama henüz uygulanmadı.

## 5. Üreticiye (Milesight) sorulacak sorular

1. UG63'ün dahili private network server'ı, mevcut veya planlanan bir firmware sürümünde **LoRaWAN 1.0.3** (ve karşılık gelen Regional Parameters, ör. RP002-1.0.3) cihaz profili seçeneğini destekliyor mu / desteyecek mi?
2. Desteklemiyorsa, bu **kalıcı bir tasarım kısıtı mı**, yoksa gelecekteki bir firmware güncellemesiyle eklenmesi planlanan bir özellik mi?
3. 1.0.3 konuşan bir cihazı, profili 1.0.2 rev B olarak tanımlanmış UG63 private NS'e bağlamanın **bilinen/dokümante edilmiş bir uyumluluk etkisi** var mı (ör. hangi spesifik alanlarda davranış farkı olabilir)?
4. Bu senaryo için Milesight'ın önerdiği resmi çözüm yolu nedir — UG63'ü packet forwarder moduna alıp harici bir LNS kullanmak mı, yoksa başka bir öneri var mı?

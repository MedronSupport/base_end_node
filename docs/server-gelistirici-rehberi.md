# LoRaWAN RFID Reader — Sunucu Geliştirici Rehberi

**Kapsam:** Bu belge, bir LoRaWAN network server/gateway'i (örn. Milesight UG63 private NS) arkasında çalışan **sunucu tarafı yazılımın**, saha cihazıyla (LoRaWAN RFID Reader) nasıl haberleşeceğini tanımlar. Belge, kullanılan istemci/dil/kütüphaneden bağımsızdır — yalnızca **protokol arayüzünü** (topic'ler, mesaj zarfı, payload formatları) tarif eder.

---

## 1. Genel Bakış

| Özellik | Değer |
|---|---|
| Bölge | EU868 |
| LoRaWAN sınıfı | Class A |
| Aktivasyon | OTAA |
| Uygulama portu (fPort) | **2** — tüm özel mesajlar (uplink/downlink) bu port üzerinden gider |
| Çok byte'lı sayısal alanlar | Her zaman **büyük-endian** (en anlamlı byte önce) |

---

## 2. MQTT Bağlantısı ve Topic'ler

| Yön | Topic | Açıklama |
|---|---|---|
| Abone ol (uplink) | `e5/up` | Cihazdan gelen tüm veri mesajları |
| Abone ol (join) | `e5/join` | Cihaz OTAA join tamamladığında |
| Yayınla (downlink) | `e5/down/{devEUI}` | Cihaza gönderilecek her mesaj, hedef cihazın DevEUI'sine özel bir topic'e yayınlanır |

> **Not:** `e5/join` topic adı ve JSON şeması, kullanılan network server'ın kendi konvansiyonuna göre değişebilir — entegrasyona başlamadan önce gerçek gateway/NS dokümantasyonuyla teyit edilmeli, ya da `e5/#` gibi bir wildcard ile dinleyip cihaz join olduğunda hangi topic'e ne geldiği gözlemlenmelidir.

---

## 3. Mesaj Zarfı (JSON Şeması)

### 3.1 Gelen (uplink/join) mesaj
```json
{
  "devEUI": "0080e115003e1f6f",
  "devAddr": "07891234",
  "applicationID": "1",
  "applicationName": "...",
  "deviceName": "...",
  "time": "2026-09-11T10:00:00+03:00",
  "data": "<base64 kodlu ham payload>"
}
```
İşlenmesi gereken tek alan **`data`** (base64) — geri kalanı loglama/tanımlama amaçlıdır.

### 3.2 Giden (downlink) mesaj
```json
{
  "confirmed": false,
  "fPort": 2,
  "data": "<base64 kodlu ham payload>"
}
```
- `fPort` her zaman **2**.
- `confirmed`: `true` ise ağ, cihazın downlink'i aldığını bir sonraki uplink'inin ACK bitiyle teyit etmesini bekler (cihaz bunu otomatik yapar). `false` ise "gönder ve unut" — teslimat garantisi/geri bildirimi yoktur. Bölüm 7'de hangi mesaj için hangisinin uygun olduğu açıklanıyor.

---

## 4. Uplink Mesajları (Cihazdan Sunucuya)

Her uplink payload'ının ilk 2 byte'ı ortaktır: `[0]=uplink counter (0-255 arası döngüsel, sıra takibi için)`, `[1]=mesaj tipi`.

### 4.1 STATUS — Tip `0x27` (14 byte)

| Byte | Alan | Açıklama |
|---|---|---|
| 0 | uplink counter | — |
| 1 | tip | `0x27` |
| 2:4 | batarya | mV, uint16 |
| 4:6 | sıcaklık | Q8.8 formatı, int16 — gerçek değer = `ham/256.0` °C |
| 6:10 | status sayacı | uint32 — bu status'un kaçıncı gönderim olduğu (her (re)join'de 0'a döner) |
| 10:14 | cihaz epoch'u | uint32, gönderim anındaki cihazın bildiği Unix zaman |

**Amaç:** Periyodik sağlık/telemetri bildirimi (varsayılan 1 saatte bir, bkz. Bölüm 5.3). Ayrıca **zaman senkronizasyon döngüsünün** birincil taşıyıcısıdır — `[6:10]`'daki sayaç, sunucunun doğru zaman düzeltmesini hangi status'a karşılık gönderdiğini eşleştirmesi için kullanılır (bkz. Bölüm 6). **Confirmed** olarak gönderilir.

**Örnek:** `00 27 0D 60 1D 00 00 00 00 00 6A 9C 2C 4B`
→ counter=0, tip=STATUS, batarya=0x0D60=3424mV, sıcaklık=0x1D00/256=29.0°C, sayaç=0, epoch=0x6A9C2C4B

### 4.2 LIVE_UID — Tip `0x45` (18 byte)

| Byte | Alan | Açıklama |
|---|---|---|
| 0 | uplink counter | — |
| 1 | tip | `0x45` |
| 2:6 | okuma zamanı | uint32, kartın fiziksel olarak okunduğu an |
| 6 | UID uzunluğu | 4 ya da 7 |
| 7:14 | UID | 7 byte'lık sabit alan, kısa UID'lerde kalan baytlar `0x00` |
| 14:18 | gönderim epoch'u | uint32 — bir yeniden deneme (retry) ise `[2:6]`'dan farklı olabilir |

**Amaç:** Bir RFID kartının okunduğu anı canlı olarak bildirir. Aynı format, daha önce teslim edilemeyip cihazın kalıcı belleğinde bekleyen bir kaydın **yeniden gönderilmesinde** de kullanılır — sunucu tarafında bu iki durumu ayırt etmenin yolu, `[2:6]` ile `[14:18]`'in aynı/farklı olmasıdır (retry'lerde farklıdır). **Confirmed** olarak gönderilir.

### 4.3 IND (Indication) — Tip `0x22` (6 byte)

| Byte | Alan | Açıklama |
|---|---|---|
| 0 | uplink counter | — |
| 1 | tip | `0x22` |
| 2:6 | gönderim epoch'u | uint32 |

**Amaç:** Veri taşımayan, hafif bir "yoklama" mesajı — tek amacı bir RX penceresi daha açıp sunucunun kuyrukladığı bir downlink'e (zaman senkronu, komut) ek bir teslim fırsatı vermektir. İki durumda gönderilir: (a) bir STATUS'un ACK sonucu belli olduktan ~3 sn sonra, (b) bir kart okuma denemesi kart bulamadan sonuçlandığında. **Unconfirmed** olarak gönderilir — sunucu tarafında özel bir işlem gerektirmez, sadece "cihaz uyanık ve dinliyor" sinyali olarak değerlendirilebilir.

### 4.4 QUERY_RESULT — Tip `0x46` (değişken uzunluk, çoklu mesaj)

Bir tarih aralığı sorgu komutuna (bkz. 5.2.2) yanıttır. Sonuçlar tek mesajda sığmayabileceği için **birden fazla ardışık uplink'e (batch)** bölünür.

**Header (6 byte):**

| Byte | Alan |
|---|---|
| 0 | uplink counter |
| 1 | tip = `0x46` |
| 2 | bu mesajdaki kayıt sayısı |
| 3 | bu ana kadar (bu sorgu için) gönderilen TOPLAM kayıt sayısı |
| 4 | bu mesajın batch index'i (0'dan başlar) |
| 5 | kırpıldı mı (0/1) — 1 ise cihazın sonuç kapasitesi doldu, daha fazla eşleşme olabilir; aralığı daraltmak gerekir |

**Header'dan sonra, art arda, her biri kendi kendini sınırlayan (TLV) kayıt blokları:**

| Alan | Boyut |
|---|---|
| UID uzunluğu | 1 byte (4 ya da 7) |
| UID | 4 ya da 7 byte (yukarıdaki uzunluğa göre) |
| zaman damgası | 4 byte, uint32 |

Bir mesajdaki kayıt sayısı **sabit değildir** — cihaz o anki radyo veri hızına (DR) göre bir mesaja sığabildiği kadar kaydı paketler; iyi sinyalde tek mesajda onlarca kayıt gidebilirken, zayıf sinyalde birkaç kayıtla sınırlı kalabilir. Sunucu, `[3]` alanını (toplam gönderilen) izleyerek sorgunun ne zaman tamamlandığını (kendi bildiği toplam eşleşme sayısına ulaşınca, ya da makul bir süre yeni batch gelmeyince) anlamalıdır — **ayrı bir "sorgu tamamlandı" mesajı yoktur.** **Unconfirmed** olarak gönderilir, ardışık batch'ler arasında ~10 saniyelik bir bekleme vardır (her batch için ayrı bir uplink fırsatı — yani buton basımı/status turu — gerekir).

**0 eşleşme durumu:** Cihaz `[2]=0, [3]=0` içeren tek, kayıtsız bir header mesajı gönderir — sunucu sessizce beklemek zorunda kalmaz.

---

## 5. Downlink Mesajları (Sunucudan Cihaza)

İlk byte, mesajın **kategorisini** belirler:

| İlk byte | Kategori |
|---|---|
| `0x01` | Zaman senkronizasyonu |
| `0x02` | Komut (ikinci byte hangi komut olduğunu belirtir) |

### 5.1 Zaman Senkronizasyonu — `0x01` (9 byte)

| Byte | Alan | Açıklama |
|---|---|---|
| 0 | tip | `0x01` |
| 1:5 | epoch | uint32 — cihazın saatine yazılacak Unix zaman |
| 5:9 | status sayacı | uint32 — **bu senkronun karşılık geldiği STATUS'un sayacıyla birebir aynı olmalı** |

**Amaç ve davranış:** Cihazın saatini düzeltir. `[5:9]`'daki sayaç, cihazın **kendi son gönderdiği** status sayacıyla eşleşmezse (örn. gecikmiş bir yanıtsa, ya da yanlış bir status'a aitse) mesaj **sessizce reddedilir** — cihaz hiçbir şey yapmaz, hiçbir uplink göndermez. Detaylar için Bölüm 6.

### 5.2 Komutlar — `0x02` kategorisi

Genel çerçeve: `[0]=0x02, [1]=komut ID, [2:N]=komuta özel parametreler`.

#### 5.2.1 Buzzer/LED — komut ID `0x01` (6 byte)

| Byte | Alan | Açıklama |
|---|---|---|
| 0 | `0x02` | |
| 1 | `0x01` | |
| 2 | hedef | bit0=buzzer, bit1=LED → `1`=buzzer, `2`=LED, `3`=ikisi |
| 3 | pattern | `0`=sürekli, `1`=bip-bip |
| 4:6 | süre | **SANİYE**, uint16, büyük-endian |

**Amaç:** Cihazın sesli/görsel uyarısını uzaktan tetikler (örn. kaybolmuş bir cihazı yerinde bulma). **Cihaz, istenen süreyi ne olursa olsun (hatalı/aşırı büyük bir değer dahil) kendi azami sınırına (3 dakika) sessizce kırpar** — güvenlik amaçlı, sunucu tarafında ayrıca bir üst sınır uygulamaya gerek yoktur ama süre alanına gerçekçi değerler göndermek önerilir.

#### 5.2.2 Tarih Aralığı Sorgusu — komut ID `0x02` (10 byte)

| Byte | Alan | Açıklama |
|---|---|---|
| 0 | `0x02` | |
| 1 | `0x02` | |
| 2:6 | başlangıç zamanı | uint32, epoch |
| 6:10 | bitiş zamanı | uint32, epoch |

**Amaç:** Cihazın kalıcı belleğinde bu tarih aralığına giren TÜM kayıtları (daha önce teslim edilmiş olsun olmasın) `0x46` (QUERY_RESULT) mesajlarıyla geri ister. Yanıt formatı için Bölüm 4.4'e bakın.

**Önemli:** `başlangıç > bitiş` ise, ya da hâlihazırda başka bir sorgu raporu devam ediyorsa, komut **sessizce yok sayılır** — cihaz hiçbir uplink göndermez, hiçbir hata bildirimi yoktur. Sunucu bu durumu ancak "makul sürede yanıt gelmedi" diye kendi tarafında tespit edebilir.

#### 5.2.3 STATUS Aralığını Değiştirme — komut ID `0x03` (4 byte)

| Byte | Alan | Açıklama |
|---|---|---|
| 0 | `0x02` | |
| 1 | `0x03` | |
| 2:4 | çarpan | uint16, büyük-endian — **gerçek aralık = çarpan × 30 saniye** |

**Sınırlar:** çarpan `[1, 2880]` (30 saniye – 24 saat). Bu aralığın dışındaki **her değer** (0 dahil) cihaz tarafında **reddedilir** — mevcut ayar hiç değişmez, kırpma yapılmaz, ve **hiçbir uplink/onay gönderilmez**. Sunucu kendi tarafında da gönderim öncesi bu sınırları doğrulamalıdır (aksi halde boşuna bir downlink fırsatı harcanır).

**Amaç ve kalıcılık:** Periyodik STATUS gönderim sıklığını uzaktan ayarlar. Değişiklik anında uygulanır (bir sonraki STATUS, komutun geldiği andan itibaren yeni süre kadar sonra gelir) VE cihazın kalıcı belleğinde saklanır — cihaz yeniden başlasa bile (beklenmedik bir reset dahil) ayar korunur.

---

## 6. Zaman Senkronizasyon Mekanizması

Cihazın kendi saati **bağımsız olarak doğru değildir** — düzenli sunucu düzeltmeleriyle ayarlı kalır. Doğru çalışması için sunucunun uyması gereken protokol:

1. Her **STATUS** (`0x27`) uplink'i alındığında, sunucu **mümkün olduğunca hızlı**, o mesajın `[6:10]` alanındaki status sayacını **aynen** taşıyan bir zaman senkron downlink'i kuyruğa almalıdır.
2. Cihaz, gelen sayacı **kendi en son gönderdiği** status sayacıyla karşılaştırır — **birebir eşleşmiyorsa** yanıt sessizce reddedilir. Bu, gecikmiş/bayat bir yanıtın yanlışlıkla kabul edilmesini önler.
3. Sayaç eşleşse bile, taşınan zaman cihazın **zaten bildiği zamandan anlamlı ölçüde geriye** gidiyorsa (ağ gecikmesi kaynaklı, çok bayat bir yanıt olabilir) yine reddedilir — gerçek RTC sapmasına izin verecek şekilde, geçen süreyle orantılı bir tolerans uygulanır.
4. **Class A kısıtı:** Downlink yalnızca bir uplink'in hemen ardından açılan RX penceresinde teslim edilebilir. Sunucu yanıtı kuyruğa almakta gecikirse (o STATUS'un kendi penceresini kaçırırsa), yanıt ancak **bir sonraki** uplink fırsatında (başka bir STATUS/kart okuma/IND mesajı) teslim edilebilir — bu noktada sayaç muhtemelen ilerlemiş olacağından yanıt **reddedilecektir**. Bu yüzden düşük gecikme kritik önemdedir.
5. **Join anında:** Cihaz her başarılı (re)join'de kendi status sayacını **0**'a sıfırlar. Sunucu, join event'i aldığında status sayacı **0** ile bir zaman senkron yanıtı göndermelidir — bu, ilk STATUS'tan önce bile cihazın saatini makul bir başlangıç noktasına getirir. Bu özel durumda, kritik/tek seferlik olduğu için **confirmed** downlink kullanılması önerilir.
6. **Confirmed kullanma tercihi:** Rutin (STATUS-tetikli) zaman senkron yanıtlarının **unconfirmed** gönderilmesi önerilir — confirmed bir downlink, cihazın otomatik olarak ekstra bir ACK-uplink'i göndermesine yol açar, bu da sırada bekleyen başka bir confirmed downlink'i tetikleyip beklenmedik bir zincirlemeye (kaskad) neden olabilir.

---

## 7. Hata/Reddetme Davranışları — Sunucunun Mutlaka Bilmesi Gerekenler

**En kritik genel kural: Cihaz, hiçbir geçersiz/reddedilen downlink için sunucuya açık bir hata bildirimi göndermez.** Aşağıdaki tüm durumlar cihaz tarafında sadece yerel olarak (fiziksel/USB log erişimi ile görülebilir) loglanır — sunucu tarafında "sessizlik" tek belirtidir.

| Durum | Cihaz davranışı |
|---|---|
| Zaman senkron sayacı uyuşmuyor | Sessiz red |
| Zaman senkron, izin verilenden fazla geriye sıçratıyor | Sessiz red |
| Buzzer/LED süresi azami sınırı aşıyor | **Reddedilmez** — sessizce kırpılır ve komut uygulanır (diğerlerinden farklı davranış) |
| Tarih aralığı sorgusu: başlangıç > bitiş | Sessiz red, hiç uplink yok |
| Tarih aralığı sorgusu: zaten bir sorgu raporu sürüyor | Sessiz red, hiç uplink yok |
| STATUS aralığı çarpanı `[1,2880]` dışında | Sessiz red, mevcut ayar korunur |
| Tanınmayan komut ID / bilinmeyen mesaj tipi | Sessiz red |

**Pratik sonuç:** Sunucu tasarımı "gönderdim, cevap gelmezse başarısız olmuştur" varsayımına dayanmalı, cihazdan bir "ret" onayı **beklememelidir**. Geçerlilik kontrollerini (aralıklar, sınırlar) mümkün olduğunca **sunucu tarafında, gönderim öncesi** yapmak, boşa harcanan downlink fırsatlarını azaltır.

---

## 8. Kalıcı Depolama (Buffer) Davranışı

- Cihaz, **sadece RFID kart okuma olaylarını** kalıcı belleğe yazar — STATUS mesajları hiçbir zaman kalıcı belleğe düşmez.
- Bir kayıt, bir kart okuması **teslim edilemediğinde** (join yok, ACK alınamadı, duty-cycle kısıtlaması vb.) oluşturulur — yani buffer'a düşen her kayıt, o an teslim edilemeyen ama sonradan otomatik olarak yeniden denenecek bir olaydır.
- Her kayıt şunları taşır: zaman damgası, UID + UID uzunluğu, teslim durumu (bekliyor/teslim edildi), bir kayıt kimliği.
- **Kapasite: 100 kayıt, dairesel** — kapasite dolduğunda en eski kayıt sessizce üzerine yazılır (silinir). Uzun süreli bağlantı kesintilerinde, kapasiteyi aşan olaylar kalıcı olarak kaybolur.
- Teslim edilmiş kayıtlar buffer'dan **silinmez**, sadece durumları güncellenir — bu yüzden Bölüm 4.4/5.2.2'deki tarih aralığı sorgusu, teslim durumundan bağımsız olarak (hem teslim edilmiş hem bekleyen) **tüm** geçmiş kayıtları döndürebilir.

---

## 9. Genel Notlar

- Tüm downlink'ler `fPort=2` üzerinden gitmeli — farklı bir port'a gönderilen mesajlar cihaz tarafında hiç işlenmez.
- Cihaz **EU868 duty-cycle** kurallarına tabidir — sunucunun gereksiz/sık downlink kuyruklaması, cihazın kendi uplink bütçesini de dolaylı olarak etkileyebilir (özellikle confirmed downlink'lerin tetiklediği otomatik ACK-uplink'leri nedeniyle).
- Uplink `[0]` byte'ındaki sayaç (uplink counter), 0-255 arası döngüsel bir genel sıra numarasıdır — STATUS'un kendi iç `[6:10]` sayacıyla (zaman senkron eşleştirmesi için kullanılan) **karıştırılmamalıdır**, ikisi farklı amaçlara hizmet eder.

import json
import base64
import struct
import time
import threading
import paho.mqtt.client as mqtt
from datetime import datetime

# --- MQTT AYARLARI ---
MQTT_BROKER = "134.122.68.112"
MQTT_PORT = 1883
MQTT_TOPIC = "e5/up"
# UYARI: "e5/join" topic adi DOGRULANMADI - UG63'un join-event MQTT semasi
# bilinmiyor, e5/up ile ayni "e5/<olay>" kaliba uyduguu VARSAYILDI. Gercek
# topic adini gateway/NS dokumanindan teyit et, ya da once "e5/#" ile wildcard
# dinleyip cihaz join oldugunda hangi topic'e ne geldigini gozlemle.
MQTT_JOIN_TOPIC = "e5/join"
MQTT_DOWNLINK_TOPIC_FMT = "e5/down/{deveui}"
MQTT_USER = "admin"
MQTT_PASS = "admin"

# fPort - cihazdaki LORAWAN_USER_APP_PORT ile aynı olmalı (uplink'lerle aynı port,
# downlink tarafı payload uzunluğuna gore ayirt ediyor).
DOWNLINK_FPORT = 2

# --- MESAJ TİPLERİ (uplink) ---
LORA_RFID_MSG_TYPE_LIVE_UID   = 0x45
LORA_RFID_MSG_TYPE_STORED_UID = 0x54
LORA_RFID_MSG_TYPE_STATUS     = 0x27
LORA_RFID_MSG_TYPE_IND        = 0x22
LORA_RFID_MSG_TYPE_QUERY_RESULT = 0x46


def _fmt_epoch(v):
    """Unix epoch'u okunabilir bir tarihe cevirir - parse hatasinda ham
    sayiyi dondurur (tum decode fonksiyonlarinca paylasilan ortak yardimci)."""
    try:
        return f"{v} ({datetime.fromtimestamp(v).strftime('%Y-%m-%d %H:%M:%S')})"
    except Exception:
        return str(v)

# --- MESAJ TİPLERİ (downlink, biz tanımlıyoruz - cihaz tarafındaki
#     LORA_DOWNLINK_MSG_TYPE_TIME_SYNC ile birebir aynı olmalı) ---
LORA_DOWNLINK_MSG_TYPE_TIME_SYNC = 0x01

# --- GENEL KOMUT PROTOKOLÜ (downlink) - cihaz tarafındaki
#     LORA_COMMAND_DOWNLINK_TYPE / LORA_CMD_ID_BUZZER_LED ile birebir aynı
#     olmalı, bkz lora_app.h ---
LORA_COMMAND_DOWNLINK_TYPE = 0x02
LORA_CMD_ID_BUZZER_LED = 0x01
# Cihazin kendi azami siniri (bkz lora_app.c MAX_BUZZER_LED_DURATION_MS).
# Sure alani telden SANIYE cinsinden gidiyor (ms degil) - 2 byte'a ms
# sigmayacagi icin ("kayip cihaz bulma" senaryosu dakikalar surebiliyor,
# 2 byte ms ile max ~65,5 sn olurdu, yetersiz kalirdi).
MAX_BUZZER_LED_DURATION_S = 180

# Tarih araligi sorgu komutu (bkz lora_app.h LORA_CMD_ID_QUERY_BY_DATE /
# LORA_RFID_MSG_TYPE_QUERY_RESULT) - cihaz eslesen kayitlari coklu, DR'ye
# duyarli batch mesajla (~10 sn arayla) geri raporlar.
LORA_CMD_ID_QUERY_BY_DATE = 0x02

# En son gorulen cihazin DevEUI'si - interaktif komutlarda deveui elle
# yazmaya gerek kalmasin diye.
last_seen_dev_eui = None


def _parse_time_arg(value):
    """Epoch (int, saniye) ya da 'YYYY-MM-DD' (o gunun 00:00'i, yerel saat)
    formatinda TEK bir kelime kabul eder - interaktif komutta boslukla
    ayrilan tarih+saat desteklenmiyor (basitlik icin)."""
    try:
        return int(value)
    except ValueError:
        pass
    try:
        return int(datetime.strptime(value, "%Y-%m-%d").timestamp())
    except ValueError:
        raise ValueError(f"Zaman formati anlasilamadi: '{value}' (epoch sayi ya da YYYY-MM-DD olmali)")


def send_query_by_date_command(client, dev_eui, start_ts, end_ts, confirmed=False):
    """Cihaza tarih araligi sorgu komutu gonderir:
    [0]=0x02 (LORA_COMMAND_DOWNLINK_TYPE)
    [1]=0x02 (LORA_CMD_ID_QUERY_BY_DATE)
    [2:6]=start_timestamp, buyuk-endian (uint32)
    [6:10]=end_timestamp, buyuk-endian (uint32)

    Cihaz eslesen kayitlari (varsa) LORA_RFID_MSG_TYPE_QUERY_RESULT (0x46)
    tipinde, coklu, DR'ye duyarli batch mesajlarla geri raporlar - her
    batch ayri bir uplink, aralarinda ~10 sn (QUERY_REPORT_DELAY_MS) var.
    Class A geregi ilk batch bile aninda gitmez, cihazin BIR SONRAKI
    uplink firsatinda (buton basimi/status turu) teslim edilir."""
    if not dev_eui or dev_eui == "Bilinmiyor":
        print("⚠️  DevEUI bilinmiyor, sorgu komutu gonderilemedi.")
        return
    if start_ts > end_ts:
        print(f"⚠️  Gecersiz aralik: start({start_ts}) > end({end_ts}).")
        return

    payload_bytes = (bytes([LORA_COMMAND_DOWNLINK_TYPE, LORA_CMD_ID_QUERY_BY_DATE])
                      + struct.pack(">I", start_ts & 0xFFFFFFFF)
                      + struct.pack(">I", end_ts & 0xFFFFFFFF))
    payload_b64 = base64.b64encode(payload_bytes).decode("ascii")

    downlink_msg = {
        "confirmed": confirmed,
        "fPort": DOWNLINK_FPORT,
        "data": payload_b64,
    }

    topic = MQTT_DOWNLINK_TOPIC_FMT.format(deveui=dev_eui)
    client.publish(topic, json.dumps(downlink_msg))
    print(f"🔎 Tarih araligi sorgusu kuyruga alindi -> {topic}")
    print(f"   start = {_fmt_epoch(start_ts)}")
    print(f"   end   = {_fmt_epoch(end_ts)}")
    print(f"   payload_hex={payload_bytes.hex().upper()}")
    print("   (Cihaz eslesen kayitlari coklu batch mesajla, ~10 sn arayla geri gonderecek - "
          "her batch icin ayrica bir uplink firsati (buton basimi/status) gerekir.)")


def send_buzzer_led_command(client, dev_eui, target=1, pattern=1, duration_s=5, confirmed=False):
    """Cihaza buzzer/LED komutu gonderir:
    [0]=0x02 (LORA_COMMAND_DOWNLINK_TYPE)
    [1]=0x01 (LORA_CMD_ID_BUZZER_LED)
    [2]=hedef (bit0=buzzer, bit1=led - orn. 1=sadece buzzer, 2=sadece led, 3=ikisi)
    [3]=pattern (0=surekli, 1=bip-bip)
    [4:6]=istenen sure, SANIYE, buyuk-endian (uint16, azami 65535 sn ~ 18,2 saat)

    NOT: duration_s burada BILEREK MAX_BUZZER_LED_DURATION_S ile sinirlanmiyor -
    cihazin KENDI ic guvenlik sinirini (docs/eylem-plani.md madde 4) test
    edebilmek icin buradan bilerek daha buyuk bir deger de gonderebilmelisin,
    cihaz onu kendi tarafinda kirpmali."""
    if not dev_eui or dev_eui == "Bilinmiyor":
        print("⚠️  DevEUI bilinmiyor, buzzer/led komutu gonderilemedi.")
        return

    duration_s = max(0, min(duration_s, 0xFFFF))  # sadece 2 byte'a sigdirmak icin, cihaz sinirindan BAGIMSIZ
    payload_bytes = (bytes([LORA_COMMAND_DOWNLINK_TYPE, LORA_CMD_ID_BUZZER_LED, target & 0xFF, pattern & 0xFF])
                      + struct.pack(">H", duration_s))
    payload_b64 = base64.b64encode(payload_bytes).decode("ascii")

    downlink_msg = {
        "confirmed": confirmed,
        "fPort": DOWNLINK_FPORT,
        "data": payload_b64,
    }

    topic = MQTT_DOWNLINK_TOPIC_FMT.format(deveui=dev_eui)
    client.publish(topic, json.dumps(downlink_msg))
    print(f"🔊 Buzzer/LED komutu kuyruga alindi -> {topic} "
          f"(hedef={target}, pattern={pattern}, istenen_sure={duration_s} sn, "
          f"cihazin azami siniri={MAX_BUZZER_LED_DURATION_S} sn, "
          f"payload_hex={payload_bytes.hex().upper()})")
    print("   (Class A geregi bu, cihazin BIR SONRAKI uplink'inin RX penceresinde teslim edilir - "
          "hemen degil, bir sonraki buton basimi/status turunde etkili olur.)")


def send_time_sync_downlink(client, dev_eui, status_count=0, confirmed=False):
    """Cihaza [1 byte tip][4 byte buyuk-endian Unix epoch (sn)][4 byte
    buyuk-endian status sayaci] seklinde bir zaman senkronizasyon downlink'i
    kuyruga alir.

    status_count: bu yanitin hangi status uplink'ine karsilik geldigi (cihazin
    kendi gonderdigi status payload'inin sayac alani). Cihaz tarafi (OnRxData)
    bunu kendi EN SON gonderdigi status'un sayaciyla karsilastirip SADECE
    ESITSE kabul ediyor - herhangi bir gecikme/telafi hesabi YOK, stale bir
    yanit sessizce reddediliyor. Join anindaki cagrida henuz hic status
    gonderilmemis oldugundan 0 kullanilir; bu varsayimin her rejoin'de de
    gecerli kalmasi icin cihaz tarafi sayacini HER basarili (re)join'de
    sifirliyor (bkz OnJoinRequest, lora_app.c).

    confirmed: SADECE join-tetikli cagrida True kullan. Status-tetikli
    cagrida bilerek False - confirmed=True her teyitte cihazin otomatik bir
    ack-uplink daha gondermesine (LoRaWAN spec geregi), bu da art arda gelen
    baska bir confirmed downlink'i tetikleyip kaskad/tikanma riski yaratiyor.

    Cihaz Class A oldugu icin bu downlink aninda gitmez - cihazin bir sonraki
    uplink'inin RX1/RX2 penceresinde teslim edilir."""
    if not dev_eui or dev_eui == "Bilinmiyor":
        print("⚠️  DevEUI bilinmiyor, time-sync downlink gonderilemedi.")
        return

    now_epoch_s = int(time.time())
    payload_bytes = (bytes([LORA_DOWNLINK_MSG_TYPE_TIME_SYNC])
                      + struct.pack(">I", now_epoch_s)
                      + struct.pack(">I", status_count & 0xFFFFFFFF))
    payload_b64 = base64.b64encode(payload_bytes).decode("ascii")

    # NOT: Bu JSON semasi genel LoRaWAN network server konvansiyonlarina gore
    # tahmin edildi (confirmed/fPort/data). UG63'un gercek downlink MQTT
    # semasiyla eslesmezse buradaki key isimlerini dokumana gore duzelt.
    downlink_msg = {
        "confirmed": confirmed,
        "fPort": DOWNLINK_FPORT,
        "data": payload_b64,
    }

    topic = MQTT_DOWNLINK_TOPIC_FMT.format(deveui=dev_eui)
    client.publish(topic, json.dumps(downlink_msg))
    print(f"⏰ Time-sync downlink kuyruga alindi -> {topic} "
          f"(epoch={now_epoch_s}, status_count={status_count}, confirmed={confirmed}, "
          f"payload_hex={payload_bytes.hex().upper()})")

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("✅ MQTT Sunucusuna başarıyla bağlanıldı.")
        client.subscribe(MQTT_TOPIC)
        client.subscribe(MQTT_JOIN_TOPIC)
        print(f"📡 '{MQTT_TOPIC}' ve '{MQTT_JOIN_TOPIC}' topikleri dinleniyor...\n")
        print("-" * 50)
    else:
        print(f"❌ Bağlantı hatası, Hata Kodu: {rc}")

def on_join_message(client, msg):
    """Join event mesaji - topic adi/JSON semasi dogrulanmadi, ilk test
    sirasinda gelen gercek mesaji loglayip formatini teyit et."""
    try:
        payload_str = msg.payload.decode('utf-8')
        payload = json.loads(payload_str)
        dev_eui = payload.get("devEUI", "Bilinmiyor")
        global last_seen_dev_eui
        if dev_eui and dev_eui != "Bilinmiyor":
            last_seen_dev_eui = dev_eui
        print(f"\n🔗 JOIN EVENT (DevEUI: {dev_eui}) - ham payload: {payload_str}")
        # Join anında henüz hiç status gönderilmedi -> status_count=0.
        # confirmed=True: join tek seferlik ve kritik, garantili teslimat istiyoruz.
        send_time_sync_downlink(client, dev_eui, status_count=0, confirmed=True)
    except json.JSONDecodeError:
        print(f"⚠️ Join topic'inde JSON olmayan bir mesaj geldi: {msg.payload}")
    except Exception as e:
        print(f"⚠️ Join mesaji islenirken hata: {e}")

def on_message(client, userdata, msg):
    if msg.topic == MQTT_JOIN_TOPIC:
        on_join_message(client, msg)
        return
    try:
        # 1. Gelen mesajı string olarak al ve JSON'a çevir
        payload_str = msg.payload.decode('utf-8')
        payload = json.loads(payload_str)
        
        # 2. JSON içindeki 'data' (base64) verisini al
        data_b64 = payload.get("data")
        if not data_b64:
            return
            
        # 3. Base64 verisini byte (ve hex) formatına çevir
        data_bytes = base64.b64decode(data_b64)
        data_hex = data_bytes.hex().upper()
        
        dev_eui = payload.get("devEUI", "Bilinmiyor")
        global last_seen_dev_eui
        if dev_eui and dev_eui != "Bilinmiyor":
            last_seen_dev_eui = dev_eui

        print(f"\n📨 YENİ MESAJ (DevEUI: {dev_eui})")
        print(f"Raw Base64 : {data_b64}")
        print(f"Hex        : {data_hex}")
        
        # Byte uzunluğu en az 2 olmalı (Counter + MsgType)
        if len(data_bytes) >= 2:
            msg_type = data_bytes[1]
            
            # 4. Mesaj tipi 0x45 (LORA_RFID_MSG_TYPE_LIVE_UID) ise parse et
            if msg_type == LORA_RFID_MSG_TYPE_LIVE_UID:
                print("🎯 [Eşleşme] Mesaj Tipi: 0x45 (LIVE_UID) tespit edildi. Parse ediliyor...")
                
                # Buffer[0]    = UplinkCounter (1 byte)
                # Buffer[1]    = Type (1 byte)
                # Buffer[2:6]  = Timestamp - kart okunduğu an (4 byte, Big Endian)
                # Buffer[6]    = UUID Length (1 byte)
                # Buffer[7:14] = UID verisi (7 byte, sabit, uuid_len'e göre doldurulmuş)
                # Buffer[14:18]= Cihazın GÖNDERİM anındaki epoch tahmini (4 byte, Big Endian)

                if len(data_bytes) >= 18:
                    # Timestamp'i 4 byte Big Endian (">I") olarak oku
                    timestamp_val = struct.unpack(">I", data_bytes[2:6])[0]
                    send_now_epoch = struct.unpack(">I", data_bytes[14:18])[0]

                    # Timestamp'leri okunabilir tarihe çevir (modül seviyesindeki ortak yardımcı)
                    ts_display = _fmt_epoch(timestamp_val)
                    send_epoch_display = _fmt_epoch(send_now_epoch)

                    # UUID Uzunluğunu al
                    uuid_len = data_bytes[6]

                    # UID verisini uzunluğa göre kopyala
                    if len(data_bytes) >= 7 + uuid_len:
                        uid_bytes = data_bytes[7:7+uuid_len]
                        uid_hex = uid_bytes.hex().upper()

                        # Formatlı şekilde ekrana yazdır
                        print("\n" + "="*40)
                        print("       RFID LIVE UID VERİSİ       ")
                        print("="*40)
                        print(f"➤ Timestamp (okuma anı) : {ts_display}")
                        print(f"➤ Cihaz epoch (gönderim anı) : {send_epoch_display}")
                        print(f"➤ UID Length : {uuid_len} byte")
                        print(f"➤ UID (Hex)  : {uid_hex}")
                        print("="*40 + "\n")
                    else:
                        print("⚠️ Hata: Gelen byte dizisi, belirtilen UID uzunluğunu (uuid_len) içerecek kadar uzun değil.")
                else:
                    print("⚠️ Hata: Veri paketi çok kısa (en az 18 byte gerekli). Timestamp/Length/gönderim-epoch byte'ları okunamıyor.")
            # 5. Mesaj tipi 0x27 (LORA_RFID_MSG_TYPE_STATUS) ise parse et
            elif msg_type == LORA_RFID_MSG_TYPE_STATUS:
                print("🔋 [Eşleşme] Mesaj Tipi: 0x27 (STATUS) tespit edildi. Parse ediliyor...")

                # Buffer[0]     = UplinkCounter (1 byte)
                # Buffer[1]     = Type (1 byte)
                # Buffer[2:4]   = Batarya gerilimi, mV, Big Endian (uint16)
                # Buffer[4:6]   = Sıcaklık, Q8.8 formatı, Big Endian (int16, değer/256.0 = derece C)
                # Buffer[6:10]  = Kaçıncı status gönderimi (uint32, Big Endian, her rejoin'de 0'a döner)
                # Buffer[10:14] = Cihazın GÖNDERİM anındaki epoch tahmini (4 byte, Big Endian)

                if len(data_bytes) >= 14:
                    battery_mv = struct.unpack(">H", data_bytes[2:4])[0]
                    temp_q8_8 = struct.unpack(">h", data_bytes[4:6])[0]
                    temp_c = temp_q8_8 / 256.0
                    status_count = struct.unpack(">I", data_bytes[6:10])[0]
                    device_now_epoch = struct.unpack(">I", data_bytes[10:14])[0]
                    device_now_display = _fmt_epoch(device_now_epoch)

                    print("\n" + "="*40)
                    print("         DURUM (STATUS) VERİSİ         ")
                    print("="*40)
                    print(f"➤ Batarya    : {battery_mv} mV")
                    print(f"➤ Sıcaklık   : {temp_c:.2f} °C")
                    print(f"➤ Status Sayacı : {status_count}")
                    print(f"➤ Cihaz epoch (gönderim anı) : {device_now_display}")
                    print("="*40 + "\n")

                    # Status mesaji = cihaz canli ve dinliyor demek - zaman
                    # senkronizasyonu icin ideal an. confirmed=False (varsayilan):
                    # bilerek garantisiz birakildi, cunku confirmed=True her
                    # teyitte cihazin otomatik ack-uplink'i tetiklemesine ve bu
                    # da baska bir confirmed downlink'i tetikleyip kaskad/tikanma
                    # riskine yol aciyor. status_count'u geri gonderiyoruz - cihaz
                    # bunu kendi en son gonderdigi sayacla karsilastirip SADECE
                    # eslesirse (taze yanitsa) kabul edecek, stale ise reddedecek.
                    send_time_sync_downlink(client, dev_eui, status_count=status_count, confirmed=False)
                else:
                    print("⚠️ Hata: Veri paketi çok kısa. Batarya/sıcaklık/sayaç/epoch byte'ları okunamıyor (en az 14 byte gerekli).")
            # 6. Mesaj tipi 0x46 (LORA_RFID_MSG_TYPE_QUERY_RESULT) ise parse et
            elif msg_type == LORA_RFID_MSG_TYPE_QUERY_RESULT:
                print("📦 [Eşleşme] Mesaj Tipi: 0x46 (QUERY_RESULT) tespit edildi. Parse ediliyor...")

                # Buffer[0] = UplinkCounter (1 byte)
                # Buffer[1] = Type (1 byte)
                # Buffer[2] = Bu mesajdaki kayıt sayısı (1 byte)
                # Buffer[3] = Bu ana kadar rapor edilen TOPLAM kayıt sayısı (1 byte)
                # Buffer[4] = Bu mesajın batch index'i (1 byte, 0'dan başlar)
                # Buffer[5] = Kırpıldı mı (0/1) - PCB_TRUNCATED, daha fazla eşleşme olabilir
                # Buffer[6:] = art arda kayıt blokları, her biri kendi kendini sınırlar (TLV):
                #              [uuid_uzunluk(1)][uuid(uuid_uzunluk byte)][timestamp(4 byte BE)]

                if len(data_bytes) >= 6:
                    record_count = data_bytes[2]
                    total_so_far = data_bytes[3]
                    batch_index = data_bytes[4]
                    truncated = data_bytes[5]

                    print("\n" + "=" * 40)
                    print("   TARİH ARALIĞI SORGU SONUCU (batch)   ")
                    print("=" * 40)
                    print(f"➤ Batch index          : {batch_index}")
                    print(f"➤ Bu mesajdaki kayıt   : {record_count}")
                    print(f"➤ Şu ana kadar toplam  : {total_so_far}")
                    print(f"➤ Kırpıldı mı          : {'EVET - aralığı daraltın' if truncated else 'Hayır'}")

                    offset = 6
                    parse_error = False
                    for i in range(record_count):
                        if offset >= len(data_bytes):
                            print("⚠️ Hata: beklenen kayıt sayısı kadar veri yok (eksik/bozuk mesaj).")
                            parse_error = True
                            break
                        uuid_len = data_bytes[offset]
                        offset += 1
                        if uuid_len not in (4, 7):
                            print(f"⚠️ Hata: beklenmeyen uuid_uzunluk={uuid_len}, parse durduruldu.")
                            parse_error = True
                            break
                        if offset + uuid_len + 4 > len(data_bytes):
                            print("⚠️ Hata: bu kayıt için yeterli byte yok (mesaj kesilmiş olabilir).")
                            parse_error = True
                            break
                        uid_bytes = data_bytes[offset:offset + uuid_len]
                        offset += uuid_len
                        ts_val = struct.unpack(">I", data_bytes[offset:offset + 4])[0]
                        offset += 4
                        print(f"   [{i + 1}] UID={uid_bytes.hex().upper()}  timestamp={_fmt_epoch(ts_val)}")

                    if record_count == 0 and not parse_error:
                        print("   (Bu sorgu için eşleşen kayıt yok.)")
                    print("=" * 40 + "\n")
                else:
                    print("⚠️ Hata: Sorgu sonucu paketi çok kısa (en az 6 byte header gerekli).")
            else:
                print(f"ℹ️ Bilinmeyen/işlenmeyen mesaj tipi: 0x{msg_type:02X}. İşlem atlandı.")
        
    except json.JSONDecodeError:
        print("⚠️ Hata: Gelen payload geçerli bir JSON formatında değil.")
    except Exception as e:
        print(f"⚠️ Beklenmeyen bir hata oluştu: {e}")

# --- İSTEMCİ BAŞLATMA ---
client = mqtt.Client()

# Kullanıcı adı ve şifre tanımlaması
client.username_pw_set(MQTT_USER, MQTT_PASS)

# Callback fonksiyonlarının atanması
client.on_connect = on_connect
client.on_message = on_message

def _print_downlink_format_banner():
    """Downlink komut payload yapisini ve gercek, hesaplanmis ornek
    mesajlari gosterir - cihaz tarafindaki lora_app.h/LoraCommand_HandleDownlink
    ile birebir ayni format (bkz docs/eylem-plani.md madde 4)."""
    ornek1 = bytes([LORA_COMMAND_DOWNLINK_TYPE, LORA_CMD_ID_BUZZER_LED, 1, 1]) + struct.pack(">H", 5)
    ornek2 = bytes([LORA_COMMAND_DOWNLINK_TYPE, LORA_CMD_ID_BUZZER_LED, 3, 0]) + struct.pack(">H", 8)
    ornek3 = bytes([LORA_COMMAND_DOWNLINK_TYPE, LORA_CMD_ID_BUZZER_LED, 1, 0]) + struct.pack(">H", 300 & 0xFFFF)

    print("\n" + "=" * 60)
    print("DOWNLINK KOMUT PAYLOAD YAPISI (sunucudan cihaza, 6 byte)")
    print("=" * 60)
    print("  byte[0]   = 0x02              <- mesaj kategorisi: KOMUT (sabit)")
    print("  byte[1]   = 0x01              <- komut ID: buzzer/led (sabit)")
    print("  byte[2]   = hedef             <- 1=buzzer, 2=led, 3=ikisi birden")
    print("  byte[3]   = pattern           <- 0=surekli, 1=bip-bip")
    print("  byte[4:6] = sure_sn           <- buyuk-endian (MSB once), uint16 SANIYE, azami 65535")
    print("-" * 60)
    print("ORNEK MESAJLAR:")
    print(f"  buzzer 1 1 5      (sadece buzzer, bip-bip, 5 sn)")
    print(f"    -> hex: {ornek1.hex(' ').upper()}")
    print(f"       [02]=komut [01]=buzzer/led [01]=hedef:buzzer [01]=pattern:bip-bip [00 05]=5 sn")
    print(f"  buzzer 3 0 8      (buzzer+led, surekli, 8 sn)")
    print(f"    -> hex: {ornek2.hex(' ').upper()}")
    print(f"       [02]=komut [01]=buzzer/led [03]=hedef:ikisi [00]=pattern:surekli [00 08]=8 sn")
    print(f"  buzzer 1 0 300    (KAYIP CIHAZ SENARYOSU - 5 dakika istendi, cihazin azami siniri "
          f"{MAX_BUZZER_LED_DURATION_S} sn'yi test etmek icin)")
    print(f"    -> hex: {ornek3.hex(' ').upper()}")
    print(f"       Cihaz bunu kendi ic guvenlik siniriyla kirpar - UART logunda")
    print(f"       'istenen=300 sn, uygulanan={MAX_BUZZER_LED_DURATION_S * 1000} ms' gorulmeli.")
    print("=" * 60)

    ornek4 = (bytes([LORA_COMMAND_DOWNLINK_TYPE, LORA_CMD_ID_QUERY_BY_DATE])
              + struct.pack(">I", 1725100800) + struct.pack(">I", 1725200800))
    print("\nTARIH ARALIGI SORGU KOMUTU (10 byte):")
    print("  byte[0]   = 0x02              <- mesaj kategorisi: KOMUT (sabit)")
    print("  byte[1]   = 0x02              <- komut ID: tarih araligi sorgu (sabit)")
    print("  byte[2:6] = start_timestamp   <- buyuk-endian (MSB once), uint32, epoch (sn)")
    print("  byte[6:10]= end_timestamp     <- buyuk-endian (MSB once), uint32, epoch (sn)")
    print("-" * 60)
    print(f"  query 1725100800 1725200800   (iki epoch ile)")
    print(f"    -> hex: {ornek4.hex(' ').upper()}")
    print(f"  query 2026-09-01 2026-09-02   (YYYY-MM-DD ile, o gunun 00:00'i)")
    print("-" * 60)
    print("SONUC MESAJI (cihazdan, 0x46 QUERY_RESULT, coklu batch halinde gelir):")
    print("  byte[0]=counter [1]=0x46 [2]=bu mesajdaki kayit [3]=su ana kadar toplam")
    print("  byte[4]=batch index [5]=kirpildi mi (0/1)")
    print("  byte[6:]=kayitlar, her biri: [uuid_uzunluk(1)][uuid(4/7)][timestamp(4, BE)]")
    print("  (Her batch ayri bir uplink - cihazin bir sonraki uplink firsatinda,")
    print("   ~10 sn arayla, DR'ye gore o an kac kayit sigiyorsa o kadar gelir.)")
    print("=" * 60)


def _print_command_help():
    _print_downlink_format_banner()
    print("\nKomutlar (Enter ile calistir):")
    print("  buzzer [hedef] [pattern] [sure_sn]")
    print("      hedef: 1=buzzer, 2=led, 3=ikisi (varsayilan 1)")
    print("      pattern: 0=surekli, 1=bip-bip (varsayilan 1)")
    print("      sure_sn: SANIYE cinsinden (varsayilan 5). Cihazin azami siniri "
          f"{MAX_BUZZER_LED_DURATION_S} sn (kayip cihaz bulma senaryosu icin) - daha "
          "buyugunu gonderip cihazin bunu kirptigini test edebilirsin (orn. 'buzzer 1 0 300').")
    print("  query <start> <end>")
    print("      start/end: epoch (sayi) ya da 'YYYY-MM-DD' (tek kelime, bosluksuz)")
    print("      orn. 'query 1725100800 1725200800' ya da 'query 2026-09-01 2026-09-02'")
    print("      Sonuclar 0x46 (QUERY_RESULT) tipinde, coklu batch halinde ~10 sn arayla gelir.")
    print("  deveui <deveui>   -> son gorulen DevEUI'yi elle ayarlar")
    print("  help              -> bu mesaji tekrar goster")
    print("  exit              -> cikis\n")


def _command_input_loop():
    """MQTT dinlemesi arka planda (loop_start) surerken, ayni terminalden
    interaktif komut girilebilmesi icin. Buzzer komutu, bir sonraki cihaz
    uplink'inde (buton basimi/status turu) teslim edilecek sekilde kuyruga
    alinir - LoRaWAN Class A'nin dogasi geregi aninda gitmez.
    NOT: format banner'i zaten program baslangicinda (baglantidan once)
    bir kez gosterildi - burada tekrar etmiyoruz, 'help' yazinca gorulur."""
    print("Komutlar hazir - format/ornekler icin 'help' yaz.\n")
    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        if not line:
            continue
        parts = line.split()
        cmd = parts[0].lower()

        if cmd == "exit":
            break
        elif cmd == "help":
            _print_command_help()
        elif cmd == "deveui":
            if len(parts) < 2:
                print("Kullanim: deveui <deveui>")
                continue
            global last_seen_dev_eui
            last_seen_dev_eui = parts[1]
            print(f"DevEUI ayarlandi: {last_seen_dev_eui}")
        elif cmd == "buzzer":
            if not last_seen_dev_eui:
                print("⚠️  Henuz bir DevEUI gorulmedi/ayarlanmadi - once bir cihaz mesaji bekle "
                      "ya da 'deveui <deveui>' ile elle ayarla.")
                continue
            try:
                target = int(parts[1]) if len(parts) > 1 else 1
                pattern = int(parts[2]) if len(parts) > 2 else 1
                duration_s = int(parts[3]) if len(parts) > 3 else 5
            except ValueError:
                print("⚠️  Parametreler sayi olmali. Kullanim: buzzer [hedef] [pattern] [sure_sn]")
                continue
            send_buzzer_led_command(client, last_seen_dev_eui, target, pattern, duration_s)
        elif cmd == "query":
            if not last_seen_dev_eui:
                print("⚠️  Henuz bir DevEUI gorulmedi/ayarlanmadi - once bir cihaz mesaji bekle "
                      "ya da 'deveui <deveui>' ile elle ayarla.")
                continue
            if len(parts) < 3:
                print("Kullanim: query <start> <end>  (epoch sayi ya da YYYY-MM-DD)")
                continue
            try:
                start_ts = _parse_time_arg(parts[1])
                end_ts = _parse_time_arg(parts[2])
            except ValueError as e:
                print(f"⚠️  {e}")
                continue
            send_query_by_date_command(client, last_seen_dev_eui, start_ts, end_ts)
        else:
            print(f"⚠️  Bilinmeyen komut: {cmd} ('help' yaz)")


if __name__ == "__main__":
    # Baglanti denemesinden ONCE goster - bekleme/hata olsa bile format hemen gorulsun.
    _print_downlink_format_banner()
    try:
        # MQTT Sunucusuna Bağlan
        client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)

        # Mesaj dinlemeyi arka plan thread'inde baslat, ana thread'i
        # interaktif komut girisi icin serbest birak.
        client.loop_start()
        _command_input_loop()

    except KeyboardInterrupt:
        print("\nÇıkış yapılıyor...")
    finally:
        client.loop_stop()
        client.disconnect()
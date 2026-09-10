import json
import base64
import struct
import time
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

# --- MESAJ TİPLERİ (downlink, biz tanımlıyoruz - cihaz tarafındaki
#     LORA_DOWNLINK_MSG_TYPE_TIME_SYNC ile birebir aynı olmalı) ---
LORA_DOWNLINK_MSG_TYPE_TIME_SYNC = 0x01


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

                    # Timestamp'leri okunabilir tarihe çevir
                    def _fmt_epoch(v):
                        try:
                            return f"{v} ({datetime.fromtimestamp(v).strftime('%Y-%m-%d %H:%M:%S')})"
                        except Exception:
                            return str(v)

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
                    try:
                        device_now_display = f"{device_now_epoch} ({datetime.fromtimestamp(device_now_epoch).strftime('%Y-%m-%d %H:%M:%S')})"
                    except Exception:
                        device_now_display = str(device_now_epoch)

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

if __name__ == "__main__":
    try:
        # MQTT Sunucusuna Bağlan
        client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
        
        # Mesajları dinlemek için sonsuz döngü başlat
        client.loop_forever()
        
    except KeyboardInterrupt:
        print("\nÇıkış yapılıyor...")
        client.disconnect()
    except Exception as e:
        print(f"Bağlantı başlatılamadı: {e}")
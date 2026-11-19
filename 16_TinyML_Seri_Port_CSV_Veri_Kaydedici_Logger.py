# ============================================================================
# BAŞLIK    : Sunucu (Receiver) ESP32 Seri Port CSV Veri Seti Kaydedicisi (Data Logger)
# DOSYA ADI : 16_TinyML_Seri_Port_CSV_Veri_Kaydedici_Logger.py
# HAZIRLAYAN: Eda Nur YAŞDIMAN (IHASresearch)
# AÇIKLAMA  : Sunucu (`15_WiFi_UDP_Promiscuous_Sniffer_Alici_Server_ve_CSV_Veri_Toplayici.ino`)
#             ESP32'den 115200 baud hızında gelen 17 sütunlu ağ telemetrisi
#             (UDP + 802.11 Promiscuous Sniffer RSSI/SNR) çıktılarını dinler ve
#             CSV dosyasına kaydeder. Belirtilen COM port bulunamazsa bilgisayara
#             bağlı aktif seri portları otomatik tarar. Her satırda anlık Paket
#             Başarım Oranı (PDR %) ve RSSI değerini ekranda gösterir; 500 Normal +
#             500 Atak = 1000 satır ("# TAMAM") tamamlandığında özet rapor basar.
# ============================================================================

import time
from pathlib import Path
import serial
from serial.tools import list_ports

KLASOR = Path(__file__).resolve().parent

# ======================= PARAMETRELER =======================
PORT = "COM5"     # Sunucu (Receiver) ESP32'nin bağlı olduğu seri port
BAUD = 115200     # Seri haberleşme hızı (15 numaralı alıcı kodu ile aynı olmalı)
CIKTI = KLASOR / time.strftime("17_TinyML_Paket_Dusurme_Atak_Veri_Seti_%Y%m%d_%H%M%S.csv")
# ============================================================


def port_sec(tercih_edilen_port: str) -> str:
    """Tercih edilen COM portu kontrol eder; yoksa bağlı ilk uygun portu seçer."""
    mevcut_portlar = [p.device for p in list_ports.comports()]
    if tercih_edilen_port in mevcut_portlar:
        return tercih_edilen_port
    if mevcut_portlar:
        print(f"[UYARI] {tercih_edilen_port} bulunamadı! Otomatik seçilen port: {mevcut_portlar[0]}")
        return mevcut_portlar[0]
    return tercih_edilen_port


def main():
    sayac = {0: 0, 1: 0}       # 0: Normal iletişim satır sayısı, 1: Drop atağı satır sayısı
    pdr_toplam = {0: 0.0, 1: 0.0}
    baslik_var = False
    aktif_port = port_sec(PORT)

    print("=====================================================================")
    print("  ESP32 Wi-Fi UDP & Promiscuous Sniffer CSV Veri Kaydedici Başlatıldı")
    print("=====================================================================")

    with serial.Serial(aktif_port, BAUD, timeout=1) as ser, open(CIKTI, "w", encoding="utf-8", newline="") as f:
        time.sleep(2)          # Seri port açıldığında ESP32 resetlenebilir, açılış için 2 sn bekle
        ser.write(b"h")        # ESP32'den CSV sütun başlığını ('ts,received,...') tekrar iste
        print(f"Aktif Port : {aktif_port} ({BAUD} baud)")
        print(f"Çıktı CSV  : {CIKTI}  (Durdurmak için Ctrl+C)\n")

        try:
            while True:
                # Seri porttan bir satır oku ve boşlukları temizle
                satir = ser.readline().decode(errors="ignore").strip()
                if not satir:
                    continue

                # '#' ile başlayan durum/bilgi mesajlarını sadece ekrana yazdır, CSV'ye ekleme
                if satir.startswith("#"):
                    print(satir)
                    if satir.startswith("# TAMAM"):
                        break

                # CSV Sütun Başlığı satırı ('ts,received,lost,...')
                elif satir.startswith("ts,"):
                    if not baslik_var:
                        f.write(satir + "\n")
                        baslik_var = True

                # Zaman damgası (rakam) ile başlayan geçerli veri satırı
                elif satir[0].isdigit() and baslik_var:
                    f.write(satir + "\n")
                    f.flush()

                    parcalar = satir.split(",")
                    label = int(parcalar[-1])
                    sayac[label] += 1

                    # Sütun 3: loss_ratio -> PDR (%) = (1 - loss_ratio) * 100
                    # Sütun 8: avg_rssi (dBm)
                    try:
                        loss_ratio = float(parcalar[3])
                        avg_rssi = float(parcalar[8])
                        pdr = (1.0 - loss_ratio) * 100.0
                        pdr_toplam[label] += pdr
                        ek_bilgi = f"PDR=%{pdr:5.1f} RSSI={avg_rssi:5.1f}dBm"
                    except (ValueError, IndexError):
                        ek_bilgi = ""

                    durum = "ATAK" if label == 1 else "norm"
                    print(f"[{durum}] {satir} | {ek_bilgi} | normal={sayac[0]}/500 atak={sayac[1]}/500")

        except KeyboardInterrupt:
            print("\n[BİLGİ] Kullanıcı tarafından (Ctrl+C) kayıt sonlandırıldı.")

    # Kayıt Sonu İstatistik Özeti
    norm_ort_pdr = (pdr_toplam[0] / sayac[0]) if sayac[0] > 0 else 0.0
    atak_ort_pdr = (pdr_toplam[1] / sayac[1]) if sayac[1] > 0 else 0.0

    print("\n=====================================================================")
    print("                    CSV VERİ KAYDI TAMAMLANDI                        ")
    print("=====================================================================")
    print(f"  Kaydedilen Dosya       : {CIKTI}")
    print(f"  Normal (Etiket=0) Satır: {sayac[0]} adet (Ortalama PDR: %{norm_ort_pdr:.2f})")
    print(f"  Atak   (Etiket=1) Satır: {sayac[1]} adet (Ortalama PDR: %{atak_ort_pdr:.2f})")
    print(f"  Toplam Veri Satırı     : {sayac[0] + sayac[1]} adet")
    print("=====================================================================")


if __name__ == "__main__":
    main()

# ============================================================================
# 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
# ============================================================================
# 1. BU KOD NEYİ HEDEFLER?
#    - Sunucu (Receiver) ESP32'den seri port üzerinden akan çapraz katmanlı
#      (UDP + IEEE 802.11 PHY/MAC) ağ ölçümlerini yakalayıp bozulmadan standart
#      CSV formatında diske kaydetmeyi,
#    - Kayıt sırasında her pencerenin Paket Başarım Oranını (PDR %) ve RSSI
#      seviyesini canlı izleyerek 500 Normal + 500 Atak örneklem dengesini
#      otomatik doğrulamayı hedefler.
#
# 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
#    - Gömülü sensör düğümlerinden ve kablosuz ağ geçitlerinden bilgisayar ortamına
#      otomatik veri seti (dataset) toplama ve telemetri arşivleme.
#
# 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
#    - Yer Kontrol İstasyonu (YKİ) Veri Toplama Arayüzü: Askeri haberleşme testlerinde
#      ve elektronik harp saha denemelerinde, alıcı modemden gelen ham RF/paket
#      metriklerini yapay zeka eğitimi için otomatik etiketli CSV veri tabanına dönüştürür.
# ============================================================================

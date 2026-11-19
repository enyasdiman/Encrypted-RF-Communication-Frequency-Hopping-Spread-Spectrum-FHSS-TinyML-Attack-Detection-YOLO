# 📡 Kriptolu RF Haberleşme, Frekans Atlama (FHSS), TinyML Saldırı Tespiti ve YOLO İHA Tespiti Proje Raporu

**Donanım & Teknolojiler:** Arduino Mega 2560 / Uno, ESP32 & ESP8266, nRF24L01+ (2.4 GHz ISM Bandı), AES-128 & AES-256 (ECB) Kriptografi, FHSS (Frequency Hopping Spread Spectrum), IEEE 802.11 Promiscuous Sniffer, Gömülü TinyML (MLP `10→16→8→1`), Ultralytics YOLO26n & ByteTrack Görüntü İşleme

---

## 📋 12 Maddelik Teknik Proje Raporu

### 1. Proje Amacı ve İstemci-Sunucu (Client-Server) RF Mimarisi
Bu projede, 2.4 GHz ISM bandında çalışan **nRF24L01+** radyo frekans modülleri ve **ESP32/ESP8266** kablosuz düğümleri kullanılarak **Verici (Client / TX)** ve **Alıcı (Server / RX)** düğümleri arasında güvenli, kesintisiz ve dinlemeye/karıştırmaya (jamming / drop attack) dayanıklı bir kablosuz haberleşme ve İHA görüntü işleme altyapısı gerçekleştirilmiştir. Temel iletişim ve sinyal varlığı testleri `01_Temel_RF_Alici_ve_RSSI_Sinyal_Testi.ino` üzerinde doğrulanmıştır.

### 2. 128-Bit ve 256-Bit Anahtarlı Kriptolu İletişim (AES-128 & AES-256)
Kablosuz ortamda (havada) taşınan verilerin üçüncü şahıslar tarafından dinlenmesini (eavesdropping) engellemek amacıyla simetrik blok şifreleme standardı olan **AES (Advanced Encryption Standard)** entegre edilmiştir:
* **AES-128 (16 Bayt Anahtar):** `02_AES128_ECB_HexAnahtar_Sifreli_ve_Acik_Metin_Alici.ino` ve `03_AES128_ECB_Kriptolu_Iletisim_Alici.ino` kodlarında 128-bit gizli anahtar kullanılarak hem şifreli (HEX) veri hem de çözülen açık metin karşılaştırmalı olarak analiz edilmiştir.
* **AES-256 (32 Bayt Anahtar):** `04_AES256_ECB_TekBlok_16Bayt_Kriptolu_Alici.ino` ve tüm FHSS modüllerinde askeri düzey **256-bit (32 bayt)** ortak gizli anahtar kullanılarak maksimum kriptografik güvenlik sağlanmıştır.

### 3. Uzun Verilerin Bloklara Parçalanarak Şifrelenmesi ve Birleştirilmesi (32-Bayt Çift Blok)
AES algoritması standart olarak **16 baytlık (128-bit)** bloklar halinde çalışmaktadır. `05_AES256_ECB_CiftBlok_32Bayt_UzunVeri_Alici.ino` ve FHSS kodlarında, nRF24L01'in maksimum paket kapasitesi olan **32 baytlık uzun veriler** gönderici tarafta iki ayrı 16 baytlık bloğa (`Blok 1: 0..15` ve `Blok 2: 16..31`) parçalanarak şifrelenmiş, alıcı (Server) tarafta ise ardışık olarak deşifre edilip tek bir tam metin/yapı (`struct`) halinde birleştirilmiştir.

### 4. Ortak Kanal Listesi ve Senkron Frekans Atlama (FHSS - Frequency Hopping)
Sabit bir frekansta yapılan yayınların tespit edilmesi ve karıştırılması kolay olduğundan, verici ve alıcı gruplar arasında **birebir aynı ortak kanal listeleri** tanımlanarak **Frekans Atlamalı Yayılı Spektrum (FHSS)** mimarisi kurulmuştur:
* **5 Kanallı FHSS (`06_AES256_FHSS_5Kanalli_Senkron_Kilitli_Alici.ino`):** `{46, 15, 75, 30, 95}` kanallarında 400 ms slot süresiyle atlama.
* **15 Kanallı FHSS (`10_AES256_FHSS_15Kanalli_Mega_Alici_ve_CSV_Log.ino` & `11_AES256_FHSS_15Kanalli_Uno_Verici_20Hz.ino`):** 2476 MHz – 2525 MHz arasındaki 15 ortak kanalda senkron veri aktarımı.
* **20 Kanallı FHSS (`07_AES256_FHSS_20Kanalli_Alici_ve_PDR_Analizi.ino`):** 20 farklı frekans kanalında 350 ms slot süresiyle genişletilmiş atlama.
* **50 Kanallı FHSS (`08_AES256_FHSS_50Kanalli_1SnHop_Alici_ve_Analiz.ino` & `09_AES256_FHSS_50Kanalli_YuksekHizli_10HopSn_Alici.ino`):** 2.4 GHz bandının tamamına yayılan 50 ortak kanalda yüksek güvenlikli frekans atlama.

### 5. Kriptolu Frekans Atlama ve Hassas Saat Hizalama (Guard Time / Erken Zıplama)
Frekans atlama senkronizasyon bilgisinin (`hopIndeksi`, `kanal`, `slotNo`, `vericiMillis`) dışarıdan manipüle edilmemesi için kontrol başlıkları veri yüküyle birlikte **AES-256 ile şifrelenmiş 32 baytlık `SenkronPaket` yapısı** içinde taşınmaktadır. Alıcı düğüm, paket içindeki kriptolu imzayı doğruladıktan sonra kendi iç saatini vericiye hizalar ve **15 ms – 50 ms erken geçiş avansı (Guard Time)** uygulayarak vericiden hemen önce yeni frekansa geçip PLL (Phase-Locked Loop) kilidini tamamlar.

### 6. Farklı Atlama Hızları (Hopping Rates) ve Yüksek Hızlı Veri Gönderimi
Sistemin farklı dinamik koşullardaki kararlılığını ölçmek amacıyla çeşitli frekans atlama ve paket gönderim hızları test edilmiştir:
* **1 Hop/sn (1000 ms Slot):** `08_AES256_FHSS_50Kanalli_1SnHop_Alici_ve_Analiz.ino` ve `10_AES256_FHSS_15Kanalli_Mega_Alici_ve_CSV_Log.ino` ile saniyede 1 frekans değişimi.
* **~2.8 Hop/sn (350-400 ms Slot):** `07_AES256_FHSS_20Kanalli_Alici_ve_PDR_Analizi.ino` ve `06_AES256_FHSS_5Kanalli_Senkron_Kilitli_Alici.ino` ile orta hızlı frekans atlama.
* **10 Hop/sn (100 ms Slot):** `09_AES256_FHSS_50Kanalli_YuksekHizli_10HopSn_Alici.ino` ile saniyede 10 farklı frekansa yüksek hızlı zıplama.
* **20 Paket/sn (20 Hz RF) & 100 Paket/sn (100 Hz Wi-Fi UDP):** `11_AES256_FHSS_15Kanalli_Uno_Verici_20Hz.ino` ile saniyede 20 şifreli paket; `14_WiFi_UDP_Drop_Atak_Simulatoru_Verici_Client.ino` ile saniyede 100 UDP paketi yayını.

### 7. Kopma Tespiti (Watchdog) ve Otomatik Yeniden Senkronizasyon (Fail-Safe)
Karıştırma (Jamming) veya paket düşmesi (Drop) nedeniyle ardışık belirli sayıda slotta (örneğin 10, 15, 25 veya 30 slot boyunca) sinyal alınamadığında, alıcının durum makinesi (`SENKRON_KILITLI` -> `SENKRON_ARANIYOR`) senkron kaybını otomatik tespit eder. Alıcı derhal **Baz Kanala (2446 MHz / Kanal 46 veya Kanal 76)** geri dönerek vericinin saat ritmini yeniden yakalar ve iletişimi kendi kendine kurtarır.

### 8. Sinyal Gücü (RSSI / RPD) ve IEEE 802.11 Promiscuous PHY/MAC Analizi
* **Donanımsal RPD (nRF24L01+):** Alıcı tarafında (`01`, `02`, `03` numaralı kodlar), nRF24L01+ donanımının dahili **RPD (Received Power Detector / RSSI eşik göstergesi)** yazmacı (`radio.testRPD()`) kullanılarak gelen her paketin sinyal gücünün **-64 dBm** eşiğinin üzerinde (`[Güçlü Sinyal]`) veya altında olduğu gerçek zamanlı olarak izlenmektedir.
* **IEEE 802.11 Promiscuous Sniffer (ESP32):** `15_WiFi_UDP_Promiscuous_Sniffer_Alici_Server_ve_CSV_Veri_Toplayici.ino` ve `20_ESP32_TinyML_Gercek_Zamanli_Drop_Atak_Dedektoru.ino` kodlarında havadaki ham Wi-Fi veri çerçevelerinden **anlık RSSI (dBm)**, **RSSI standart sapması (`rssi_std`)**, **Gürültü Tabanı (`noise_floor`)**, **SNR (dB)** ve **MAC Retry Biti (`retry_ratio`)** doğrudan donanım seviyesinde ölçülmektedir.

### 9. Paket Başarım Oranı (PDR / PSR) ve Kayıp Analizi
Kodlar çalıştırıldığında seri port üzerinden her **100 paketlik** gözlem penceresinde ve **1000 paketlik** kümülatif periyotlarda otomatik performans raporu üretilmektedir (`07`, `08`, `09`, `10`, `15` ve `20` numaralı alıcı kodları):
* Beklenen paket sayısı, başarıyla alınan paket sayısı, ardışık düşen paket sayısı (`max_consec_lost`) ve kayıp paket sayısı hesaplanır.
* **Anlık Paket Başarım Oranı (`%PDR / %PSR`)**, **Paket Kayıp Oranı (`%`)** ve **1000 Paketlik Kümülatif Ortalama Başarım** yüzdesi ekrana basılır.

### 10. CSV Formatında Veri Kaydı (Logging) ve Paket Düşürme (Drop Attack) Simülasyonu
Deneysel verilerin dışa aktarılıp analiz edilebilmesi ve yapay zeka modellerinin eğitilebilmesi için iki farklı CSV veri kayıt altyapısı kurulmuştur:
* **FHSS CSV Log (`10_AES256_FHSS_15Kanalli_Mega_Alici_ve_CSV_Log.ino`):** Alınan şifreli paketler **`Log_No,Kanal,Cozulen_Metin`** başlığı altında doğrudan CSV formatında üretilir.
* **Wi-Fi Drop Attack Simülasyonu ve CSV Veri Seti (`14`, `15`, `16` ve `17` Numaralı Dosyalar):** `14_WiFi_UDP_Drop_Atak_Simulatoru_Verici_Client.ino` 30 saniye Normal İletişim (`label=0`) ve 30 saniye Paket Düşürme Atağı (`label=1`, %20-%80 paket düşürme + TX güç düşürme) uygular. `15_WiFi_UDP_Promiscuous_Sniffer_Alici_Server_ve_CSV_Veri_Toplayici.ino` ve `16_TinyML_Seri_Port_CSV_Veri_Kaydedici_Logger.py` ise her 1 saniyelik pencere için 16 ağ özniteliğini `17_TinyML_Paket_Dusurme_Atak_Veri_Seti.csv` dosyasına (500 Normal + 500 Atak = 1000 satır) kaydeder.

### 11. Gömülü TinyML (Çok Katmanlı Algılayıcı - MLP) ile ESP32 Üzerinde Gerçek Zamanlı Saldırı Tespiti
`18_TinyML_MLP_Model_Egitimi_ve_Header_Uretici.py` betiği, `17_TinyML_Paket_Dusurme_Atak_Veri_Seti.csv` verisini kullanarak kartların konumuna bağımlı mutlak RSSI/SNR değerlerini eler ve 10 dinamik öznitelik (`received`, `lost`, `loss_ratio`, `max_consec_lost`, `avg_gap_ms`, `gap_std_ms`, `max_gap_ms`, `rssi_std`, `retry_ratio`, `sniff_frames`) ile **3 katmanlı bir Yapay Sinir Ağı (`10 → 16 → 8 → 1` MLP)** eğitir:
* Eğitilen modelin ağırlıkları ve Z-Score normalizasyon sabitleri **321 parametre (1.3 KB)** boyutunda C dizisi olarak `19_TinyML_MLP_Model_Agirliklari.h` dosyasına kaydedilir.
* `20_ESP32_TinyML_Gercek_Zamanli_Drop_Atak_Dedektoru.ino` bu modeli doğrudan ESP32 üzerinde **~25 mikrosaniyede** çalıştırarak paket düşürme saldırılarını **%98.5 doğrulukla** gerçek zamanlı tespit eder. Sistem mimarisi `21_TinyML_Paket_Dusurme_Atak_Tespiti_Rehberi.md` dosyasında detaylandırılmıştır.

### 12. YOLO26n ve ByteTrack ile Gerçek Zamanlı İHA (UAV) Tespiti ve Yörünge Takibi
`22_YOLO26n_ve_ByteTrack_IHA_Tespit_ve_Takip_Sistemi.py` uygulamasında, `25_IHA_Egitim_ve_Dogrulama_Videosu_2.webm` ile ince ayarı (*fine-tuning*) yapılmış **YOLO26n** derin öğrenme modeli (`23_YOLO26n_FineTuned_IHA_Modeli.pt`) ve **ByteTrack** algoritması kullanılarak videolarda (`24_IHA_Test_Videosu_1.mp4` ve `25_IHA_Egitim_ve_Dogrulama_Videosu_2.webm`) gerçek zamanlı **İHA Tespiti ve Yörünge Takibi** gerçekleştirilmiştir:
* Tespit edilen İHA'lar sınırlayıcı kutu (*bounding box*), hedef merkez nişangahı (*crosshair*) ve son 40 karenin hareket izi (*trajectory*) ile görselleştirilir; ekran üzerindeki HUD panelinde **Anlık FPS** ve **Tespit Başarım Oranı (%)** canlı olarak raporlanır. Detaylı kullanım kılavuzu `26_YOLO26n_IHA_Tespit_ve_Takip_Rehberi.md` dosyasındadır.

---

## 📂 Depo (Repository) Dosya İndeksi

| Sıra | İçeriğe Göre Düzenlenmiş Dosya Adı | Teknik İçerik ve Görevi |
| :---: | :--- | :--- |
| **01** | [`01_Temel_RF_Alici_ve_RSSI_Sinyal_Testi.ino`](./01_Temel_RF_Alici_ve_RSSI_Sinyal_Testi.ino) | 2446 MHz temel RF alıcı (RX) ve donanımsal RPD/RSSI sinyal gücü testi |
| **02** | [`02_AES128_ECB_HexAnahtar_Sifreli_ve_Acik_Metin_Alici.ino`](./02_AES128_ECB_HexAnahtar_Sifreli_ve_Acik_Metin_Alici.ino) | AES-128 (Hex Key) alıcı; şifreli HEX ve çözülen metin karşılaştırması + RPD |
| **03** | [`03_AES128_ECB_Kriptolu_Iletisim_Alici.ino`](./03_AES128_ECB_Kriptolu_Iletisim_Alici.ino) | AES-128 (`"kho-128-bit-key!"`) 32-bayt çift blok kriptolu alıcı + RPD |
| **04** | [`04_AES256_ECB_TekBlok_16Bayt_Kriptolu_Alici.ino`](./04_AES256_ECB_TekBlok_16Bayt_Kriptolu_Alici.ino) | AES-256 (256-bit Key) 16-bayt tek blok şifre çözücü ve HEX/Metin analizi |
| **05** | [`05_AES256_ECB_CiftBlok_32Bayt_UzunVeri_Alici.ino`](./05_AES256_ECB_CiftBlok_32Bayt_UzunVeri_Alici.ino) | AES-256 (256-bit Key) 32-bayt (2x16 bayt blok) uzun veri birleştirici alıcı |
| **06** | [`06_AES256_FHSS_5Kanalli_Senkron_Kilitli_Alici.ino`](./06_AES256_FHSS_5Kanalli_Senkron_Kilitli_Alici.ino) | AES-256 kriptolu, 5 kanallı, 400 ms slot süreli senkron kilitli FHSS alıcı |
| **07** | [`07_AES256_FHSS_20Kanalli_Alici_ve_PDR_Analizi.ino`](./07_AES256_FHSS_20Kanalli_Alici_ve_PDR_Analizi.ino) | AES-256 kriptolu, 20 kanallı, 350 ms slotlu FHSS alıcı ve 100 paket PDR raporu |
| **08** | [`08_AES256_FHSS_50Kanalli_1SnHop_Alici_ve_Analiz.ino`](./08_AES256_FHSS_50Kanalli_1SnHop_Alici_ve_Analiz.ino) | AES-256 kriptolu, 50 kanallı, 1 sn/hop FHSS alıcı ve 100 paket PDR analizi |
| **09** | [`09_AES256_FHSS_50Kanalli_YuksekHizli_10HopSn_Alici.ino`](./09_AES256_FHSS_50Kanalli_YuksekHizli_10HopSn_Alici.ino) | AES-256 kriptolu, 50 kanallı yüksek hızlı (10 hop/sn) FHSS alıcı ve PDR raporu |
| **10** | [`10_AES256_FHSS_15Kanalli_Mega_Alici_ve_CSV_Log.ino`](./10_AES256_FHSS_15Kanalli_Mega_Alici_ve_CSV_Log.ino) | 15 kanallı AES-256 FHSS Sunucu/Alıcı (Mega) - **CSV Log** ve 100/1000 paket PSR raporu |
| **11** | [`11_AES256_FHSS_15Kanalli_Uno_Verici_20Hz.ino`](./11_AES256_FHSS_15Kanalli_Uno_Verici_20Hz.ino) | 15 kanallı AES-256 FHSS İstemci/Verici (Uno) - Saniyede 20 şifreli paket (20 Hz) yayını |
| **12** | [`12_Kriptografi_Veri_Akis_Semalari_ve_NIST_Analizi.md`](./12_Kriptografi_Veri_Akis_Semalari_ve_NIST_Analizi.md) | Simetrik, Asimetrik ve Hash veri akış şemaları ile NIST anahtar boyutu grafikleri |
| **13** | [`13_Kriptografi_Yontemleri_ve_Algoritmalari_Rehberi.md`](./13_Kriptografi_Yontemleri_ve_Algoritmalari_Rehberi.md) | Kriptografik yöntemlerin ve algoritmaların detaylı teknik karşılaştırma rehberi |
| **14** | [`14_WiFi_UDP_Drop_Atak_Simulatoru_Verici_Client.ino`](./14_WiFi_UDP_Drop_Atak_Simulatoru_Verici_Client.ino) | **Client (ESP32/ESP8266):** 100 Hz UDP paket vericisi; 30sn Normal / 30sn Drop Attack (%20-%80 kayıp + TX güç düşürme) simülatörü |
| **15** | [`15_WiFi_UDP_Promiscuous_Sniffer_Alici_Server_ve_CSV_Veri_Toplayici.ino`](./15_WiFi_UDP_Promiscuous_Sniffer_Alici_Server_ve_CSV_Veri_Toplayici.ino) | **Server (ESP32):** Wi-Fi AP + 802.11 Promiscuous Sniffer; RSSI, SNR, Retry ve kayıp metriklerini CSV olarak basar |
| **16** | [`16_TinyML_Seri_Port_CSV_Veri_Kaydedici_Logger.py`](./16_TinyML_Seri_Port_CSV_Veri_Kaydedici_Logger.py) | Sunucu ESP32'den gelen CSV satırlarını otomatik port seçimi ve canlı PDR (%) gösterimiyle kaydeden Python betiği |
| **17** | [`17_TinyML_Paket_Dusurme_Atak_Veri_Seti.csv`](./17_TinyML_Paket_Dusurme_Atak_Veri_Seti.csv) | 500 Normal + 500 Drop Attack (1000 satır, 17 sütun) gerçek donanım eğitim ve test veri seti |
| **18** | [`18_TinyML_MLP_Model_Egitimi_ve_Header_Uretici.py`](./18_TinyML_MLP_Model_Egitimi_ve_Header_Uretici.py) | MLP (`10 → 16 → 8 → 1`) sinir ağı eğitimi, doğruluk/F1 analizi ve otomatik C header (`19_...h`) üretim betiği |
| **19** | [`19_TinyML_MLP_Model_Agirliklari.h`](./19_TinyML_MLP_Model_Agirliklari.h) | 321 parametrelik (`1.3 KB`) float32 sinir ağı ağırlıkları ve Z-Score normalizasyon dizileri |
| **20** | [`20_ESP32_TinyML_Gercek_Zamanli_Drop_Atak_Dedektoru.ino`](./20_ESP32_TinyML_Gercek_Zamanli_Drop_Atak_Dedektoru.ino) | **TinyML Dedektör (ESP32):** Eğitilen MLP modelini ESP32 üzerinde (~25 µs) koşturarak gerçek zamanlı saldırı ve PDR (%) tespiti yapar |
| **21** | [`21_TinyML_Paket_Dusurme_Atak_Tespiti_Rehberi.md`](./21_TinyML_Paket_Dusurme_Atak_Tespiti_Rehberi.md) | TinyML Drop Attack tespit mimarisi, öznitelik mühendisliği ve karışıklık matrisi dokümantasyonu |
| **22** | [`22_YOLO26n_ve_ByteTrack_IHA_Tespit_ve_Takip_Sistemi.py`](./22_YOLO26n_ve_ByteTrack_IHA_Tespit_ve_Takip_Sistemi.py) | Fine-tune edilmiş YOLO26n + ByteTrack ile İHA tespiti, merkez crosshair, yörünge izi ve canlı FPS/Başarım HUD uygulaması |
| **23** | [`23_YOLO26n_FineTuned_IHA_Modeli.pt`](./23_YOLO26n_FineTuned_IHA_Modeli.pt) | Fine-tune edilmiş tek sınıflı (`IHA`) YOLO26n derin öğrenme model ağırlığı |
| **24** | [`24_IHA_Test_Videosu_1.mp4`](./24_IHA_Test_Videosu_1.mp4) | İHA tespiti test videosu 1 (Farklı açı ve sahnede İHA uçuşu) |
| **25** | [`25_IHA_Egitim_ve_Dogrulama_Videosu_2.webm`](./25_IHA_Egitim_ve_Dogrulama_Videosu_2.webm) | İHA tespiti eğitim ve doğrulama videosu 2 (Quadcopter uçuşu) |
| **26** | [`26_YOLO26n_IHA_Tespit_ve_Takip_Rehberi.md`](./26_YOLO26n_IHA_Tespit_ve_Takip_Rehberi.md) | YOLO26n İHA tespit ve ByteTrack yörünge takip sistemi mimarisi ve kullanım rehberi |

// ============================================================================
// BAŞLIK    : Wi-Fi Promiscuous Sniffer Destekli UDP Sunucu (Server/RX) ve CSV Veri Toplayıcı
// DOSYA ADI : 15_WiFi_UDP_Promiscuous_Sniffer_Alici_Server_ve_CSV_Veri_Toplayici.ino
// HAZIRLAYAN: Eda Nur YAŞDIMAN (IHASresearch)
// AÇIKLAMA  : ESP32 üzerinde çalışan Wi-Fi Erişim Noktası (Access Point / Server)
//             ve Fiziksel/MAC Katmanı (PHY/MAC) Gözlemcisi.
//             İstemciden (14_WiFi_UDP_Drop_Atak_Simulatoru_Verici_Client.ino) gelen
//             UDP paketlerini dinlerken aynı zamanda ESP32'nin Promiscuous Mode
//             (Sniffer) API'sini kullanarak havadaki ham IEEE 802.11 veri
//             çerçevelerinden RSSI, Gürültü Tabanı (Noise Floor), SNR ve MAC Retry
//             Biti ölçümlerini çıkarır. Her 1 saniyelik pencere sonunda 16 ağ
//             özniteliğini ve sınıf etiketini (0=Normal, 1=Atak) CSV formatında
//             Seri Porta basar. 500 Normal + 500 Atak satırda tamamlanır.
// ============================================================================

#include <WiFi.h>       // ESP32 Wi-Fi kütüphanesi
#include <WiFiUdp.h>    // UDP soket haberleşme kütüphanesi
#include <math.h>       // Standart sapma (sqrtf, fmaxf) hesaplamaları için matematik kütüphanesi
#include "esp_wifi.h"   // ESP32 Promiscuous Mode (802.11 Sniffer) alt seviye API'si

// ========================== SUNUCU VE VERİ SETİ AYARLARI ==========================
#define AP_SSID          "UAV_LINK"   // Kurulacak Wi-Fi Access Point (AP) ağ adı
#define AP_PASS          "uavlink123" // Wi-Fi AP WPA2 şifresi
#define AP_KANAL         1            // Sabit Wi-Fi frekans kanalı (Kanal 1: 2412 MHz)
#define SERVER_PORT      4210         // Dinlenecek UDP port numarası
#define PENCERE_MS       1000         // İstatistik ve CSV öznitelik çıkarma penceresi: 1000 ms (1 sn)
#define PENCERE_BEKLENEN 100          // İstemciden 1 saniyelik pencerede beklenen paket sayısı (100 Hz)
#define HEDEF_NORMAL     500          // Veri seti için toplanacak hedef Normal (label=0) satır sayısı
#define HEDEF_ATAK       500          // Veri seti için toplanacak hedef Atak (label=1) satır sayısı
// ==================================================================================

// CSV Veri Seti Sütun Başlığı (16 Ölçüm + 1 Hedef Etiket = 17 Sütun)
const char *BASLIK =
  "ts,received,lost,loss_ratio,max_consec_lost,avg_gap_ms,gap_std_ms,max_gap_ms,"
  "avg_rssi,rssi_std,min_rssi,avg_noise,avg_snr,min_snr,retry_ratio,sniff_frames,label";

// İstemci (14_WiFi_UDP_Drop_Atak_Simulatoru_Verici_Client.ino) ile BİREBİR AYNI 5 Baytlık UDP Paket Yapısı
struct __attribute__((packed)) Paket {
  uint32_t seq;    // 4 Bayt: Paket sıra numarası
  uint8_t  label;  // 1 Bayt: Faz etiketi (0 = Normal, 1 = Drop Atağı)
};

WiFiUDP udp;       // UDP sunucu nesnesi

// İstemci Wi-Fi AP'ye bağlandığında MAC adresi otomatik öğrenilir;
// böylece Promiscuous Sniffer sadece bu istemciden gelen 802.11 çerçevelerini filtreler.
uint8_t       clientMac[6];
volatile bool clientVar = false;

// Promiscuous (Sniffer) kesme fonksiyonu (ISR/Callback) ile ana döngü arasında
// veri yarışını (race condition) önleyen FreeRTOS kritik bölge kilidi (spinlock)
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Fiziksel (PHY) ve MAC Katmanı Pencere Akümülatörleri
float    rssiToplam = 0, rssiKareToplam = 0, rssiMin = 0;
float    noiseToplam = 0, snrToplam = 0, snrMin = 0;
uint32_t sniffSayisi = 0, retrySayisi = 0;

// UDP Taşıma (Transport) Katmanı Pencere İstatistikleri
uint32_t pencereAlinan = 0, pencereKayip = 0, pencereMaxArdisik = 0;
float    gapToplam = 0, gapKareToplam = 0, gapMax = 0;
uint32_t gapSayisi = 0;
uint32_t sonSeq = 0, sonAlimUs = 0;
bool     seqVar = false;
uint32_t alacak = 0;        // Hiç paket gelmeyen boş pencerelerde yazılan kayıp, sonraki pakette mükerrer sayılmasın

// Sınıf Etiketi ve Veri Seti Sayaç Takibi
int8_t   pencereEtiket = -1;       // -1 = Bu pencerede henüz paket gelmedi
bool     karisik       = false;    // Aynı 1 sn pencerede hem 0 hem 1 etiketi geldiyse (faz geçiş anı)
uint8_t  sonEtiket     = 0;        // Son bilinen geçerli sınıf etiketi
uint32_t sayac[2]      = { 0, 0 }; // sayac[0]: Normal satır sayısı, sayac[1]: Atak satır sayısı
bool     bitti         = false;    // 500 Normal + 500 Atak tamamlandı bayrağı

// Hiç çerçeve yakalanamayan (%100 kayıp) pencerelerde kullanılacak son geçerli PHY değerleri
float sonRssi = 0, sonNoise = -96, sonSnr = 0;

// ----------------------------------------------------------------------------
// FONKSİYON : wifiOlay
// GÖREVİ    : İstemci (Client) AP'ye bağlandığında MAC adresini kaydeder,
//             ayrıldığında ise oturum bayraklarını sıfırlar.
// ----------------------------------------------------------------------------
void wifiOlay(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
    memcpy(clientMac, info.wifi_ap_staconnected.mac, 6);
    clientVar = true;
    Serial.printf("# Client baglandi: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  clientMac[0], clientMac[1], clientMac[2], clientMac[3], clientMac[4], clientMac[5]);
  } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
    clientVar = false;
    seqVar = false;
    alacak = 0;
    Serial.println("# Client ayrildi");
  }
}

// ----------------------------------------------------------------------------
// FONKSİYON : snifferCallback (Promiscuous Mode 802.11 Paket Yakalayıcı)
// GÖREVİ    : Havadaki tüm IEEE 802.11 Veri (DATA) çerçevelerini yakalar.
//             Çerçevenin 10..15. baytlarındaki Gönderici MAC (Address 2) adresini
//             kontrol ederek yalnızca bizim İstemciden gelen çerçeveleri seçer.
//             Donanımsal radyo başlığından (rx_ctrl) RSSI (dBm), Gürültü Tabanı
//             (noise_floor), SNR (dB) ve 802.11 Frame Control alanından (bit 11)
//             MAC Yeniden Gönderim (Retry) bitini okur.
// ----------------------------------------------------------------------------
void snifferCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (!clientVar || type != WIFI_PKT_DATA) return;
  wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
  wifi_pkt_rx_ctrl_t *rx = &pkt->rx_ctrl;
  if (rx->sig_len < 24) return;                               // Geçerli 802.11 MAC başlığı en az 24 bayttır
  if (memcmp(pkt->payload + 10, clientMac, 6) != 0) return;   // Address 2 (Gönderen MAC) istemciyle eşleşmeli

  float rssi  = rx->rssi;                                     // Alınan Sinyal Gücü Göstergesi (dBm)
  float noise = rx->noise_floor;                              // Ortam Gürültü Tabanı (dBm)
  float snr   = rssi - noise;                                 // Sinyal-Gürültü Oranı (SNR - dB)
  bool  retry = (pkt->payload[1] >> 3) & 0x1;                 // IEEE 802.11 Frame Control Retry biti (Bit 11)

  portENTER_CRITICAL_ISR(&mux);
  if (sniffSayisi == 0) {
    rssiMin = rssi;
    snrMin  = snr;
  } else {
    if (rssi < rssiMin) rssiMin = rssi;
    if (snr  < snrMin)  snrMin  = snr;
  }
  rssiToplam     += rssi;
  rssiKareToplam += rssi * rssi;
  noiseToplam    += noise;
  snrToplam      += snr;
  if (retry) retrySayisi++;
  sniffSayisi++;
  portEXIT_CRITICAL_ISR(&mux);
}

// ----------------------------------------------------------------------------
// FONKSİYON : paketIsle
// GÖREVİ    : Gelen her UDP paketinin sıra numarasını (seq) bir önceki paketle
//             karşılaştırarak arada düşen (kayıp) paket sayısını, maksimum ardışık
//             kaybı (max_consec_lost) ve paketler arası varış süresi farkını
//             (inter-arrival gap_ms) mikrosaniye hassasiyetinde hesaplar.
// ----------------------------------------------------------------------------
void paketIsle(const Paket &p) {
  uint32_t simdiUs = micros();

  if (!seqVar || p.seq <= sonSeq) {
    seqVar = true;                       // İlk paket veya istemci yeniden başlatıldı
  } else {
    uint32_t bosluk = p.seq - sonSeq - 1;
    if (bosluk > pencereMaxArdisik) pencereMaxArdisik = bosluk;
    uint32_t dus = min(bosluk, alacak);
    alacak -= dus;
    pencereKayip += bosluk - dus;

    // İki ardışık paket arasındaki varış zamanı farkı (ms)
    float gap = (simdiUs - sonAlimUs) / 1000.0f;
    gapToplam     += gap;
    gapKareToplam += gap * gap;
    if (gap > gapMax) gapMax = gap;
    gapSayisi++;
  }
  sonSeq    = p.seq;
  sonAlimUs = simdiUs;
  pencereAlinan++;

  // Paketin taşıdığı gerçek sınıf etiketini (0=Normal, 1=Atak) kontrol et
  uint8_t lbl = p.label ? 1 : 0;
  if (pencereEtiket == -1) pencereEtiket = lbl;
  else if (pencereEtiket != lbl) karisik = true; // Aynı pencerede faz değiştiyse işaretle
  sonEtiket = lbl;
}

// ----------------------------------------------------------------------------
// FONKSİYON : pencereSifirla
// GÖREVİ    : 1 saniyelik ölçüm penceresi sonunda sayaçları yeni pencere için sıfırlar.
// ----------------------------------------------------------------------------
void pencereSifirla() {
  pencereAlinan = pencereKayip = pencereMaxArdisik = 0;
  gapToplam = gapKareToplam = gapMax = 0;
  gapSayisi = 0;
  pencereEtiket = -1;
  karisik = false;
}

// ----------------------------------------------------------------------------
// FONKSİYON : pencereBas
// GÖREVİ    : Her 1000 ms'de bir çağrılır. UDP ve Sniffer istatistiklerini
//             birleştirir, standart sapmaları ve kayıp/başarım oranlarını
//             hesaplayarak tek bir CSV satırı halinde Seri Porta yazar.
// ----------------------------------------------------------------------------
void pencereBas() {
  float rT, rKT, rMin, nT, sT, sMin;
  uint32_t sayi, retry;

  // Sniffer kesme verilerini atomik olarak kopyala ve sıfırla
  portENTER_CRITICAL(&mux);
  rT   = rssiToplam;  rKT  = rssiKareToplam; rMin = rssiMin;
  nT   = noiseToplam; sT   = snrToplam;      sMin = snrMin;
  sayi = sniffSayisi; retry = retrySayisi;
  rssiToplam = rssiKareToplam = noiseToplam = snrToplam = 0;
  sniffSayisi = retrySayisi = 0;
  portEXIT_CRITICAL(&mux);

  if (!seqVar) { pencereSifirla(); return; }   // Henüz ilk paket gelmediyse bekle

  // Bu 1 saniyelik pencerede hiç paket ulaşılamadıysa (%100 Drop / Jamming durumu)
  if (pencereAlinan == 0) {
    pencereKayip      = PENCERE_BEKLENEN;
    pencereMaxArdisik = PENCERE_BEKLENEN;
    alacak           += PENCERE_BEKLENEN;
    gapMax            = PENCERE_MS;
    pencereEtiket     = sonEtiket;
  }

  // Faz geçişine (Normal <-> Atak) denk gelen karma etiketli pencereyi veri seti temizliği için atla
  if (karisik) {
    Serial.println("# Faz gecisi penceresi atlandi");
    pencereSifirla();
    return;
  }

  uint8_t  label = pencereEtiket;
  uint32_t hedef = label ? HEDEF_ATAK : HEDEF_NORMAL;
  if (sayac[label] >= hedef) { pencereSifirla(); return; }   // Bu sınıfın 500 satırlık kotası dolduysa atla

  // PHY / MAC Katmanı İstatistikleri (Ortalama RSSI, RSSI Standart Sapması, SNR, Retry Oranı)
  float avgRssi  = sonRssi,  rssiStd = 0,     minRssi    = sonRssi;
  float avgNoise = sonNoise, avgSnr  = sonSnr, minSnr    = sonSnr, retryOrani = 0;
  if (sayi > 0) {
    avgRssi    = rT / sayi;
    rssiStd    = sqrtf(fmaxf(rKT / sayi - avgRssi * avgRssi, 0));
    minRssi    = rMin;
    avgNoise   = nT / sayi;
    avgSnr     = sT / sayi;
    minSnr     = sMin;
    retryOrani = (float)retry / sayi;
    sonRssi = avgRssi; sonNoise = avgNoise; sonSnr = avgSnr;
  }

  // Paketler Arası Zaman Boşluğu (Inter-Arrival Gap) Ortalaması ve Standart Sapması
  float avgGap = PENCERE_MS, gapStd = 0;
  if (gapSayisi > 0) {
    avgGap = gapToplam / gapSayisi;
    gapStd = sqrtf(fmaxf(gapKareToplam / gapSayisi - avgGap * avgGap, 0));
  }

  // Paket Kayıp Oranı (Loss Ratio) ve Paket Başarım Oranı (PDR %)
  uint32_t toplamPaket = pencereAlinan + pencereKayip;
  float kayipOrani = toplamPaket ? (float)pencereKayip / toplamPaket : 0;
  float pdrYuzde   = (1.0f - kayipOrani) * 100.0f;

  // CSV Veri Satırı Çıktısı (16_TinyML_Seri_Port_CSV_Veri_Kaydedici_Logger.py tarafından doğrudan kaydedilir)
  Serial.printf("%lu,%lu,%lu,%.2f,%lu,%.1f,%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,%.1f,%.1f,%.2f,%lu,%u\n",
                millis(), pencereAlinan, pencereKayip, kayipOrani, pencereMaxArdisik,
                avgGap, gapStd, gapMax,
                avgRssi, rssiStd, minRssi, avgNoise, avgSnr, minSnr,
                retryOrani, sayi, label);

  sayac[label]++;

  // Her 25 satırda bir Seri Port ekranına '#' ile başlayan bilgilendirici PDR & RSSI özeti bas
  // ('#' ile başladığı için logger betiği bunu ekranda gösterir ancak CSV dosyasını bozmaz)
  uint32_t toplamKayit = sayac[0] + sayac[1];
  if (toplamKayit % 25 == 0) {
    Serial.printf("# [OZET #%lu] Sinif=%s | PDR=%%%.1f (Alinan=%lu Kayip=%lu) | RSSI=%.1f dBm SNR=%.1f dB | Normal=%lu/%d Atak=%lu/%d\n",
                  toplamKayit, label ? "ATAK" : "NORMAL", pdrYuzde, pencereAlinan, pencereKayip,
                  avgRssi, avgSnr, sayac[0], HEDEF_NORMAL, sayac[1], HEDEF_ATAK);
  }

  // 500 Normal + 500 Atak = 1000 satır tamamlandığında bitiş sinyali gönder
  if (sayac[0] >= HEDEF_NORMAL && sayac[1] >= HEDEF_ATAK) {
    bitti = true;
    Serial.printf("# TAMAM: normal=%lu atak=%lu\n", sayac[0], sayac[1]);
  }
  pencereSifirla();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Wi-Fi Access Point (Sunucu) Kurulumu
  WiFi.onEvent(wifiOlay);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, AP_KANAL);
  WiFi.setSleep(false);
  udp.begin(SERVER_PORT);

  // ESP32 802.11 Promiscuous Mode (Sniffer) Kurulumu
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&snifferCallback);
  wifi_promiscuous_filter_t filtre = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
  esp_wifi_set_promiscuous_filter(&filtre);

  Serial.printf("# AP: %s, IP: %s, port: %d\n", AP_SSID, WiFi.softAPIP().toString().c_str(), SERVER_PORT);
  Serial.printf("# Hedef: %d normal + %d atak satir. 'h' = baslik, 'r' = sayaclari sifirla\n",
                HEDEF_NORMAL, HEDEF_ATAK);
  Serial.println(BASLIK);
}

void loop() {
  // Seri Port Komut Kontrolü ('h': CSV başlığını tekrar bas, 'r': sayaçları sıfırla)
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'h') Serial.println(BASLIK);
    if (c == 'r') {
      sayac[0] = sayac[1] = 0;
      bitti = false;
      Serial.println("# Sayaclar sifirlandi");
    }
  }

  // Gelen UDP paketlerini oku ve sıra numarası / zaman farkı analizine gönder
  int boyut;
  while ((boyut = udp.parsePacket()) > 0) {
    Paket p;
    if (boyut == sizeof(p) && udp.read((uint8_t *)&p, sizeof(p)) == sizeof(p)) {
      paketIsle(p);
    } else {
      udp.flush();
    }
  }

  // Her 1000 ms'de (1 sn) bir CSV satırı üret
  static uint32_t sonPencere = 0;
  if (millis() - sonPencere >= PENCERE_MS) {
    sonPencere += PENCERE_MS;
    if (clientVar && !bitti) pencereBas();
  }
  delay(1);
}

/* ============================================================================
 * 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
 * ============================================================================
 * 1. BU KOD NEYİ HEDEFLER?
 *    - ESP32 üzerinde hem standart UDP Sunucusu (Uygulama/Taşıma Katmanı) hem de
 *      Promiscuous Mode 802.11 Sniffer (Fiziksel/MAC Katmanı) çalıştırarak
 *      **Çapraz Katmanlı (Cross-Layer) Ağ Telemetrisi** toplamayı,
 *    - Her 1 saniyelik gözlem penceresinde paket kaybı, ardışık düşen paket sayısı,
 *      paketler arası gecikme sapması (jitter / gap_std_ms), RSSI dalgalanması
 *      (rssi_std), SNR ve MAC yeniden deneme oranı (retry_ratio) gibi 16 kritik
 *      parametreyi hesaplayıp standart **CSV formatında** kaydetmeyi hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Kurumsal Wi-Fi ağlarında ve endüstriyel kablosuz sensör ağlarında (WSN)
 *      bağlantı kalitesi izleme, paket kaybı teşhisi ve makine öğrenmesi modelleri
 *      için gerçek donanım tabanlı eğitim veri seti (dataset) oluşturma.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - İHA Yer Kontrol İstasyonu (YKİ / GCS) Spektrum ve Link Kalitesi Kaydedicisi:
 *      Uçuş esnasında hem UDP telemetri kayıplarını hem de alt seviye RF sinyal/gürültü
 *      (RSSI/SNR/Retry) parametrelerini eşzamanlı kaydederek Elektronik Harp (EH)
 *      veya Siber Saldırı (Drop Attack) izlerini yakalar.
 *    - Askeri Haberleşme Kara Kutusu: Görev Sonu Analiz (AAR) süreçlerinde hangi
 *      saniyelerde sinyal karıştırması veya paket düşürme yaşandığını milisaniye
 *      zaman damgasıyla CSV formatında raporlar.
 * ============================================================================ */

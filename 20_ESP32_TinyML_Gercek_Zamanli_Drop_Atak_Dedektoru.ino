// ============================================================================
// BAŞLIK    : ESP32 Üzerinde TinyML (MLP) ile Gerçek Zamanlı Drop Atağı Dedektörü
// DOSYA ADI : 20_ESP32_TinyML_Gercek_Zamanli_Drop_Atak_Dedektoru.ino
// HAZIRLAYAN: Eda Nur YAŞDIMAN (IHASresearch)
// AÇIKLAMA  : Sunucu (Server) ESP32 üzerinde çalışan gömülü yapay zeka (TinyML)
//             saldırı tespit sistemi. Her 1 saniyelik pencerede ağdan 10 özniteliği
//             (alınan/kayıp paket, kayıp oranı, ardışık kayıp, paket aralığı
//             istatistikleri, RSSI standart sapması, 802.11 MAC retry oranı ve
//             yakalanan çerçeve sayısı) çıkarır. `18_TinyML_MLP_Model_Egitimi_ve_Header_Uretici.py`
//             tarafından üretilen `19_TinyML_MLP_Model_Agirliklari.h` içerisindeki
//             3 katmanlı Çok Katmanlı Algılayıcıyı (MLP: 10 -> 16 -> 8 -> 1)
//             doğrudan ESP32 üzerinde (~25 mikrosaniyede) çalıştırarak bağlantıda
//             Drop Attack olup olmadığını gerçek zamanlı tespit eder ve Paket
//             Başarım Oranı (PDR %) ile Doğruluk (Accuracy) metriklerini raporlar.
// ============================================================================

#include <WiFi.h>       // ESP32 Wi-Fi kütüphanesi
#include <WiFiUdp.h>    // UDP haberleşme kütüphanesi
#include <math.h>       // expf, sqrtf, fmaxf matematik fonksiyonları
#include "esp_wifi.h"   // ESP32 Promiscuous Mode (802.11 Sniffer) API'si
#include "19_TinyML_MLP_Model_Agirliklari.h" // Eğitilen MLP ağırlıkları ve Z-Score normalizasyon sabitleri

// ========================== SUNUCU VE TESPİT AYARLARI ==========================
#define AP_SSID          "UAV_LINK"   // Wi-Fi Access Point (AP) adı
#define AP_PASS          "uavlink123" // Wi-Fi AP WPA2 parolası
#define AP_KANAL         1            // Wi-Fi Kanal 1 (2412 MHz)
#define SERVER_PORT      4210         // UDP port numarası
#define PENCERE_MS       1000         // Çıkarım (Inference) pencere süresi: 1000 ms (1 sn)
#define PENCERE_BEKLENEN 100          // Saniyede beklenen paket sayısı (100 Hz)
// ===============================================================================

// İstemci (14_WiFi_UDP_Drop_Atak_Simulatoru_Verici_Client.ino) ile BİREBİR AYNI 5 Baytlık UDP Paket Yapısı
struct __attribute__((packed)) Paket {
  uint32_t seq;    // 4 Bayt: Paket sıra numarası
  uint8_t  label;  // 1 Bayt: Doğrulama için gerçek faz etiketi (0 = Normal, 1 = Drop Atağı)
};

WiFiUDP udp;       // UDP nesnesi

// İstemci MAC adresi (Promiscuous Sniffer filtrelemesi için)
uint8_t       clientMac[6];
volatile bool clientVar = false;

// Kesme (ISR) güvenliği için FreeRTOS spinlock
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
float    rssiToplam = 0, rssiKareToplam = 0;
uint32_t sniffSayisi = 0, retrySayisi = 0;

// UDP Pencere İstatistikleri
uint32_t pencereAlinan = 0, pencereKayip = 0, pencereMaxArdisik = 0;
float    gapToplam = 0, gapKareToplam = 0, gapMax = 0;
uint32_t gapSayisi = 0;
uint32_t sonSeq = 0, sonAlimUs = 0;
bool     seqVar = false;
uint32_t alacak = 0;        // Boş pencerelerde yazılan kayıp sonraki pakette tekrar sayılmasın

// Gerçek Etiket ve Karışıklık Matrisi (Confusion Matrix) Sayaçları
int8_t   pencereEtiket = -1;               // -1 = Bu pencerede henüz paket yok
bool     karisik       = false;            // Faz geçiş penceresi kontrolü
uint8_t  sonEtiket     = 0;                // Son bilinen etiket
uint32_t tn = 0, fp = 0, fn = 0, tp = 0;   // True Negative, False Positive, False Negative, True Positive

// Son geçerli ortalama RSSI (dBm)
float sonRssi = 0;

// ----------------------------------------------------------------------------
// FONKSİYON : wifiOlay
// GÖREVİ    : İstemci AP'ye bağlandığında MAC adresini öğrenir.
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
// FONKSİYON : snifferCallback
// GÖREVİ    : 802.11 DATA çerçevelerinden RSSI ve MAC Retry bitini yakalar.
// ----------------------------------------------------------------------------
void snifferCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (!clientVar || type != WIFI_PKT_DATA) return;
  wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
  wifi_pkt_rx_ctrl_t *rx = &pkt->rx_ctrl;
  if (rx->sig_len < 24) return;
  if (memcmp(pkt->payload + 10, clientMac, 6) != 0) return;   // addr2 = gönderen MAC kontrolü

  float rssi  = rx->rssi;
  bool  retry = (pkt->payload[1] >> 3) & 0x1;                 // IEEE 802.11 FC Retry biti (bit 11)

  portENTER_CRITICAL_ISR(&mux);
  rssiToplam     += rssi;
  rssiKareToplam += rssi * rssi;
  if (retry) retrySayisi++;
  sniffSayisi++;
  portEXIT_CRITICAL_ISR(&mux);
}

// ----------------------------------------------------------------------------
// FONKSİYON : paketIsle
// GÖREVİ    : Gelen UDP paketlerinden kayıp sayısını, maksimum ardışık kaybı
//             ve paketler arası zaman farklarını (gap_ms) hesaplar.
// ----------------------------------------------------------------------------
void paketIsle(const Paket &p) {
  uint32_t simdiUs = micros();

  if (!seqVar || p.seq <= sonSeq) {
    seqVar = true;                       // İlk paket veya istemci yeniden başladı
  } else {
    uint32_t bosluk = p.seq - sonSeq - 1;
    if (bosluk > pencereMaxArdisik) pencereMaxArdisik = bosluk;
    uint32_t dus = min(bosluk, alacak);
    alacak -= dus;
    pencereKayip += bosluk - dus;

    // Paketler arası geçen süre (ms)
    float gap = (simdiUs - sonAlimUs) / 1000.0f;
    gapToplam     += gap;
    gapKareToplam += gap * gap;
    if (gap > gapMax) gapMax = gap;
    gapSayisi++;
  }
  sonSeq    = p.seq;
  sonAlimUs = simdiUs;
  pencereAlinan++;

  uint8_t lbl = p.label ? 1 : 0;
  if (pencereEtiket == -1) pencereEtiket = lbl;
  else if (pencereEtiket != lbl) karisik = true;
  sonEtiket = lbl;
}

// ============================================================================
// 🧠 GÖMÜLÜ YAPAY SİNİR AĞI (TINYML MLP) İLERİ GEÇİŞ (FORWARD PASS) MOTORU
// Mimari: 10 Giriş Özniteliği -> 16 Gizli Nöron (ReLU) -> 8 Gizli Nöron (ReLU) -> 1 Çıkış (Sigmoid)
// ============================================================================
void katman(const float *W, const float *B, const float *giris, float *cikis,
            int nGiris, int nCikis, bool relu) {
  for (int o = 0; o < nCikis; o++) {
    float t = B[o];
    for (int i = 0; i < nGiris; i++) {
      t += W[o * nGiris + i] * giris[i];
    }
    // Gizli katmanlarda ReLU aktivasyonu: max(0, t)
    cikis[o] = (relu && t < 0) ? 0 : t;
  }
}

float atakOlasiligi(const float *oz) {
  float x[N_OZELLIK], h1[L1_CIKIS], h2[L2_CIKIS], z;

  // 1. Z-Score Standartlaştırma: x_norm = (x - mean) / std
  for (int i = 0; i < N_OZELLIK; i++) {
    x[i] = (oz[i] - OZ_MEAN[i]) / OZ_STD[i];
  }

  // 2. Katman 1 (10 -> 16, ReLU)
  katman(M_W1, M_B1, x,  h1, L1_GIRIS, L1_CIKIS, true);

  // 3. Katman 2 (16 -> 8, ReLU)
  katman(M_W2, M_B2, h1, h2, L2_GIRIS, L2_CIKIS, true);

  // 4. Çıkış Katmanı (8 -> 1, Lineer Logit -> Sigmoid Olasılık)
  katman(M_W3, M_B3, h2, &z, L3_GIRIS, L3_CIKIS, false);
  return 1.0f / (1.0f + expf(-z));
}

void pencereSifirla() {
  pencereAlinan = pencereKayip = pencereMaxArdisik = 0;
  gapToplam = gapKareToplam = gapMax = 0;
  gapSayisi = 0;
  pencereEtiket = -1;
  karisik = false;
}

// ----------------------------------------------------------------------------
// FONKSİYON : detayliRaporBas
// GÖREVİ    : Seri porttan 's' tuşuna basıldığında Karışıklık Matrisi,
//             Doğruluk, Kesinlik (Precision), Duyarlılık (Recall) ve F1-Skoru basar.
// ----------------------------------------------------------------------------
void detayliRaporBas() {
  uint32_t toplam = tn + fp + fn + tp;
  if (toplam == 0) {
    Serial.println("# [RAPOR] Henuz test penceresi birikmedi.");
    return;
  }
  float dogruluk  = (float)(tn + tp) / toplam * 100.0f;
  float precision = (tp + fp) ? ((float)tp / (tp + fp) * 100.0f) : 100.0f;
  float recall    = (tp + fn) ? ((float)tp / (tp + fn) * 100.0f) : 100.0f;
  float f1        = (precision + recall > 0) ? (2.0f * precision * recall / (precision + recall)) : 0.0f;

  Serial.println("\n# ===================================================================");
  Serial.println("#   TINYML DROP ATTACK DEDEKTÖRÜ - KÜMÜLATİF PERFORMANS RAPORU");
  Serial.println("# ===================================================================");
  Serial.printf("#   Toplam Pencere : %lu sn | Dogruluk (Accuracy): %%%.2f\n", toplam, dogruluk);
  Serial.printf("#   Kesinlik (Prec): %%%.2f | Duyarlilik (Recall): %%%.2f | F1-Skoru: %%%.2f\n",
                precision, recall, f1);
  Serial.printf("#   Karisiklik Matrisi -> TN=%lu (Dogru Normal) | FP=%lu (Yanlis Alarm)\n", tn, fp);
  Serial.printf("#                      -> FN=%lu (Kacan Atak)   | TP=%lu (Dogru Atak)\n", fn, tp);
  Serial.println("# ===================================================================\n");
}

// ----------------------------------------------------------------------------
// FONKSİYON : pencereBas
// GÖREVİ    : Her 1 saniyede bir 10 özniteliği vektörleştirir, TinyML MLP
//             modelini koşturur ve gerçek zamanlı saldırı tespit sonucunu
//             PDR (Paket Başarım Oranı %) ile birlikte Seri Porta basar.
// ----------------------------------------------------------------------------
void pencereBas() {
  float rT, rKT;
  uint32_t sayi, retry;
  portENTER_CRITICAL(&mux);
  rT = rssiToplam; rKT = rssiKareToplam;
  sayi = sniffSayisi; retry = retrySayisi;
  rssiToplam = rssiKareToplam = 0;
  sniffSayisi = retrySayisi = 0;
  portEXIT_CRITICAL(&mux);

  if (!seqVar) { pencereSifirla(); return; }   // Henüz paket gelmedi

  // Hiç paket gelmediyse beklenenlerin tamamı kayıp
  if (pencereAlinan == 0) {
    pencereKayip      = PENCERE_BEKLENEN;
    pencereMaxArdisik = PENCERE_BEKLENEN;
    alacak           += PENCERE_BEKLENEN;
    gapMax            = PENCERE_MS;
    pencereEtiket     = sonEtiket;
  }

  // Faz değişimine denk gelen pencereyi atla
  if (karisik) {
    Serial.println("# Faz gecisi penceresi atlandi");
    pencereSifirla();
    return;
  }

  uint8_t label = pencereEtiket;

  float avgRssi = sonRssi, rssiStd = 0, retryOrani = 0;
  if (sayi > 0) {
    avgRssi    = rT / sayi;
    rssiStd    = sqrtf(fmaxf(rKT / sayi - avgRssi * avgRssi, 0));
    retryOrani = (float)retry / sayi;
    sonRssi    = avgRssi;
  }

  float avgGap = PENCERE_MS, gapStd = 0;
  if (gapSayisi > 0) {
    avgGap = gapToplam / gapSayisi;
    gapStd = sqrtf(fmaxf(gapKareToplam / gapSayisi - avgGap * avgGap, 0));
  }

  uint32_t toplamPaket = pencereAlinan + pencereKayip;
  float kayipOrani = toplamPaket ? (float)pencereKayip / toplamPaket : 0;
  float pdrYuzde   = (1.0f - kayipOrani) * 100.0f;

  // Özellik vektörü sırası 18 numaralı eğitim betiğindeki OZELLIKLER listesi ile BİREBİR AYNI olmalıdır:
  // [received, lost, loss_ratio, max_consec_lost, avg_gap_ms, gap_std_ms, max_gap_ms, rssi_std, retry_ratio, sniff_frames]
  float oz[N_OZELLIK] = {
    (float)pencereAlinan, (float)pencereKayip, kayipOrani, (float)pencereMaxArdisik,
    avgGap, gapStd, gapMax,
    rssiStd, retryOrani, (float)sayi,
  };

  // TinyML MLP Çıkarım (Inference) Süresi Ölçümü ve Tahmin
  uint32_t t0     = micros();
  float    p      = atakOlasiligi(oz);
  uint32_t sure   = micros() - t0;
  uint8_t  tahmin = p > 0.5f ? 1 : 0;

  // Karışıklık Matrisi Güncellemesi
  if (label == 0) { if (tahmin) fp++; else tn++; }
  else            { if (tahmin) tp++; else fn++; }
  uint32_t toplam = tn + fp + fn + tp;

  Serial.printf("%s gercek=%s tahmin=%s p=%.3f (%lu us) | PDR=%%%.0f kayip=%.2f rssi=%.1f | "
                "dogruluk=%.3f (%lu/%lu) TN=%lu FP=%lu FN=%lu TP=%lu\n",
                tahmin == label ? "OK  " : "HATA",
                label ? "ATAK  " : "NORMAL", tahmin ? "ATAK  " : "NORMAL", p, sure,
                pdrYuzde, kayipOrani, avgRssi,
                (float)(tn + tp) / toplam, tn + tp, toplam, tn, fp, fn, tp);
  pencereSifirla();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.onEvent(wifiOlay);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, AP_KANAL);
  WiFi.setSleep(false);
  udp.begin(SERVER_PORT);

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&snifferCallback);
  wifi_promiscuous_filter_t filtre = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
  esp_wifi_set_promiscuous_filter(&filtre);

  Serial.printf("# AP: %s, IP: %s, port: %d\n", AP_SSID, WiFi.softAPIP().toString().c_str(), SERVER_PORT);
  Serial.println("# TinyML drop atagi dedektoru hazir. 'r' = sayaclari sifirla, 's' = detayli rapor");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      tn = fp = fn = tp = 0;
      Serial.println("# Sayaclar sifirlandi");
    } else if (c == 's' || c == 'S') {
      detayliRaporBas();
    }
  }

  int boyut;
  while ((boyut = udp.parsePacket()) > 0) {
    Paket p;
    if (boyut == sizeof(p) && udp.read((uint8_t *)&p, sizeof(p)) == sizeof(p)) {
      paketIsle(p);
    } else {
      udp.flush();
    }
  }

  static uint32_t sonPencere = 0;
  if (millis() - sonPencere >= PENCERE_MS) {
    sonPencere += PENCERE_MS;
    if (clientVar) pencereBas();
  }
  delay(1);
}

/* ============================================================================
 * 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
 * ============================================================================
 * 1. BU KOD NEYİ HEDEFLER?
 *    - Herhangi bir harici bilgisayara ihtiyaç duymadan, doğrudan **ESP32 mikrodenetleyicisi
 *      üzerinde (Edge AI / TinyML)** gerçek zamanlı **Paket Düşürme (Drop Attack)**
 *      tespiti yapmayı,
 *    - Mutlak RSSI/SNR değerleri yerine konumdan bağımsız 10 dinamik özniteliği
 *      (`received`, `lost`, `loss_ratio`, `max_consec_lost`, `avg_gap_ms`,
 *      `gap_std_ms`, `max_gap_ms`, `rssi_std`, `retry_ratio`, `sniff_frames`)
 *      kullanarak sadece **~25 mikrosaniye** çıkarım süresi ve **%98.5 doğruluk**
 *      ile saldırıyı yakalamayı hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Akıllı şebekeler (Smart Grid), endüstriyel IoT ağ geçitleri ve otonom robot
 *      haberleşmelerinde düşük güç tüketimli gömülü Saldırı Tespit Sistemi (IDS)
 *      olarak kullanılır.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - İHA / SİHA Otonom Elektronik Korunma (EP - Electronic Protection) Tetikleyicisi:
 *      İHA üzerindeki haberleşme işlemcisi, her saniye çalışan bu 1.3 KB'lık TinyML
 *      modeli sayesinde yer kontrol istasyonu ile arasındaki linke **Drop Attack /
 *      Jamming** yapıldığını anında (`p > 0.5`) tespit eder. Saldırı tespit edildiği
 *      anda otonom olarak **FHSS (Frekans Atlama)** moduna geçebilir, yedek uydu/RF
 *      kanalını devreye sokabilir veya "Eve Dön" (Return-to-Home) protokolünü başlatabilir.
 * ============================================================================ */

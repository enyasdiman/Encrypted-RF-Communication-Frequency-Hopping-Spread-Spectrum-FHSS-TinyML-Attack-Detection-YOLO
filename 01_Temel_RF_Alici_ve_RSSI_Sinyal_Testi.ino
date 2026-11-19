// ============================================================================
// BAŞLIK    : 2.4 GHz Sabit Frekanslı (2446 MHz) Temel RF Alıcı ve RSSI/RPD Analizörü
// DOSYA ADI : 01_Temel_RF_Alici_ve_RSSI_Sinyal_Testi.ino
// AÇIKLAMA  : 2446 MHz (Kanal 46) sabit frekansında kriptosuz temel RF veri 
//             alımını, paket sayımını ve donanımsal RPD (Received Power Detector)
//             ile sinyal gücü (-64 dBm eşiği) tespitini gerçekleştirir.
// ============================================================================

#include <SPI.h>       // Arduino ile nRF24L01 arasındaki yüksek hızlı SPI haberleşme kütüphanesi
#include <nRF24L01.h>  // nRF24L01+ donanım yazmaç (register) tanımlamaları
#include <RF24.h>      // RF24 radyo kontrol ve paket yönetimi kütüphanesi

// --- DONANIM PİN TANIMLAMALARI (KULLANILAN KARTA GÖRE) ---
// Arduino Mega 2560 için standart SPI CSN pini 53, Uno/Nano için 10'dur.
#define CE_PIN   9             // Chip Enable: Modülün RX/TX mod geçişlerini kontrol eden pin
#define CSN_PIN 53             // Chip Select Not: SPI veri hattı seçim pini (Mega: 53, Uno: 10)
#define LED_PIN LED_BUILTIN    // Paket alındığında görsel uyarı verecek dahili LED pini

RF24 radio(CE_PIN, CSN_PIN);   // CE ve CSN pinleri ile RF24 radyo nesnesi oluşturulur

// Verici (Client/TX) ile BİREBİR AYNI olması gereken 5 baytlık (40-bit) Boru (Pipe) Adresi
const byte address[6] = "00001";

// Başarıyla alınan toplam paket sayısını tutan sayaç
unsigned long alinanPaketSayisi = 0;

void setup() {
  pinMode(LED_PIN, OUTPUT);     // Dahili LED pinini çıkış olarak ayarla
  digitalWrite(LED_PIN, LOW);   // Başlangıçta LED'i söndür

  Serial.begin(115200);         // Bilgisayar ile 115200 baud hızında seri haberleşmeyi başlat
  while (!Serial) { ; }         // Seri port bağlantısı kurulana kadar bekle

  Serial.println(F("\n=================================================="));
  Serial.println(F("   2446 MHz 'kho-merhaba' ALICI (RECEIVER) TESTI  "));
  Serial.println(F("=================================================="));

  // 1. nRF24L01 Donanım Modülünü Başlat ve Kontrol Et
  if (!radio.begin()) {
    Serial.println(F("[HATA ❌] Alici nRF24L01 modulu algilanamadi! Kablolari kontrol edin."));
    while (1); // Donanım hatası varsa programı güvenli sonsuz döngüde kilitle
  }
  Serial.println(F("[OK ✔] nRF24L01 Modulu basariyla baslatildi."));

  // 2. Verici (TX) ile Eşleşen Radyo Frekans (RF) Parametreleri
  radio.setChannel(46);           // Çalışma frekansı: 2400 + 46 = 2446 MHz
  radio.setAutoAck(false);        // Otomatik onay (ACK) paketi göndermeyi kapat (tek yönlü hızlı akış)
  radio.setDataRate(RF24_1MBPS);  // Hava veri iletim hızı: 1 Mbps (yüksek kararlılık ve menzil dengesi)
  radio.setPALevel(RF24_PA_LOW);  // Güç yükseltici (PA) seviyesi: Düşük (yakın mesafe test kararlılığı için)

  // 3. Okuma Kanalını (Pipe 1) Aç ve Dinleme (Server/RX) Moduna Geç
  radio.openReadingPipe(1, address); // 1 numaralı okuma borusunu "00001" adresine tanımla
  radio.startListening();            // Radyoyu Alıcı (RX) moduna geçir ve havayı dinlemeye başla

  Serial.print(F("[BILGI] Dinleme Frekansi: "));
  Serial.print(2400 + radio.getChannel()); // Aktif frekansı MHz cinsinden ekrana yazdır
  Serial.println(F(" MHz"));
  Serial.println(F("[BILGI] Sinyal bekleniyor... Lutfen vericiyi calistirin.\n"));
}

void loop() {
  // RX FIFO tamponunda havadan yakalanmış okunmaya hazır bir paket var mı?
  if (radio.available()) {
    char gelenMesaj[32] = ""; // Maksimum 32 baytlık nRF24L01 paket yükünü (payload) tutacak dizi

    // Havadan gelen paketi SPI üzerinden 'gelenMesaj' dizisine oku
    radio.read(&gelenMesaj, sizeof(gelenMesaj));

    alinanPaketSayisi++; // Başarılı paket sayacını 1 artır

    // Paket alındığını donanımsal olarak göstermek için LED'i yak
    digitalWrite(LED_PIN, HIGH);

    // Alınan paketin detaylarını Seri Port Ekranına (Log olarak) yazdır
    Serial.print(F("[ALINDI ✔] Paket #"));
    Serial.print(alinanPaketSayisi);
    Serial.print(F(" | Frekans: 2446 MHz | Icerik: \""));
    Serial.print(gelenMesaj);
    Serial.print(F("\""));

    // Donanımsal RSSI / RPD (Received Power Detector) Kontrolü:
    // Alınan sinyalin gücü -64 dBm eşiğinin üzerindeyse true döner
    if (radio.testRPD()) {
      Serial.print(F(" [Guclu Sinyal]"));
    }
    Serial.println();

    delay(20);                  // LED'in insan gözüyle görülmesi için 20 ms bekle
    digitalWrite(LED_PIN, LOW); // LED'i söndür
  }
}

/* ============================================================================
 * 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
 * ============================================================================
 * 1. BU KOD NEYİ HEDEFLER?
 *    - 2.4 GHz ISM bandında (2446 MHz) çalışan iki RF düğümü arasındaki fiziksel
 *      katman (PHY) bağlantısını doğrulamayı,
 *    - Otomatik el sıkışma (Auto-ACK) olmadan düşük gecikmeli (low-latency) 
 *      tek yönlü veri akışı sağlamayı,
 *    - nRF24L01+ donanımının dahili RPD (Received Power Detector / RSSI eşik)
 *      yazmacını kullanarak gelen sinyalin gücünü (-64 dBm üstü/altı) analiz
 *      etmeyi hedefler.
 *
 * 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
 *    - Kablosuz sensör ağları (WSN), akıllı tarım ve sera telemetri sistemleri,
 *    - Endüstriyel otomasyon sahalarında kablosuz durum izleme ve uzaktan kumanda,
 *    - Bina içi (indoor) sinyal kapsama alanı haritalaması ve mesafe/engel 
 *      zayıflama (path loss) testlerinde referans alıcı olarak kullanılır.
 *
 * 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
 *    - Taktik Saha Spektrum İzleme ve Sinyal Tespiti (SIGINT / ELINT): Belirli bir
 *      frekansta yayın yapan dost veya düşman unsurların sinyal gücünü (RSSI/RPD)
 *      ölçerek vericinin yakınlığını (yaklaşma/uzaklaşma durumunu) tahmin etmede,
 *    - İHA / İKA (İnsansız Hava ve Kara Araçları) Kısa Menzil Telemetri Testi:
 *      Kriptografi ve frekans atlama (FHSS) katmanları devreye alınmadan önce
 *      anten kazancı, LOS (Line-of-Sight / Görüş Hattı) kalitesi ve donanım
 *      sağlamlığının saha doğrulamasında temel yapı taşı olarak kullanılır.
 * ============================================================================ */

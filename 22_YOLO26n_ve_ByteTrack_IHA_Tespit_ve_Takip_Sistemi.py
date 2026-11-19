# ============================================================================
# BAŞLIK    : YOLO26n ve ByteTrack ile Gerçek Zamanlı İHA (UAV) Tespit ve Takip Sistemi
# DOSYA ADI : 22_YOLO26n_ve_ByteTrack_IHA_Tespit_ve_Takip_Sistemi.py
# HAZIRLAYAN: Eda Nur YAŞDIMAN (IHASresearch)
# AÇIKLAMA  : `25_IHA_Egitim_ve_Dogrulama_Videosu_2.webm` ile fine-tune edilmiş
#             `23_YOLO26n_FineTuned_IHA_Modeli.pt` (YOLO26n) modelini kullanarak
#             video akışları (`24_IHA_Test_Videosu_1.mp4` ve `25_...webm`) üzerinde
#             gerçek zamanlı İHA (UAV) tespiti ve ByteTrack algoritmasıyla yörünge
#             takibi (trajectory tracking) gerçekleştirir. Ekran üzerine hedef
#             sınırlayıcı kutusu (bounding box), hedef merkez nişangahı (crosshair),
#             hareket izi (trajectory line) ve anlık FPS / Tespit Başarım Oranı
#             HUD paneli çizer.
#
# KULLANIM  :
#     python 22_YOLO26n_ve_ByteTrack_IHA_Tespit_ve_Takip_Sistemi.py
#
# KLAVYE KISAYOLLARI (Pencere Açıkken):
#     q / ESC : Çıkış ve özet performans raporunu bas
#     SPACE   : Videoyu duraklat / devam ettir
#     t       : Yörünge takip çizgisini (Trajectory) aç / kapat
#     v       : 25_IHA_Egitim_ve_Dogrulama_Videosu_2.webm ve 24_IHA_Test_Videosu_1.mp4 arasında geçiş yap
# ============================================================================

from collections import defaultdict
from pathlib import Path
import time

import cv2
import numpy as np
import torch
from ultralytics import YOLO

ROOT = Path(__file__).resolve().parent

# ======================= PARAMETRELER =======================
MODEL = ROOT / "23_YOLO26n_FineTuned_IHA_Modeli.pt"          # Fine-tune edilmiş YOLO26n ağırlık dosyası (1 sınıf: IHA)
VIDEO = ROOT / "25_IHA_Egitim_ve_Dogrulama_Videosu_2.webm"   # Test için: ROOT / "24_IHA_Test_Videosu_1.mp4"
CONF = 0.25                                                  # Güven eşiği (Confidence threshold)
IMGSZ = 640                                                  # YOLO giriş görüntü boyutu
TRACK = True                                                 # True: ByteTrack ile ID ve yörünge izi takibi yap
SHOW = True                                                  # True: Canlı OpenCV penceresinde göster
SAVE = False                                                 # True: İşlenen videoyu runs/run_video/ altına kaydet
# ============================================================


def cihaz_sec():
    """Bilgisayarda CUDA destekli GPU varsa GPU (0), yoksa CPU seçer."""
    return 0 if torch.cuda.is_available() else "cpu"


def hud_ciz(kare, fps, kare_no, toplam_kare, tespit_sayisi, tespitli_kare_sayisi, video_adi):
    """Kare üzerine yarı saydam bilgi paneli (FPS, Tespit Oranı, Aktif Video) çizer."""
    ust = kare.copy()
    cv2.rectangle(ust, (10, 10), (460, 105), (20, 20, 20), -1)
    cv2.addWeighted(ust, 0.65, kare, 0.35, 0, kare)

    basarim = (tespitli_kare_sayisi / kare_no * 100.0) if kare_no > 0 else 0.0
    durum_renk = (0, 255, 0) if tespit_sayisi > 0 else (0, 165, 255)

    cv2.putText(kare, f"IHA Tespit & Takip (YOLO26n) | {video_adi[:28]}",
                (20, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.50, (255, 255, 255), 1, cv2.LINE_AA)
    cv2.putText(kare, f"FPS: {fps:5.1f} | Kare: {kare_no}/{toplam_kare} | Anlik IHA: {tespit_sayisi}",
                (20, 58), cv2.FONT_HERSHEY_SIMPLEX, 0.52, durum_renk, 2, cv2.LINE_AA)
    cv2.putText(kare, f"Tespit Basarim Orani: %{basarim:.1f}  [q:Cikis | Space:Dur | v:Video]",
                (20, 84), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (200, 255, 200), 1, cv2.LINE_AA)


def main():
    cihaz = cihaz_sec()
    print("=====================================================================")
    print("  YOLO26n İHA (UAV) Tespit ve Yörünge Takip Sistemi Başlatılıyor")
    print("=====================================================================")
    print(f"  Model Dosyası : {MODEL.name}")
    print(f"  Çalışma Cihazı: {'NVIDIA CUDA GPU (device=0)' if cihaz == 0 else 'CPU'}")

    model = YOLO(str(MODEL))
    videolar = [
        ROOT / "25_IHA_Egitim_ve_Dogrulama_Videosu_2.webm",
        ROOT / "24_IHA_Test_Videosu_1.mp4",
    ]
    aktif_video = VIDEO if VIDEO.exists() else videolar[0]

    cap = cv2.VideoCapture(str(aktif_video))
    if not cap.isOpened():
        raise RuntimeError(f"Video dosyası açılamadı: {aktif_video}")

    fps_kaynak = cap.get(cv2.CAP_PROP_FPS) or 30.0
    toplam_kare = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))

    writer = None
    out_path = None
    if SAVE:
        out_dir = ROOT / "runs" / "run_video"
        out_dir.mkdir(parents=True, exist_ok=True)
        out_path = out_dir / f"{aktif_video.stem}_{MODEL.stem}.mp4"

    # Her İHA ID'si için son 40 karenin merkez koordinatlarını (yörünge izi) tutan sözlük
    yorunge_gecmisi = defaultdict(lambda: [])
    iz_goster = TRACK
    kare_no = 0
    tespitli_kare_sayisi = 0
    baslangic_zamani = time.perf_counter()

    while True:
        t0 = time.perf_counter()
        ok, frame = cap.read()
        if not ok:          # Video bitti
            break

        kare_no += 1

        # Kareyi modele ver: TRACK aktifse ByteTrack ile takip et, değilse standart tespit yap
        if iz_goster:
            r = model.track(frame, imgsz=IMGSZ, conf=CONF, device=cihaz,
                            persist=True, tracker="bytetrack.yaml", verbose=False)[0]
        else:
            r = model.predict(frame, imgsz=IMGSZ, conf=CONF, device=cihaz, verbose=False)[0]

        vis = r.plot(line_width=2)
        anlik_tespit = 0

        # Tespit edilen İHA kutularının merkez nişangahını (crosshair) ve yörünge izini çiz
        if r.boxes is not None and len(r.boxes) > 0:
            anlik_tespit = len(r.boxes)
            tespitli_kare_sayisi += 1
            boxes_xywh = r.boxes.xywh.cpu().numpy()
            track_ids = (
                r.boxes.id.int().cpu().tolist()
                if r.boxes.id is not None
                else list(range(len(boxes_xywh)))
            )

            for (cx, cy, w, h), tid in zip(boxes_xywh, track_ids):
                merkez = (int(cx), int(cy))
                # Hedef merkez noktası (Kırmızı Crosshair)
                cv2.circle(vis, merkez, 4, (0, 0, 255), -1)

                if iz_goster:
                    iz = yorunge_gecmisi[tid]
                    iz.append((float(cx), float(cy)))
                    if len(iz) > 40:
                        iz.pop(0)
                    if len(iz) >= 2:
                        noktalar = np.hstack(iz).astype(np.int32).reshape((-1, 1, 2))
                        cv2.polylines(vis, [noktalar], isClosed=False, color=(0, 255, 255), thickness=2)

        anlik_fps = 1.0 / max(time.perf_counter() - t0, 1e-6)
        hud_ciz(vis, anlik_fps, kare_no, toplam_kare, anlik_tespit, tespitli_kare_sayisi, aktif_video.name)

        if SAVE:
            if writer is None:   # Boyut ilk kareden belli olduğu için VideoWriter burada açılır
                writer = cv2.VideoWriter(
                    str(out_path),
                    cv2.VideoWriter_fourcc(*"mp4v"),
                    fps_kaynak,
                    (vis.shape[1], vis.shape[0]),
                )
            writer.write(vis)

        if SHOW:
            cv2.imshow("IHA Tespiti ve Yorunge Takibi (YOLO26n)", vis)
            k = cv2.waitKey(1) & 0xFF
            if k in (ord("q"), 27):       # q veya ESC: Çıkış
                break
            elif k == ord(" "):           # Boşluk (Space): Herhangi bir tuşa basılana kadar duraklat
                cv2.waitKey(0)
            elif k in (ord("t"), ord("T")):
                iz_goster = not iz_goster
                yorunge_gecmisi.clear()
            elif k in (ord("v"), ord("V")):
                # 25_...webm ve 24_...mp4 arasında anında geçiş yap
                cap.release()
                aktif_video = videolar[1] if aktif_video == videolar[0] else videolar[0]
                cap = cv2.VideoCapture(str(aktif_video))
                toplam_kare = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
                kare_no = 0
                tespitli_kare_sayisi = 0
                yorunge_gecmisi.clear()
                print(f"[BİLGİ] Aktif video değiştirildi -> {aktif_video.name}")

    cap.release()
    if writer:
        writer.release()
        print("Kayıt tamamlandı:", out_path)
    cv2.destroyAllWindows()

    # Çalışma Sonu Performans Raporu
    gecen_sure = max(time.perf_counter() - baslangic_zamani, 1e-6)
    ort_fps = kare_no / gecen_sure
    tespit_orani = (tespitli_kare_sayisi / kare_no * 100.0) if kare_no > 0 else 0.0

    print("\n=====================================================================")
    print("             İHA TESPİT VE TAKİP PERFORMANS RAPORU                   ")
    print("=====================================================================")
    print(f"  İncelenen Video          : {aktif_video.name}")
    print(f"  İşlenen Toplam Kare      : {kare_no}")
    print(f"  İHA Tespit Edilen Kare   : {tespitli_kare_sayisi} (%{tespit_orani:.2f})")
    print(f"  Ortalama İşleme Hızı     : {ort_fps:.2f} FPS")
    print("=====================================================================")


if __name__ == "__main__":
    main()

# ============================================================================
# 📌 PROJE ANALİZİ VE UYGULAMA ALANLARI DEĞERLENDİRMESİ
# ============================================================================
# 1. BU KOD NEYİ HEDEFLER?
#    - Özel İHA görüntüleriyle ince ayarı (fine-tuning) yapılmış hafif **YOLO26n**
#      derin öğrenme modelini (`23_YOLO26n_FineTuned_IHA_Modeli.pt`) kullanarak
#      videolar üzerinde gerçek zamanlı **İHA (UAV) Tespiti ve Yörünge Takibi (ByteTrack)**
#      yapmayı,
#    - Hedefin ekran üzerindeki konumunu, merkez nişangahını (crosshair), hareket
#      izini ve anlık tespit başarım yüzdesini yüksek FPS hızında görselleştirmeyi
#      hedefler.
#
# 2. NORMALDE (SİVİL / ENDÜSTRİYEL ALANDA) NE İÇİN KULLANILIR?
#    - Havalimanları, enerji santralleri ve sivil tesis çevresinde izinsiz uçan
#      drone'ların güvenlik kameralarıyla otomatik tespiti,
#    - Otonom kargo ve tarım İHA'larının görsel çarpışma önleme (Sense & Avoid)
#      sistemleri.
#
# 3. SAVUNMA SANAYİİ VE ASKERİ SİSTEMLERDE NEREDE KULLANILABİLİR?
#    - Hava Savunma ve Anti-Drone (C-UAS / Counter-UAV) Elektro-Optik Sistemleri:
#      Radar kesit alanı (RCS) çok düşük olan mikro/mini İHA ve FPV kamikaze
#      drone'larının elektro-optik/termal kameralar üzerinden gerçek zamanlı tespit
#      ve takibini sağlar.
#    - Otonom Önleyici İHA (Interceptor UAV) Güdüm Sistemi: Tespit edilen hedef
#      İHA'nın merkez koordinatları (`cx, cy`) ve yörünge vektörü doğrudan uçuş
#      kontrolcüsüne (PID / Gimbal) aktarılarak hedefin otonom kilitlenme (lock-on)
#      ile takip edilmesinde kullanılır.
# ============================================================================

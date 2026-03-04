# Panduan Instalasi Sensor RPM (Much Racing) ⚙️

## SPESIAL: Metode Lilitan Kabel Busi (Inductive) 🌀

---

## Skematik Sistem Lengkap

### Wiring Penuh (AKI 12V → Buck → ESP32-C3)

![Skematik Sistem Lengkap](rpm_full_schematic.png)

### Rangkaian Signal Conditioning (Sederhana)

![Skematik Sederhana](rpm_schematic_simple.png)

---

## 1. Pemilihan Jenis Kabel (PENTING!)

Anda bertanya tentang **"Kabel Probe"**. Berikut rekomendasinya:

### ❌ Kabel Probe Multimeter (Tebal)

Kabel test lead multimeter biasanya punya isolasi sangat tebal (Double Insulated).

* **Efek**: Jarak antara kawat tembaga dan inti busi jadi jauh.
* **Hasil**: Sinyal **SANGAT LEMAH**. PC817 mungkin tidak akan menyala.
* *Saran: Jangan gunakan kecuali Anda kupas kulit luarnya.*

### ✅ Kawat Email / Magnet Wire (0.2-0.3mm) - *Disarankan untuk Inductive*

Kawat tembaga berlapiskan pernis tipis.

* Isolasi sangat tipis → sinyal induksi kuat.
* Mudah dililit rapat di kabel busi.
* Hemat tempat, tahan panas.

### ✅ Kabel Serabut Biasa (Kabel Body Motor / AWG 22)

Kabel listrik standar motor (warna-warni).

* Isolasi tipis, kawat serabut tembaga.
* Sinyal induksi bisa menembus isolasi dengan baik.
* Mudah dililit rapat.

### 🔥 Kabel Audio / Microphone (Shielded Cable) - *Terbaik untuk Noise*

Jika jarak mesin ke dashboard jauh (> 50cm).

* Gunakan kabel stereo/mic (isi 2 + ground pelindung).
* **Inti kabel**: Pakai untuk sinyal (+).
* **Serabut luar (Shield)**: Sambung ke Ground **HANYA di sisi ESP32**. Jangan sambung di sisi mesin. Ini membuang noise liar.

---

## 2. Cara Lilit yang Benar

1. **Jumlah Lilitan**: Lilit **7-10 kali** serapat mungkin di kabel busi.
2. **Posisi**: Letakkan di tengah kabel busi (jauhi coil/CDI).
3. **Kunci dengan Ties**: Ikat ujung lilitan dengan kabel ties atau lakban agar tidak bergeser.

---

## 3. Komponen Rangkaian Signal Conditioning

| Komponen | Nilai | Fungsi |
|---|---|---|
| D1 | 1N4148 | Rectifier — potong tegangan negatif AC |
| R1 | 10kΩ | Voltage divider bawah (ke GND) |
| R2 | 10kΩ | Voltage divider atas (seri ke GPIO) |
| C1 | 100nF (kode 104) | Filter noise dari percikan busi |

---

## 4. Wiring Inductive ke ESP32-C3 (Tanpa PC817)

```
Lilitan 7x di Kabel Busi
         │
      Ujung A ──[D1: 1N4148]──┬──[R2: 10kΩ]──┬── GPIO 2 (ESP32-C3)
                              │               │
                           [R1: 10kΩ]      [C1: 100nF]
                              │               │
      Ujung B ───────────────GND BERSAMA─────GND
                         (Rangka Motor / Massa)
```

---

## 5. Wiring Power (AKI 12V)

```
AKI 12V (+) ──→ [Buck Converter 12V→5V] ──→ 5V IN (ESP32-C3)
AKI 12V (-) ──→ Rangka Motor = GND Bersama ──→ GND (ESP32-C3)
```

> ⚠️ **PENTING**: Set output Buck Converter ke tepat **5V** sebelum disambungkan ke ESP32-C3!

---

## 6. Wiring Output ke ESP32 Utama (via ESP-NOW)

Tidak ada kabel sinyal antara ESP32-C3 dan ESP32 utama!
Data RPM dikirim **nirkabel via WiFi ESP-NOW** secara otomatis.

```
ESP32-C3 (Sender) ──[ ESP-NOW Wireless ]──→ ESP32 Utama (Receiver/GPS Unit)
```

---

## 7. Setting Kode untuk 2-Tak

Di file `WirelessSender/src/main.cpp`:

```cpp
#define PPR 1.0          // 2-tak: 1 pulsa = 1 rotasi ✅
#define SEND_INTERVAL 30 // Kirim data setiap 30ms (~33Hz)
#define PIN_SIGNAL 2     // GPIO 2 ESP32-C3
```

---

## 8. Indikator LED Status (GPIO 8 — NeoPixel Onboard)

| Warna | Status |
|---|---|
| 🔴 Merah blink lambat (1Hz) | Idle — RPM = 0 |
| 🔵 Biru blink cepat (10Hz) | Aktif — RPM terdeteksi & dikirim |
| 🔵 Biru solid | Scan selesai, receiver ditemukan |
| 🔴 Merah terang | Error ESP-NOW |

---

Selamat mencoba! 🏍️

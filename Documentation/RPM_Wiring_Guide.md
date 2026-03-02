# Panduan Instalasi Sensor RPM (Much Racing) ⚙️

## SPESIAL: Metode Lilitan Kabel Busi (Inductive) 🌀

---

## 1. Pemilihan Jenis Kabel (PENTING!)

Anda bertanya tentang **"Kabel Probe"**. Berikut rekomendasinya:

### ❌ Kabel Probe Multimeter (Tebal)
Kabel test lead multimeter biasanya punya isolasi sangat tebal (Double Insulated).
*   **Efek**: Jarak antara kawat tembaga dan inti busi jadi jauh.
*   **Hasil**: Sinyal **SANGAT LEMAH**. PC817 mungkin tidak akan menyala.
*   *Saran: Jangan gunakan kecuali Anda kupas kulit luarnya.*

### ✅ Kabel Serabut Biasa (Kabel Body Motor / AWG 22) - *Disarankan*
Kabel listrik standar motor (warna-warni).
*   Isolasi tipis, kawat serabut tembaga.
*   Sinyal induksi bisa menembus isolasi dengan baik.
*   Mudah dililit rapat.

### 🔥 Kabel Audio / Microphone (Shielded Cable) - *Terbaik untuk Noise*
Jika jarak mesin ke dashboard jauh (> 50cm).
*   Gunakan kabel stereo/mic (isi 2 + ground pelindung).
*   **Inti kabel**: Pakai untuk sinyal (+).
*   **Serabut luar (Shield)**: Sambung ke Ground **HANYA di sisi ESP32**. Jangan sambung di sisi mesin. Ini membuang noise liar.

---

## 2. Cara Lilit yang Benar
1.  **Lilitan Rapat**: Gunakan kabel serabut biasa. Lilit **15-20 kali** serapat mungkin di kabel busi.
2.  **Kunci dengan Ties**: Ikat ujung lilitan dengan kabel ties atau lakban agar tidak bergeser.

---

## 3. Wiring ke PC817 (Inductive)

```
[ Ujung Kabel Lilitan 1 ] ------------> Pin 1 (Anode) PC817

[ Ujung Kabel Lilitan 2 ] --+---------> Pin 2 (Cathode) PC817
                            |
[ Ground Body Mesin ] ------+
```
*   **Grounding**: Salah satu ujung lilitan **WAJIB** ke Ground Body.

---

## 4. Wiring Output PC817 ke ESP32

```
Pin 4 (Collector) --> GPIO 35 (ESP32)
Pin 3 (Emitter)   --> Ground (ESP32)

* Pull-Up Resistor 10k: Wajib pasang dari Pin 4 ke 3.3V.
```

## 5. Setting Menu 2-Tak
*   **PULSE PER REV**: **1.0**

Selamat mencoba!

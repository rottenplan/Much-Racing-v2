# Panduan Setup Wireless RPM (ESP-NOW) 📡🏎️

Sistem ini memungkinkan pembacaan RPM secara wireless dari mesin ke dashboard menggunakan dua unit ESP32 yang berkomunikasi via protokol **ESP-NOW** (Low Latency).

## 1. Daftar Komponen (Bill of Materials)
*   **Modul Transmitter**: 1x ESP32-C3 Super Mini (atau model ESP32 lainnya).
*   **Power Supply**: 1x Module Step-Down / Buck Converter (Tegangan Aki 12V -> 5V).
*   **Resistor**: 1x 10k Ohm (sebagai resistor pengaman/pulldown).
*   **Kabel**: Kabel serabut biasa (untuk lilitan induksi).
*   **Casing**: Box plastik kecil/Project box (opsional, untuk melindungi modul dari panas mesin).

## 2. Skema Wiring Daya (Step-Down) ⚡
Agar modul tidak perlu baterai dan menyala otomatis dengan kendaraan:

1.  **Input Step-Down**:
    *   **IN+**: Hubungkan ke Kabel Kunci Kontak / ACC (+) 12V.
    *   **IN-**: Hubungkan ke Ground / Body Kendaraan (-).
2.  **Output Step-Down**:
    *   Setel output ke **tepat 5.0V** menggunakan voltmeter sebelum disambung ke ESP32.
    *   **OUT+**: Hubungkan ke pin **5V / VCC** pada ESP32-C3.
    *   **OUT-**: Hubungkan ke pin **GND** pada ESP32-C3.

## 3. Skema Sensor RPM (Inductive) 🌀
1.  **Lilitan**: Lilitkan kabel serabut rapat-rapat sebanyak 10-15 kali pada kabel busi.
2.  **Koneksi**: 
    *   Ujung kabel lilitan masuk ke pin **GPIO 2** di ESP32-C3.
    *   Pasang **Resistor 10k Ohm** di antara pin GPIO 2 dan Ground (GND) di sisi ESP32 (untuk stabilitas sinyal).

## 4. Cara Kerja & Prioritas Sinyal ⚖️
*   **Sender (ESP32-C3)**: Menghitung pulsa dari busi dan memancarkan data RPM setiap 30ms via ESP-NOW.
*   **Receiver (Device Utama)**: 
    *   Secara otomatis mendengarkan sinyal broadcast ESP-NOW.
    *   **Prioritas**: Jika sinyal Wireless terdeteksi, sistem akan mengabaikan sensor kabel fisik (Wired) selama sinyal wireless masih aktif.
    *   **Fallback**: Jika modul wireless mati, sistem otomatis kembali membaca sensor dari kabel fisik (jika terpasang).

## 5. Firmware
*   Kode untuk transmitter (C3) dapat ditemukan di: `Firmware/examples/WirelessSender.ino`.
*   Flash menggunakan Board: **ESP32C3 Dev Module**.

---
*Dokumentasi ini dibuat untuk proyek Much Racing - Antigravity.*

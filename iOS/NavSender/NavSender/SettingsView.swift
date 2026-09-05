import SwiftUI

// MARK: - Setelan: WiFi device, IP, BLE manual test, log, tentang

struct SettingsView: View {
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var live: LiveStore

    @State private var ipInput = ""

    var body: some View {
        Form {
            Section {
                Text("Device WiFi (Live & Sesi)")
                    .font(.headline)
                    .listRowBackground(Color(.secondarySystemBackground))
            }

            Section("Koneksi WiFi") {
                TextField("Alamat device (mis. http://192.168.4.1)", text: $ipInput)
                    .keyboardType(.URL)
                    .textInputAutocapitalization(.never)
                    .disableAutocorrection(true)
                Button("Simpan Alamat") {
                    let url = ipInput.trimmingCharacters(in: .whitespaces)
                    live.setBaseURL(url.hasPrefix("http") ? url : "http://\(url)")
                    ipInput = APClient.savedBaseURL()
                }
                Button {
                    Task { await live.tick(force: true) }
                } label: {
                    Label("Cek Koneksi", systemImage: "antenna.radiowaves.left.and.right")
                }
                HStack {
                    Text("Status")
                    Spacer()
                    Text(live.connected ? "Terhubung" : "Putus")
                        .foregroundColor(live.connected ? .green : .secondary)
                }
            }

            Section("Lima Mode Koneksi") {
                Label("AP: iPhone join \u{201C}MuchRacing-GPS\u{201D} (12345678), alamat otomatis 192.168.4.1", systemImage: "1.circle")
                Label("Hotspot/rumah: device join WiFi yang sama dengan iPhone, isi IP device (tampil di layar Web Server device)", systemImage: "2.circle")
                Label("Live/Sesi butuh salah satu di atas; Navigasi cukup BLE saja", systemImage: "3.circle")
            }
            .font(.footnote)

            Section("Bluetooth (Navigasi)") {
                Label(ble.status.text, systemImage: "dot.radiowaves.left.and.right")
                HStack {
                    Button("Scan / Ulangi") { ble.scan() }
                        .buttonStyle(.borderedProminent)
                    Button("Putus") { ble.disconnect() }
                        .buttonStyle(.bordered)
                        .disabled(!ble.status.isConnected)
                }
            }

            Section("Uji Manual BLE") {
                ManualTestSection()
            }

            Section("Log BLE (RX)") {
                if ble.rxLog.isEmpty {
                    Text("Belum ada data masuk")
                        .foregroundColor(.secondary)
                } else {
                    Text(ble.rxLog.suffix(600))
                        .font(.system(.caption, design: .monospaced))
                        .textSelection(.enabled)
                }
                Button("Bersihkan Log", role: .destructive) { ble.clearLog() }
            }

            Section("Tentang") {
                LabeledContent("Aplikasi", value: "MuchRacing iOS 1.0")
                LabeledContent("Firmware", value: "v4.1.4")
                LabeledContent("Protokol", value: "BLE NUS + HTTP AP")
                Label("Nav JSON: {icon,dist,text} • LogPacket 32B di SD", systemImage: "info.circle")
            }
            .font(.footnote)
        }
        .navigationTitle("Setelan")
        .onAppear {
            ipInput = APClient.savedBaseURL()
        }
    }
}
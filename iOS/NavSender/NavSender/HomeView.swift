import SwiftUI

// MARK: - Beranda: hub dashboard + status device (mirip PRO HUB web)

struct HomeView: View {
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var live: LiveStore
    @Binding var selection: AppTab

    // Info "Cara pakai" disembunyikan; muncul saat bendera di header ditekan.
    @State private var showHowTo = false
    // Animasi intro tersusun saat Beranda pertama kali tampil.
    @State private var introPlayed = false

    private let columns = [GridItem(.flexible()), GridItem(.flexible())]

    var body: some View {
        ZStack {
            ScrollView {
                VStack(spacing: 16) {
                    header
                        .opacity(introPlayed ? 1 : 0)
                        .offset(y: introPlayed ? 0 : -14)
                        .animation(.spring(response: 0.5, dampingFraction: 0.8).delay(0.05), value: introPlayed)
                    statusRow
                        .opacity(introPlayed ? 1 : 0)
                        .offset(y: introPlayed ? 0 : -10)
                        .animation(.spring(response: 0.5, dampingFraction: 0.8).delay(0.12), value: introPlayed)
                    hubGrid
                }
                .padding()
            }
            .background(RacingBackground())

            if showHowTo {
                howToPopup
            }
        }
        .navigationTitle("MuchRacing")
        .onAppear {
            ble.start()
            guard !introPlayed else { return }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                withAnimation(.spring(response: 0.55, dampingFraction: 0.78)) {
                    introPlayed = true
                }
            }
        }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                RacingBadge(text: "PRO HUB", color: .neonOrange, icon: "bolt.fill")
                Spacer()
                Button {
                    withAnimation(.spring(response: 0.35, dampingFraction: 0.75)) {
                        showHowTo.toggle()
                    }
                } label: {
                    Image(systemName: "flag.checkered")
                        .font(.title3.weight(.semibold))
                        .foregroundColor(showHowTo ? .neonOrange : .white.opacity(0.4))
                        .neonGlow(showHowTo ? .neonOrange : .clear, radius: 5)
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Cara pakai")
            }
            Text("Kontrol penuh device dari iPhone")
                .font(.title2.weight(.heavy))
                .foregroundColor(.white)
            Text("Telemetri live, navigasi, drag, dan sesi — semua langsung dari perangkat MuchRacing.")
                .font(.footnote)
                .foregroundColor(.secondary)
            CheckeredStrip()
                .padding(.top, 4)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var statusRow: some View {
        HStack(spacing: 12) {
            statusCard(title: "Bluetooth (Nav)",
                       dot: ble.status.isConnected ? .green : .gray,
                       text: ble.status.text,
                       action: { selection = .nav })
            statusCard(title: "WiFi (Live/Sesi)",
                       dot: live.connected ? .green : .gray,
                       text: live.connected ? "Device terhubung" : "Luar jaringan AP",
                       action: { selection = .settings })
        }
    }

    private func statusCard(title: String, dot: Color, text: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            RacingCard(accent: dot == .gray ? .neonRed : dot, glow: true) {
                VStack(alignment: .leading, spacing: 6) {
                    HStack(spacing: 6) {
                        Circle()
                            .fill(dot)
                            .frame(width: 8, height: 8)
                            .neonGlow(dot, radius: 5)
                        Text(title).font(.caption.weight(.bold)).foregroundColor(.secondary)
                    }
                    Text(text)
                        .font(.footnote.weight(.medium))
                        .foregroundColor(.white)
                        .lineLimit(2)
                    Spacer(minLength: 0)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(12)
            }
        }
        .buttonStyle(.plain)
    }

    private var hubGrid: some View {
        let items: [(String, String, String, Color, AppTab)] = [
            ("Live Telemetri", "gauge.with.dots.needle.67percent", "Speed, RPM, trip, bat", .green, .live),
            ("GPS Navigasi", "location.north.line.fill", "Turn-by-turn via BLE", .orange, .nav),
            ("Drag Meter", "flag.checkered", "0-60, 100, 402 m", .red, .live),
            ("Sesi & Lap", "clock.badge.checkmark", "Lap times dari SD", .cyan, .sessions),
            ("WiFi Device", "wifi", "AP/hotspot, IP", .blue, .settings),
            ("Uji Manual BLE", "dot.radiowaves.left.and.right", "Kirim manuver", .purple, .settings),
        ]
        return LazyVGrid(columns: columns, spacing: 12) {
            ForEach(Array(items.enumerated()), id: \.offset) { i, it in
                hubTile(it.0, it.1, it.2, color: it.3, index: i) { selection = it.4 }
            }
        }
    }

    private func hubTile(_ title: String, _ icon: String, _ desc: String, color: Color, index: Int, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            RacingCard(accent: color, glow: true) {
                VStack(alignment: .leading, spacing: 8) {
                    Image(systemName: icon)
                        .font(.title2.weight(.semibold))
                        .foregroundColor(color)
                        .neonGlow(color, radius: 7)
                    Text(title)
                        .font(.subheadline.weight(.bold))
                        .foregroundColor(.white)
                        .multilineTextAlignment(.leading)
                    Text(desc)
                        .font(.caption2)
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.leading)
                    Spacer(minLength: 0)
                }
                .frame(maxWidth: .infinity, minHeight: 110, alignment: .leading)
                .padding(12)
            }
        }
        .buttonStyle(.plain)
        .opacity(introPlayed ? 1 : 0)
        .scaleEffect(introPlayed ? 1 : 0.7)
        .offset(y: introPlayed ? 0 : 24)
        .animation(.spring(response: 0.55, dampingFraction: 0.78).delay(Double(index) * 0.06), value: introPlayed)
    }

    // MARK: - Popup "Cara pakai" dengan backdrop blur

    private var howToPopup: some View {
        ZStack {
            // Backdrop: blur + gelap; ketuk di luar kartu untuk menutup.
            Rectangle()
                .fill(.ultraThinMaterial)
                .ignoresSafeArea()
                .onTapGesture { dismissHowTo() }
                .transition(.opacity)

            Color.black.opacity(0.35)
                .ignoresSafeArea()
                .allowsHitTesting(false)

            // Kartu popup di tengah layar.
            RacingCard(accent: .neonYellow, glow: true) {
                VStack(alignment: .leading, spacing: 12) {
                    HStack {
                        RacingBadge(text: "CARA PAKAI", color: .neonYellow, icon: "lightbulb.fill")
                        Spacer()
                        Button {
                            dismissHowTo()
                        } label: {
                            Image(systemName: "xmark.circle.fill")
                                .font(.title3)
                                .foregroundColor(.neonOrange)
                        }
                        .buttonStyle(.plain)
                    }
                    Text("1. Nyalakan WiFi AP device (SSID \u{201C}MuchRacing-GPS\u{201D}, sandi 12345678).\n2. Di iPhone: Settings \u{203A} Wi-Fi \u{203A} pilih MuchRacing-GPS.\n3. Kembali ke app — Live, Drag, dan Sesi otomatis terhubung.\n4. Untuk Navigasi, gunakan BLE MuchRacing-Nav saat tidak di AP.")
                        .font(.footnote)
                        .foregroundColor(.secondary)
                        .lineSpacing(3)
                    CheckeredStrip()
                        .padding(.top, 2)
                }
                .padding(18)
                .frame(maxWidth: 330)
            }
            .contentShape(RoundedRectangle(cornerRadius: 16))
            .onTapGesture { /* ketuk di kartu tidak menutup popup */ }
            .padding(24)
            .transition(.scale(scale: 0.92).combined(with: .opacity))
        }
    }

    private func dismissHowTo() {
        withAnimation(.spring(response: 0.3, dampingFraction: 0.8)) {
            showHowTo = false
        }
    }
}
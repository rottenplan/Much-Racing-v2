import SwiftUI
import MapKit

// MARK: - Panel navigasi turn-by-turn untuk layar Live (di atas spidometer)
// Cari tujuan -> hitung rute -> tampil kartu manuver sambil spidometer tetap
// terlihat. Semua state & alur (auto-advance, keep-alive, langkah manual)
// ditangani oleh NavigationSession agar konsisten dengan tab Navigasi penuh.

struct NavigationPanelView: View {
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var route: RouteManager
    @EnvironmentObject var location: LocationTracker
    @EnvironmentObject var session: NavigationSession

    @State private var destQuery = ""
    @State private var showError = false
    @State private var errorText = ""

    // Kirim ulang langkah tiap 45 detik: layar device tidak akan "tidur"
    // walaupun pengendara berhenti lama (lampu merah, macet).
    private let keepAlive = Timer.publish(every: 45, on: .main, in: .common).autoconnect()

    var body: some View {
        VStack(spacing: 14) {
            if route.steps.isEmpty {
                searchCard
            } else if session.isActive || session.hasArrived {
                instructionCard
                progressRow
                actionButtons
            } else {
                resumeCard
            }
        }
        .onAppear { if session.isActive { location.start() } }
        .onReceive(location.$lastLocation) { loc in
            if let loc { session.updateProgress(with: loc) }
        }
        .onReceive(keepAlive) { _ in
            session.keepAlive()
        }
        .alert("Perhatian", isPresented: $showError) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(errorText)
        }
    }

    // MARK: - Turunan state

    private var iconSymbol: String {
        switch session.currentStep?.icon ?? 1 {
        case 0: return "flag.checkered"
        case 2: return "arrow.up.left"
        case 3: return "arrow.left"
        case 4: return "arrow.up.left"
        case 5: return "arrow.up.right"
        case 6: return "arrow.right"
        case 7: return "arrow.up.right"
        case 8: return "arrow.uturn.down"
        case 9: return "arrow.triangle.2.circlepath"
        default: return "arrow.up"
        }
    }

    private var distanceText: String {
        if session.hasArrived { return "Tiba \u{2713}" }
        guard let rem = session.remainingM else { return "--" }
        if rem >= 1000 { return String(format: "%.1f km", rem / 1000) }
        return "\(Int(rem)) m"
    }

    // MARK: - Alur

    @MainActor
    private func startNavigation() {
        let query = destQuery.trimmingCharacters(in: .whitespaces)
        guard !query.isEmpty else { return }

        if location.authorization == .notDetermined {
            location.requestPermission()
            errorText = "Setujui izin lokasi yang muncul, lalu tekan 'Cari & Hitung Rute' lagi."
            showError = true
            return
        }

        guard let userLoc = location.lastLocation else {
            errorText = "Lokasi iPhone belum tersedia. Aktifkan izin lokasi di Settings, lalu coba lagi."
            showError = true
            return
        }

        route.isComputing = true
        Task {
            defer { route.isComputing = false }
            do {
                guard let dest = try await route.searchDestination(query, near: userLoc.coordinate) else {
                    errorText = "Tempat tidak ditemukan. Coba nama atau alamat lain."
                    showError = true
                    return
                }
                let ok = try await route.computeRoute(from: MKMapItem.forCurrentLocation(), to: dest)
                if !ok {
                    errorText = "Rute tidak ditemukan ke tujuan tersebut."
                    showError = true
                } else {
                    session.start()
                }
            } catch {
                errorText = route.friendlyError(error)
                showError = true
            }
        }
    }

    // MARK: - UI

    private var searchCard: some View {
        RacingCard(accent: .neonOrange, glow: true) {
            VStack(alignment: .leading, spacing: 10) {
                Label("Tujuan Navigasi", systemImage: "point.topleft.down.curvedto.point.bottomright.up")
                    .font(.headline.weight(.bold))
                    .foregroundColor(.white)
                TextField("Alamat / nama tempat", text: $destQuery)
                    .textFieldStyle(.roundedBorder)
                    .submitLabel(.search)
                    .onSubmit { startNavigation() }
                Button {
                    startNavigation()
                } label: {
                    if route.isComputing {
                        ProgressView()
                    } else {
                        Label("Cari & Hitung Rute", systemImage: "magnifyingglass")
                    }
                }
                .frame(maxWidth: .infinity)
                .buttonStyle(.borderedProminent)
                .tint(.neonOrange)
                .disabled(destQuery.trimmingCharacters(in: .whitespaces).isEmpty || route.isComputing)

                RouteOptionsView()

                Label("Rute dihitung via internet — hitung saat online, rute otomatis disimpan lalu navigasi jalan lewat BLE tanpa internet.", systemImage: "info.circle")
                    .font(.footnote)
                    .foregroundColor(.neonYellow)
            }
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private var resumeCard: some View {
        RacingCard(accent: .neonGreen, glow: true) {
            VStack(alignment: .leading, spacing: 10) {
                if route.routes.count > 1 {
                    RouteAlternatives {
                        if session.isActive { session.start() }
                    }
                }

                Text(route.routeSummary)
                    .font(.headline.weight(.bold))
                    .fontDesign(.rounded)
                    .monospacedDigit()
                Text("\(route.steps.count) langkah • mulai: \(route.steps.first?.instruction ?? "")")
                    .font(.caption)
                    .foregroundColor(.secondary)
                HStack(spacing: 12) {
                    Button {
                        session.start()
                    } label: {
                        Label("Mulai Navigasi", systemImage: "play.fill")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(.neonGreen)
                    .disabled(route.steps.isEmpty)

                    Button {
                        session.reset()
                        route.reset()
                    } label: {
                        Label("Hapus Rute", systemImage: "trash")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                    .tint(.neonRed)
                }

                if route.isSavedRoute {
                    Label("Rute tersimpan — bisa dipakai offline lewat BLE tanpa internet.", systemImage: "bolt.fill")
                        .font(.footnote)
                        .foregroundColor(.neonGreen)
                }
            }
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private var instructionCard: some View {
        RacingCard(accent: session.hasArrived ? .neonGreen : .neonOrange, glow: true) {
            VStack(spacing: 10) {
                Image(systemName: iconSymbol)
                    .font(.system(size: 52, weight: .bold))
                    .foregroundColor(session.hasArrived ? .neonGreen : .neonOrange)
                    .neonGlow(session.hasArrived ? .neonGreen : .neonOrange, radius: 10)
                Text(session.currentStep?.instruction ?? "Menghitung...")
                    .font(.title3.weight(.bold))
                    .multilineTextAlignment(.center)
                Text(distanceText)
                    .font(.racingDigits(34))
                    .foregroundColor(session.hasArrived ? .neonGreen : .white)
                    .neonGlow(session.hasArrived ? .neonGreen : .neonOrange, radius: 6)
            }
            .frame(maxWidth: .infinity)
            .padding(18)
        }
    }

    private var progressRow: some View {
        HStack {
            Text("Langkah \(session.currentIndex + 1) dari \(route.steps.count)")
            Spacer()
            if ble.status.isConnected {
                Label("Terhubung", systemImage: "checkmark.circle.fill")
                    .foregroundColor(.neonGreen)
            } else {
                Label("BLE terputus", systemImage: "exclamationmark.triangle.fill")
                    .foregroundColor(.neonOrange)
            }
        }
        .font(.caption)
        .foregroundColor(.secondary)
    }

    private var actionButtons: some View {
        VStack(spacing: 10) {
            HStack(spacing: 12) {
                Button {
                    session.advance()
                } label: {
                    Label("Langkah Berikutnya", systemImage: "forward.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .disabled(route.steps.isEmpty || session.hasArrived)

                Button {
                    session.back()
                } label: {
                    Label("Kembali", systemImage: "backward.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(session.currentIndex == 0 || session.hasArrived)
            }

            Button {
                session.end()
            } label: {
                Label("Akhiri Navigasi", systemImage: "stop.circle.fill")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .tint(.red)
        }
    }
}
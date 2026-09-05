import SwiftUI
import MapKit

// MARK: - Panel navigasi turn-by-turn untuk layar Live (di atas spidometer)
// Cari tujuan -> hitung rute -> tampil kartu manuver sambil spidometer tetap
// terlihat. Langkah berikutnya terkirim ke device via BLE otomatis saat
// posisi iPhone mendekati titik belokan.

struct NavigationPanelView: View {
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var route: RouteManager
    @EnvironmentObject var location: LocationTracker

    @State private var destQuery = ""
    @State private var navIndex = 0
    @State private var remainingM: Double?
    @State private var hasArrived = false
    @State private var isActive = false
    @State private var showError = false
    @State private var errorText = ""

    // Kirim ulang langkah tiap 45 detik: layar device tidak akan "tidur"
    // walaupun pengendara berhenti lama (lampu merah, macet).
    private let keepAlive = Timer.publish(every: 45, on: .main, in: .common).autoconnect()

    var body: some View {
        VStack(spacing: 14) {
            if route.steps.isEmpty {
                searchCard
            } else if isActive {
                instructionCard
                progressRow
                actionButtons
            } else {
                resumeCard
            }
        }
        .onAppear { if isActive { location.start() } }
        .onChange(of: isActive) { _, newValue in
            if newValue { location.start() } else { location.stop() }
        }
        .onDisappear { location.stop() }
        .onReceive(location.$lastLocation) { loc in
            if let loc { updateProgress(loc) }
        }
        .onReceive(keepAlive) { _ in
            guard isActive, !hasArrived, let step = currentStep else { return }
            ble.send(stepJSON(step))
        }
        .alert("Perhatian", isPresented: $showError) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(errorText)
        }
    }

    // MARK: - State

    private var currentStep: NavStep? {
        guard !route.steps.isEmpty, navIndex < route.steps.count else { return nil }
        return route.steps[navIndex]
    }

    private var iconSymbol: String {
        switch currentStep?.icon ?? 1 {
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
        if hasArrived { return "Tiba \u{2713}" }
        guard let rem = remainingM else { return "--" }
        if rem >= 1000 { return String(format: "%.1f km", rem / 1000) }
        return "\(Int(rem)) m"
    }

    private func stepJSON(_ step: NavStep) -> String {
        let escaped = step.instruction
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"")
        return "{\"icon\":\(step.icon),\"dist\":\(step.distanceM),\"text\":\"\(escaped)\"}"
    }

    // MARK: - Alur

    @MainActor
    private func startNavigation() {
        let query = destQuery.trimmingCharacters(in: .whitespaces)
        guard !query.isEmpty else { return }

        guard ble.status.isConnected else {
            errorText = "Hubungkan dulu ke MuchRacing-Nav melalui BLE (tab Navigasi atau Setelan)."
            showError = true
            return
        }

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
                    beginNavigation()
                }
            } catch {
                errorText = "Gagal menghitung rute: \(error.localizedDescription)"
                showError = true
            }
        }
    }

    private func beginNavigation() {
        navIndex = 0
        hasArrived = false
        isActive = true
        if let step = currentStep {
            remainingM = Double(step.distanceM)
            ble.send(stepJSON(step))
        }
    }

    // Maju ke langkah berikutnya saat posisi iPhone < 25 m dari titik manuver.
    private func updateProgress(_ loc: CLLocation) {
        guard isActive, !hasArrived else { return }
        guard let step = currentStep else { return }
        let target = CLLocation(latitude: step.coordinate.latitude,
                                longitude: step.coordinate.longitude)
        let dist = loc.distance(from: target)
        remainingM = max(0, dist)

        if dist < 25 {
            if navIndex < route.steps.count - 1 {
                navIndex += 1
                if let s = currentStep { ble.send(stepJSON(s)) }
            } else {
                hasArrived = true
                isActive = false
                location.stop()
                if let s = currentStep { ble.send(stepJSON(s)) }
            }
        }
    }

    private func endNavigation() {
        isActive = false
        location.stop()
        ble.sendClear()
    }

    // MARK: - UI

    private var searchCard: some View {
        VStack(alignment: .leading, spacing: 10) {
            Label("Tujuan Navigasi", systemImage: "point.topleft.down.curvedto.point.bottomright.up")
                .font(.headline)
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
            .disabled(destQuery.trimmingCharacters(in: .whitespaces).isEmpty || route.isComputing)

            Label("Rute dihitung via internet — keluar sementara dari AP MuchRacing-GPS.", systemImage: "info.circle")
                .font(.footnote)
                .foregroundColor(.yellow)
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 12).fill(Color(.secondarySystemBackground)))
    }

    private var resumeCard: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(route.routeSummary)
                .font(.headline)
            Text("\(route.steps.count) langkah • mulai: \(route.steps.first?.instruction ?? "")")
                .font(.caption)
                .foregroundColor(.secondary)
            HStack(spacing: 12) {
                Button {
                    beginNavigation()
                } label: {
                    Label("Mulai Navigasi", systemImage: "play.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(.green)
                .disabled(!ble.status.isConnected)

                Button {
                    route.reset()
                } label: {
                    Label("Hapus Rute", systemImage: "trash")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .tint(.red)
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 12).fill(Color(.secondarySystemBackground)))
    }

    private var instructionCard: some View {
        VStack(spacing: 10) {
            Image(systemName: iconSymbol)
                .font(.system(size: 52))
                .foregroundColor(hasArrived ? .green : .accentColor)
            Text(currentStep?.instruction ?? "Menghitung...")
                .font(.title3)
                .multilineTextAlignment(.center)
            Text(distanceText)
                .font(.title)
                .bold()
                .foregroundColor(hasArrived ? .green : .primary)
        }
        .frame(maxWidth: .infinity)
        .padding(18)
        .background(RoundedRectangle(cornerRadius: 14).fill(Color(.secondarySystemBackground)))
    }

    private var progressRow: some View {
        HStack {
            Text("Langkah \(navIndex + 1) dari \(route.steps.count)")
            Spacer()
            if ble.status.isConnected {
                Label("Terhubung", systemImage: "checkmark.circle.fill")
                    .foregroundColor(.green)
            } else {
                Label("BLE terputus", systemImage: "exclamationmark.triangle.fill")
                    .foregroundColor(.orange)
            }
        }
        .font(.caption)
        .foregroundColor(.secondary)
    }

    private var actionButtons: some View {
        VStack(spacing: 10) {
            HStack(spacing: 12) {
                Button {
                    if navIndex < route.steps.count - 1 {
                        navIndex += 1
                        hasArrived = false
                        isActive = true
                        if let s = currentStep { ble.send(stepJSON(s)) }
                    } else {
                        hasArrived = true
                        isActive = false
                        location.stop()
                        if let s = currentStep { ble.send(stepJSON(s)) }
                    }
                } label: {
                    Label("Langkah Berikutnya", systemImage: "forward.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .disabled(route.steps.isEmpty)

                Button {
                    if navIndex > 0 {
                        navIndex -= 1
                        hasArrived = false
                        isActive = true
                        if let s = currentStep { ble.send(stepJSON(s)) }
                    }
                } label: {
                    Label("Kembali", systemImage: "backward.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(navIndex == 0)
            }

            Button {
                endNavigation()
            } label: {
                Label("Akhiri Navigasi", systemImage: "stop.circle.fill")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .tint(.red)
        }
    }
}
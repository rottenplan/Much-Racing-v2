import Foundation
import CoreLocation

// MARK: - State navigasi bersama untuk panel Live & layar Navigasi penuh.
//
// Sebelumnya logika turn-by-turn (auto-advance 25 m, keep-alive 45 detik,
// langkah manual, kirim JSON ke BLE) ditulis dua kali: di NavigationPanelView
// dan NavigationSessionView. Semua state & alur dipindah ke sini agar
// konsisten dan tidak saling bertabrakan.

@MainActor
final class NavigationSession: ObservableObject {
    @Published var isActive = false
    @Published var currentIndex = 0
    @Published var remainingM: Double?
    @Published var hasArrived = false

    // State untuk live countdown ala Google Maps: jarak tersisa ke belokan
    // aktif dikirim ulang ke device tiap berubah (dibulatkan 5 m, minimal
    // jeda 1 detik) supaya tampilan device ikut menghitung mundur.
    private var lastPushedDist: Double = -1
    private var lastPushTime = Date.distantPast
    private let minPushGap: TimeInterval = 1.0

    private let ble: BLEManager
    private let route: RouteManager
    private let location: LocationTracker

    init(ble: BLEManager, route: RouteManager, location: LocationTracker) {
        self.ble = ble
        self.route = route
        self.location = location
    }

    var steps: [NavStep] { route.steps }

    var currentStep: NavStep? {
        guard !route.steps.isEmpty, currentIndex < route.steps.count else { return nil }
        return route.steps[currentIndex]
    }

    var nextStep: NavStep? {
        guard currentIndex + 1 < route.steps.count else { return nil }
        return route.steps[currentIndex + 1]
    }

    var stepText: String {
        guard !route.steps.isEmpty else { return "-" }
        return hasArrived ? "selesai" : "langkah \(currentIndex + 1)/\(route.steps.count)"
    }

    // Sisa jarak rute: jarak ke manuver saat ini + seluruh langkah berikutnya.
    var remainingRouteMeters: Double {
        guard !route.steps.isEmpty, currentIndex < route.steps.count else { return 0 }
        var total = remainingM ?? Double(route.steps[currentIndex].distanceM)
        for i in (currentIndex + 1)..<route.steps.count {
            total += Double(route.steps[i].distanceM)
        }
        return max(0, total)
    }

    func reset() {
        isActive = false
        currentIndex = 0
        remainingM = nil
        hasArrived = false
        lastPushedDist = -1
        lastPushTime = .distantPast
    }

    // Mulai / lanjutkan navigasi dari langkah pertama.
    func start() {
        guard !route.steps.isEmpty else { return }
        currentIndex = 0
        hasArrived = false
        remainingM = nil
        lastPushedDist = -1
        lastPushTime = .distantPast
        isActive = true
        location.start()
        if let step = currentStep {
            remainingM = Double(step.distanceM)
            ble.send(stepJSON(step))
        }
    }

    // Maju ke langkah berikutnya saat posisi iPhone sudah sangat dekat (< 15 m)
    // dari titik manuver, lalu dorong sisa jarak live ke layar device.
    func updateProgress(with loc: CLLocation) {
        guard isActive, !hasArrived else { return }
        guard let step = currentStep else { return }
        let target = CLLocation(latitude: step.coordinate.latitude,
                                longitude: step.coordinate.longitude)
        let dist = loc.distance(from: target)
        remainingM = max(0, dist)

        if dist < 15 {
            advance()
        } else {
            pushDistance()
        }
    }

    // Dorong jarak ke belokan yang sekarang (hitung mundur) ke layar device.
    // Dibulatkan ke kelipatan 5 m dan dibatasi 1 push/detik agar hemat BLE &
    // layar device; nilai tetap ter-update mendekati belokan.
    private func pushDistance() {
        guard isActive, !hasArrived, ble.status.isConnected else { return }
        guard let step = currentStep, let rem = remainingM else { return }
        let rounded = (rem / 5).rounded() * 5
        let now = Date()
        guard rounded != lastPushedDist, now.timeIntervalSince(lastPushTime) >= minPushGap else { return }
        lastPushedDist = rounded
        lastPushTime = now
        ble.send(stepJSON(step, distOverride: rounded))
    }

    // Langkah berikutnya (tombol manual atau auto-advance).
    func advance() {
        guard isActive else { return }
        lastPushedDist = -1
        lastPushTime = .distantPast
        if currentIndex < route.steps.count - 1 {
            currentIndex += 1
            remainingM = Double(route.steps[currentIndex].distanceM)
            if let s = currentStep { ble.send(stepJSON(s)) }
        } else if !hasArrived {
            hasArrived = true
            isActive = false
            location.stop()
            if let s = currentStep { ble.send(stepJSON(s)) }
        }
    }

    // Kembali satu langkah (mode manual, bila GPS kurang presisi).
    func back() {
        guard isActive, currentIndex > 0 else { return }
        currentIndex -= 1
        hasArrived = false
        lastPushedDist = -1
        lastPushTime = .distantPast
        remainingM = Double(route.steps[currentIndex].distanceM)
        if let s = currentStep { ble.send(stepJSON(s)) }
    }

    // Kirim ulang langkah tiap 45 detik: layar device tidak akan "tidur"
    // walaupun pengendara berhenti lama (lampu merah, macet). Memakai jarak
    // live kalau ada, agar tampilan tetap konsisten dengan hitung mundur.
    func keepAlive() {
        guard isActive, !hasArrived, let step = currentStep else { return }
        sendCurrent(step)
    }

    // BLE baru (kembali) tersambung saat navigasi berjalan: kirim ulang langkah
    // aktif supaya layar device langsung menampilkan manuver yang benar.
    func onBleConnected() {
        guard isActive, !hasArrived, let step = currentStep else { return }
        sendCurrent(step)
    }

    private func sendCurrent(_ step: NavStep) {
        if let rem = remainingM {
            ble.send(stepJSON(step, distOverride: (rem / 5).rounded() * 5))
        } else {
            ble.send(stepJSON(step))
        }
    }

    func end() {
        isActive = false
        location.stop()
        ble.sendClear()
    }

    private func stepJSON(_ step: NavStep, distOverride: Double? = nil) -> String {
        let escaped = step.instruction
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"")

        // Jarak: biasanya jarak tetap dari langkah MapKit; saat live countdown
        // berjalan (sebelum langkah berpindah) pakai nilai override agar layar
        // device ikut menghitung mundur ala Google Maps.
        let dist = distOverride.map { Int(round($0)) } ?? step.distanceM

        // Info tambahan untuk layar device: rute alternatif terpilih + total
        // sisa jarak ke tujuan (ditampilkan sebagai badge & TOTAL di device).
        var extra = ""
        if route.routes.count > 1 {
            extra += String(format: ",\"route\":%d,\"routes\":%d",
                            route.selectedRouteIndex + 1, route.routes.count)
        }
        let total = Int(remainingRouteMeters)
        if total >= 0 {
            extra += ",\"total\":\(total)"
        }

        return "{\"icon\":\(step.icon),\"dist\":\(dist),\"text\":\"\(escaped)\"\(extra)}"
    }
}
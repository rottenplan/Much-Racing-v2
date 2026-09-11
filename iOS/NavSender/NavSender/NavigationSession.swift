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
    }

    // Mulai / lanjutkan navigasi dari langkah pertama.
    func start() {
        guard !route.steps.isEmpty else { return }
        currentIndex = 0
        hasArrived = false
        remainingM = nil
        isActive = true
        location.start()
        if let step = currentStep {
            remainingM = Double(step.distanceM)
            ble.send(stepJSON(step))
        }
    }

    // Maju ke langkah berikutnya saat posisi iPhone < 25 m dari titik manuver.
    func updateProgress(with loc: CLLocation) {
        guard isActive, !hasArrived else { return }
        guard let step = currentStep else { return }
        let target = CLLocation(latitude: step.coordinate.latitude,
                                longitude: step.coordinate.longitude)
        let dist = loc.distance(from: target)
        remainingM = max(0, dist)

        if dist < 25 { advance() }
    }

    // Langkah berikutnya (tombol manual atau auto-advance).
    func advance() {
        guard isActive else { return }
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
        remainingM = Double(route.steps[currentIndex].distanceM)
        if let s = currentStep { ble.send(stepJSON(s)) }
    }

    // Kirim ulang langkah tiap 45 detik: layar device tidak akan "tidur"
    // walaupun pengendara berhenti lama (lampu merah, macet).
    func keepAlive() {
        guard isActive, !hasArrived, let step = currentStep else { return }
        ble.send(stepJSON(step))
    }

    func end() {
        isActive = false
        location.stop()
        ble.sendClear()
    }

    private func stepJSON(_ step: NavStep) -> String {
        let escaped = step.instruction
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"")

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

        return "{\"icon\":\(step.icon),\"dist\":\(step.distanceM),\"text\":\"\(escaped)\"\(extra)}"
    }
}
import SwiftUI
import MapKit
import CoreLocation

struct MapPin: Identifiable {
    let id: Int
    let coordinate: CLLocationCoordinate2D
    let tint: Color
}

// Layar navigasi live: menampilkan langkah saat ini, peta rute, jarak ke
// belokan, dan otomatis mengirim langkah berikutnya ke device saat pengendara
// mendekati titik manuver. Ada juga tombol manual untuk maju/mundur langkah.
struct NavigationSessionView: View {
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var route: RouteManager
    @EnvironmentObject var location: LocationTracker
    @Environment(\.dismiss) private var dismiss

    @State private var currentIndex = 0
    @State private var remainingM: Double?
    @State private var hasArrived = false
    @State private var region = MKCoordinateRegion(
        center: CLLocationCoordinate2D(latitude: -6.2, longitude: 106.8),
        span: MKCoordinateSpan(latitudeDelta: 0.05, longitudeDelta: 0.05))
    @State private var pins: [MapPin] = []

    // Kirim ulang langkah tiap 45 detik: layar device tidak akan "tidur"
    // walaupun pengendara berhenti lama (lampu merah, macet).
    private let keepAlive = Timer.publish(every: 45, on: .main, in: .common).autoconnect()

    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                instructionCard
                mapCard
                progressRow
                actionButtons
            }
            .padding()
        }
        .navigationTitle("Navigasi")
        .navigationBarTitleDisplayMode(.inline)
        .onAppear { startNavigation() }
        .onReceive(location.$lastLocation) { loc in
            if let loc { updateProgress(loc) }
        }
        .onReceive(keepAlive) { _ in
            guard !hasArrived, let step = currentStep else { return }
            ble.send(stepJSON(step))
        }
        .onDisappear { location.stop() }
    }

    // MARK: - State

    private var currentStep: NavStep? {
        guard !route.steps.isEmpty, currentIndex < route.steps.count else { return nil }
        return route.steps[currentIndex]
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
        if hasArrived { return "Tiba ✓" }
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

    private func startNavigation() {
        guard !route.steps.isEmpty else { return }
        location.start()
        currentIndex = 0
        hasArrived = false
        if let step = currentStep {
            remainingM = Double(step.distanceM)
            ble.send(stepJSON(step))
        }
        setRegionAndPins()
    }

    // Maju ke langkah berikutnya saat posisi iPhone < 25 m dari titik manuver.
    private func updateProgress(_ loc: CLLocation) {
        guard let step = currentStep, !hasArrived else { return }
        let target = CLLocation(latitude: step.coordinate.latitude,
                                longitude: step.coordinate.longitude)
        let dist = loc.distance(from: target)
        remainingM = max(0, dist)

        if dist < 25 {
            if currentIndex < route.steps.count - 1 {
                currentIndex += 1
                if let s = currentStep { ble.send(stepJSON(s)) }
            } else {
                hasArrived = true
                if let s = currentStep { ble.send(stepJSON(s)) }
            }
        }
    }

    private func setRegionAndPins() {
        guard let poly = route.route?.polyline, poly.pointCount > 0 else { return }
        let rect = poly.boundingMapRect
        var r = MKCoordinateRegion(rect)
        // Pastikan span minimum agar Map tidak error untuk rute pendek.
        r.span.latitudeDelta = max(r.span.latitudeDelta, 0.005)
        r.span.longitudeDelta = max(r.span.longitudeDelta, 0.005)
        region = r

        let pts = poly.points()
        pins = [
            MapPin(id: 0, coordinate: pts[0].coordinate, tint: .green),
            MapPin(id: 1, coordinate: pts[poly.pointCount - 1].coordinate, tint: .red)
        ]
    }

    // MARK: - UI

    private var instructionCard: some View {
        VStack(spacing: 10) {
            Image(systemName: iconSymbol)
                .font(.system(size: 56))
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
        .padding(20)
        .background(RoundedRectangle(cornerRadius: 14).fill(Color(.secondarySystemBackground)))
    }

    private var mapCard: some View {
        Map(coordinateRegion: $region,
            interactionModes: .all,
            showsUserLocation: true,
            userTrackingMode: nil,
            annotationItems: pins) { pin in
            MapMarker(coordinate: pin.coordinate, tint: pin.tint)
        }
        .frame(height: 220)
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }

    private var progressRow: some View {
        HStack {
            Text("Langkah \(currentIndex + 1) dari \(route.steps.count)")
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
                    if currentIndex < route.steps.count - 1 {
                        currentIndex += 1
                        hasArrived = false
                        if let s = currentStep { ble.send(stepJSON(s)) }
                    } else {
                        hasArrived = true
                        if let s = currentStep { ble.send(stepJSON(s)) }
                    }
                } label: {
                    Label("Langkah Berikutnya", systemImage: "forward.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .disabled(route.steps.isEmpty)

                Button {
                    if currentIndex > 0 {
                        currentIndex -= 1
                        hasArrived = false
                        if let s = currentStep { ble.send(stepJSON(s)) }
                    }
                } label: {
                    Label("Kembali", systemImage: "backward.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .disabled(currentIndex == 0)
            }

            Button {
                ble.sendClear()
                location.stop()
                dismiss()
            } label: {
                Label("Akhiri Navigasi", systemImage: "stop.circle.fill")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .tint(.red)
        }
    }
}
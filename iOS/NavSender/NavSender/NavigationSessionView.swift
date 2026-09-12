import SwiftUI
import MapKit
import CoreLocation
import UIKit

// MARK: - Layar navigasi ala CarPlay
//
// Peta gelap full-screen (night mode) dengan:
//  • Banner manuver besar di atas: panah + jarak ke belokan + instruksi,
//    plus strip kecil langkah berikutnya (banner sekunder ala CarPlay).
//  • Kamera peta mengikuti posisi & arah iPhone (heading mode) — seperti
//    Apple Maps di CarPlay. Kalau pengendara geser peta, muncul tombol
//    "ikuti lagi".
//  • Garis rute biru dengan casing putih + pin start (hijau) & tujuan (merah).
//  • Bar ETA di bawah: estimasi tiba, sisa rute, langkah ke-x, status BLE,
//    tombol lewati-langkah manual, dan tombol akhiri navigasi.
//  • Semua state & alur (auto-advance, keep-alive, langkah manual) ditangani
//    NavigationSession — sama dengan panel Live.
struct NavigationSessionView: View {
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var route: RouteManager
    @EnvironmentObject var location: LocationTracker
    @EnvironmentObject var session: NavigationSession
    @Environment(\.dismiss) private var dismiss

    // Peta: heading mode mengikuti posisi & arah kendaraan.
    @State private var camera: MapCameraPosition = .userLocation(followsHeading: true, fallback: .automatic)
    @State private var isUserPanning = false
    @State private var followHeading = true

    // Daftar belokan lengkap (drawer ala Google Maps) yang dibuka dari bar ETA.
    @State private var showStepList = false

    // Kirim ulang langkah tiap 45 detik: layar device tidak akan "tidur"
    // walaupun pengendara berhenti lama (lampu merah, macet).
    private let keepAlive = Timer.publish(every: 45, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            mapLayer.ignoresSafeArea()

            VStack(spacing: 0) {
                // 1) Banner manuver di atas (berisi tombol Selesai di kanan).
                maneuverBanner

                // Area tengah peta bebas, kontrol dikelompokkan rapi di bawah.
                Spacer(minLength: 8)

                // 2) Segmen kontrol: recenter (kiri) + kompas (kanan).
                HStack {
                    if isUserPanning { recenterButton }
                    Spacer()
                    compassButton
                }
                .padding(.horizontal, 20)
                .padding(.bottom, 8)

                // 3) Bar ETA / info sisa rute di paling bawah.
                etaBar
            }
        }
        .navigationBarBackButtonHidden(true)
        .toolbar(.hidden, for: .navigationBar)
        .onAppear {
            // Jangan biarkan layar iPhone tidur/mengunci selama navigasi —
            // kalau tidak, UI menghilang saat rute berjalan.
            UIApplication.shared.isIdleTimerDisabled = true
            startNavigation()
        }
        .onReceive(location.$lastLocation) { loc in
            if let loc { session.updateProgress(with: loc) }
        }
        .onReceive(keepAlive) { _ in
            session.keepAlive()
        }
        .onDisappear {
            // Kembalikan perilaku normal layar setelah layar navigasi ditutup.
            UIApplication.shared.isIdleTimerDisabled = false
            location.stop()
        }
        .sheet(isPresented: $showStepList) {
            StepListSheet(currentIndex: $session.currentIndex)
                .presentationDetents([.medium, .large])
                .presentationDragIndicator(.visible)
        }
    }

    // MARK: - Peta

    private var mapLayer: some View {
        Map(position: $camera) {
            // Casing putih + garis biru ala navigasi CarPlay.
            if let poly = route.polylineForDrawing {
                MapPolyline(poly)
                    .stroke(.white, style: StrokeStyle(lineWidth: 9, lineCap: .round, lineJoin: .round))
                MapPolyline(poly)
                    .stroke(.blue, style: StrokeStyle(lineWidth: 6, lineCap: .round, lineJoin: .round))
            }
            if let start = route.steps.first?.coordinate {
                Marker("Start", systemImage: "location.fill", coordinate: start)
                    .tint(.green)
            }
            if let dest = route.steps.last?.coordinate {
                Marker("Tujuan", systemImage: "flag.checkered", coordinate: dest)
                    .tint(.red)
            }
            UserAnnotation()
        }
        // Mode malam ala CarPlay, tapi jangan terlalu gelap: nama jalan & label
        // tetap terbaca (POI diredupkan via emphasis .muted).
        .mapStyle(.standard(elevation: .flat, emphasis: .muted, showsTraffic: true))
        .overlay(Color.black.opacity(0.22).allowsHitTesting(false))
        .mapControls {
            // MapCompass bawaan MapKit tampil di kanan-atas peta dan menabrak
            // status bar (icon baterai). Diganti kompas custom (compassButton).
            MapScaleView()
        }
        // Kalau pengendara geser/zoom manual, posisi kamera berubah menjadi
        // region/rect/camera → keluar dari mode follow, tampilkan tombol "ikuti lagi".
        .onChange(of: camera) { _, newValue in
            isUserPanning = newValue.region != nil || newValue.rect != nil || newValue.camera != nil
        }
    }

    private var recenterButton: some View {
        Button {
            followHeading = true
            camera = .userLocation(followsHeading: true, fallback: .automatic)
            isUserPanning = false
        } label: {
            ZStack {
                Circle()
                    .fill(RacingTheme.card)
                    .overlay(Circle().stroke(Color.neonCyan.opacity(0.6), lineWidth: 1))
                    .shadow(color: .black.opacity(0.3), radius: 4, y: 2)
                Image(systemName: "location.fill")
                    .font(.callout.weight(.bold))
                    .foregroundColor(.neonCyan)
            }
            .frame(width: 38, height: 38)
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Ikuti posisi saya")
    }

    // Kompas custom (kanan-bawah, di atas bar ETA): tidak menabrak status bar.
    // Jarum mengarah ke utara sesuai heading kendaraan; ketuk untuk toggle
    // mode mengikuti arah (heading) vs menghadap utara.
    private var compassButton: some View {
        Button {
            followHeading.toggle()
            camera = followHeading
                ? .userLocation(followsHeading: true, fallback: .automatic)
                : .userLocation(fallback: .automatic)
            isUserPanning = false
        } label: {
            ZStack {
                Circle()
                    .fill(RacingTheme.card)
                    .overlay(Circle().stroke(Color.neonOrange.opacity(0.6), lineWidth: 1))
                    .shadow(color: .black.opacity(0.3), radius: 4, y: 2)
                Image(systemName: "location.north.fill")
                    .font(.callout.weight(.bold))
                    .foregroundColor(.neonOrange)
                    // Rotasi negatif agar jarum menunjuk utara sesuai heading.
                    .rotationEffect(.degrees(-(location.lastLocation?.course ?? 0)))
                Text("N")
                    .font(.system(size: 8, weight: .bold))
                    .foregroundColor(.white.opacity(0.8))
                    .offset(y: 13)
            }
            .frame(width: 38, height: 38)
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Kompas — atur arah peta")
    }

    // MARK: - Banner manuver (ala CarPlay)

    private var maneuverBanner: some View {
        VStack(spacing: 2) {
            HStack(spacing: 14) {
                Image(systemName: iconSymbol)
                    .font(.system(size: 42, weight: .bold))
                    .foregroundColor(.white)
                    .frame(width: 60)
                    .neonGlow(session.hasArrived ? .neonGreen : .neonOrange, radius: 8)
                VStack(alignment: .leading, spacing: 2) {
                    Text(distanceText)
                        .font(.racingDigits(30))
                        .foregroundColor(.white)
                        .neonGlow(session.hasArrived ? .neonGreen : .neonOrange, radius: 5)
                    Text(currentInstruction)
                        .font(.subheadline.weight(.semibold))
                        .foregroundColor(.white.opacity(0.92))
                        .lineLimit(2)
                }
                Spacer(minLength: 8)

                // Tombol stop besar di pojok kanan-atas: selalu terlihat dan
                // mudah ditekan saat berkendara (tidak berada di bar bawah).
                Button {
                    session.end()
                    dismiss()
                } label: {
                    Label("Selesai", systemImage: "stop.fill")
                        .font(.subheadline.weight(.bold))
                        .foregroundColor(.white)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 9)
                        .background(Capsule().fill(Color.neonRed))
                        .overlay(Capsule().stroke(Color.neonRed.opacity(0.7), lineWidth: 1))
                        .neonGlow(.neonRed, radius: 6)
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Akhiri navigasi")
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
            .background(
                RacingTheme.racingShape
                    .fill(session.hasArrived ? RacingTheme.neonGreen.opacity(0.28) : RacingTheme.neonOrange.opacity(0.22))
                    .overlay(RacingTheme.racingShape.stroke(session.hasArrived ? Color.neonGreen.opacity(0.9) : Color.neonOrange.opacity(0.9), lineWidth: 1.5))
                    .shadow(color: (session.hasArrived ? Color.neonGreen : Color.neonOrange).opacity(0.45), radius: 14)
            )
            .padding(.horizontal, 12)
            .padding(.top, 8)

            // Banner sekunder: langkah berikutnya.
            if let next = session.nextStep {
                HStack(spacing: 8) {
                    Image(systemName: Self.symbol(for: next.icon))
                        .font(.system(size: 15, weight: .semibold))
                        .foregroundColor(.neonOrange)
                    Text(displayInstruction(next))
                        .lineLimit(1)
                    Spacer()
                }
                .font(.caption.weight(.semibold))
                .foregroundColor(.white)
                .padding(.horizontal, 16)
                .padding(.vertical, 7)
                .background(RacingTheme.tagShape.fill(Color.black.opacity(0.55))
                    .overlay(RacingTheme.tagShape.stroke(Color.neonOrange.opacity(0.5), lineWidth: 1)))
                .padding(.horizontal, 24)
            }
        }
    }

    // MARK: - Bar bawah (ETA / sisa rute)

    private var etaBar: some View {
        // Ketuk kartu ETA untuk membuka daftar belokan lengkap (ala Google Maps).
        HStack(spacing: 12) {
            Button {
                showStepList = true
            } label: {
                HStack(spacing: 8) {
                    VStack(alignment: .leading, spacing: 1) {
                        Text(arrivalText)
                            .font(.racingDigits(20))
                            .foregroundColor(session.hasArrived ? .neonGreen : .white)
                            .neonGlow(session.hasArrived ? .neonGreen : .neonOrange, radius: 4)
                        Text("\(remainingRouteText) • \(session.stepText)")
                            .font(.caption2)
                            .foregroundColor(.secondary)
                    }
                    Image(systemName: "chevron.up")
                        .font(.footnote.weight(.semibold))
                        .foregroundColor(.neonOrange)
                }
            }
            .buttonStyle(.plain)

            Spacer()

            if ble.status.isConnected {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundColor(.neonGreen)
                    .neonGlow(.neonGreen, radius: 4)
            } else {
                Image(systemName: "exclamationmark.triangle.fill")
                    .foregroundColor(.neonOrange)
            }

            // Lewati langkah secara manual (kalau GPS kurang presisi).
            Button {
                session.advance()
            } label: {
                Image(systemName: "forward.fill")
                    .font(.body.weight(.semibold))
                    .foregroundColor(.neonOrange)
                    .padding(10)
                    .background(Circle().fill(RacingTheme.card))
                    .overlay(Circle().stroke(Color.neonOrange.opacity(0.5), lineWidth: 1))
            }
            .buttonStyle(.plain)
            .disabled(route.steps.isEmpty || session.hasArrived)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(
            RacingTheme.racingShape
                .fill(RacingTheme.card.opacity(0.96))
                .overlay(RacingTheme.racingShape.stroke(Color.neonOrange.opacity(0.55), lineWidth: 1))
                .shadow(color: .black.opacity(0.5), radius: 8, y: 3)
        )
        .padding(.horizontal, 12)
        .padding(.bottom, 8)
    }

    // MARK: - Turunan state

    private var currentInstruction: String {
        if session.hasArrived { return "Tiba di tujuan" }
        guard let step = session.currentStep else { return "Menghitung..." }
        return displayInstruction(step)
    }

    private var iconSymbol: String { Self.symbol(for: session.currentStep?.icon ?? 1) }

    static func symbol(for icon: Int) -> String {
        switch icon {
        case 0: return "flag.checkered"               // tiba
        case 2: return "arrow.up.left"                // kiri sedikit
        case 3: return "turn.left"                    // kiri
        case 4: return "turn.sharp.left"              // kiri tajam
        case 5: return "arrow.up.right"               // kanan sedikit
        case 6: return "turn.right"                   // kanan
        case 7: return "turn.sharp.right"             // kanan tajam
        case 8: return "arrow.uturn.left"             // putar balik
        case 9: return "arrow.triangle.2.circlepath"  // bundaran
        default: return "arrow.up"                    // lurus
        }
    }

    private var distanceText: String {
        if session.hasArrived { return "Tiba ✓" }
        guard let rem = session.remainingM else { return "--" }
        return Self.formatDistance(rem)
    }

    static func formatDistance(_ meters: Double) -> String {
        if meters >= 1000 { return String(format: "%.1f km", meters / 1000) }
        return "\(Int(meters)) m"
    }

    // Instruksi MapKit sering berbahasa Inggris; tampilkan padanan lokal.
    private func displayInstruction(_ step: NavStep) -> String {
        if step.icon == 0 { return "Tiba di tujuan" }
        switch step.instruction.trimmingCharacters(in: .whitespaces).lowercased() {
        case "continue", "continue on": return "Lanjutkan"
        default: return step.instruction
        }
    }

    private var remainingRouteText: String {
        if session.hasArrived { return "Tiba di tujuan" }
        let rem = session.remainingRouteMeters
        if rem >= 1000 { return String(format: "%.1f km", rem / 1000) }
        return "\(Int(rem)) m"
    }

    // Estimasi jam tiba: proporsi sisa rute terhadap total rute × waktu tempuh.
    private var arrivalText: String {
        if session.hasArrived { return "Tiba" }
        guard route.distanceMeters > 0 else { return "Menghitung..." }
        let fraction = min(1, session.remainingRouteMeters / route.distanceMeters)
        let eta = Date().addingTimeInterval(route.travelSeconds * fraction)
        return "Tiba \(eta.formatted(date: .omitted, time: .shortened))"
    }

    // MARK: - Alur

    private func startNavigation() {
        guard !route.steps.isEmpty else { return }
        session.start()
    }
}

// MARK: - Drawer daftar belokan lengkap (ala Google Maps)

private struct StepListSheet: View {
    @EnvironmentObject var route: RouteManager
    @Binding var currentIndex: Int
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()

            if route.steps.isEmpty {
                Spacer()
                Text("Belum ada rute.")
                    .foregroundColor(.secondary)
                Spacer()
            } else {
                ScrollViewReader { proxy in
                    List {
                        startRow
                        ForEach(route.steps) { step in
                            stepRow(step)
                                .id("step-\(step.id)")
                        }
                    }
                    .listStyle(.plain)
                    .scrollContentBackground(.hidden)
                    .onAppear { scrollToCurrent(proxy) }
                    .onChange(of: currentIndex) { _, _ in scrollToCurrent(proxy) }
                }
            }
        }
        .background(RacingBackground())
    }

    private var header: some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 8) {
                    RacingBadge(text: "BELOKAN", color: .neonOrange, icon: "arrow.triangle.turn.up.right.diamond.fill")
                }
                Text("\(max(0, route.steps.count - 1)) langkah • \(route.routeSummary)")
                    .font(.caption.weight(.semibold))
                    .foregroundColor(.secondary)
            }
            Spacer()
            Button {
                dismiss()
            } label: {
                Image(systemName: "xmark.circle.fill")
                    .font(.title3)
                    .foregroundColor(.neonOrange)
            }
            .buttonStyle(.plain)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
    }

    private var startRow: some View {
        HStack(spacing: 14) {
            Image(systemName: "location.fill")
                .font(.system(size: 22, weight: .semibold))
                .foregroundColor(.neonGreen)
                .frame(width: 34)
                .neonGlow(.neonGreen, radius: 6)
            VStack(alignment: .leading, spacing: 2) {
                Text("Posisi awal")
                    .font(.subheadline.weight(.semibold))
                Text("Berangkat")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
            Spacer()
        }
        .padding(.vertical, 4)
        .listRowBackground(RacingTheme.card)
    }

    private func stepRow(_ step: NavStep) -> some View {
        let isCurrent = isCurrentStep(step)
        let isArrival = step.icon == 0

        return HStack(spacing: 14) {
            Image(systemName: NavigationSessionView.symbol(for: step.icon))
                .font(.system(size: 22, weight: .semibold))
                .foregroundColor(isCurrent ? .white : (isArrival ? .neonRed : .neonOrange))
                .frame(width: 34)
                .neonGlow(isCurrent ? .neonOrange : (isArrival ? .neonRed : .neonOrange), radius: isCurrent ? 6 : 0)
            VStack(alignment: .leading, spacing: 2) {
                Text(displayText(step))
                    .font(.subheadline.weight(isCurrent ? .bold : .regular))
                    .foregroundColor(isCurrent ? .white : .primary)
                    .lineLimit(2)
                if isCurrent {
                    Text("LANGKAH SAAT INI")
                        .font(.caption2.weight(.bold))
                        .tracking(1)
                        .foregroundColor(.white.opacity(0.9))
                }
            }
            Spacer()
            if !isArrival {
                Text(NavigationSessionView.formatDistance(Double(step.distanceM)))
                    .font(.subheadline.monospacedDigit())
                    .foregroundColor(isCurrent ? .white.opacity(0.9) : .secondary)
            }
        }
        .padding(.vertical, 4)
        .listRowBackground(isCurrent ? Color.neonOrange : RacingTheme.card)
    }

    private func isCurrentStep(_ step: NavStep) -> Bool {
        guard currentIndex >= 0, currentIndex < route.steps.count else { return false }
        return route.steps[currentIndex].id == step.id
    }

    private func displayText(_ step: NavStep) -> String {
        if step.icon == 0 { return "Tiba di tujuan" }
        switch step.instruction.trimmingCharacters(in: .whitespaces).lowercased() {
        case "continue", "continue on": return "Lanjutkan"
        default: return step.instruction
        }
    }

    private func scrollToCurrent(_ proxy: ScrollViewProxy) {
        guard currentIndex >= 0, currentIndex < route.steps.count else { return }
        let id = "step-\(route.steps[currentIndex].id)"
        withAnimation { proxy.scrollTo(id, anchor: .center) }
    }
}
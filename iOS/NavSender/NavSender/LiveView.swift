import SwiftUI
import MapKit

// MARK: - Live: telemetri real-time dari device AP + mode drag

struct LiveView: View {
    @EnvironmentObject var live: LiveStore
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var route: RouteManager
    @EnvironmentObject var location: LocationTracker

    enum Mode: String, CaseIterable, Identifiable {
        case telemetry = "Telemetri"
        case drag = "Drag"
        case navigation = "Navigasi"
        var id: String { rawValue }
    }

    @State private var mode: Mode = .telemetry
    @State private var camera: MapCameraPosition = .region(MKCoordinateRegion(
        center: CLLocationCoordinate2D(latitude: -6.2, longitude: 106.8),
        span: MKCoordinateSpan(latitudeDelta: 0.008, longitudeDelta: 0.008)))

    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                liveBadge
                if !live.connected {
                    connectionWarning
                }
                Picker("Mode", selection: $mode) {
                    ForEach(Mode.allCases) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented)

                switch mode {
                case .telemetry: telemetryContent
                case .drag: dragContent
                case .navigation: navigationContent
                }
            }
            .padding()
            .padding(.bottom, 16)
        }
        .background(RacingBackground())
        .navigationTitle("Live")
        .onReceive(live.$telemetry) { t in
            guard let t else { return }
            let c = t.coordinate
            if let current = camera.region {
                let moved = hypot(current.center.latitude - c.latitude, current.center.longitude - c.longitude) > 0.0004
                if moved {
                    camera = .region(MKCoordinateRegion(center: c, span: current.span))
                }
            }
        }
    }

    // MARK: - Badge

    private var liveBadge: some View {
        HStack(spacing: 10) {
            Circle()
                .fill(live.connected ? Color.neonGreen : Color.gray)
                .frame(width: 10, height: 10)
                .neonGlow(live.connected ? .neonGreen : .clear, radius: 6)
            Text(live.connected ? "DEVICE LIVE" : "TIDAK TERHUBUNG")
                .font(.caption.weight(.bold))
                .tracking(1.5)
                .foregroundColor(live.connected ? .neonGreen : .secondary)
            Spacer()
            if let t = live.lastUpdated {
                Text(t, style: .time)
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
        .padding(.horizontal, 4)
    }

    private var connectionWarning: some View {
        HStack(spacing: 10) {
            Image(systemName: "wifi.slash")
                .font(.title3)
                .foregroundColor(.neonYellow)
            Text("Device tidak terhubung — hanya data terakhir yang tampil.")
                .font(.footnote)
                .foregroundColor(.white)
                .lineLimit(2)
            Spacer(minLength: 4)
            Button("Hubungkan") {
                ble.scan()
                Task { await live.tick(force: true) }
            }
            .font(.caption.weight(.bold))
            .buttonStyle(.borderedProminent)
            .controlSize(.small)
            .tint(.neonYellow)
        }
        .padding(10)
        .background(
            RoundedRectangle(cornerRadius: 12)
                .fill(Color.neonYellow.opacity(0.12))
                .overlay(RoundedRectangle(cornerRadius: 12).stroke(Color.neonYellow.opacity(0.6), lineWidth: 1))
        )
    }

    // MARK: - Navigasi

    private var navigationContent: some View {
        VStack(spacing: 14) {
            NavigationPanelView()
            speedometer
            rpmBar
        }
    }

    // MARK: - Telemetri

    private var telemetryContent: some View {
        VStack(spacing: 14) {
            speedometer
            rpmBar
            statGrid
            trailMap
            dragReadout
        }
    }

    private var speedometer: some View {
        VStack(spacing: 4) {
            ZStack {
                Circle()
                    .fill(RacingTheme.card.opacity(0.5))
                Circle()
                    .stroke(RacingTheme.cardBorderDark, lineWidth: 12)
                Circle()
                    .trim(from: 0, to: speedFraction)
                    .stroke(AngularGradient(gradient: Gradient(colors: [.neonGreen, .neonYellow, .neonOrange, .neonRed]),
                                            center: .center),
                            style: StrokeStyle(lineWidth: 12, lineCap: .round))
                    .rotationEffect(.degrees(-90))
                    .shadow(color: speedFraction > 0.85 ? Color.neonRed.opacity(0.8) : Color.neonOrange.opacity(0.6),
                            radius: 10)
                // Penanda 0% (grid ala panel balap)
                Circle()
                    .stroke(RacingTheme.cardBorderDark, style: StrokeStyle(lineWidth: 1, dash: [2, 6]))
                    .padding(14)
                VStack(spacing: 2) {
                    Text("\(Int(live.telemetry?.speed ?? 0))")
                        .font(.racingDigits(56))
                        .foregroundColor(.white)
                        .neonGlow(speedFraction > 0.85 ? .neonRed : .neonOrange, radius: 10)
                    Text("km/h")
                        .font(.caption.weight(.bold))
                        .tracking(1.5)
                        .foregroundColor(.secondary)
                }
            }
            .frame(width: 200, height: 200)
        }
    }

    private var speedFraction: Double {
        let s = live.telemetry?.speed ?? 0
        return min(1, s / 220)
    }

    private var rpmBar: some View {
        RacingCard(accent: .neonCyan) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text("RPM")
                        .font(.caption.weight(.bold))
                        .tracking(1.5)
                        .foregroundColor(.secondary)
                    Spacer()
                    Text("\(Int(live.telemetry?.rpm ?? 0))")
                        .font(.racingDigits(18))
                        .foregroundColor(.neonCyan)
                        .neonGlow(.neonCyan, radius: 6)
                }
                GeometryReader { geo in
                    ZStack(alignment: .leading) {
                        Capsule().fill(RacingTheme.cardBorderDark)
                        Capsule()
                            .fill(LinearGradient(colors: [.neonCyan, .neonMagenta], startPoint: .leading, endPoint: .trailing))
                            .frame(width: geo.size.width * rpmFraction)
                            .shadow(color: .neonCyan.opacity(0.6), radius: 6)
                        // Zona shift light: 15% terakhir
                        Rectangle()
                            .fill(Color.clear)
                            .frame(width: geo.size.width * 0.15)
                            .overlay(
                                Capsule().stroke(Color.neonRed.opacity(0.8), lineWidth: 1.5)
                            )
                            .frame(maxWidth: .infinity, alignment: .trailing)
                    }
                }
                .frame(height: 12)
            }
            .padding(12)
        }
    }

    private var rpmFraction: Double {
        let r = live.telemetry?.rpm ?? 0
        return min(1, r / 12000)
    }

    private var statGrid: some View {
        let grid = [
            ("TOTAL TRIP", String(format: "%.1f km", live.telemetry?.trip ?? 0)),
            ("SATELIT", "\(live.telemetry?.sats ?? 0)"),
            ("BATERAI", "\(live.telemetry?.bat_percent ?? 0)%"),
            ("TEGANGAN", String(format: "%.2f V", live.telemetry?.bat_voltage ?? 0)),
        ]
        return LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 10) {
            ForEach(grid, id: \.0) { title, value in
                RacingStat(label: title,
                           value: value,
                           color: title == "SATELIT" ? .neonCyan : .white,
                           glowColor: title == "BATERAI" ? .neonYellow : .neonOrange)
            }
        }
    }

    private var trailMap: some View {
        let coords = live.points.map(\.coordinate)
        return Map(position: $camera) {
            if coords.count > 1 {
                MapPolyline(coordinates: coords).stroke(.orange, lineWidth: 3)
            }
            if let t = live.telemetry {
                Annotation("", coordinate: t.coordinate) {
                    Image(systemName: "location.north.fill")
                        .foregroundColor(.red)
                        .font(.title2)
                        .shadow(radius: 3)
                }
            }
        }
        .frame(height: 220)
        .clipShape(RoundedRectangle(cornerRadius: 12))
        .overlay(alignment: .topTrailing) {
            Button {
                live.toggleRecording()
            } label: {
                HStack(spacing: 5) {
                    Circle().fill(live.isRecording ? .red : .clear).frame(width: 8, height: 8)
                    Text(live.isRecording ? "MEREKAM" : "REK")
                        .font(.caption).bold()
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .background(Capsule().fill(Color.black.opacity(0.65)))
            }
            .buttonStyle(.plain)
            .padding(10)
        }
    }

    private var dragReadout: some View {
        RacingCard(accent: .neonRed, glow: true) {
            VStack(spacing: 8) {
                HStack {
                    Text("DRAG LIVE")
                        .font(.headline.weight(.bold))
                        .tracking(1)
                    Spacer()
                    RacingBadge(text: live.drag.status.rawValue.uppercased(), color: dragStatusColor)
                }
                HStack {
                    stat("0-60", live.drag.t0to60)
                    stat("0-100", live.drag.t0to100)
                    stat("100-200", live.drag.t100to200)
                    stat("402 m", live.drag.t402m)
                }
            }
            .padding(12)
        }
    }

    private var dragStatusColor: Color {
        switch live.drag.status {
        case .running: return .green
        case .finished: return .yellow
        case .armed: return .orange
        case .idle: return .gray
        }
    }

    private func stat(_ label: String, _ value: Double?) -> some View {
        VStack(spacing: 3) {
            Text(value.map { String(format: "%.2f", $0) } ?? "--")
                .font(.subheadline).bold()
                .foregroundColor(value == nil ? .secondary : .white)
            Text(label).font(.caption2).foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity)
    }

    // MARK: - Drag mode

    private var dragContent: some View {
        RacingCard(accent: .neonRed, glow: true) {
            VStack(spacing: 16) {
                // Status (SIAP/BERJALAN/SELESAI) selalu di atas, tidak tertutup
                // ui bawah.
                HStack {
                    Text("DRAG LIVE")
                        .font(.headline.weight(.bold))
                        .tracking(1)
                    Spacer()
                    RacingBadge(text: live.drag.status.rawValue.uppercased(), color: dragStatusColor)
                }

                ZStack {
                    Circle()
                        .fill(RacingTheme.card.opacity(0.5))
                    Circle()
                        .stroke(RacingTheme.cardBorderDark, lineWidth: 10)
                    Circle()
                        .trim(from: 0, to: speedFraction)
                        .stroke(Color.neonRed, style: StrokeStyle(lineWidth: 10, lineCap: .round))
                        .rotationEffect(.degrees(-90))
                        .shadow(color: .neonRed.opacity(0.8), radius: 10)
                    VStack(spacing: 2) {
                        Text("\(Int(live.telemetry?.speed ?? 0))")
                            .font(.racingDigits(64))
                            .foregroundColor(.white)
                            .neonGlow(.neonRed, radius: 10)
                        Text("km/h")
                            .font(.caption.weight(.bold))
                            .tracking(1.5)
                            .foregroundColor(.secondary)
                    }
                }
                .frame(width: 220, height: 220)

                Text("Jarak: \(Int(live.drag.distance)) m")
                    .font(.title3.weight(.bold))
                    .fontDesign(.rounded)
                    .monospacedDigit()

                HStack {
                    stat("0-60", live.drag.t0to60)
                    stat("0-100", live.drag.t0to100)
                    stat("100-200", live.drag.t100to200)
                }

                stat("402 m", live.drag.t402m)

                Text("Otomatis: mulai saat kecepatan \u{2265}5 km/h, selesai <3 km/h.")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
            .padding(16)
        }
    }
}
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
                if live.connected {
                    Picker("Mode", selection: $mode) {
                        ForEach(Mode.allCases) { Text($0.rawValue).tag($0) }
                    }
                    .pickerStyle(.segmented)

                    switch mode {
                    case .telemetry: telemetryContent
                    case .drag: dragContent
                    case .navigation: navigationContent
                    }
                } else {
                    disconnectedContent
                }
            }
            .padding()
        }
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
                .fill(live.connected ? .green : .gray)
                .frame(width: 10, height: 10)
            Text(live.connected ? "DEVICE LIVE" : "TIDAK TERHUBUNG")
                .font(.caption).bold()
                .tracking(1)
            Spacer()
            if let t = live.lastUpdated {
                Text(t, style: .time)
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
        .padding(.horizontal, 4)
    }

    private var disconnectedContent: some View {
        VStack(spacing: 14) {
            Image(systemName: "wifi.slash")
                .font(.system(size: 44))
                .foregroundColor(.secondary)
            Text("Belum terhubung ke device")
                .font(.headline)
            Text("Hubungkan iPhone ke WiFi AP \u{201C}MuchRacing-GPS\u{201D} (sandi 12345678) atau ke jaringan tempat device terhubung, lalu tekan Coba Lagi.")
                .font(.footnote)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
            Button {
                Task { await live.tick(force: true) }
            } label: {
                Label("Coba Lagi", systemImage: "arrow.clockwise")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
        }
        .padding(24)
        .background(RoundedRectangle(cornerRadius: 12).fill(Color(.secondarySystemBackground)))
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
                    .stroke(Color(.systemGray5), lineWidth: 12)
                Circle()
                    .trim(from: 0, to: speedFraction)
                    .stroke(AngularGradient(gradient: Gradient(colors: [.green, .yellow, .orange, .red]),
                                            center: .center),
                            style: StrokeStyle(lineWidth: 12, lineCap: .round))
                    .rotationEffect(.degrees(-90))
                VStack(spacing: 2) {
                    Text("\(Int(live.telemetry?.speed ?? 0))")
                        .font(.system(size: 56, weight: .bold, design: .rounded))
                        .foregroundColor(.white)
                    Text("km/h")
                        .font(.caption)
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
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("RPM").font(.caption).bold().foregroundColor(.secondary)
                Spacer()
                Text("\(Int(live.telemetry?.rpm ?? 0))").font(.subheadline).bold().foregroundColor(.white)
            }
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color(.systemGray5))
                    Capsule()
                        .fill(LinearGradient(colors: [.cyan, .purple], startPoint: .leading, endPoint: .trailing))
                        .frame(width: geo.size.width * rpmFraction)
                }
            }
            .frame(height: 10)
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
                VStack(spacing: 4) {
                    Text(title).font(.caption2).foregroundColor(.secondary)
                    Text(value).font(.headline).foregroundColor(.white)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 12)
                .background(RoundedRectangle(cornerRadius: 10).fill(Color(.secondarySystemBackground)))
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
        VStack(spacing: 8) {
            HStack {
                Text("Drag Live").font(.headline)
                Spacer()
                Text(live.drag.status.rawValue)
                    .font(.caption).bold()
                    .padding(.horizontal, 8).padding(.vertical, 3)
                    .background(Capsule().fill(dragStatusColor))
            }
            HStack {
                stat("0-60", live.drag.t0to60)
                stat("0-100", live.drag.t0to100)
                stat("100-200", live.drag.t100to200)
                stat("402 m", live.drag.t402m)
            }
        }
        .padding(12)
        .background(RoundedRectangle(cornerRadius: 12).fill(Color(.secondarySystemBackground)))
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
        VStack(spacing: 16) {
            ZStack {
                Circle()
                    .stroke(Color(.systemGray5), lineWidth: 10)
                Circle()
                    .trim(from: 0, to: speedFraction)
                    .stroke(.red, style: StrokeStyle(lineWidth: 10, lineCap: .round))
                    .rotationEffect(.degrees(-90))
                VStack(spacing: 2) {
                    Text("\(Int(live.telemetry?.speed ?? 0))")
                        .font(.system(size: 64, weight: .bold, design: .rounded))
                        .foregroundColor(.white)
                    Text("km/h").font(.caption).foregroundColor(.secondary)
                }
            }
            .frame(width: 220, height: 220)

            Text("Jarak: \(Int(live.drag.distance)) m")
                .font(.title3).bold()

            Text(live.drag.status.rawValue)
                .font(.headline)
                .foregroundColor(dragStatusColor)

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
        .background(RoundedRectangle(cornerRadius: 12).fill(Color(.secondarySystemBackground)))
    }
}
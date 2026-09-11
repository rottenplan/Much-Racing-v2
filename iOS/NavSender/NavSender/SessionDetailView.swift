import SwiftUI
import MapKit
import Charts

// MARK: - Detail sesi: statistik, lap, drag, grafik, dan replay peta

struct SessionDetailView: View {
    let analysis: SessionAnalysis

    @State private var region: MKCoordinateRegion?
    @State private var scrub = 0.0

    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                headerCard
                statsGrid
                if analysis.laps.isEmpty == false {
                    lapsCard
                }
                if hasDragStats {
                    dragCard
                }
                speedChartCard
                replayMapCard
                shareCard
            }
            .padding()
        }
        .background(RacingBackground())
        .navigationTitle(analysis.name)
        .navigationBarTitleDisplayMode(.inline)
        .onAppear { fitRegion() }
    }

    // MARK: - Header

    private var headerCard: some View {
        RacingCard(accent: analysis.sessionType == "DRAG" ? .neonRed : .neonCyan, glow: true) {
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    RacingBadge(text: analysis.sessionType,
                                color: analysis.sessionType == "DRAG" ? .neonRed : .neonCyan,
                                icon: analysis.sessionType == "DRAG" ? "flag.checkered" : "timer")
                    Spacer()
                    Text(analysis.date, style: .date)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                Text(analysis.name)
                    .font(.title3.weight(.bold))
                    .fontDesign(.rounded)
                    .monospacedDigit()
                Text(String(format: "%.1f detik data", analysis.totalTime))
                    .font(.caption)
                    .foregroundColor(.secondary)
                CheckeredStrip()
                    .padding(.top, 2)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(12)
        }
    }

    // MARK: - Statistik

    private var statsGrid: some View {
        let stats = [
            ("JARAK", String(format: "%.0f m", analysis.totalDistance)),
            ("MAKS", String(format: "%.1f km/h", analysis.maxSpeed)),
            ("RATA2", String(format: "%.1f km/h", analysis.avgSpeed)),
            ("RPM MAX", "\(Int(analysis.maxRpm))"),
        ]
        return LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 10) {
            ForEach(stats, id: \.0) { title, value in
                RacingStat(label: title,
                           value: value,
                           color: title == "MAKS" ? .neonOrange : (title == "RPM MAX" ? .neonRed : .white),
                           glowColor: title == "MAKS" ? .neonOrange : (title == "RPM MAX" ? .neonRed : .neonCyan))
            }
        }
    }

    // MARK: - Lap

    private var lapsCard: some View {
        RacingCard(accent: .neonYellow, glow: true) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text("LAP TIMES")
                        .font(.headline.weight(.bold))
                        .tracking(1)
                    Spacer()
                    if let best = analysis.bestLapMs {
                        RacingBadge(text: "BEST \(formatLap(best))", color: .neonGreen, icon: "star.fill")
                    }
                }
                ForEach(analysis.laps) { lap in
                    HStack {
                        Text("Lap \(lap.lapNumber)")
                            .font(.subheadline.weight(.semibold))
                        Spacer()
                        if let s3 = lap.s3Ms {
                            Text("S1 \(formatMs(s3))")
                                .font(.caption2)
                                .foregroundColor(.secondary)
                        }
                        Text(formatLap(lap.timeMs))
                            .font(.racingDigits(16))
                        if let best = analysis.bestLapMs, lap.timeMs == best {
                            Image(systemName: "star.fill")
                                .font(.caption)
                                .foregroundColor(.neonYellow)
                                .neonGlow(.neonYellow, radius: 5)
                        } else {
                            Text(String(format: "+%.2f", analysis.gapToBest(lap: lap)))
                                .font(.caption.weight(.semibold))
                                .foregroundColor(.neonOrange)
                        }
                    }
                    Divider().overlay(Color.neonOrange.opacity(0.15))
                }
            }
            .padding(12)
        }
    }

    // MARK: - Drag

    private var hasDragStats: Bool {
        analysis.drag.time0to60 != nil || analysis.drag.time0to100 != nil ||
            analysis.drag.time100to200 != nil || analysis.drag.time402m != nil
    }

    private var dragCard: some View {
        RacingCard(accent: .neonRed, glow: true) {
            VStack(alignment: .leading, spacing: 8) {
                Text("DRAG SUMMARY")
                    .font(.headline.weight(.bold))
                    .tracking(1)
                HStack {
                    dragStat("0-60", analysis.drag.time0to60)
                    dragStat("0-100", analysis.drag.time0to100)
                    dragStat("100-200", analysis.drag.time100to200)
                    dragStat("402 m", analysis.drag.time402m)
                }
            }
            .padding(12)
        }
    }

    private func dragStat(_ label: String, _ value: Double?) -> some View {
        VStack(spacing: 4) {
            Text(value.map { String(format: "%.2f", $0) } ?? "--")
                .font(.subheadline).bold()
            Text(label).font(.caption2).foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity)
    }

    // MARK: - Grafik kecepatan

    private var speedChartCard: some View {
        RacingCard(accent: .neonOrange) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text("KECEPATAN")
                        .font(.headline.weight(.bold))
                        .tracking(1)
                    Spacer()
                    RacingBadge(text: "km/h", color: .neonOrange)
                }
                Chart {
                    ForEach(Array(analysis.points.enumerated()), id: \.offset) { index, p in
                        LineMark(x: .value("Waktu", index),
                                 y: .value("Speed", p.speed))
                            .foregroundStyle(Color.neonOrange)
                            .interpolationMethod(.catmullRom)
                    }
                }
                .frame(height: 140)
                .chartYAxisLabel("km/h")
            }
            .padding(12)
        }
    }

    // MARK: - Replay peta

    private var replayMapCard: some View {
        RacingCard(accent: .neonCyan) {
            VStack(alignment: .leading, spacing: 8) {
            Text("REPLAY RUTE")
                .font(.headline.weight(.bold))
                .tracking(1)
            if let region {
                Map(position: .constant(.region(region))) {
                    let segments = speedSegments
                    ForEach(Array(segments.enumerated()), id: \.offset) { _, seg in
                        MapPolyline(coordinates: seg.coords)
                            .stroke(seg.color, lineWidth: 3)
                    }
                    if let pt = scrubPoint {
                        Annotation("", coordinate: pt.coordinate) {
                            ZStack {
                                Circle().fill(.white).frame(width: 14, height: 14)
                                Circle().fill(.black).frame(width: 8, height: 8)
                            }
                        }
                    }
                }
                .frame(height: 260)
                .clipShape(RoundedRectangle(cornerRadius: 12))

                if analysis.points.count > 1 {
                    HStack(spacing: 6) {
                        Text("Scrub")
                            .font(.caption.weight(.semibold))
                            .foregroundColor(.secondary)
                        Slider(value: $scrub, in: 0...Double(analysis.points.count - 1), step: 1)
                            .tint(.neonCyan)
                    }
                    if let pt = scrubPoint {
                        Text("Speed \(Int(pt.speed)) km/h • RPM \(Int(pt.rpm)) • t +\(String(format: "%.1f", pt.time / 1000))s")
                            .font(.caption.weight(.medium))
                            .foregroundColor(.neonCyan)
                            .monospacedDigit()
                    }
                }
            }
        }
        .padding(12)
    }
}

    private var scrubIndex: Int {
        guard analysis.points.count > 0 else { return 0 }
        return min(Int(scrub.rounded()), analysis.points.count - 1)
    }

    private var scrubPoint: TelemetryPoint? {
        guard analysis.points.count > 0 else { return nil }
        return analysis.points[scrubIndex]
    }

    private var speedSegments: [(color: Color, coords: [CLLocationCoordinate2D])] {
        var out: [(Color, [CLLocationCoordinate2D])] = []
        guard analysis.points.count > 0 else { return out }

        func bucket(_ s: Double) -> Color {
            switch s {
            case ..<40: return .green
            case ..<80: return .yellow
            case ..<120: return .orange
            default: return .red
            }
        }

        var current: Color? = nil
        var coords: [CLLocationCoordinate2D] = []
        for p in analysis.points {
            let c = bucket(p.speed)
            if current == c {
                coords.append(p.coordinate)
            } else {
                if let cur = current, coords.count > 0 {
                    out.append((cur, coords))
                }
                current = c
                coords = [p.coordinate]
            }
        }
        if let cur = current, coords.count > 0 {
            out.append((cur, coords))
        }
        return out
    }

    private func fitRegion() {
        guard analysis.points.count > 1 else { return }
        let lat = analysis.points.map(\.lat)
        let lng = analysis.points.map(\.lng)
        guard let minLat = lat.min(), let maxLat = lat.max(),
              let minLng = lng.min(), let maxLng = lng.max() else { return }
        let center = CLLocationCoordinate2D(latitude: (minLat + maxLat) / 2,
                                            longitude: (minLng + maxLng) / 2)
        let span = MKCoordinateSpan(latitudeDelta: max((maxLat - minLat) * 1.4, 0.0004),
                                    longitudeDelta: max((maxLng - minLng) * 1.4, 0.0004))
        region = MKCoordinateRegion(center: center, span: span)
    }

    // MARK: - Bagikan GPX

    private var shareCard: some View {
        let gpx = buildGPX(points: analysis.points, name: analysis.name)
        return ShareLink(item: gpx) {
            Label("Bagikan sebagai GPX", systemImage: "square.and.arrow.up")
                .frame(maxWidth: .infinity)
        }
        .buttonStyle(.bordered)
        .tint(.neonOrange)
    }

    private func buildGPX(points: [TelemetryPoint], name: String) -> String {
        var xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        xml += "<gpx version=\"1.1\" creator=\"MuchRacing iOS\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
        xml += "<trk><name>\(name)</name><trkseg>\n"
        for p in points {
            xml += "<trkpt lat=\"\(p.lat)\" lon=\"\(p.lng)\"><ele>0</ele></trkpt>\n"
        }
        xml += "</trkseg></trk></gpx>\n"
        return xml
    }

    // MARK: - Format

    private func formatLap(_ ms: Int) -> String {
        let mins = ms / 60000
        let secs = Double(ms % 60000) / 1000.0
        return String(format: "%d:%05.2f", mins, secs)
    }

    private func formatMs(_ ms: Int) -> String {
        String(format: "%.2f", Double(ms) / 1000.0)
    }
}
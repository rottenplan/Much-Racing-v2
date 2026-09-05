import Foundation
import Combine
import CoreLocation

// MARK: - Tampungan state live + drag + REC (polling /api/live)

enum DragStatus: String {
    case idle = "STANDBY"
    case armed = "SIAP"
    case running = "BERJALAN"
    case finished = "SELESAI"
}

struct DragRun: Equatable {
    var status: DragStatus = .idle
    var startTime: TimeInterval = 0
    var lastSampleTime: TimeInterval = 0
    var lastSampleSpeed: Double = 0
    var distance: Double = 0
    var t0to60: Double?
    var t0to100: Double?
    var t100to200: Double?
    var t402m: Double?

    mutating func reset() {
        self = DragRun()
    }
}

struct TripPoint: Identifiable {
    var id: Double { time }
    let time: TimeInterval
    let lat: Double
    let lng: Double
    let speed: Double
    let rpm: Double
    var coordinate: CLLocationCoordinate2D { CLLocationCoordinate2D(latitude: lat, longitude: lng) }
}

@MainActor
final class LiveStore: ObservableObject {
    @Published var telemetry: LiveTelemetry?
    @Published var connected = false
    @Published var lastUpdated: Date?
    @Published var isRecording = false
    @Published var points: [TripPoint] = []
    @Published var drag: DragRun = DragRun()
    @Published var polling = false

    private var task: Task<Void, Never>?

    func start() {
        guard task == nil else { return }
        polling = true
        task = Task { [weak self] in
            while !Task.isCancelled {
                guard let self else { return }
                await self.tick()
                try? await Task.sleep(nanoseconds: 1_000_000_000)
            }
        }
    }

    func stop() {
        task?.cancel()
        task = nil
        polling = false
    }

    func setBaseURL(_ url: String) {
        APClient.saveBaseURL(url)
        reconnectImmediately()
    }

    func reconnectImmediately() {
        APClient.saveBaseURL(APClient.savedBaseURL())
        Task { await tick(force: true) }
    }

    func tick(force: Bool = false) async {
        let client = APClient()
        if let t = try? await client.fetchLive() {
            connected = true
            lastUpdated = Date()
            telemetry = t
            record(t)
            updateDrag(with: t, now: Date().timeIntervalSince1970)
        } else if force || !connected {
            connected = false
        }
    }

    // MARK: - REC (rekam trail + data)

    func toggleRecording() {
        isRecording.toggle()
        if isRecording { points = [] }
    }

    private func record(_ t: LiveTelemetry) {
        guard isRecording else { return }
        points.append(TripPoint(time: Date().timeIntervalSince1970,
                                lat: t.lat, lng: t.lng,
                                speed: t.speed, rpm: t.rpm))
        let maxPoints = 3000
        if points.count > maxPoints { points.removeFirst(points.count - maxPoints) }
    }

    // MARK: - Drag run (meniru logika DragModeView web)

    private func updateDrag(with t: LiveTelemetry, now: TimeInterval) {
        let speed = t.speed
        let dt = drag.lastSampleTime == 0 ? 0 : max(0, now - drag.lastSampleTime)

        switch drag.status {
        case .idle:
            if speed < 5 { drag.status = .armed }

        case .armed:
            if speed >= 5 {
                drag.startTime = now
                drag.distance = 0
                drag.t0to60 = nil
                drag.t0to100 = nil
                drag.t100to200 = nil
                drag.t402m = nil
                drag.status = .running
            }

        case .running:
            if speed < 3 {
                drag.status = .finished
            } else if dt > 0 {
                drag.distance += ((drag.lastSampleSpeed + speed) / 2) * dt / 3.6
                let runTime = now - drag.startTime
                if drag.t0to60 == nil, speed >= 60 { drag.t0to60 = runTime }
                if drag.t0to100 == nil, speed >= 100 { drag.t0to100 = runTime }
                if drag.t100to200 == nil, speed >= 200 { drag.t100to200 = runTime }
                if drag.t402m == nil, drag.distance >= 402 { drag.t402m = runTime }
            }

        case .finished:
            if speed < 3 {
                drag.reset()
                drag.status = .armed
            }
        }

        drag.lastSampleSpeed = speed
        drag.lastSampleTime = now
    }
}
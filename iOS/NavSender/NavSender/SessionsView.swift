import SwiftUI

// MARK: - Sesi: daftar rekaman dari SD device (via WiFi AP)

struct SessionsView: View {
    @State private var files: [SessionFileInfo] = []
    @State private var loading = false
    @State private var errorText: String?
    @State private var downloadingPath: String?

    @State private var analysis: SessionAnalysis?
    @State private var showDetail = false

    var body: some View {
        Group {
            if loading && files.isEmpty {
                ProgressView("Membaca sessions device...")
            } else if files.isEmpty {
                emptyState
            } else {
                list
            }
        }
        .navigationTitle("Sesi")
        .background(RacingBackground())
        .navigationDestination(isPresented: $showDetail) {
            if let a = analysis {
                SessionDetailView(analysis: a)
            }
        }
        .onAppear { Task { await load() } }
        .refreshable { await load() }
        .alert("Gagal membaca sesi", isPresented: .constant(errorText != nil)) {
            Button("OK") { errorText = nil }
        } message: {
            Text(errorText ?? "")
        }
    }

    private var list: some View {
        List(files) { file in
            Button {
                open(file)
            } label: {
                HStack {
                    Image(systemName: file.name.uppercased().contains("DRAG") ? "flag.checkered" : "timer")
                        .font(.title3.weight(.semibold))
                        .foregroundColor(file.name.uppercased().contains("DRAG") ? .neonRed : .neonCyan)
                        .frame(width: 34)
                        .neonGlow(file.name.uppercased().contains("DRAG") ? .neonRed : .neonCyan, radius: 5)
                    VStack(alignment: .leading, spacing: 4) {
                        Text(file.name)
                            .font(.subheadline.weight(.bold))
                            .foregroundColor(.white)
                        HStack(spacing: 6) {
                            RacingBadge(text: file.name.uppercased().contains("DRAG") ? "DRAG" : "TRACK",
                                        color: file.name.uppercased().contains("DRAG") ? .neonRed : .neonCyan)
                            Text(file.size)
                                .font(.caption2)
                                .foregroundColor(.secondary)
                        }
                    }
                    Spacer()
                    if downloadingPath == file.path {
                        ProgressView()
                    } else {
                        Image(systemName: "chevron.right")
                            .font(.caption.weight(.bold))
                            .foregroundColor(.neonOrange)
                    }
                }
            }
            .disabled(downloadingPath != nil)
            .listRowBackground(RacingTheme.card)
        }
        .listStyle(.insetGrouped)
        .scrollContentBackground(.hidden)
    }

    private var emptyState: some View {
        RacingCard(accent: .neonOrange, glow: true) {
            VStack(spacing: 14) {
                Image(systemName: "externaldrive.badge.clock")
                    .font(.system(size: 44))
                    .foregroundColor(.neonOrange)
                    .neonGlow(.neonOrange, radius: 8)
                Text("BELUM ADA SESI")
                    .font(.headline.weight(.bold))
                    .tracking(1)
                Text("Hubungkan iPhone ke WiFi AP device (\u{201C}MuchRacing-GPS\u{201D} / 12345678) lalu tarik untuk memuat daftar sesi dari SD card.")
                    .font(.footnote)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
                Button {
                    Task { await load(force: true) }
                } label: {
                    Label("Muat Ulang", systemImage: "arrow.clockwise")
                }
                .buttonStyle(.borderedProminent)
                .tint(.neonOrange)
            }
            .frame(maxWidth: .infinity)
            .padding(24)
        }
    }

    private func load(force: Bool = false) async {
        guard !loading || force else { return }
        loading = true
        defer { loading = false }
        do {
            let list = try await APClient().fetchSessions()
            files = list
                .filter { $0.name.hasSuffix(".csv") }
                .sorted { $0.name > $1.name } // run_N terbaru dulu
        } catch {
            errorText = "Tidak dapat terhubung ke device (\(error.localizedDescription)). Pastikan iPhone di WiFi AP MuchRacing-GPS."
        }
    }

    private func open(_ file: SessionFileInfo) {
        guard downloadingPath == nil else { return }
        downloadingPath = file.path
        Task {
            defer { downloadingPath = nil }
            do {
                let data = try await APClient().download(path: file.path)
                let a = LogParser.parse(data, fileName: file.name)
                analysis = a
                showDetail = true
            } catch {
                errorText = "Gagal mengunduh \(file.name)."
            }
        }
    }
}
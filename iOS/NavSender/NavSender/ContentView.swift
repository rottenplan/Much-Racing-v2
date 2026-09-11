import SwiftUI

enum AppTab: Hashable, CaseIterable {
    case home, live, nav, sessions, settings

    var title: String {
        switch self {
        case .home: return "Beranda"
        case .live: return "Live"
        case .nav: return "Navigasi"
        case .sessions: return "Sesi"
        case .settings: return "Setelan"
        }
    }

    var icon: String {
        switch self {
        case .home: return "house.fill"
        case .live: return "gauge.with.dots.needle.50percent"
        case .nav: return "arrow.up.right"
        case .sessions: return "list.bullet"
        case .settings: return "gearshape.fill"
        }
    }

    var order: Int {
        switch self {
        case .home: return 0
        case .live: return 1
        case .nav: return 2
        case .sessions: return 3
        case .settings: return 4
        }
    }
}

// MARK: - Root: animated tab switcher + custom racing tab bar

struct ContentView: View {
    @StateObject private var ble: BLEManager
    @StateObject private var route: RouteManager
    @StateObject private var location: LocationTracker
    @StateObject private var live: LiveStore
    @StateObject private var navSession: NavigationSession

    @State private var selection: AppTab = .home
    @State private var visited: Set<AppTab> = [.home]

    init() {
        let ble = BLEManager()
        let route = RouteManager()
        let location = LocationTracker()
        let live = LiveStore()
        let navSession = NavigationSession(ble: ble, route: route, location: location)
        _ble = StateObject(wrappedValue: ble)
        _route = StateObject(wrappedValue: route)
        _location = StateObject(wrappedValue: location)
        _live = StateObject(wrappedValue: live)
        _navSession = StateObject(wrappedValue: navSession)
    }

    var body: some View {
        ZStack {
            RacingTheme.background.ignoresSafeArea()
            tabContent
        }
        .safeAreaInset(edge: .bottom, spacing: 0) {
            RacingTabBar(selection: $selection)
        }
        .environmentObject(ble)
        .environmentObject(route)
        .environmentObject(location)
        .environmentObject(live)
        .environmentObject(navSession)
        .preferredColorScheme(.dark)
        .tint(.neonOrange)
        .onAppear {
            ble.start()
            live.start()
        }
        .onDisappear {
            live.stop()
        }
        .onChange(of: selection) { _, new in
            visited.insert(new)
        }
    }

    private var tabContent: some View {
        ZStack {
            ForEach(AppTab.allCases, id: \.self) { tab in
                if visited.contains(tab) {
                    tabView(for: tab)
                        .transition(.scale(scale: 0.92).combined(with: .opacity))
                        .opacity(tab == selection ? 1 : 0)
                        .offset(x: CGFloat(tab.order - selection.order) * -34)
                        .scaleEffect(tab == selection ? 1 : 0.97)
                        .zIndex(tab == selection ? 1 : 0)
                        .allowsHitTesting(tab == selection)
                        .accessibilityHidden(tab != selection)
                }
            }
        }
        .animation(.spring(response: 0.42, dampingFraction: 0.84, blendDuration: 0.1), value: selection)
        .animation(.easeOut(duration: 0.30), value: visited)
    }

    @ViewBuilder
    private func tabView(for tab: AppTab) -> some View {
        switch tab {
        case .home:
            NavigationStack { HomeView(selection: $selection) }
        case .live:
            NavigationStack { LiveView() }
        case .nav:
            NavigationStack { NavigationRootView() }
        case .sessions:
            NavigationStack { SessionsView() }
        case .settings:
            NavigationStack { SettingsView() }
        }
    }
}

// MARK: - Racing Tab Bar: sliding glow pill + bouncing icon

private struct RacingTabBar: View {
    @Binding var selection: AppTab
    @Namespace private var ns

    var body: some View {
        HStack(spacing: 0) {
            ForEach(AppTab.allCases, id: \.self) { tab in
                tabButton(tab)
            }
        }
        .padding(.top, 6)
        .padding(.bottom, 4)
        .background(
            RacingTheme.card.opacity(0.98)
                .overlay(alignment: .top) {
                    LinearGradient(
                        colors: [.clear, .neonOrange.opacity(0.85), .clear],
                        startPoint: .leading, endPoint: .trailing
                    )
                    .frame(height: 1)
                }
                .ignoresSafeArea(edges: .bottom)
        )
        .shadow(color: .black.opacity(0.5), radius: 12, y: -4)
        .animation(.spring(response: 0.38, dampingFraction: 0.68, blendDuration: 0.1), value: selection)
    }

    private func tabButton(_ tab: AppTab) -> some View {
        Button {
            selection = tab
        } label: {
            VStack(spacing: 3) {
                ZStack {
                    if selection == tab {
                        Capsule()
                            .fill(Color.neonOrange.opacity(0.16))
                            .overlay(Capsule().stroke(Color.neonOrange.opacity(0.5), lineWidth: 1))
                            .transition(.scale(scale: 0.6).combined(with: .opacity))
                            .matchedGeometryEffect(id: "tabpill", in: ns)
                    }
                    Image(systemName: tab.icon)
                        .font(.system(size: 19, weight: .semibold))
                        .foregroundColor(selection == tab ? .neonOrange : .white.opacity(0.42))
                        .neonGlow(selection == tab ? .neonOrange : .clear, radius: 6)
                        .scaleEffect(selection == tab ? 1.14 : 1.0)
                }
                .frame(width: 58, height: 30)

                Text(tab.title)
                    .font(.system(size: 10, weight: selection == tab ? .bold : .medium))
                    .foregroundColor(selection == tab ? .white : .white.opacity(0.4))
                    .scaleEffect(selection == tab ? 1.0 : 0.78)
                    .opacity(selection == tab ? 1 : 0.7)
            }
            .frame(maxWidth: .infinity)
            .padding(.vertical, 5)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(tab.title)
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
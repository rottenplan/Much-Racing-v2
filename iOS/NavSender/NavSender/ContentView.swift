import SwiftUI

enum AppTab: Hashable {
    case home, live, nav, sessions, settings
}

// MARK: - Root: TabView 5 tab, shared services via environment

struct ContentView: View {
    @StateObject private var ble = BLEManager()
    @StateObject private var route = RouteManager()
    @StateObject private var location = LocationTracker()
    @StateObject private var live = LiveStore()

    @State private var selection: AppTab = .home

    var body: some View {
        TabView(selection: $selection) {
            NavigationStack {
                HomeView(selection: $selection)
            }
            .tabItem { Label("Beranda", systemImage: "house.fill") }
            .tag(AppTab.home)

            NavigationStack {
                LiveView()
            }
            .tabItem { Label("Live", systemImage: "gauge.with.dots.needle.50percent") }
            .tag(AppTab.live)

            NavigationStack {
                NavigationRootView()
            }
            .tabItem { Label("Navigasi", systemImage: "arrow.up.right") }
            .tag(AppTab.nav)

            NavigationStack {
                SessionsView()
            }
            .tabItem { Label("Sesi", systemImage: "list.bullet") }
            .tag(AppTab.sessions)

            NavigationStack {
                SettingsView()
            }
            .tabItem { Label("Setelan", systemImage: "gearshape.fill") }
            .tag(AppTab.settings)
        }
        .environmentObject(ble)
        .environmentObject(route)
        .environmentObject(location)
        .environmentObject(live)
        .preferredColorScheme(.dark)
        .onAppear {
            ble.start()
            live.start()
        }
        .onDisappear {
            live.stop()
        }
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
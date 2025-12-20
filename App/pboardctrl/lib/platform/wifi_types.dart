class WifiNetwork {
  const WifiNetwork({
    required this.ssid,
    required this.signalQuality,
    required this.security,
  });

  final String ssid;
  final int? signalQuality; // 0..100, if available
  final String? security;
}

class WifiConnectResult {
  const WifiConnectResult({required this.ok, this.error});

  final bool ok;
  final String? error;
}


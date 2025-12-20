class MonitorSnapshot {
  MonitorSnapshot({
    required this.capVoltage,
    required this.capAdcRaw,
    required this.current,
    required this.capacitanceF,
    required this.temperatures,
    required this.boardTemp,
    required this.heatsinkTemp,
    required this.wireTemps,
    required this.outputs,
    required this.ready,
    required this.off,
    required this.ac,
    required this.relay,
    required this.fanSpeed,
    required this.wifiSta,
    required this.wifiConnected,
    required this.wifiRssi,
    required this.eventWarnUnread,
    required this.eventErrorUnread,
    required this.sessionValid,
    required this.sessionRunning,
    required this.sessionEnergyWh,
    required this.sessionDurationS,
    required this.sessionPeakPowerW,
    required this.sessionPeakCurrentA,
  });

  final double capVoltage;
  final double capAdcRaw;
  final double current;
  final double capacitanceF;
  final List<double?> temperatures;
  final double? boardTemp;
  final double? heatsinkTemp;
  final List<double?> wireTemps;
  final Map<int, bool> outputs;
  final bool ready;
  final bool off;
  final bool ac;
  final bool relay;
  final int fanSpeed;
  final bool wifiSta;
  final bool wifiConnected;
  final int? wifiRssi;
  final int eventWarnUnread;
  final int eventErrorUnread;
  final bool sessionValid;
  final bool sessionRunning;
  final double? sessionEnergyWh;
  final double? sessionDurationS;
  final double? sessionPeakPowerW;
  final double? sessionPeakCurrentA;

  static double? _parseTemp(dynamic value) {
    if (value is num) {
      if (!value.isFinite) return null;
      if (value == -127) return null;
      return value.toDouble();
    }
    return null;
  }

  static List<double?> _parseTempList(dynamic value) {
    if (value is List) {
      return value.map(_parseTemp).toList();
    }
    return const [];
  }

  static Map<int, bool> _parseOutputs(dynamic value) {
    final outputs = <int, bool>{};
    if (value is Map) {
      for (var i = 1; i <= 10; i++) {
        final key = 'output$i';
        outputs[i] = value[key] == true;
      }
    }
    return outputs;
  }

  factory MonitorSnapshot.fromJson(Map<String, dynamic> json) {
    final temps = _parseTempList(json['temperatures']);
    final wireTemps = _parseTempList(json['wireTemps']);
    final eventUnread = json['eventUnread'];
    final session = json['session'];

    return MonitorSnapshot(
      capVoltage: (json['capVoltage'] as num?)?.toDouble() ?? 0.0,
      capAdcRaw: (json['capAdcRaw'] as num?)?.toDouble() ?? 0.0,
      current: (json['current'] as num?)?.toDouble() ?? 0.0,
      capacitanceF: (json['capacitanceF'] as num?)?.toDouble() ?? 0.0,
      temperatures: temps,
      boardTemp: _parseTemp(json['boardTemp']),
      heatsinkTemp: _parseTemp(json['heatsinkTemp']),
      wireTemps: wireTemps,
      outputs: _parseOutputs(json['outputs']),
      ready: json['ready'] == true,
      off: json['off'] == true,
      ac: json['ac'] == true,
      relay: json['relay'] == true,
      fanSpeed: (json['fanSpeed'] as num?)?.round() ?? 0,
      wifiSta: json['wifiSta'] == true,
      wifiConnected: json['wifiConnected'] != false,
      wifiRssi: (json['wifiRssi'] as num?)?.round(),
      eventWarnUnread: eventUnread is Map ? (eventUnread['warn'] as num?)?.toInt() ?? 0 : 0,
      eventErrorUnread: eventUnread is Map ? (eventUnread['error'] as num?)?.toInt() ?? 0 : 0,
      sessionValid: session is Map ? session['valid'] == true : false,
      sessionRunning: session is Map ? session['running'] == true : false,
      sessionEnergyWh: session is Map ? (session['energy_Wh'] as num?)?.toDouble() : null,
      sessionDurationS: session is Map ? (session['duration_s'] as num?)?.toDouble() : null,
      sessionPeakPowerW: session is Map ? (session['peakPower_W'] as num?)?.toDouble() : null,
      sessionPeakCurrentA: session is Map ? (session['peakCurrent_A'] as num?)?.toDouble() : null,
    );
  }
}

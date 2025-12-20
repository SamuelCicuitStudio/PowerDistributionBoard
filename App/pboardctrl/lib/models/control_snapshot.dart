class ControlSnapshot {
  ControlSnapshot({
    required this.ledFeedback,
    required this.buzzerMute,
    required this.manualMode,
    required this.relay,
    required this.ready,
    required this.off,
    required this.fanSpeed,
    required this.outputs,
    required this.outputAccess,
    required this.deviceId,
    required this.onTimeMs,
    required this.offTimeMs,
    required this.acFrequency,
    required this.chargeResistor,
    required this.wifiSSID,
    required this.tempTripC,
    required this.tempWarnC,
    required this.idleCurrentA,
    required this.currLimit,
    required this.wireOhmPerM,
    required this.wireGauge,
    required this.wireTauSec,
    required this.wireKLoss,
    required this.wireThermalC,
    required this.wireKp,
    required this.wireKi,
    required this.floorKp,
    required this.floorKi,
    required this.ntcBeta,
    required this.ntcR0,
    required this.ntcFixedRes,
    required this.ntcPressMv,
    required this.ntcReleaseMv,
    required this.ntcDebounceMs,
    required this.ntcMinC,
    required this.ntcMaxC,
    required this.ntcSamples,
    required this.ntcGateIndex,
    required this.floorThicknessMm,
    required this.floorMaterial,
    required this.floorMaterialCode,
    required this.floorMaxC,
    required this.nichromeFinalTempC,
    required this.loopMode,
    required this.mixPreheatMs,
    required this.mixPreheatOnMs,
    required this.mixKeepMs,
    required this.mixFrameMs,
    required this.timingMode,
    required this.timingProfile,
    required this.wireRes,
    required this.targetRes,
  });

  final bool ledFeedback;
  final bool buzzerMute;
  final bool manualMode;
  final bool relay;
  final bool ready;
  final bool off;
  final int fanSpeed;
  final Map<int, bool> outputs;
  final Map<int, bool> outputAccess;
  final String deviceId;
  final int onTimeMs;
  final int offTimeMs;
  final int acFrequency;
  final double chargeResistor;
  final String wifiSSID;
  final double tempTripC;
  final double tempWarnC;
  final double idleCurrentA;
  final double currLimit;
  final double wireOhmPerM;
  final int wireGauge;
  final double wireTauSec;
  final double wireKLoss;
  final double wireThermalC;
  final double wireKp;
  final double wireKi;
  final double floorKp;
  final double floorKi;
  final double ntcBeta;
  final double ntcR0;
  final double ntcFixedRes;
  final double ntcPressMv;
  final double ntcReleaseMv;
  final int ntcDebounceMs;
  final double ntcMinC;
  final double ntcMaxC;
  final int ntcSamples;
  final int ntcGateIndex;
  final double floorThicknessMm;
  final String floorMaterial;
  final int floorMaterialCode;
  final double floorMaxC;
  final double nichromeFinalTempC;
  final String loopMode;
  final int mixPreheatMs;
  final int mixPreheatOnMs;
  final int mixKeepMs;
  final int mixFrameMs;
  final String timingMode;
  final String timingProfile;
  final Map<int, double> wireRes;
  final double targetRes;

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

  static Map<int, double> _parseWireRes(dynamic value) {
    final res = <int, double>{};
    if (value is Map) {
      for (var i = 1; i <= 10; i++) {
        final key = '$i';
        final v = value[key];
        if (v is num) {
          res[i] = v.toDouble();
        }
      }
    }
    return res;
  }

  static String _str(dynamic value, String fallback) {
    final s = value?.toString() ?? '';
    return s.isEmpty ? fallback : s;
  }

  factory ControlSnapshot.fromJson(Map<String, dynamic> json) {
    return ControlSnapshot(
      ledFeedback: json['ledFeedback'] == true,
      buzzerMute: json['buzzerMute'] == true,
      manualMode: json['manualMode'] == true,
      relay: json['relay'] == true,
      ready: json['ready'] == true,
      off: json['off'] == true,
      fanSpeed: (json['fanSpeed'] as num?)?.round() ?? 0,
      outputs: _parseOutputs(json['outputs']),
      outputAccess: _parseOutputs(json['outputAccess']),
      deviceId: json['deviceId']?.toString() ?? '',
      onTimeMs: (json['onTime'] as num?)?.toInt() ?? 500,
      offTimeMs: (json['offTime'] as num?)?.toInt() ?? 500,
      acFrequency: (json['acFrequency'] as num?)?.toInt() ?? 50,
      chargeResistor: (json['chargeResistor'] as num?)?.toDouble() ?? 0.0,
      wifiSSID: json['wifiSSID']?.toString() ?? '',
      tempTripC: (json['tempTripC'] as num?)?.toDouble() ?? 0.0,
      tempWarnC: (json['tempWarnC'] as num?)?.toDouble() ?? 0.0,
      idleCurrentA: (json['idleCurrentA'] as num?)?.toDouble() ?? 0.0,
      currLimit: (json['currLimit'] as num?)?.toDouble() ?? 0.0,
      wireOhmPerM: (json['wireOhmPerM'] as num?)?.toDouble() ?? 0.0,
      wireGauge: (json['wireGauge'] as num?)?.toInt() ?? 0,
      wireTauSec: (json['wireTauSec'] as num?)?.toDouble() ?? 0.0,
      wireKLoss: (json['wireKLoss'] as num?)?.toDouble() ?? 0.0,
      wireThermalC: (json['wireThermalC'] as num?)?.toDouble() ?? 0.0,
      wireKp: (json['wireKp'] as num?)?.toDouble() ?? 0.0,
      wireKi: (json['wireKi'] as num?)?.toDouble() ?? 0.0,
      floorKp: (json['floorKp'] as num?)?.toDouble() ?? 0.0,
      floorKi: (json['floorKi'] as num?)?.toDouble() ?? 0.0,
      ntcBeta: (json['ntcBeta'] as num?)?.toDouble() ?? 0.0,
      ntcR0: (json['ntcR0'] as num?)?.toDouble() ?? 0.0,
      ntcFixedRes: (json['ntcFixedRes'] as num?)?.toDouble() ?? 0.0,
      ntcPressMv: (json['ntcPressMv'] as num?)?.toDouble() ?? 0.0,
      ntcReleaseMv: (json['ntcReleaseMv'] as num?)?.toDouble() ?? 0.0,
      ntcDebounceMs: (json['ntcDebounceMs'] as num?)?.toInt() ?? 0,
      ntcMinC: (json['ntcMinC'] as num?)?.toDouble() ?? 0.0,
      ntcMaxC: (json['ntcMaxC'] as num?)?.toDouble() ?? 0.0,
      ntcSamples: (json['ntcSamples'] as num?)?.toInt() ?? 1,
      ntcGateIndex: (json['ntcGateIndex'] as num?)?.toInt() ?? 1,
      floorThicknessMm: (json['floorThicknessMm'] as num?)?.toDouble() ?? 0.0,
      floorMaterial: _str(json['floorMaterial'], 'wood'),
      floorMaterialCode: (json['floorMaterialCode'] as num?)?.toInt() ?? 0,
      floorMaxC: (json['floorMaxC'] as num?)?.toDouble() ?? 0.0,
      nichromeFinalTempC:
          (json['nichromeFinalTempC'] as num?)?.toDouble() ?? 0.0,
      loopMode: _str(json['loopMode'], 'advanced'),
      mixPreheatMs: (json['mixPreheatMs'] as num?)?.toInt() ?? 0,
      mixPreheatOnMs: (json['mixPreheatOnMs'] as num?)?.toInt() ?? 0,
      mixKeepMs: (json['mixKeepMs'] as num?)?.toInt() ?? 0,
      mixFrameMs: (json['mixFrameMs'] as num?)?.toInt() ?? 0,
      timingMode: _str(json['timingMode'], 'preset'),
      timingProfile: _str(json['timingProfile'], 'medium'),
      wireRes: _parseWireRes(json['wireRes']),
      targetRes: (json['targetRes'] as num?)?.toDouble() ?? 0.0,
    );
  }
}

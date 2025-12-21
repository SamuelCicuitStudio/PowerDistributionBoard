import 'package:flutter/material.dart';

import '../../../l10n/app_strings.dart';
import '../../../models/control_snapshot.dart';

class DeviceSettingsTab extends StatefulWidget {
  const DeviceSettingsTab({
    super.key,
    required this.controls,
    required this.busy,
    required this.onControl,
    required this.onControlBatchComplete,
  });

  final ControlSnapshot? controls;
  final bool busy;
  final Future<void> Function(String target, dynamic value) onControl;
  final Future<void> Function() onControlBatchComplete;

  @override
  State<DeviceSettingsTab> createState() => _DeviceSettingsTabState();
}

class _DeviceSettingsTabState extends State<DeviceSettingsTab> {
  final Map<String, TextEditingController> _c = {};

  TextEditingController _ctrl(String key) =>
      _c.putIfAbsent(key, () => TextEditingController());

  static const Map<String, Map<String, ({int onMs, int offMs})>>
      _timingPresets = {
    'sequential': {
      'hot': (onMs: 10, offMs: 5),
      'medium': (onMs: 10, offMs: 20),
      'gentle': (onMs: 10, offMs: 60),
    },
    'mixed': {
      'hot': (onMs: 10, offMs: 5),
      'medium': (onMs: 10, offMs: 20),
      'gentle': (onMs: 10, offMs: 60),
    },
    'advanced': {
      'hot': (onMs: 10, offMs: 15),
      'medium': (onMs: 10, offMs: 45),
      'gentle': (onMs: 10, offMs: 120),
    },
  };

  @override
  void dispose() {
    for (final v in _c.values) {
      v.dispose();
    }
    super.dispose();
  }

  @override
  void didUpdateWidget(covariant DeviceSettingsTab oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.controls != oldWidget.controls && widget.controls != null) {
      _seedFromSnapshot(widget.controls!, onlyEmpty: true);
    }
  }

  void _seedFromSnapshot(ControlSnapshot s, {required bool onlyEmpty}) {
    void set(String key, String value) {
      final ctrl = _ctrl(key);
      if (onlyEmpty && ctrl.text.isNotEmpty) return;
      ctrl.text = value;
    }

    set('acFrequency', s.acFrequency.toString());
    set('chargeResistor', s.chargeResistor.toStringAsFixed(3));
    set('currLimit', s.currLimit.toStringAsFixed(2));
    set('tempWarnC', s.tempWarnC.toStringAsFixed(1));
    set('tempTripC', s.tempTripC.toStringAsFixed(1));

    set('idleCurrentA', s.idleCurrentA.toStringAsFixed(2));
    set('wireTauSec', s.wireTauSec.toStringAsFixed(3));
    set('wireKLoss', s.wireKLoss.toStringAsFixed(3));
    set('wireThermalC', s.wireThermalC.toStringAsFixed(3));

    set('ntcBeta', s.ntcBeta.toStringAsFixed(1));
    set('ntcR0', s.ntcR0.toStringAsFixed(1));
    set('ntcFixedRes', s.ntcFixedRes.toStringAsFixed(1));
    set('ntcMinC', s.ntcMinC.toStringAsFixed(1));
    set('ntcMaxC', s.ntcMaxC.toStringAsFixed(1));
    set('ntcSamples', s.ntcSamples.toString());
    set('ntcGateIndex', s.ntcGateIndex.toString());
    set('ntcPressMv', s.ntcPressMv.toStringAsFixed(1));
    set('ntcReleaseMv', s.ntcReleaseMv.toStringAsFixed(1));
    set('ntcDebounceMs', s.ntcDebounceMs.toString());

    set('loopMode', s.loopMode);
    set('mixPreheatMs', s.mixPreheatMs.toString());
    set('mixPreheatOnMs', s.mixPreheatOnMs.toString());
    set('mixKeepMs', s.mixKeepMs.toString());
    set('mixFrameMs', s.mixFrameMs.toString());

    set('timingMode', s.timingMode);
    set('timingProfile', s.timingProfile);
    set('onTime', s.onTimeMs.toString());
    set('offTime', s.offTimeMs.toString());

    set('wireGauge', s.wireGauge.toString());
    set('floorThicknessMm', s.floorThicknessMm.toStringAsFixed(1));
    set('floorMaterial', s.floorMaterial);
    set('floorMaxC', s.floorMaxC.toStringAsFixed(1));
    set('nichromeFinalTempC', s.nichromeFinalTempC.toStringAsFixed(1));

    set('wireOhmPerM', s.wireOhmPerM.toStringAsFixed(4));
    set('targetRes', s.targetRes.toStringAsFixed(3));
    for (var i = 1; i <= 10; i++) {
      final r = s.wireRes[i] ?? 0.0;
      set('r${i.toString().padLeft(2, '0')}', r.toStringAsFixed(3));
    }
  }

  void _reset() {
    final s = widget.controls;
    if (s == null) return;
    _seedFromSnapshot(s, onlyEmpty: false);
    setState(() {});
  }

  int? _i(String key) => int.tryParse(_ctrl(key).text.trim());
  double? _d(String key) => double.tryParse(_ctrl(key).text.trim());
  String _s(String key) => _ctrl(key).text.trim();

  void _applyTimingPresetIfNeeded() {
    final timingMode = _s('timingMode');
    if (timingMode != 'preset') return;

    final loopMode = _s('loopMode');
    final loopKey =
        (loopMode == 'mixed' || loopMode == 'sequential') ? loopMode : 'advanced';
    final profile = _s('timingProfile').isEmpty ? 'medium' : _s('timingProfile');

    final preset = _timingPresets[loopKey]?[profile];
    if (preset == null) return;

    _ctrl('onTime').text = preset.onMs.toString();
    _ctrl('offTime').text = preset.offMs.toString();
  }

  String _resolvedTimingText(AppStrings strings) {
    final onMs = _i('onTime');
    final offMs = _i('offTime');
    if (onMs == null || offMs == null) return '--';
    return strings.t(
      'Ton {on} ms / Toff {off} ms',
      {'on': onMs.toString(), 'off': offMs.toString()},
    );
  }

  Future<void> _save() async {
    _applyTimingPresetIfNeeded();

    final cmds = <(String, dynamic)>[
      ('acFrequency', _i('acFrequency') ?? 50),
      ('chargeResistor', _d('chargeResistor') ?? 0.0),
      ('currLimit', _d('currLimit') ?? 0.0),
      ('tempWarnC', _d('tempWarnC') ?? 0.0),
      ('tempTripC', _d('tempTripC') ?? 0.0),
      ('idleCurrentA', _d('idleCurrentA') ?? 0.0),
      ('wireTauSec', _d('wireTauSec') ?? 0.0),
      ('wireKLoss', _d('wireKLoss') ?? 0.0),
      ('wireThermalC', _d('wireThermalC') ?? 0.0),
      ('ntcBeta', _d('ntcBeta') ?? 0.0),
      ('ntcR0', _d('ntcR0') ?? 0.0),
      ('ntcFixedRes', _d('ntcFixedRes') ?? 0.0),
      ('ntcMinC', _d('ntcMinC') ?? 0.0),
      ('ntcMaxC', _d('ntcMaxC') ?? 0.0),
      ('ntcSamples', _i('ntcSamples') ?? 0),
      ('ntcGateIndex', _i('ntcGateIndex') ?? 0),
      ('ntcPressMv', _d('ntcPressMv') ?? 0.0),
      ('ntcReleaseMv', _d('ntcReleaseMv') ?? 0.0),
      ('ntcDebounceMs', _i('ntcDebounceMs') ?? 0),
      ('loopMode', _s('loopMode')),
      ('mixPreheatMs', _i('mixPreheatMs') ?? 0),
      ('mixPreheatOnMs', _i('mixPreheatOnMs') ?? 0),
      ('mixKeepMs', _i('mixKeepMs') ?? 0),
      ('mixFrameMs', _i('mixFrameMs') ?? 0),
      ('timingMode', _s('timingMode')),
      ('timingProfile', _s('timingProfile')),
      ('onTime', _i('onTime') ?? 500),
      ('offTime', _i('offTime') ?? 500),
      ('wireGauge', _i('wireGauge') ?? 0),
      ('floorThicknessMm', _d('floorThicknessMm') ?? 0.0),
      ('floorMaterial', _s('floorMaterial')),
      ('floorMaxC', _d('floorMaxC') ?? 0.0),
      ('nichromeFinalTempC', _d('nichromeFinalTempC') ?? 0.0),
      ('wireOhmPerM', _d('wireOhmPerM') ?? 0.0),
      ('targetRes', _d('targetRes') ?? 0.0),
    ];

    for (var idx = 1; idx <= 10; idx++) {
      final key = 'r${idx.toString().padLeft(2, '0')}';
      final v = _d(key);
      if (v != null) {
        cmds.add(('wireRes$idx', v));
      }
    }

    for (final cmd in cmds) {
      await widget.onControl(cmd.$1, cmd.$2);
    }
    await widget.onControlBatchComplete();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final strings = context.strings;
    final s = widget.controls;
    if (s != null && _c.isEmpty) {
      _seedFromSnapshot(s, onlyEmpty: false);
    }

    final loopMode =
        _s('loopMode').isEmpty ? (s?.loopMode ?? 'advanced') : _s('loopMode');
    final timingMode =
        _s('timingMode').isEmpty ? (s?.timingMode ?? 'preset') : _s('timingMode');
    final profile = _s('timingProfile').isEmpty
        ? (s?.timingProfile ?? 'medium')
        : _s('timingProfile');
    final showMix = loopMode == 'mixed';
    final manualTiming = timingMode == 'manual';

    _applyTimingPresetIfNeeded();

    final left = Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          strings.t('Device Settings'),
          style: theme.textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w700),
        ),
        const SizedBox(height: 12),
        _groupTitle(theme, strings.t('Sampling & Power')),
        _numField(strings.t('Sampling Rate'), 'acFrequency', unit: 'Hz'),
        _numField(strings.t('Charge Resistor'), 'chargeResistor', unit: 'Ohm'),
        _numField(strings.t('Current Trip Limit'), 'currLimit', unit: 'A'),
        const SizedBox(height: 10),
        _groupTitle(theme, strings.t('Thermal Safety')),
        _numField(strings.t('Temp Warning'), 'tempWarnC', unit: 'C'),
        _numField(strings.t('Temp Trip (Shutdown)'), 'tempTripC', unit: 'C'),
        const SizedBox(height: 10),
        _groupTitle(theme, strings.t('Thermal Model')),
        _numField(strings.t('Idle Current Baseline'), 'idleCurrentA', unit: 'A'),
        _numField(strings.t('Wire Tau (time constant)'), 'wireTauSec', unit: 's'),
        _numField(strings.t('Heat Loss k'), 'wireKLoss', unit: 'W/K'),
        _numField(strings.t('Thermal Mass C'), 'wireThermalC', unit: 'J/K'),
        const SizedBox(height: 10),
        _groupTitle(theme, strings.t('NTC Input')),
        _numField(strings.t('NTC Beta'), 'ntcBeta', unit: 'K'),
        _numField(strings.t('NTC R0'), 'ntcR0', unit: 'Ohm'),
        _numField(strings.t('NTC Fixed Resistor'), 'ntcFixedRes', unit: 'Ohm'),
        _numField(strings.t('NTC Min Temp'), 'ntcMinC', unit: 'C'),
        _numField(strings.t('NTC Max Temp'), 'ntcMaxC', unit: 'C'),
        _numField(strings.t('NTC Samples'), 'ntcSamples'),
        _numField(strings.t('NTC Wire Index'), 'ntcGateIndex'),
        _numField(strings.t('Button Press Threshold'), 'ntcPressMv', unit: 'mV'),
        _numField(strings.t('Button Release Threshold'), 'ntcReleaseMv', unit: 'mV'),
        _numField(strings.t('Button Debounce'), 'ntcDebounceMs', unit: 'ms'),
        const SizedBox(height: 10),
        _groupTitle(theme, strings.t('Loop Timing')),
        if (manualTiming) ...[
          _numField(strings.t('ON Time'), 'onTime', unit: 'ms'),
          _numField(strings.t('OFF Time'), 'offTime', unit: 'ms'),
        ],
        _selectField(
          theme,
          label: strings.t('Loop Mode'),
          key: 'loopMode',
          value: loopMode,
          options: [
            ('advanced', strings.t('Advanced (grouped)')),
            ('sequential', strings.t('Sequential (coolest-first)')),
            ('mixed', strings.t('Mixed (preheat + keep-warm, sequential pulses)')),
          ],
          onChanged: (_) {
            _applyTimingPresetIfNeeded();
            setState(() {});
          },
        ),
        if (showMix) ...[
          _numField(strings.t('Mixed Preheat Duration'), 'mixPreheatMs', unit: 'ms'),
          _numField(strings.t('Preheat Pulse (primary)'), 'mixPreheatOnMs', unit: 'ms'),
          _numField(strings.t('Keep-warm Pulse (others)'), 'mixKeepMs', unit: 'ms'),
          _numField(strings.t('Frame Period'), 'mixFrameMs', unit: 'ms'),
        ],
        _selectField(
          theme,
          label: strings.t('Timing Settings'),
          key: 'timingMode',
          value: timingMode,
          options: [
            ('preset', strings.t('Normal (Hot / Medium / Gentle)')),
            ('manual', strings.t('Advanced (manual ON/OFF)')),
          ],
          onChanged: (_) {
            _applyTimingPresetIfNeeded();
            setState(() {});
          },
        ),
        if (!manualTiming) ...[
          _selectField(
            theme,
            label: strings.t('Heat Profile'),
            key: 'timingProfile',
            value: profile,
            options: [
              ('hot', strings.t('Hot / Fast')),
              ('medium', strings.t('Medium')),
              ('gentle', strings.t('Gentle / Cool')),
            ],
            onChanged: (_) {
              _applyTimingPresetIfNeeded();
              setState(() {});
            },
          ),
          _readonlyField(
            theme,
            label: strings.t('Resolved Timing'),
            value: _resolvedTimingText(strings),
          ),
        ],
        _numField(strings.t('Wire Gauge (AWG)'), 'wireGauge'),
      ],
    );

    final right = Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          strings.t('Nichrome Calibration'),
          style: theme.textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w700),
        ),
        const SizedBox(height: 12),
        _groupTitle(theme, strings.t('Floor Settings')),
        _numField(
          strings.t('Floor Thickness (20-50 mm)'),
          'floorThicknessMm',
          unit: 'mm',
        ),
        _numField(strings.t('Nichrome Final Temp'), 'nichromeFinalTempC', unit: 'C'),
        _selectField(
          theme,
          label: strings.t('Floor Material'),
          key: 'floorMaterial',
          value:
              _s('floorMaterial').isEmpty ? (s?.floorMaterial ?? 'wood') : _s('floorMaterial'),
          options: [
            ('wood', strings.t('Wood')),
            ('epoxy', strings.t('Epoxy')),
            ('concrete', strings.t('Concrete')),
            ('slate', strings.t('Slate')),
            ('marble', strings.t('Marble')),
            ('granite', strings.t('Granite')),
          ],
          onChanged: (_) => setState(() {}),
        ),
        _numField(strings.t('Max Floor Temp (<= 35 C)'), 'floorMaxC', unit: 'C'),
        const SizedBox(height: 10),
        _groupTitle(theme, strings.t('Nichrome Calibration')),
        LayoutBuilder(
          builder: (context, constraints) {
            final twoCol = constraints.maxWidth >= 420;
            final gap = 12.0;
            final colW = twoCol ? ((constraints.maxWidth - gap) / 2) : constraints.maxWidth;

            return Wrap(
              spacing: gap,
              runSpacing: 10,
              children: [
                SizedBox(
                  width: colW,
                  child: _numField(
                    strings.t('Wire Resistivity (Ohm/m)'),
                    'wireOhmPerM',
                  ),
                ),
                for (var i = 1; i <= 10; i++)
                  SizedBox(
                    width: colW,
                    child: _numField(
                      'R${i.toString().padLeft(2, '0')}',
                      'r${i.toString().padLeft(2, '0')}',
                      unit: 'Ohm',
                    ),
                  ),
                SizedBox(
                  width: constraints.maxWidth,
                  child: _numField(
                    strings.t('Target Resistance'),
                    'targetRes',
                    unit: 'Ohm',
                  ),
                ),
              ],
            );
          },
        ),
      ],
    );

    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 980),
        child: _zoomBox(
          theme,
          child: Column(
            children: [
              LayoutBuilder(
                builder: (context, constraints) {
                  final twoCol = constraints.maxWidth >= 860;
                  if (!twoCol) {
                    return Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        left,
                        const SizedBox(height: 18),
                        right,
                      ],
                    );
                  }
                  return Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Expanded(child: left),
                      const SizedBox(width: 18),
                      Expanded(child: right),
                    ],
                  );
                },
              ),
              const SizedBox(height: 14),
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  _roundButton(
                    theme,
                    tooltip: strings.t('Save'),
                    color: theme.colorScheme.primary,
                    icon: Icons.check,
                    onTap: widget.busy ? null : _save,
                  ),
                  const SizedBox(width: 14),
                  _roundButton(
                    theme,
                    tooltip: strings.t('Cancel'),
                    color: theme.colorScheme.outline,
                    icon: Icons.close,
                    onTap: () async {
                      _reset();
                    },
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _zoomBox(ThemeData theme, {required Widget child}) {
    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(235),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: theme.colorScheme.outline.withAlpha(179)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withAlpha(26),
            blurRadius: 18,
            offset: const Offset(0, 10),
          ),
        ],
      ),
      child: child,
    );
  }

  Widget _groupTitle(ThemeData theme, String title) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8, top: 10),
      child: Text(
        title,
        style: theme.textTheme.labelLarge?.copyWith(
          color: theme.colorScheme.onSurface.withAlpha(179),
          fontWeight: FontWeight.w700,
        ),
      ),
    );
  }

  Widget _numField(String label, String key, {String? unit}) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 10),
      child: TextField(
        controller: _ctrl(key),
        keyboardType: const TextInputType.numberWithOptions(decimal: true),
        decoration: InputDecoration(
          labelText: label,
          suffixText: unit,
          border: const OutlineInputBorder(),
        ),
      ),
    );
  }

  Widget _readonlyField(
    ThemeData theme, {
    required String label,
    required String value,
  }) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 10),
      child: InputDecorator(
        decoration: InputDecoration(
          labelText: label,
          border: const OutlineInputBorder(),
        ),
        child: Text(
          value,
          style: theme.textTheme.bodyMedium?.copyWith(
            color: theme.colorScheme.onSurface.withAlpha(179),
            fontWeight: FontWeight.w600,
          ),
        ),
      ),
    );
  }

  Widget _selectField(
    ThemeData theme, {
    required String label,
    required String key,
    required String value,
    required List<(String, String)> options,
    required ValueChanged<String> onChanged,
  }) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 10),
      child: InputDecorator(
        decoration: InputDecoration(
          labelText: label,
          border: const OutlineInputBorder(),
        ),
        child: DropdownButtonHideUnderline(
          child: DropdownButton<String>(
            value: options.any((o) => o.$1 == value) ? value : options.first.$1,
            isDense: true,
            isExpanded: true,
            items: [
              for (final o in options)
                DropdownMenuItem(value: o.$1, child: Text(o.$2)),
            ],
            onChanged: widget.busy
                ? null
                : (v) {
                    if (v == null) return;
                    _ctrl(key).text = v;
                    onChanged(v);
                  },
          ),
        ),
      ),
    );
  }

  Widget _roundButton(
    ThemeData theme, {
    required String tooltip,
    required Color color,
    required IconData icon,
    required Future<void> Function()? onTap,
  }) {
    return Tooltip(
      message: tooltip,
      child: InkWell(
        borderRadius: BorderRadius.circular(40),
        onTap: onTap == null
            ? null
            : () {
                onTap();
              },
        child: Container(
          width: 64,
          height: 64,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            color: theme.colorScheme.surface.withAlpha(102),
            border: Border.all(color: color.withAlpha(102)),
            boxShadow: [
              BoxShadow(color: color.withAlpha(38), blurRadius: 18),
            ],
          ),
          child: Icon(icon, color: color, size: 28),
        ),
      ),
    );
  }
}

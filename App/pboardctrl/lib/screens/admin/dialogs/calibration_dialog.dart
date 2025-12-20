import 'dart:async';
import 'dart:io';
import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../../../api/powerboard_api.dart';
import '../../../l10n/app_strings.dart';

class CalibrationDialog extends StatefulWidget {
  const CalibrationDialog({super.key, required this.api});

  final PowerBoardApi api;

  @override
  State<CalibrationDialog> createState() => _CalibrationDialogState();
}

class _CalibrationDialogState extends State<CalibrationDialog> {
  bool _loading = true;
  bool _paused = false;
  bool _historyMode = false;
  bool _showHelp = false;
  bool _busy = false;
  String? _error;

  Map<String, dynamic> _calibStatus = const {};
  List<_CalibSample> _samples = const [];
  Map<String, dynamic>? _wireTestStatus;

  List<_HistoryItem> _historyItems = const [];
  String? _historySelected;

  _CalibPiSuggestion? _piSuggest;
  String? _ntcCalibrateStatus;

  Timer? _timer;
  bool _refreshInFlight = false;

  final TextEditingController _ntcRef = TextEditingController();
  final TextEditingController _wireTestTarget =
      TextEditingController(text: '120');
  final TextEditingController _wireTestIndex = TextEditingController(text: '1');

  @override
  void initState() {
    super.initState();
    _refreshAll();
    _fetchPiSuggest();
    _timer = Timer.periodic(const Duration(seconds: 1), (_) => _refreshAll());
  }

  @override
  void dispose() {
    _timer?.cancel();
    _ntcRef.dispose();
    _wireTestTarget.dispose();
    _wireTestIndex.dispose();
    super.dispose();
  }

  void _toast(String msg) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  Future<void> _refreshAll() async {
    if (_refreshInFlight) return;
    _refreshInFlight = true;
    try {
      final status = await widget.api.getJson('/calib_status');
      final wire = await widget.api.getJsonOptional('/wire_test_status');

      if (!mounted) return;
      setState(() {
        _calibStatus = status;
        _wireTestStatus = wire;
        _loading = false;
        _error = null;
      });

      if (_paused || _historyMode) return;
      await _loadLatestSamples();
    } on TimeoutException {
      // Ignore transient timeout.
    } on SocketException {
      // Ignore transient network errors.
    } catch (err) {
      if (!mounted) return;
      setState(() {
        _loading = false;
        _error = err.toString();
      });
    } finally {
      _refreshInFlight = false;
    }
  }

  Future<void> _loadLatestSamples() async {
    try {
      final total = (_calibStatus['count'] is num)
          ? (_calibStatus['count'] as num).toInt()
          : 0;
      if (total <= 0) {
        if (mounted) setState(() => _samples = const []);
        return;
      }
      final offset = math.max(0, total - 200);
      final page =
          await widget.api.getJson('/calib_data?offset=$offset&count=200');
      final raw = page['samples'];
      final samples = <_CalibSample>[];
      if (raw is List) {
        for (final item in raw) {
          if (item is Map) {
            samples.add(_CalibSample.fromJson(item.cast<String, dynamic>()));
          }
        }
      }
      if (!mounted) return;
      setState(() => _samples = samples);
    } on TimeoutException {
      // Ignore transient timeout.
    } on SocketException {
      // Ignore transient network errors.
    }
  }

  Future<void> _start(String mode) async {
    if (_busy) return;
    setState(() => _busy = true);
    try {
      await widget.api.postJson(
        '/calib_start',
        {
          'mode': mode,
          'interval_ms': 500,
          'max_samples': 1200,
        },
        includeEpoch: true,
      );
      if (!mounted) return;
      setState(() => _historyMode = false);
      await _refreshAll();
      await _refreshHistoryList();
    } catch (err) {
      if (mounted) setState(() => _error = err.toString());
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _stop() async {
    if (_busy) return;
    setState(() => _busy = true);
    try {
      await widget.api.postJson('/calib_stop', const {}, includeEpoch: true);
      await _refreshAll();
      await _refreshHistoryList();
    } catch (err) {
      if (mounted) setState(() => _error = err.toString());
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _clear() async {
    if (_busy) return;
    setState(() => _busy = true);
    try {
      await widget.api.postJson('/calib_clear', const {});
      if (!mounted) return;
      setState(() {
        _samples = const [];
        _historyMode = false;
      });
      await _refreshAll();
      await _refreshHistoryList();
    } catch (err) {
      if (mounted) setState(() => _error = err.toString());
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _refreshHistoryList() async {
    try {
      final json = await widget.api.getJson('/calib_history_list');
      final items = <_HistoryItem>[];
      final raw = json['items'];
      if (raw is List) {
        for (final v in raw) {
          if (v is Map) {
            items.add(_HistoryItem.fromJson(v.cast<String, dynamic>()));
          }
        }
      }
      items.sort((a, b) => (b.startEpoch ?? 0).compareTo(a.startEpoch ?? 0));
      if (!mounted) return;
      setState(() {
        _historyItems = items;
        if (_historySelected == null && items.isNotEmpty) {
          _historySelected = items.first.name;
        }
      });
    } catch (_) {
      // Ignore.
    }
  }

  Future<_CalibPiSuggestion?> _fetchPiSuggest() async {
    try {
      final json = await widget.api.getJson('/calib_pi_suggest');
      final d = _CalibPiSuggestion.fromJson(json);
      if (!mounted) return d;
      setState(() => _piSuggest = d);
      return d;
    } catch (err) {
      _toast('PI suggest failed: $err');
      return null;
    }
  }

  Future<void> _persistPiSuggest(_CalibPiSuggestion d) async {
    final payload = d.toSavePayload();
    if (payload.isEmpty) {
      _toast('No valid model values to save.');
      return;
    }
    try {
      await widget.api.postJson('/calib_pi_save', payload);
      _toast('Model & PI values saved.');
      await _fetchPiSuggest();
    } catch (err) {
      _toast('Persist failed: $err');
    }
  }

  Future<void> _computeFromHistoryAndPersist() async {
    final name = _historySelected;
    if (name == null || name.isEmpty) {
      _toast('Select a saved history first.');
      return;
    }

    setState(() => _busy = true);
    try {
      final fallback = _piSuggest ?? await _fetchPiSuggest();
      final fb = fallback ?? _CalibPiSuggestion.empty();

      final json = await widget.api.getJson(
        '/calib_history_file?name=${Uri.encodeComponent(name)}',
      );
      final raw = json['samples'];
      final samples = <_CalibSample>[];
      if (raw is List) {
        for (final item in raw) {
          if (item is Map) {
            samples.add(_CalibSample.fromJson(item.cast<String, dynamic>()));
          }
        }
      }

      final computed = _computeModelFromSamples(samples, fb);
      if (computed == null) {
        _toast('Not enough data to compute model.');
        return;
      }

      if (!mounted) return;
      setState(() => _piSuggest = computed);
      await _persistPiSuggest(computed);
    } catch (err) {
      _toast('History model computation failed: $err');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _loadSelectedHistory() async {
    final name = _historySelected;
    if (name == null || name.isEmpty) {
      _toast('Select a saved history first.');
      return;
    }
    setState(() {
      _busy = true;
      _historyMode = true;
      _paused = true;
    });
    try {
      final json = await widget.api.getJson(
        '/calib_history_file?name=${Uri.encodeComponent(name)}',
      );
      final raw = json['samples'];
      final samples = <_CalibSample>[];
      if (raw is List) {
        for (final item in raw) {
          if (item is Map) {
            samples.add(_CalibSample.fromJson(item.cast<String, dynamic>()));
          }
        }
      }
      if (!mounted) return;
      setState(() => _samples = samples);
    } catch (err) {
      _toast('Failed to load saved history: $err');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _ntcCalibrate() async {
    if (_busy) return;
    setState(() {
      _busy = true;
      _ntcCalibrateStatus = 'Running...';
    });
    try {
      final payload = <String, dynamic>{};
      final ref = double.tryParse(_ntcRef.text.trim());
      if (ref != null) payload['ref_temp_c'] = ref;
      final res = await widget.api.postJson('/ntc_calibrate', payload);
      final r0 =
          (res['r0_ohm'] is num) ? (res['r0_ohm'] as num).toDouble() : null;
      final refC =
          (res['ref_c'] is num) ? (res['ref_c'] as num).toDouble() : null;
      if (!mounted) return;
      setState(() {
        _ntcCalibrateStatus = (r0 != null && refC != null)
            ? 'R0=${r0.toStringAsFixed(1)} ohm @ ${refC.toStringAsFixed(1)} C'
            : 'OK';
      });
      await _refreshAll();
    } catch (err) {
      if (!mounted) return;
      setState(() => _ntcCalibrateStatus = 'Failed');
      _toast('NTC calibrate failed: $err');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _wireTestStart() async {
    if (_busy) return;
    final target = double.tryParse(_wireTestTarget.text.trim());
    final idx = int.tryParse(_wireTestIndex.text.trim());
    if (target == null || !target.isFinite || target <= 0) {
      _toast('Enter a valid target temperature.');
      return;
    }
    setState(() => _busy = true);
    try {
      final payload = <String, dynamic>{'target_c': target};
      if (idx != null && idx > 0) payload['wire_index'] = idx;
      await widget.api.postJson('/wire_test_start', payload);
      await _refreshAll();
      _toast('Wire test started.');
    } catch (err) {
      _toast('Wire test start failed: $err');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _wireTestStop() async {
    if (_busy) return;
    setState(() => _busy = true);
    try {
      await widget.api.postJson('/wire_test_stop', const {});
      await _refreshAll();
    } catch (err) {
      _toast('Wire test stop failed: $err');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final strings = context.strings;
    final running = _calibStatus['running'] == true;
    final mode = _calibStatus['mode']?.toString() ?? '--';
    final count = _calibStatus['count'] ?? 0;
    final interval = _calibStatus['interval_ms'] ?? '--';
    final tempNow = _samples.isEmpty ? null : _samples.last.tempC;
    final timeNowMs = _samples.isEmpty ? null : _samples.last.tMs;

    final wireRunning = _wireTestStatus?['running'] == true;
    final wireTemp = (_wireTestStatus?['temp_c'] is num)
        ? (_wireTestStatus?['temp_c'] as num).toDouble()
        : null;
    final wireDuty = (_wireTestStatus?['duty'] is num)
        ? (_wireTestStatus?['duty'] as num).toDouble()
        : null;
    final wireOn = _wireTestStatus?['on_ms'];
    final wireOff = _wireTestStatus?['off_ms'];

    return AlertDialog(
      titlePadding: const EdgeInsets.fromLTRB(18, 14, 18, 10),
      contentPadding: const EdgeInsets.fromLTRB(18, 0, 18, 12),
      title: Row(
        children: [
          Text(strings.t('Calibration')),
          const Spacer(),
          IconButton(
            tooltip: strings.t('Help'),
            onPressed: () => setState(() => _showHelp = !_showHelp),
            icon: const Icon(Icons.help_outline),
          ),
        ],
      ),
      content: SizedBox(
        width: 980,
        child: _loading
            ? const Center(child: CircularProgressIndicator())
            : SingleChildScrollView(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    if (_error != null)
                      Padding(
                        padding: const EdgeInsets.only(bottom: 10),
                        child: Text(
                          _error!,
                          style: theme.textTheme.bodySmall
                              ?.copyWith(color: theme.colorScheme.error),
                        ),
                      ),
                    if (_showHelp) _helpCard(theme),
                    _actionBar(theme, running: running),
                    const SizedBox(height: 12),
                    _statusRow(
                      theme,
                      running: running,
                      mode: mode,
                      samples: count.toString(),
                      intervalMs: interval.toString(),
                      tempNow: tempNow,
                    ),
                    const SizedBox(height: 12),
                    _historyPicker(theme),
                    const SizedBox(height: 12),
                    _ntcCalibrateCard(theme),
                    const SizedBox(height: 12),
                    _piSuggestCard(theme),
                    const SizedBox(height: 12),
                    _chartCard(theme, tempNow: tempNow, timeNowMs: timeNowMs),
                    const SizedBox(height: 12),
                    _wireTestCard(
                      theme,
                      running: wireRunning,
                      tempC: wireTemp,
                      duty: wireDuty,
                      onMs: wireOn,
                      offMs: wireOff,
                    ),
                  ],
                ),
              ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: Text(strings.t('Close')),
        ),
      ],
    );
  }

  Widget _actionBar(ThemeData theme, {required bool running}) {
    final strings = context.strings;
    return Wrap(
      spacing: 10,
      runSpacing: 10,
      children: [
        FilledButton.tonal(
          onPressed: (_busy || running) ? null : () => _start('ntc'),
          child: Text(strings.t('NTC Calibration')),
        ),
        FilledButton.tonal(
          onPressed: (_busy || running) ? null : () => _start('model'),
          child: Text(strings.t('Temp Model Calibration')),
        ),
        FilledButton.tonal(
          onPressed: (_busy || !running) ? null : _stop,
          child: Text(strings.t('Stop')),
        ),
        OutlinedButton(
          onPressed: _busy
              ? null
              : () async {
                  final next = !_historyMode;
                  setState(() {
                    _historyMode = next;
                    if (next) _paused = true;
                  });
                  if (next) {
                    await _refreshHistoryList();
                  } else {
                    await _refreshAll();
                  }
                },
          child: Text(
            _historyMode ? strings.t('Live') : strings.t('Load History'),
          ),
        ),
        OutlinedButton(
          onPressed: _busy ? null : _clear,
          child: Text(strings.t('Clear')),
        ),
      ],
    );
  }

  Widget _statusRow(
    ThemeData theme, {
    required bool running,
    required String mode,
    required String samples,
    required String intervalMs,
    required double? tempNow,
  }) {
    final strings = context.strings;
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(38),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: Wrap(
        spacing: 14,
        runSpacing: 10,
        crossAxisAlignment: WrapCrossAlignment.center,
        children: [
          Text(
            strings.t(
              'Status: {state}',
              {
                'state': running ? strings.t('Running') : strings.t('Idle'),
              },
            ),
            style: theme.textTheme.titleSmall?.copyWith(
              fontWeight: FontWeight.w800,
            ),
          ),
          _pill(theme, strings.t('Mode'), mode),
          _pill(theme, strings.t('Samples'), samples),
          _pill(theme, strings.t('Interval'), '$intervalMs ms'),
          _pill(
            theme,
            strings.t('Temp'),
            tempNow == null ? '--' : '${tempNow.toStringAsFixed(1)} C',
          ),
        ],
      ),
    );
  }

  Widget _pill(ThemeData theme, String label, String value) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(102),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: theme.colorScheme.onSurface.withAlpha(26)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            '$label: ',
            style: theme.textTheme.bodySmall?.copyWith(
              color: theme.colorScheme.onSurface.withAlpha(179),
              fontWeight: FontWeight.w700,
            ),
          ),
          Text(
            value,
            style: theme.textTheme.bodySmall?.copyWith(
              fontWeight: FontWeight.w800,
            ),
          ),
        ],
      ),
    );
  }

  Widget _helpCard(ThemeData theme) {
    final strings = context.strings;
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(26),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            strings.t('Calibration help'),
            style: theme.textTheme.titleSmall?.copyWith(
              fontWeight: FontWeight.w800,
            ),
          ),
          const SizedBox(height: 8),
          Text(
            '1) NTC calibration: calibrates the NTC using a known reference temperature. Leave blank to use heatsink temp.',
            style: theme.textTheme.bodySmall,
          ),
          const SizedBox(height: 6),
          Text(
            '2) Temp model calibration: records temperature/time while the system drives a repeatable PWM profile; then suggests tau/k/C and PI gains.',
            style: theme.textTheme.bodySmall,
          ),
          const SizedBox(height: 6),
          Text(
            '3) Wire test: runs the PI controller to reach and hold a target temperature on the NTC-attached wire.',
            style: theme.textTheme.bodySmall,
          ),
        ],
      ),
    );
  }

  static String _formatMs(int ms) {
    final totalSeconds = ms ~/ 1000;
    final m = totalSeconds ~/ 60;
    final s = totalSeconds % 60;
    return '${m.toString().padLeft(2, '0')}:${s.toString().padLeft(2, '0')}';
  }

  static String _fmt(double? v) {
    if (v == null || !v.isFinite) return '--';
    return v.toStringAsFixed(3);
  }

  // Stubs (filled in next patches)
  Widget _historyPicker(ThemeData theme) {
    final strings = context.strings;
    final enabled = _historyMode && !_busy;
    return AnimatedOpacity(
      duration: const Duration(milliseconds: 160),
      opacity: _historyMode ? 1.0 : 0.35,
      child: IgnorePointer(
        ignoring: !_historyMode,
        child: Container(
          padding: const EdgeInsets.all(14),
          decoration: BoxDecoration(
            color: theme.colorScheme.surface.withAlpha(26),
            borderRadius: BorderRadius.circular(14),
            border: Border.all(color: theme.colorScheme.onSurface.withAlpha(20)),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                strings.t('Saved history'),
                style: theme.textTheme.titleSmall?.copyWith(
                  fontWeight: FontWeight.w800,
                ),
              ),
              const SizedBox(height: 10),
              Row(
                children: [
                  Expanded(
                    child: InputDecorator(
                      decoration: InputDecoration(
                        labelText: strings.t('Saved history'),
                        border: OutlineInputBorder(),
                      ),
                      child: DropdownButtonHideUnderline(
                        child: DropdownButton<String>(
                          isDense: true,
                          isExpanded: true,
                          value: _historySelected != null &&
                                  _historyItems.any((e) => e.name == _historySelected)
                              ? _historySelected
                              : (_historyItems.isEmpty ? null : _historyItems.first.name),
                          items: [
                            for (final h in _historyItems)
                              DropdownMenuItem(
                                value: h.name,
                                child: Text(h.displayLabel()),
                              ),
                          ],
                          onChanged: !enabled
                              ? null
                              : (v) => setState(() => _historySelected = v),
                        ),
                      ),
                    ),
                  ),
                  const SizedBox(width: 10),
                  OutlinedButton(
                    onPressed: enabled ? _loadSelectedHistory : null,
                    child: Text(strings.t('Load')),
                  ),
                  const SizedBox(width: 10),
                  OutlinedButton(
                    onPressed: enabled ? _refreshHistoryList : null,
                    child: Text(strings.t('Refresh')),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _ntcCalibrateCard(ThemeData theme) {
    final strings = context.strings;
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(26),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            strings.t('NTC calibrate'),
            style: theme.textTheme.titleSmall?.copyWith(
              fontWeight: FontWeight.w800,
            ),
          ),
          const SizedBox(height: 10),
          Row(
            children: [
              Expanded(
                child: TextField(
                  controller: _ntcRef,
                  decoration: InputDecoration(
                    labelText: strings.t('Reference temp (C, blank = heatsink)'),
                    border: OutlineInputBorder(),
                  ),
                  keyboardType:
                      const TextInputType.numberWithOptions(decimal: true),
                ),
              ),
              const SizedBox(width: 10),
              FilledButton(
                onPressed: _busy ? null : _ntcCalibrate,
                child: Text(strings.t('Calibrate NTC')),
              ),
            ],
          ),
          if (_ntcCalibrateStatus != null) ...[
            const SizedBox(height: 8),
            Text(
              _ntcCalibrateStatus!,
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.onSurface.withAlpha(179),
                fontWeight: FontWeight.w600,
              ),
            ),
          ],
        ],
      ),
    );
  }

  Widget _piSuggestCard(ThemeData theme) {
    final strings = context.strings;
    final d = _piSuggest;
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(26),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Text(
                strings.t('Temp model + PI'),
                style: theme.textTheme.titleSmall?.copyWith(
                  fontWeight: FontWeight.w800,
                ),
              ),
              const Spacer(),
              OutlinedButton(
                onPressed: _busy ? null : _fetchPiSuggest,
                child: Text(strings.t('Refresh')),
              ),
              const SizedBox(width: 10),
              OutlinedButton(
                onPressed:
                    (_busy || !_historyMode) ? null : _computeFromHistoryAndPersist,
                child: Text(strings.t('Suggest from History')),
              ),
              const SizedBox(width: 10),
              OutlinedButton(
                onPressed:
                    (_busy || d == null) ? null : () => _persistPiSuggest(d),
                child: Text(strings.t('Persist & Reload')),
              ),
            ],
          ),
          const SizedBox(height: 10),
          Wrap(
            spacing: 12,
            runSpacing: 12,
            children: [
              _statCard(theme, strings.t('Thermal model'), [
                ('Tau', d?.wireTau, 's'),
                ('k loss', d?.wireKLoss, 'W/K'),
                ('C', d?.wireC, 'J/K'),
                ('Max P', d?.maxPowerW, 'W'),
              ]),
              _statCard(
                theme,
                strings.t('Wire PI (suggested)'),
                [
                  ('Kp', d?.wireKpSuggest, ''),
                  ('Ki', d?.wireKiSuggest, ''),
                ],
                footer: d == null
                    ? '--'
                    : strings.t(
                        'Current: {kp} / {ki}',
                        {
                          'kp': _fmt(d.wireKpCurrent),
                          'ki': _fmt(d.wireKiCurrent),
                        },
                      ),
              ),
              _statCard(
                theme,
                strings.t('Floor PI (suggested)'),
                [
                  ('Kp', d?.floorKpSuggest, ''),
                  ('Ki', d?.floorKiSuggest, ''),
                ],
                footer: d == null
                    ? '--'
                    : strings.t(
                        'Current: {kp} / {ki}',
                        {
                          'kp': _fmt(d.floorKpCurrent),
                          'ki': _fmt(d.floorKiCurrent),
                        },
                      ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _statCard(
    ThemeData theme,
    String title,
    List<(String, double?, String)> rows, {
    String? footer,
  }) {
    return Container(
      width: 300,
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(38),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            title,
            style: theme.textTheme.labelLarge?.copyWith(
              fontWeight: FontWeight.w800,
            ),
          ),
          const SizedBox(height: 8),
          for (final r in rows)
            Padding(
              padding: const EdgeInsets.only(bottom: 6),
              child: Row(
                children: [
                  Expanded(
                    child: Text(
                      r.$1,
                      style: theme.textTheme.bodySmall?.copyWith(
                        color: theme.colorScheme.onSurface.withAlpha(179),
                      ),
                    ),
                  ),
                  Text(
                    r.$2 == null
                        ? '--'
                        : '${_fmt(r.$2)}${r.$3.isEmpty ? '' : ' ${r.$3}'}',
                    style: theme.textTheme.bodySmall?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                ],
              ),
            ),
          if (footer != null) ...[
            Divider(color: theme.colorScheme.onSurface.withAlpha(20)),
            Text(
              footer,
              style: theme.textTheme.bodySmall?.copyWith(
                fontWeight: FontWeight.w800,
              ),
            ),
          ],
        ],
      ),
    );
  }
  Widget _chartCard(
    ThemeData theme, {
    required double? tempNow,
    required int? timeNowMs,
  }) {
    final strings = context.strings;
    final t = timeNowMs == null ? '--:--' : _formatMs(timeNowMs);
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(26),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Text(
                strings.t('Temperature vs Time'),
                style: theme.textTheme.titleSmall?.copyWith(
                  fontWeight: FontWeight.w800,
                ),
              ),
              const Spacer(),
              OutlinedButton(
                onPressed: _busy
                    ? null
                    : () async {
                        setState(() {
                          _historyMode = false;
                          _paused = false;
                        });
                        await _refreshAll();
                      },
                child: Text(strings.t('Most recent')),
              ),
              const SizedBox(width: 10),
              OutlinedButton(
                onPressed: _busy ? null : () => setState(() => _paused = !_paused),
                child: Text(_paused ? strings.t('Resume') : strings.t('Pause')),
              ),
            ],
          ),
          const SizedBox(height: 10),
          Wrap(
            spacing: 10,
            runSpacing: 10,
            children: [
              _pill(
                theme,
                strings.t('Temp'),
                tempNow == null ? '--' : '${tempNow.toStringAsFixed(1)} C',
              ),
              _pill(theme, strings.t('Time'), t),
              _pill(theme, strings.t('Max'), '150 C'),
            ],
          ),
          const SizedBox(height: 10),
          SizedBox(
            height: 220,
            child: _CalibChart(samples: _samples),
          ),
        ],
      ),
    );
  }
  Widget _wireTestCard(
    ThemeData theme, {
    required bool running,
    required double? tempC,
    required double? duty,
    required dynamic onMs,
    required dynamic offMs,
  }) {
    final strings = context.strings;
    final onOff = (onMs != null && offMs != null) ? '$onMs / $offMs ms' : '--';
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(26),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            strings.t('Wire test'),
            style: theme.textTheme.titleSmall?.copyWith(
              fontWeight: FontWeight.w800,
            ),
          ),
          const SizedBox(height: 10),
          Wrap(
            spacing: 10,
            runSpacing: 10,
            children: [
              _pill(
                theme,
                strings.t('State'),
                running ? strings.t('Running') : strings.t('Idle'),
              ),
              _pill(
                theme,
                strings.t('Temp'),
                tempC == null ? '--' : '${tempC.toStringAsFixed(1)} C',
              ),
              _pill(
                theme,
                strings.t('Duty'),
                duty == null ? '--' : '${(duty * 100).round()}%',
              ),
              _pill(theme, strings.t('ON/OFF'), onOff),
            ],
          ),
          const SizedBox(height: 10),
          Row(
            children: [
              Expanded(
                child: TextField(
                  controller: _wireTestTarget,
                  decoration: InputDecoration(
                    labelText: strings.t('Target temp (C)'),
                    border: OutlineInputBorder(),
                  ),
                  keyboardType:
                      const TextInputType.numberWithOptions(decimal: true),
                ),
              ),
              const SizedBox(width: 10),
              SizedBox(
                width: 140,
                child: TextField(
                  controller: _wireTestIndex,
                  decoration: InputDecoration(
                    labelText: strings.t('Wire'),
                    border: OutlineInputBorder(),
                  ),
                  keyboardType: TextInputType.number,
                ),
              ),
              const SizedBox(width: 10),
              FilledButton(
                onPressed: (_busy || running) ? null : _wireTestStart,
                child: Text(strings.t('Start')),
              ),
              const SizedBox(width: 10),
              OutlinedButton(
                onPressed: (_busy || !running) ? null : _wireTestStop,
                child: Text(strings.t('Stop')),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _HistoryItem {
  const _HistoryItem({required this.name, this.startEpoch});

  final String name;
  final int? startEpoch;

  factory _HistoryItem.fromJson(Map<String, dynamic> json) {
    final epoch =
        (json['start_epoch'] is num) ? (json['start_epoch'] as num).toInt() : null;
    return _HistoryItem(name: json['name']?.toString() ?? '', startEpoch: epoch);
  }

  String displayLabel() {
    if (startEpoch == null) return name;
    final dt = DateTime.fromMillisecondsSinceEpoch(startEpoch! * 1000);
    final stamp =
        '${dt.year}-${dt.month.toString().padLeft(2, '0')}-${dt.day.toString().padLeft(2, '0')} '
        '${dt.hour.toString().padLeft(2, '0')}:${dt.minute.toString().padLeft(2, '0')}';
    return '$stamp - $name';
  }
}

class _CalibSample {
  const _CalibSample({
    required this.tMs,
    required this.tempC,
    required this.v,
    required this.i,
  });

  final int tMs;
  final double tempC;
  final double v;
  final double i;

  factory _CalibSample.fromJson(Map<String, dynamic> json) {
    final t = (json['t_ms'] is num) ? (json['t_ms'] as num).toInt() : 0;
    final temp =
        (json['temp_c'] is num) ? (json['temp_c'] as num).toDouble() : double.nan;
    final v = (json['v'] is num) ? (json['v'] as num).toDouble() : double.nan;
    final i = (json['i'] is num) ? (json['i'] as num).toDouble() : double.nan;
    return _CalibSample(tMs: t, tempC: temp, v: v, i: i);
  }
}

class _CalibPiSuggestion {
  const _CalibPiSuggestion({
    required this.wireTau,
    required this.wireKLoss,
    required this.wireC,
    required this.maxPowerW,
    required this.wireKpSuggest,
    required this.wireKiSuggest,
    required this.floorKpSuggest,
    required this.floorKiSuggest,
    required this.wireKpCurrent,
    required this.wireKiCurrent,
    required this.floorKpCurrent,
    required this.floorKiCurrent,
  });

  final double? wireTau;
  final double? wireKLoss;
  final double? wireC;
  final double? maxPowerW;
  final double? wireKpSuggest;
  final double? wireKiSuggest;
  final double? floorKpSuggest;
  final double? floorKiSuggest;
  final double? wireKpCurrent;
  final double? wireKiCurrent;
  final double? floorKpCurrent;
  final double? floorKiCurrent;

  factory _CalibPiSuggestion.empty() => const _CalibPiSuggestion(
        wireTau: null,
        wireKLoss: null,
        wireC: null,
        maxPowerW: null,
        wireKpSuggest: null,
        wireKiSuggest: null,
        floorKpSuggest: null,
        floorKiSuggest: null,
        wireKpCurrent: null,
        wireKiCurrent: null,
        floorKpCurrent: null,
        floorKiCurrent: null,
      );

  factory _CalibPiSuggestion.fromJson(Map<String, dynamic> json) {
    double? d(String k) => (json[k] is num) ? (json[k] as num).toDouble() : null;

    return _CalibPiSuggestion(
      wireTau: d('wire_tau'),
      wireKLoss: d('wire_k_loss'),
      wireC: d('wire_c'),
      maxPowerW: d('max_power_w'),
      wireKpSuggest: d('wire_kp_suggest'),
      wireKiSuggest: d('wire_ki_suggest'),
      floorKpSuggest: d('floor_kp_suggest'),
      floorKiSuggest: d('floor_ki_suggest'),
      wireKpCurrent: d('wire_kp_current'),
      wireKiCurrent: d('wire_ki_current'),
      floorKpCurrent: d('floor_kp_current'),
      floorKiCurrent: d('floor_ki_current'),
    );
  }

  Map<String, dynamic> toSavePayload() {
    final payload = <String, dynamic>{};
    if (wireTau != null && wireTau!.isFinite) payload['wire_tau'] = wireTau;
    if (wireKLoss != null && wireKLoss!.isFinite) payload['wire_k_loss'] = wireKLoss;
    if (wireC != null && wireC!.isFinite) payload['wire_c'] = wireC;
    if (wireKpSuggest != null && wireKpSuggest!.isFinite) {
      payload['wire_kp'] = wireKpSuggest;
    }
    if (wireKiSuggest != null && wireKiSuggest!.isFinite) {
      payload['wire_ki'] = wireKiSuggest;
    }
    if (floorKpSuggest != null && floorKpSuggest!.isFinite) {
      payload['floor_kp'] = floorKpSuggest;
    }
    if (floorKiSuggest != null && floorKiSuggest!.isFinite) {
      payload['floor_ki'] = floorKiSuggest;
    }
    return payload;
  }

  _CalibPiSuggestion copyWith({
    double? wireTau,
    double? wireKLoss,
    double? wireC,
    double? maxPowerW,
    double? wireKpSuggest,
    double? wireKiSuggest,
    double? floorKpSuggest,
    double? floorKiSuggest,
  }) {
    return _CalibPiSuggestion(
      wireTau: wireTau ?? this.wireTau,
      wireKLoss: wireKLoss ?? this.wireKLoss,
      wireC: wireC ?? this.wireC,
      maxPowerW: maxPowerW ?? this.maxPowerW,
      wireKpSuggest: wireKpSuggest ?? this.wireKpSuggest,
      wireKiSuggest: wireKiSuggest ?? this.wireKiSuggest,
      floorKpSuggest: floorKpSuggest ?? this.floorKpSuggest,
      floorKiSuggest: floorKiSuggest ?? this.floorKiSuggest,
      wireKpCurrent: wireKpCurrent,
      wireKiCurrent: wireKiCurrent,
      floorKpCurrent: floorKpCurrent,
      floorKiCurrent: floorKiCurrent,
    );
  }
}

_CalibPiSuggestion? _computeModelFromSamples(
  List<_CalibSample> samples,
  _CalibPiSuggestion fallback,
) {
  if (samples.length < 5) return null;

  final tms = <double>[];
  final temps = <double>[];
  final powers = <double>[];
  var maxPower = 0.0;

  for (final s in samples) {
    final t = s.tMs.toDouble();
    final temp = s.tempC;
    final v = s.v;
    final i = s.i;

    tms.add(t.isFinite ? t : double.nan);
    temps.add(temp.isFinite ? temp : double.nan);

    var p = double.nan;
    if (v.isFinite && i.isFinite) {
      p = v * i;
      if (p.isFinite && p > maxPower) maxPower = p;
    }
    powers.add(p);
  }

  if (!maxPower.isFinite || maxPower <= 0) {
    maxPower = (fallback.maxPowerW != null && fallback.maxPowerW!.isFinite)
        ? fallback.maxPowerW!
        : 0.0;
  }
  final threshold = math.max(5.0, maxPower * 0.2);

  var start = 0;
  while (start < powers.length &&
      !(powers[start].isFinite && powers[start] > threshold)) {
    start++;
  }
  if (start >= powers.length) start = 0;

  var end = -1;
  var lowCount = 0;
  for (var i = start + 1; i < powers.length; i++) {
    if (!powers[i].isFinite || powers[i] < threshold) {
      lowCount++;
      if (lowCount >= 3) {
        end = i - 2;
        break;
      }
    } else {
      lowCount = 0;
    }
  }

  var peakIndex = start;
  var peakTemp = double.negativeInfinity;
  for (var i = start; i < temps.length; i++) {
    final temp = temps[i];
    if (temp.isFinite && temp > peakTemp) {
      peakTemp = temp;
      peakIndex = i;
    }
    if (end >= start && i >= end) break;
  }
  if (!peakTemp.isFinite) return null;
  if (end < start || end > peakIndex) end = peakIndex;

  double avgLastTemps(List<double> values, int count) {
    var sum = 0.0;
    var used = 0;
    for (var i = values.length - 1; i >= 0 && used < count; i--) {
      final v = values[i];
      if (v.isFinite) {
        sum += v;
        used++;
      }
    }
    return used > 0 ? (sum / used) : double.nan;
  }

  final ambient = avgLastTemps(temps, 10);
  if (!ambient.isFinite) return null;

  final deltaT = peakTemp - ambient;
  if (!deltaT.isFinite || deltaT <= 1.0) return null;

  final tStartMs = tms[start].isFinite ? tms[start] : 0.0;
  final tPeakMs = tms[peakIndex].isFinite ? tms[peakIndex] : tStartMs;

  final t63 = ambient + 0.632 * deltaT;
  var t63Ms = double.nan;
  for (var i = start; i <= peakIndex; i++) {
    if (temps[i].isFinite && temps[i] >= t63 && tms[i].isFinite) {
      t63Ms = tms[i];
      break;
    }
  }

  var tauSec = double.nan;
  if (t63Ms.isFinite && t63Ms > tStartMs) {
    tauSec = (t63Ms - tStartMs) / 1000.0;
  } else if (tPeakMs > tStartMs) {
    tauSec = (tPeakMs - tStartMs) / 3000.0;
  }

  var pSum = 0.0;
  var pCount = 0;
  for (var i = start; i <= peakIndex; i++) {
    final p = powers[i];
    if (p.isFinite && p > 0) {
      pSum += p;
      pCount++;
    }
  }
  var pAvg = pCount > 0 ? pSum / pCount : double.nan;
  if (!pAvg.isFinite || pAvg <= 0) pAvg = maxPower;

  var kLoss = pAvg / deltaT;
  if (!kLoss.isFinite || kLoss <= 0) kLoss = double.nan;

  final thermalC =
      (kLoss.isFinite && tauSec.isFinite) ? (tauSec * kLoss) : double.nan;

  final kEff = (kLoss.isFinite && kLoss > 1e-6) ? kLoss : double.nan;
  final kWire = (kEff.isFinite && kEff > 0) ? (maxPower / kEff) : double.nan;

  var wireKpSuggest = double.nan;
  var wireKiSuggest = double.nan;
  var floorKpSuggest = double.nan;
  var floorKiSuggest = double.nan;

  if (kWire.isFinite && kWire > 0 && tauSec.isFinite && tauSec > 0) {
    final tcWire = tauSec * 3;
    wireKpSuggest = tauSec / (kWire * tcWire);
    wireKiSuggest = 1 / (kWire * tcWire);
  }

  if (tauSec.isFinite && tauSec > 0) {
    final tcFloor = tauSec * 9;
    floorKpSuggest = tauSec / tcFloor;
    floorKiSuggest = 1 / tcFloor;
  }

  double? pick(double v, double? fb) => v.isFinite ? v : fb;

  return fallback.copyWith(
    wireTau: pick(tauSec, fallback.wireTau),
    wireKLoss: pick(kLoss, fallback.wireKLoss),
    wireC: pick(thermalC, fallback.wireC),
    maxPowerW: pick(maxPower, fallback.maxPowerW),
    wireKpSuggest: pick(wireKpSuggest, fallback.wireKpSuggest),
    wireKiSuggest: pick(wireKiSuggest, fallback.wireKiSuggest),
    floorKpSuggest: pick(floorKpSuggest, fallback.floorKpSuggest),
    floorKiSuggest: pick(floorKiSuggest, fallback.floorKiSuggest),
  );
}

class _CalibChart extends StatelessWidget {
  const _CalibChart({required this.samples});

  final List<_CalibSample> samples;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Container(
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(20),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: CustomPaint(
        painter: _CalibChartPainter(
          samples: samples,
          gridColor: theme.colorScheme.onSurface.withAlpha(26),
          lineColor: theme.colorScheme.primary.withAlpha(204),
          textColor: theme.colorScheme.onSurface.withAlpha(153),
        ),
      ),
    );
  }
}

class _CalibChartPainter extends CustomPainter {
  _CalibChartPainter({
    required this.samples,
    required this.gridColor,
    required this.lineColor,
    required this.textColor,
  });

  final List<_CalibSample> samples;
  final Color gridColor;
  final Color lineColor;
  final Color textColor;

  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    const leftPad = 42.0;
    const topPad = 16.0;
    const rightPad = 12.0;
    const bottomPad = 20.0;
    final plot = Rect.fromLTWH(
      rect.left + leftPad,
      rect.top + topPad,
      rect.width - leftPad - rightPad,
      rect.height - topPad - bottomPad,
    );

    final gridPaint = Paint()
      ..color = gridColor
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1;

    canvas.drawRRect(
      RRect.fromRectAndRadius(rect, const Radius.circular(12)),
      gridPaint,
    );

    const yMax = 150.0;
    const yMin = 0.0;

    for (var i = 0; i <= 5; i++) {
      final y = plot.top + (plot.height * (i / 5.0));
      canvas.drawLine(Offset(plot.left, y), Offset(plot.right, y), gridPaint);
    }
    for (var i = 0; i <= 5; i++) {
      final x = plot.left + (plot.width * (i / 5.0));
      canvas.drawLine(Offset(x, plot.top), Offset(x, plot.bottom), gridPaint);
    }

    if (samples.length < 2) return;
    final xs = samples.map((s) => s.tMs.toDouble()).toList(growable: false);
    final ys = samples.map((s) => s.tempC).toList(growable: false);
    final xMin = xs.first;
    final xMax = xs.last <= xMin ? xMin + 1 : xs.last;

    Offset toPoint(double t, double temp) {
      final x = plot.left + ((t - xMin) / (xMax - xMin)) * plot.width;
      final clamped = temp.isFinite ? temp.clamp(yMin, yMax) : yMin;
      final y = plot.bottom - ((clamped - yMin) / (yMax - yMin)) * plot.height;
      return Offset(x, y);
    }

    final path = Path();
    final first = toPoint(xs.first, ys.first);
    path.moveTo(first.dx, first.dy);
    for (var i = 1; i < samples.length; i++) {
      final p = toPoint(xs[i], ys[i]);
      path.lineTo(p.dx, p.dy);
    }

    final linePaint = Paint()
      ..color = lineColor
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.0;

    canvas.drawPath(path, linePaint);

    final tp = TextPainter(
      textDirection: TextDirection.ltr,
      textAlign: TextAlign.left,
    );
    const ticks = [0, 30, 60, 90, 120, 150];
    for (final t in ticks) {
      final y = plot.bottom - ((t / yMax) * plot.height);
      tp.text = TextSpan(
        text: '$t',
        style: TextStyle(
          color: textColor,
          fontSize: 11,
          fontWeight: FontWeight.w600,
        ),
      );
      tp.layout(maxWidth: leftPad - 6);
      tp.paint(canvas, Offset(rect.left + 6, y - (tp.height / 2)));
    }
  }

  @override
  bool shouldRepaint(covariant _CalibChartPainter oldDelegate) {
    return oldDelegate.samples != samples ||
        oldDelegate.gridColor != gridColor ||
        oldDelegate.lineColor != lineColor ||
        oldDelegate.textColor != textColor;
  }
}

import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../../../l10n/app_strings.dart';
import '../../../models/monitor_snapshot.dart';

class LiveTab extends StatelessWidget {
  const LiveTab({
    super.key,
    required this.monitor,
    required this.onOpenHistory,
    required this.onOpenCalibration,
    required this.onOpenError,
    required this.onOpenLog,
  });

  final MonitorSnapshot? monitor;
  final VoidCallback onOpenHistory;
  final VoidCallback onOpenCalibration;
  final VoidCallback onOpenError;
  final VoidCallback onOpenLog;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final strings = context.strings;
    final m = monitor;
    final sessionStatus = m == null
        ? strings.t('No active session')
        : (m.sessionValid
            ? (m.sessionRunning
                ? strings.t('Running')
                : strings.t('Last session'))
            : strings.t('No active session'));

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            _legendChip(theme, strings.t('ON (energized)'), theme.colorScheme.primary),
            _legendChip(theme, strings.t('OFF (connected)'), theme.colorScheme.outline),
            _legendChip(
              theme,
              strings.t('No wire / Not connected'),
              const Color(0xFF00E5FF),
            ),
          ],
        ),
        const SizedBox(height: 14),
        Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 980),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Expanded(
                  flex: 3,
                  child: _liveStage(theme, m),
                ),
                const SizedBox(width: 14),
                Expanded(
                  flex: 2,
                  child: _zoomBox(
                    theme,
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Text(
                          strings.t('Current Session'),
                          style: theme.textTheme.titleMedium?.copyWith(
                            fontWeight: FontWeight.w700,
                          ),
                        ),
                        const SizedBox(height: 10),
                        Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 12,
                            vertical: 10,
                          ),
                          decoration: BoxDecoration(
                            color: theme.colorScheme.surface.withAlpha(38),
                            borderRadius: BorderRadius.circular(12),
                            border: Border.all(
                              color: theme.colorScheme.onSurface.withAlpha(20),
                            ),
                          ),
                          child: Text(
                            sessionStatus,
                            style: theme.textTheme.titleSmall?.copyWith(
                              fontWeight: FontWeight.w700,
                            ),
                          ),
                        ),
                        const SizedBox(height: 12),
                        _metricRow(theme, strings.t('Energy'), m?.sessionEnergyWh, 'Wh'),
                        _metricRow(theme, strings.t('Duration'), m?.sessionDurationS, 's'),
                        _metricRow(theme, strings.t('Peak Power'), m?.sessionPeakPowerW, 'W'),
                        _metricRow(
                          theme,
                          strings.t('Peak Current'),
                          m?.sessionPeakCurrentA,
                          'A',
                        ),
                        const SizedBox(height: 12),
                        Wrap(
                          spacing: 10,
                          runSpacing: 10,
                          children: [
                            OutlinedButton(
                              onPressed: onOpenHistory,
                              child: Text(strings.t('History')),
                            ),
                            OutlinedButton(
                              onPressed: onOpenCalibration,
                              child: Text(strings.t('Calibration')),
                            ),
                            OutlinedButton(
                              onPressed: onOpenError,
                              child: Text(strings.t('Error')),
                            ),
                            OutlinedButton(
                              onPressed: onOpenLog,
                              child: Text(strings.t('Log')),
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }

  Widget _zoomBox(ThemeData theme, {required Widget child}) {
    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(26),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: theme.colorScheme.onSurface.withAlpha(20)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withAlpha(51),
            blurRadius: 24,
          ),
        ],
      ),
      child: child,
    );
  }

  Widget _legendChip(ThemeData theme, String label, Color dotColor) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(38),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: dotColor.withAlpha(102)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: 10,
            height: 10,
            decoration: BoxDecoration(shape: BoxShape.circle, color: dotColor),
          ),
          const SizedBox(width: 8),
          Text(label),
        ],
      ),
    );
  }

  Widget _metricRow(ThemeData theme, String label, double? value, String unit) {
    final text = value == null ? '-- $unit' : '${value.toStringAsFixed(2)} $unit';
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Row(
        children: [
          Expanded(
            child: Text(
              label,
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.onSurface.withAlpha(153),
              ),
            ),
          ),
          Text(
            text,
            style: theme.textTheme.bodyMedium?.copyWith(
              fontWeight: FontWeight.w700,
            ),
          ),
        ],
      ),
    );
  }

  Widget _liveStage(ThemeData theme, MonitorSnapshot? m) {
    final outputs = m?.outputs ?? {};
    final wireTemps = m?.wireTemps ?? const [];

    return AspectRatio(
      aspectRatio: 1,
      child: Container(
        decoration: BoxDecoration(
          color: theme.colorScheme.surface.withAlpha(20),
          borderRadius: BorderRadius.circular(18),
          border: Border.all(color: theme.colorScheme.onSurface.withAlpha(20)),
        ),
        child: LayoutBuilder(
          builder: (context, constraints) {
            final size = math.min(constraints.maxWidth, constraints.maxHeight);
            final center = Offset(constraints.maxWidth / 2, constraints.maxHeight / 2);
            final radius = size * 0.40;

            List<Widget> dots = [];
            for (var i = 1; i <= 10; i++) {
              final angle = (-math.pi / 2) + (2 * math.pi) * ((i - 1) / 10.0);
              final pos = Offset(
                center.dx + radius * math.cos(angle),
                center.dy + radius * math.sin(angle),
              );
              final on = outputs[i] == true;
              final temp = (i - 1) < wireTemps.length ? wireTemps[i - 1] : null;
              dots.add(
                Positioned(
                  left: pos.dx - 18,
                  top: pos.dy - 18,
                  child: _liveDot(theme, index: i, on: on, temp: temp),
                ),
              );
            }

            return Stack(
              children: [
                Center(
                  child: Container(
                    width: size * 0.55,
                    height: size * 0.55,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: theme.colorScheme.surface.withAlpha(26),
                      border: Border.all(
                        color: theme.colorScheme.onSurface.withAlpha(26),
                      ),
                    ),
                  ),
                ),
                ...dots,
              ],
            );
          },
        ),
      ),
    );
  }

  Widget _liveDot(
    ThemeData theme, {
    required int index,
    required bool on,
    required double? temp,
  }) {
    final color = on ? theme.colorScheme.primary : theme.colorScheme.outline;
    final tempText = temp == null ? '--C' : '${temp.toStringAsFixed(0)}C';
    return Column(
      children: [
        Container(
          width: 14,
          height: 14,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            color: color,
            boxShadow: [
              BoxShadow(color: color.withAlpha(76), blurRadius: 10),
            ],
          ),
        ),
        const SizedBox(height: 4),
        Text(
          'O$index',
          style: theme.textTheme.labelSmall?.copyWith(
            fontWeight: FontWeight.w700,
            color: theme.colorScheme.onSurface.withAlpha(204),
          ),
        ),
        Text(
          tempText,
          style: theme.textTheme.labelSmall?.copyWith(
            color: theme.colorScheme.onSurface.withAlpha(153),
          ),
        ),
      ],
    );
  }
}

import 'package:flutter/material.dart';

import '../../../l10n/app_strings.dart';
import '../../../models/control_snapshot.dart';
import '../../../models/monitor_snapshot.dart';
import '../../../widgets/radial_gauge_card.dart';

class DashboardTab extends StatelessWidget {
  const DashboardTab({
    super.key,
    required this.monitor,
    required this.controls,
    required this.isMuted,
    required this.busy,
    required this.onControl,
  });

  final MonitorSnapshot? monitor;
  final ControlSnapshot? controls;
  final bool isMuted;
  final bool busy;
  final Future<void> Function(String target, dynamic value) onControl;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final strings = context.strings;
    final wide = MediaQuery.sizeOf(context).width >= 980;
    final m = monitor;
    final c = controls;

    final gauges = <Widget>[
      RadialGaugeCard(
        title: strings.t('Voltage'),
        valueText: m == null ? '--V' : '${m.capVoltage.toStringAsFixed(0)}V',
        subtitle: strings.t(
          'ADC raw (/100): {value}',
          {'value': m == null ? '--' : m.capAdcRaw.toStringAsFixed(2)},
        ),
        color: theme.colorScheme.tertiary,
        progress: m == null ? 0.0 : (m.capVoltage / 400.0),
        muted: isMuted,
      ),
      RadialGaugeCard(
        title: strings.t('Current'),
        valueText: m == null ? '--A' : '${m.current.toStringAsFixed(1)}A',
        color: theme.colorScheme.primary,
        progress: m == null ? 0.0 : (m.current / 100.0),
        muted: isMuted,
      ),
    ];

    final temps = m?.temperatures ?? const [];
    for (var i = 0; i < 12; i++) {
      final t = (i < temps.length) ? temps[i] : null;
      final label = i == 0
          ? strings.t('Board 01')
          : i == 1
              ? strings.t('Board 02')
              : i == 2
                  ? strings.t('Heatsink')
                  : strings.t('Temp {index}', {'index': '${i + 1}'});
      gauges.add(
        RadialGaugeCard(
          title: label,
          valueText: t == null ? strings.t('Off') : '${t.toStringAsFixed(0)}C',
          color: theme.colorScheme.error,
          progress: t == null ? 0.0 : (t / 150.0),
          muted: isMuted,
        ),
      );

      if (i == 2) {
        gauges.add(
          RadialGaugeCard(
            title: strings.t('Capacitance'),
            valueText: m == null
                ? '--mF'
                : '${(m.capacitanceF * 1000).toStringAsFixed(0)}mF',
            color: theme.colorScheme.secondary,
            progress: m == null ? 0.0 : (m.capacitanceF / 2.0),
            muted: isMuted,
          ),
        );
      }
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _modeBox(theme, c, strings),
        const SizedBox(height: 18),
        GridView.count(
          crossAxisCount: wide ? 3 : 2,
          shrinkWrap: true,
          physics: const NeverScrollableScrollPhysics(),
          crossAxisSpacing: 14,
          mainAxisSpacing: 14,
          childAspectRatio: wide ? 1.1 : 1.0,
          children: gauges,
        ),
      ],
    );
  }

  Widget _modeBox(ThemeData theme, ControlSnapshot? c, AppStrings strings) {
    final ready = monitor?.ready == true;
    final off = monitor?.off == true;
    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withAlpha(235),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: theme.colorScheme.outline.withAlpha(179)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withAlpha(18),
            blurRadius: 16,
            offset: const Offset(0, 10),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Wrap(
            spacing: 14,
            runSpacing: 10,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              Text(strings.t('Auto')),
              Switch(
                value: c?.manualMode == true,
                onChanged: busy ? null : (v) => onControl('mode', v),
              ),
              Text(strings.t('Manual')),
              const SizedBox(width: 10),
              Text(strings.t('LT')),
              Switch(
                value: c?.ledFeedback == true,
                onChanged: busy ? null : (v) => onControl('ledFeedback', v),
              ),
              const SizedBox(width: 10),
              Text(strings.t('Ready')),
              _ledDot(theme, ready),
              const SizedBox(width: 10),
              Text(strings.t('OFF')),
              _ledDot(theme, off, onColor: theme.colorScheme.error),
            ],
          ),
          const SizedBox(height: 10),
          FilledButton(
            onPressed: busy ? null : () => onControl('calibrate', true),
            child: Text(strings.t('Force Calibration')),
          ),
        ],
      ),
    );
  }

  Widget _ledDot(ThemeData theme, bool on, {Color? onColor}) {
    final c = on ? (onColor ?? theme.colorScheme.primary) : theme.colorScheme.outline;
    return Container(
      width: 14,
      height: 14,
      decoration: BoxDecoration(
        shape: BoxShape.circle,
        color: c,
        boxShadow: on ? [BoxShadow(color: c.withAlpha(102), blurRadius: 10)] : null,
      ),
    );
  }
}

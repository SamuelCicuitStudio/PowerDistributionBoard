import 'package:flutter/material.dart';

import '../../../l10n/app_strings.dart';
import '../../../models/control_snapshot.dart';
import '../../../models/monitor_snapshot.dart';

class ManualControlTab extends StatelessWidget {
  const ManualControlTab({
    super.key,
    required this.monitor,
    required this.controls,
    required this.busy,
    required this.fanSpeed,
    required this.onFanChanged,
    required this.onFanCommit,
    required this.onSetRelay,
    required this.onSetOutput,
  });

  final MonitorSnapshot? monitor;
  final ControlSnapshot? controls;
  final bool busy;
  final int fanSpeed;
  final ValueChanged<int> onFanChanged;
  final ValueChanged<int> onFanCommit;
  final Future<void> Function(bool value) onSetRelay;
  final Future<void> Function(int outputIndex, bool value) onSetOutput;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final strings = context.strings;
    final wide = MediaQuery.sizeOf(context).width >= 980;

    final relay = controls?.relay ?? false;
    final outputs = monitor?.outputs ?? controls?.outputs ?? {};

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Container(
          padding: const EdgeInsets.all(18),
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
              Text(strings.t('Fan')),
              SizedBox(
                width: 220,
                child: Slider(
                  value: fanSpeed.toDouble(),
                  min: 0,
                  max: 100,
                  divisions: 100,
                  label: '$fanSpeed%',
                  onChanged: busy
                      ? null
                      : (v) => onFanChanged(v.round()),
                  onChangeEnd:
                      busy ? null : (v) => onFanCommit(v.round()),
                ),
              ),
              Text(strings.t('Relay')),
              Switch(
                value: relay,
                onChanged: busy ? null : (v) => onSetRelay(v),
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        GridView.count(
          crossAxisCount: wide ? 5 : 2,
          shrinkWrap: true,
          physics: const NeverScrollableScrollPhysics(),
          crossAxisSpacing: 12,
          mainAxisSpacing: 12,
          childAspectRatio: 2.3,
          children: [
            for (var i = 1; i <= 10; i++)
              _outputTile(theme, strings, i, outputs[i] == true),
          ],
        ),
      ],
    );
  }

  Widget _outputTile(
    ThemeData theme,
    AppStrings strings,
    int index,
    bool value,
  ) {
    final accent = value ? theme.colorScheme.primary : theme.colorScheme.outline;
    return Card(
      elevation: 0,
      color: theme.colorScheme.surface.withAlpha(38),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(14),
        side: BorderSide(color: accent.withAlpha(102)),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 12),
        child: Row(
          children: [
            Container(
              width: 10,
              height: 10,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: value ? accent : accent.withAlpha(128),
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                strings.t('Output {index}', {'index': index.toString()}),
                style: theme.textTheme.labelLarge?.copyWith(
                  fontWeight: FontWeight.w600,
                ),
              ),
            ),
            Switch(
              value: value,
              onChanged: busy ? null : (v) => onSetOutput(index, v),
            ),
          ],
        ),
      ),
    );
  }
}

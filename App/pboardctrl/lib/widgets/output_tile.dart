import 'package:flutter/material.dart';

class OutputTile extends StatelessWidget {
  const OutputTile({
    super.key,
    required this.index,
    required this.isOn,
    required this.enabled,
    required this.onChanged,
  });

  final int index;
  final bool isOn;
  final bool enabled;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final accent = isOn ? theme.colorScheme.primary : theme.colorScheme.outline;
    return Card(
      elevation: 0,
      color: theme.colorScheme.surface,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(14),
        side: BorderSide(color: accent.withAlpha(102)),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        child: Row(
          children: [
            Container(
              width: 10,
              height: 10,
              decoration: BoxDecoration(
                color: isOn ? accent : accent.withAlpha(128),
                shape: BoxShape.circle,
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
                  child: Text(
                    'Output $index',
                    style: theme.textTheme.labelLarge?.copyWith(
                      color: enabled
                          ? theme.colorScheme.onSurface
                          : theme.colorScheme.onSurface.withAlpha(102),
                    ),
                  ),
            ),
            Switch(
              value: isOn,
              onChanged: enabled ? onChanged : null,
            ),
          ],
        ),
      ),
    );
  }
}

import 'package:flutter/material.dart';

import '../../../l10n/app_strings.dart';
import '../../../models/control_snapshot.dart';
import '../../../models/monitor_snapshot.dart';

class TopStatusBar extends StatelessWidget {
  const TopStatusBar({
    super.key,
    required this.monitor,
    required this.controls,
    required this.onOpenWarnings,
    required this.onOpenErrors,
    required this.onChangeConnection,
    required this.onDisconnect,
  });

  final MonitorSnapshot? monitor;
  final ControlSnapshot? controls;
  final VoidCallback onOpenWarnings;
  final VoidCallback onOpenErrors;
  final VoidCallback onChangeConnection;
  final VoidCallback onDisconnect;

  int _wifiBars(int? rssi) {
    if (rssi == null) return 0;
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final strings = context.strings;
    final language = context.language;
    final m = monitor;
    final manual = controls?.manualMode == true;
    final dotColor = manual ? const Color(0xFFFFA500) : theme.colorScheme.primary;
    final bars = _wifiBars(m?.wifiRssi);
    final wifiIcon = m == null
        ? Icons.wifi_off
        : m.wifiSta
            ? (m.wifiConnected ? Icons.wifi : Icons.wifi_off)
            : Icons.wifi_tethering;

    final statusItems = <Widget>[
      Container(
        width: 10,
        height: 10,
        decoration: BoxDecoration(
          shape: BoxShape.circle,
          color: dotColor,
          boxShadow: [
            BoxShadow(color: dotColor.withAlpha(76), blurRadius: 10),
          ],
        ),
      ),
      _chip(
        theme,
        icon: Icon(wifiIcon, size: 18, color: theme.colorScheme.primary),
        text: m == null
            ? strings.t('Wi-Fi')
            : m.wifiSta
                ? strings.t('Wi-Fi {bars}/4', {'bars': bars.toString()})
                : strings.t('AP'),
        tooltip: strings.t('Wi-Fi signal'),
      ),
      _chip(
        theme,
        icon: const Icon(Icons.thermostat, size: 18),
        text: m?.boardTemp == null ? '--C' : '${m!.boardTemp!.toStringAsFixed(0)}C',
        tooltip: strings.t('Board temperature'),
      ),
      _chip(
        theme,
        icon: const Icon(Icons.thermostat_outlined, size: 18),
        text: m?.heatsinkTemp == null ? '--C' : '${m!.heatsinkTemp!.toStringAsFixed(0)}C',
        tooltip: strings.t('Heatsink temperature'),
      ),
      _chip(
        theme,
        text: 'DC ${m == null ? '--' : m.capVoltage.toStringAsFixed(0)}V',
        tooltip: strings.t('DC bus voltage'),
      ),
      _chip(
        theme,
        text: 'I ${m == null ? '--' : m.current.toStringAsFixed(1)}A',
        tooltip: strings.t('DC current'),
      ),
    ];

    final actionItems = <Widget>[
      _eventPill(
        theme,
        color: const Color(0xFFFFC800),
        icon: Icons.warning_amber_rounded,
        count: m?.eventWarnUnread ?? 0,
        tooltip: strings.t('Warnings'),
        onTap: onOpenWarnings,
      ),
      _eventPill(
        theme,
        color: const Color(0xFFFF3B30),
        icon: Icons.error_outline,
        count: m?.eventErrorUnread ?? 0,
        tooltip: strings.t('Errors'),
        onTap: onOpenErrors,
      ),
      PopupMenuButton<String>(
        tooltip: strings.t('Menu'),
        onSelected: (value) {
          if (value == 'change_connection') onChangeConnection();
          if (value == 'lang_en') language.setLanguageCode('en');
          if (value == 'lang_it') language.setLanguageCode('it');
          if (value == 'disconnect') onDisconnect();
        },
        itemBuilder: (context) => [
          PopupMenuItem(
            value: 'change_connection',
            child: Text(strings.t('Change Connection')),
          ),
          const PopupMenuDivider(),
          CheckedPopupMenuItem(
            value: 'lang_en',
            checked: language.locale.languageCode == 'en',
            child: Text(strings.t('English')),
          ),
          CheckedPopupMenuItem(
            value: 'lang_it',
            checked: language.locale.languageCode == 'it',
            child: Text(strings.t('Italian')),
          ),
          const PopupMenuDivider(),
          PopupMenuItem(
            value: 'disconnect',
            child: Text(strings.t('Disconnect')),
          ),
        ],
        child: Container(
          width: 34,
          height: 34,
          decoration: BoxDecoration(
            color: theme.colorScheme.surface.withAlpha(102),
            shape: BoxShape.circle,
            border: Border.all(
              color: theme.colorScheme.onSurface.withAlpha(26),
            ),
          ),
          alignment: Alignment.center,
          child: Text(
            'A',
            style: theme.textTheme.titleMedium?.copyWith(
              fontWeight: FontWeight.w700,
            ),
          ),
        ),
      ),
    ];

    return Padding(
      padding: const EdgeInsets.fromLTRB(14, 8, 14, 8),
      child: LayoutBuilder(
        builder: (context, constraints) {
          final narrow = constraints.maxWidth < 980;
          if (narrow) {
            return Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  crossAxisAlignment: WrapCrossAlignment.center,
                  children: statusItems,
                ),
                const SizedBox(height: 8),
                Row(
                  mainAxisAlignment: MainAxisAlignment.end,
                  children: _withSpacing(actionItems, 8),
                ),
              ],
            );
          }

          return Row(
            children: [
              statusItems.first,
              const SizedBox(width: 10),
              ..._withSpacing(statusItems.sublist(1), 8),
              const Spacer(),
              ..._withSpacing(actionItems, 8),
            ],
          );
        },
      ),
    );
  }

  List<Widget> _withSpacing(List<Widget> items, double spacing) {
    if (items.isEmpty) return const [];
    return [
      for (var i = 0; i < items.length; i++) ...[
        if (i > 0) SizedBox(width: spacing),
        items[i],
      ],
    ];
  }

  Widget _chip(
    ThemeData theme, {
    Widget? icon,
    required String text,
    required String tooltip,
  }) {
    return Tooltip(
      message: tooltip,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
        decoration: BoxDecoration(
          color: theme.colorScheme.surface.withAlpha(102),
          borderRadius: BorderRadius.circular(16),
          border: Border.all(color: theme.colorScheme.onSurface.withAlpha(26)),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (icon != null) ...[
              IconTheme(
                data: IconThemeData(
                  color: theme.colorScheme.onSurface.withAlpha(204),
                ),
                child: icon,
              ),
              const SizedBox(width: 6),
            ],
            Text(
              text,
              style: theme.textTheme.labelLarge?.copyWith(
                fontWeight: FontWeight.w600,
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _eventPill(
    ThemeData theme, {
    required Color color,
    required IconData icon,
    required int count,
    required String tooltip,
    required VoidCallback onTap,
  }) {
    return Tooltip(
      message: tooltip,
      child: InkWell(
        borderRadius: BorderRadius.circular(18),
        onTap: onTap,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
          decoration: BoxDecoration(
            color: theme.colorScheme.surface.withAlpha(102),
            borderRadius: BorderRadius.circular(18),
            border: Border.all(color: color.withAlpha(76)),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(icon, size: 18, color: color),
              const SizedBox(width: 6),
              Text(
                count.toString(),
                style: theme.textTheme.labelLarge?.copyWith(
                  fontWeight: FontWeight.w700,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}


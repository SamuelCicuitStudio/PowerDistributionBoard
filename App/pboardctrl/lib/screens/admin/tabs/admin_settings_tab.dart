import 'package:flutter/material.dart';

import '../../../l10n/app_strings.dart';

class AdminSettingsTab extends StatelessWidget {
  const AdminSettingsTab({
    super.key,
    required this.busy,
    required this.currentPw,
    required this.newUsername,
    required this.newPassword,
    required this.wifiSsid,
    required this.wifiPassword,
    required this.onSave,
    required this.onReset,
  });

  final bool busy;
  final TextEditingController currentPw;
  final TextEditingController newUsername;
  final TextEditingController newPassword;
  final TextEditingController wifiSsid;
  final TextEditingController wifiPassword;
  final Future<void> Function() onSave;
  final VoidCallback onReset;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final strings = context.strings;
    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 460),
        child: _zoomBox(
          theme,
          child: Column(
            children: [
              TextField(
                controller: currentPw,
                obscureText: true,
                decoration: InputDecoration(
                  labelText: strings.t('Current Password'),
                  border: OutlineInputBorder(),
                ),
              ),
              const SizedBox(height: 10),
              TextField(
                controller: newUsername,
                decoration: InputDecoration(
                  labelText: strings.t('New Admin Username'),
                  border: OutlineInputBorder(),
                ),
              ),
              const SizedBox(height: 10),
              TextField(
                controller: newPassword,
                obscureText: true,
                decoration: InputDecoration(
                  labelText: strings.t('New Admin Password'),
                  border: OutlineInputBorder(),
                ),
              ),
              const SizedBox(height: 16),
              Divider(color: theme.colorScheme.onSurface.withAlpha(26)),
              const SizedBox(height: 16),
              TextField(
                controller: wifiSsid,
                decoration: InputDecoration(
                  labelText: strings.t('Wi-Fi SSID'),
                  border: OutlineInputBorder(),
                ),
              ),
              const SizedBox(height: 10),
              TextField(
                controller: wifiPassword,
                obscureText: true,
                decoration: InputDecoration(
                  labelText: strings.t('Wi-Fi Password'),
                  border: OutlineInputBorder(),
                ),
              ),
              const SizedBox(height: 12),
              _roundActions(
                theme,
                strings: strings,
                onSave: busy ? null : onSave,
                onCancel: onReset,
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

  Widget _roundActions(
    ThemeData theme, {
    required AppStrings strings,
    required Future<void> Function()? onSave,
    required VoidCallback onCancel,
  }) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        _roundButton(
          theme,
          tooltip: strings.t('Save'),
          color: theme.colorScheme.primary,
          icon: Icons.check,
          onTap: onSave,
        ),
        const SizedBox(width: 14),
        _roundButton(
          theme,
          tooltip: strings.t('Cancel'),
          color: theme.colorScheme.outline,
          icon: Icons.close,
          onTap: () async => onCancel(),
        ),
      ],
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

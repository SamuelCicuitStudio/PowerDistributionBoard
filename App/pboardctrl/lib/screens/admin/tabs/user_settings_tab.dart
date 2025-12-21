import 'package:flutter/material.dart';

import '../../../l10n/app_strings.dart';
import '../../../models/control_snapshot.dart';

class UserSettingsTab extends StatelessWidget {
  const UserSettingsTab({
    super.key,
    required this.controls,
    required this.busy,
    required this.currentPw,
    required this.newPw,
    required this.newDeviceId,
    required this.onSaveUser,
    required this.onResetUser,
    required this.onSetAccess,
  });

  final ControlSnapshot? controls;
  final bool busy;
  final TextEditingController currentPw;
  final TextEditingController newPw;
  final TextEditingController newDeviceId;
  final Future<void> Function() onSaveUser;
  final VoidCallback onResetUser;
  final Future<void> Function(int outputIndex, bool allowed) onSetAccess;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final strings = context.strings;
    final wide = MediaQuery.sizeOf(context).width >= 980;
    final access = controls?.outputAccess ?? {};

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 420),
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
                    controller: newPw,
                    obscureText: true,
                    decoration: InputDecoration(
                      labelText: strings.t('New Password'),
                      border: OutlineInputBorder(),
                    ),
                  ),
                  const SizedBox(height: 10),
                  TextField(
                    controller: newDeviceId,
                    decoration: InputDecoration(
                      labelText: strings.t('New Device ID'),
                      border: OutlineInputBorder(),
                    ),
                  ),
                  const SizedBox(height: 12),
                  _roundActions(
                    theme,
                    strings: strings,
                    onSave: busy ? null : onSaveUser,
                    onCancel: onResetUser,
                  ),
                ],
              ),
            ),
          ),
        ),
        const SizedBox(height: 18),
        Text(
          strings.t('Output Access'),
          style: theme.textTheme.titleMedium?.copyWith(
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(height: 10),
        GridView.count(
          crossAxisCount: wide ? 5 : 2,
          shrinkWrap: true,
          physics: const NeverScrollableScrollPhysics(),
          crossAxisSpacing: 12,
          mainAxisSpacing: 12,
          childAspectRatio: 2.4,
          children: [
            for (var i = 1; i <= 10; i++)
              _accessTile(
                theme,
                strings,
                i,
                access[i] == true,
              ),
          ],
        ),
      ],
    );
  }

  Widget _accessTile(
    ThemeData theme,
    AppStrings strings,
    int index,
    bool value,
  ) {
    return Card(
      elevation: 0,
      color: theme.colorScheme.surface.withAlpha(38),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(14),
        side: BorderSide(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 12),
        child: Row(
          children: [
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
              onChanged: busy ? null : (v) => onSetAccess(index, v),
            ),
          ],
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

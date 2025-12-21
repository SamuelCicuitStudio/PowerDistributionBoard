import 'package:flutter/material.dart';

import '../../../api/powerboard_api.dart';
import '../../../l10n/app_strings.dart';
import '../../../widgets/smooth_scroll_controller.dart';

class LastEventDialog extends StatefulWidget {
  const LastEventDialog({super.key, required this.api, this.focus});

  final PowerBoardApi api;
  final String? focus; // "warning" | "error"

  @override
  State<LastEventDialog> createState() => _LastEventDialogState();
}

class _LastEventDialogState extends State<LastEventDialog> {
  bool _loading = true;
  String? _error;
  Map<String, dynamic> _data = const {};
  final SmoothScrollController _scrollController = SmoothScrollController();

  @override
  void initState() {
    super.initState();
    _load();
  }

  @override
  void dispose() {
    _scrollController.dispose();
    super.dispose();
  }

  Future<void> _load() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final res = await widget.api.getJson('/last_event?mark_read=1');
      setState(() => _data = res);
    } catch (err) {
      setState(() => _error = err.toString());
    } finally {
      setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final strings = context.strings;
    final lastError =
        _data['last_error'] is Map ? _data['last_error'] as Map : const {};
    final lastStop =
        _data['last_stop'] is Map ? _data['last_stop'] as Map : const {};
    final warnings =
        _data['warnings'] is List ? _data['warnings'] as List : const [];
    final errors = _data['errors'] is List ? _data['errors'] as List : const [];

    Widget list(String title, List items) {
      final isErr = title.toLowerCase().contains('error');
      final color = isErr ? theme.colorScheme.error : theme.colorScheme.secondary;
      return Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            title,
            style: theme.textTheme.titleSmall?.copyWith(
              fontWeight: FontWeight.w700,
              color: color,
            ),
          ),
          const SizedBox(height: 6),
          if (items.isEmpty)
            Text(
              isErr
                  ? strings.t('No errors logged yet.')
                  : strings.t('No warnings logged yet.'),
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.onSurface.withAlpha(153),
              ),
            )
          else
            for (final item in items.take(30))
              Text(
                '- ${(item is Map ? item['reason'] : item) ?? '--'}',
                style: theme.textTheme.bodySmall,
              ),
        ],
      );
    }

    return AlertDialog(
      title: Text(strings.t('Error / Warning')),
      content: SizedBox(
        width: 820,
        child: _loading
            ? const Center(child: CircularProgressIndicator())
                : _error != null
                    ? Text(_error!)
                    : SingleChildScrollView(
                        controller: _scrollController,
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                          strings.t(
                            'State: {state}',
                            {'state': (_data['state'] ?? '--').toString()},
                          ),
                          style: theme.textTheme.titleMedium?.copyWith(
                            fontWeight: FontWeight.w700,
                          ),
                        ),
                        const SizedBox(height: 10),
                        Text(
                          strings.t(
                            'Last error: {reason}',
                            {'reason': (lastError['reason'] ?? '--').toString()},
                          ),
                        ),
                        Text(
                          strings.t(
                            'Last stop: {reason}',
                            {'reason': (lastStop['reason'] ?? '--').toString()},
                          ),
                        ),
                        const SizedBox(height: 14),
                        list(strings.t('Warnings'), warnings),
                        const SizedBox(height: 12),
                        list(strings.t('Errors'), errors),
                      ],
                    ),
                  ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: Text(strings.t('Close')),
        ),
        TextButton(onPressed: _load, child: Text(strings.t('Refresh'))),
      ],
    );
  }
}

import 'package:flutter/material.dart';

import '../../../api/powerboard_api.dart';
import '../../../l10n/app_strings.dart';
import '../../../widgets/smooth_scroll_controller.dart';

class DeviceLogDialog extends StatefulWidget {
  const DeviceLogDialog({super.key, required this.api});

  final PowerBoardApi api;

  @override
  State<DeviceLogDialog> createState() => _DeviceLogDialogState();
}

class _DeviceLogDialogState extends State<DeviceLogDialog> {
  bool _loading = true;
  String? _error;
  String _log = '';
  final SmoothScrollController _scrollController = SmoothScrollController();

  @override
  void dispose() {
    _scrollController.dispose();
    super.dispose();
  }

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final text = await widget.api.getText('/device_log');
      setState(() => _log = text);
    } catch (err) {
      setState(() => _error = err.toString());
    } finally {
      setState(() => _loading = false);
    }
  }

  Future<void> _clear() async {
    try {
      await widget.api.postJson('/device_log_clear', const {});
      await _load();
    } catch (err) {
      setState(() => _error = err.toString());
    }
  }

  @override
  Widget build(BuildContext context) {
    final strings = context.strings;
    return AlertDialog(
      title: Text(strings.t('Device Log')),
      content: SizedBox(
        width: 820,
        child: _loading
            ? const Center(child: CircularProgressIndicator())
                : _error != null
                    ? Text(_error!)
                    : SingleChildScrollView(
                        controller: _scrollController,
                        child: SelectableText(
                          _log.isEmpty ? strings.t('(empty)') : _log,
                        ),
                      ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: Text(strings.t('Close')),
        ),
        TextButton(onPressed: _load, child: Text(strings.t('Refresh'))),
        FilledButton(onPressed: _clear, child: Text(strings.t('Clear'))),
      ],
    );
  }
}

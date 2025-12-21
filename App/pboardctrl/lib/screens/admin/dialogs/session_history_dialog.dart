import 'package:flutter/material.dart';

import '../../../api/powerboard_api.dart';
import '../../../l10n/app_strings.dart';
import '../../../widgets/smooth_scroll_controller.dart';

class SessionHistoryDialog extends StatefulWidget {
  const SessionHistoryDialog({super.key, required this.api});

  final PowerBoardApi api;

  @override
  State<SessionHistoryDialog> createState() => _SessionHistoryDialogState();
}

class _SessionHistoryDialogState extends State<SessionHistoryDialog> {
  bool _loading = true;
  String? _error;
  List<Map<String, dynamic>> _rows = const [];
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
      final data = await widget.api.getJson('/History.json');
      final history = data['history'];
      final rows = <Map<String, dynamic>>[];
      if (history is List) {
        for (final item in history) {
          if (item is Map<String, dynamic>) rows.add(item);
          if (item is Map) {
            rows.add(item.map((k, v) => MapEntry(k.toString(), v)));
          }
        }
      }
      setState(() => _rows = rows);
    } catch (err) {
      setState(() => _error = err.toString());
    } finally {
      setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final strings = context.strings;
    return AlertDialog(
      title: Text(strings.t('Session History')),
      content: SizedBox(
        width: 720,
        child: _loading
            ? const Center(child: CircularProgressIndicator())
            : _error != null
                ? Text(_error!)
                : _rows.isEmpty
                    ? Text(strings.t('No sessions recorded yet.'))
                    : SingleChildScrollView(
                        controller: _scrollController,
                        child: DataTable(
                          columns: [
                            DataColumn(label: Text(strings.t('Start (ms)'))),
                            DataColumn(label: Text(strings.t('Duration (s)'))),
                            DataColumn(label: Text(strings.t('Energy (Wh)'))),
                            DataColumn(label: Text(strings.t('Peak P (W)'))),
                            DataColumn(label: Text(strings.t('Peak I (A)'))),
                          ],
                          rows: [
                            for (final r in _rows)
                              DataRow(
                                cells: [
                                  DataCell(Text('${r['start_ms'] ?? '--'}')),
                                  DataCell(Text('${r['duration_s'] ?? '--'}')),
                                  DataCell(Text('${r['energy_Wh'] ?? '--'}')),
                                  DataCell(Text('${r['peakPower_W'] ?? '--'}')),
                                  DataCell(Text('${r['peakCurrent_A'] ?? '--'}')),
                                ],
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
        TextButton(
          onPressed: _load,
          child: Text(strings.t('Refresh')),
        ),
      ],
    );
  }
}

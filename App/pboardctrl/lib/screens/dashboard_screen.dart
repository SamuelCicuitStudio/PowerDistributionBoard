import 'dart:async';

import 'package:flutter/material.dart';

import '../api/powerboard_api.dart';
import '../models/control_snapshot.dart';
import '../models/monitor_snapshot.dart';
import '../widgets/info_card.dart';
import '../widgets/output_tile.dart';
import 'login_screen.dart';

class DashboardScreen extends StatefulWidget {
  const DashboardScreen({
    super.key,
    required this.baseUrl,
    required this.role,
  });

  final String baseUrl;
  final UserRole role;

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  late final PowerBoardApi _api;
  MonitorSnapshot? _monitor;
  ControlSnapshot? _controls;
  String _stateLabel = 'Unknown';
  int _tabIndex = 0; // 0=Dashboard, 1=Manual
  String? _error;
  bool _loading = true;
  bool _monitorBusy = false;
  bool _statusBusy = false;
  bool _controlBusy = false;
  DateTime? _lastUpdate;
  Timer? _monitorTimer;
  Timer? _statusTimer;
  int _fanSpeed = 0;

  @override
  void initState() {
    super.initState();
    _api = PowerBoardApi(baseUrl: widget.baseUrl);
    _loadInitial();
    _startPolling();
  }

  @override
  void dispose() {
    _monitorTimer?.cancel();
    _statusTimer?.cancel();
    _api.dispose();
    super.dispose();
  }

  Future<void> _loadInitial() async {
    setState(() => _loading = true);
    try {
      await _refreshControls();
      await _refreshMonitor();
      await _refreshStatus();
    } finally {
      if (mounted) {
        setState(() => _loading = false);
      }
    }
  }

  void _startPolling() {
    _monitorTimer =
        Timer.periodic(const Duration(seconds: 1), (_) => _refreshMonitor());
    _statusTimer =
        Timer.periodic(const Duration(seconds: 2), (_) => _refreshStatus());
  }

  Future<void> _refreshMonitor() async {
    if (_monitorBusy) return;
    _monitorBusy = true;
    try {
      final json = await _api.getJsonOptional('/monitor');
      if (json == null) return;
      final monitor = MonitorSnapshot.fromJson(json);
      if (!mounted) return;
      setState(() {
        _monitor = monitor;
        _fanSpeed = monitor.fanSpeed;
        _lastUpdate = DateTime.now();
        _error = null;
      });
    } on UnauthorizedException {
      _handleUnauthorized();
    } catch (err) {
      if (mounted) {
        setState(() => _error = err.toString());
      }
    } finally {
      _monitorBusy = false;
    }
  }

  Future<void> _refreshControls() async {
    try {
      final json = await _api.getJsonOptional('/load_controls');
      if (json == null) return;
      final controls = ControlSnapshot.fromJson(json);
      if (!mounted) return;
      setState(() {
        _controls = controls;
        _fanSpeed = controls.fanSpeed;
        _error = null;
      });
      if (widget.role == UserRole.user &&
          controls.manualMode != true &&
          _tabIndex == 1) {
        if (mounted) {
          setState(() => _tabIndex = 0);
        }
      }
    } on UnauthorizedException {
      _handleUnauthorized();
    } catch (err) {
      if (mounted) {
        setState(() => _error = err.toString());
      }
    }
  }

  Future<void> _refreshStatus() async {
    if (_statusBusy) return;
    _statusBusy = true;
    try {
      final status = await _api.fetchStatus();
      if (status == null) return;
      if (!mounted) return;
      setState(() {
        _stateLabel = status;
        _error = null;
      });
    } on UnauthorizedException {
      _handleUnauthorized();
    } catch (err) {
      if (mounted) {
        setState(() => _error = err.toString());
      }
    } finally {
      _statusBusy = false;
    }
  }

  void _handleUnauthorized() {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Session expired. Please log in again.')),
    );
    Navigator.of(context).pushAndRemoveUntil(
      MaterialPageRoute(
        builder: (context) => LoginScreen(baseUrl: widget.baseUrl),
      ),
      (route) => false,
    );
  }

  Future<void> _sendControl(String target, dynamic value) async {
    if (_controlBusy) return;
    _controlBusy = true;
    try {
      final result = await _api.sendControlSet(target, value);
      if (!result.ok && mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text(result.error ?? 'Command failed.')),
        );
      }
      await _refreshControls();
      await _refreshMonitor();
    } on UnauthorizedException {
      _handleUnauthorized();
    } catch (err) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Command failed: $err')),
        );
      }
    } finally {
      _controlBusy = false;
    }
  }

  Future<void> _disconnect() async {
    try {
      await _api.disconnect();
    } catch (_) {
      // Best-effort disconnect; still return to login.
    }
    if (!mounted) return;
    Navigator.of(context).pushAndRemoveUntil(
      MaterialPageRoute(
        builder: (context) => LoginScreen(baseUrl: widget.baseUrl),
      ),
      (route) => false,
    );
  }

  String _formatNumber(double? value, {int decimals = 1}) {
    if (value == null) return '--';
    return value.toStringAsFixed(decimals);
  }

  String _formatTemp(double? value) {
    if (value == null) return '--';
    return '${value.toStringAsFixed(1)} C';
  }

  Color _stateColor(ThemeData theme) {
    switch (_stateLabel.toLowerCase()) {
      case 'running':
        return theme.colorScheme.primary;
      case 'error':
        return theme.colorScheme.error;
      case 'shutdown':
        return theme.colorScheme.outline;
      case 'idle':
        return theme.colorScheme.secondary;
      default:
        return theme.colorScheme.outline;
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final monitor = _monitor;
    final controls = _controls;
    final canShowAccess =
        widget.role == UserRole.user ? controls?.outputAccess : null;
    final outputs = monitor?.outputs ?? controls?.outputs ?? {};
    final showManualTab =
        widget.role != UserRole.user || (controls?.manualMode == true);

    final wide = MediaQuery.sizeOf(context).width >= 820;
    final destinations = <NavigationRailDestination>[
      const NavigationRailDestination(
        icon: Icon(Icons.dashboard_outlined),
        selectedIcon: Icon(Icons.dashboard),
        label: Text('Dashboard'),
      ),
      if (showManualTab)
        const NavigationRailDestination(
          icon: Icon(Icons.tune_outlined),
          selectedIcon: Icon(Icons.tune),
          label: Text('Manual'),
        ),
    ];

    final content = _buildTabContent(
      theme,
      monitor: monitor,
      controls: controls,
      outputs: outputs,
      access: canShowAccess,
      showManualTab: showManualTab,
    );

    return Scaffold(
      appBar: AppBar(
        title: Text(
          widget.role == UserRole.admin
              ? 'PowerBoard (Admin)'
              : widget.role == UserRole.user
                  ? 'PowerBoard (User)'
                  : 'PowerBoard',
        ),
        actions: [
          IconButton(
            tooltip: 'Refresh',
            onPressed: _loading ? null : _loadInitial,
            icon: const Icon(Icons.refresh),
          ),
          IconButton(
            tooltip: 'Disconnect',
            onPressed: _disconnect,
            icon: const Icon(Icons.logout),
          ),
          const SizedBox(width: 8),
        ],
      ),
      body: wide
          ? Row(
              children: [
                NavigationRail(
                  selectedIndex: _tabIndex,
                  onDestinationSelected: (value) {
                    if (value == 1 && !showManualTab) return;
                    setState(() => _tabIndex = value);
                  },
                  labelType: NavigationRailLabelType.all,
                  destinations: destinations,
                ),
                VerticalDivider(
                  width: 1,
                  thickness: 1,
                  color: theme.colorScheme.onSurface.withAlpha(20),
                ),
                Expanded(child: content),
              ],
            )
          : Stack(
              children: [
                content,
                Align(
                  alignment: Alignment.bottomCenter,
                  child: NavigationBar(
                    selectedIndex: _tabIndex,
                    onDestinationSelected: (value) {
                      if (value == 1 && !showManualTab) return;
                      setState(() => _tabIndex = value);
                    },
                    destinations: const [
                      NavigationDestination(
                        icon: Icon(Icons.dashboard_outlined),
                        selectedIcon: Icon(Icons.dashboard),
                        label: 'Dashboard',
                      ),
                      NavigationDestination(
                        icon: Icon(Icons.tune_outlined),
                        selectedIcon: Icon(Icons.tune),
                        label: 'Manual',
                      ),
                    ],
                  ),
                ),
              ],
            ),
    );
  }

  Widget _buildTabContent(
    ThemeData theme, {
    required MonitorSnapshot? monitor,
    required ControlSnapshot? controls,
    required Map<int, bool> outputs,
    required Map<int, bool>? access,
    required bool showManualTab,
  }) {
    final tab = showManualTab ? _tabIndex : 0;
    final bottomPad = MediaQuery.sizeOf(context).width < 820 ? 88.0 : 0.0;
    final child = tab == 1
        ? _buildManualTab(theme, monitor, controls, outputs, access)
        : _buildDashboardTab(theme, monitor, controls, outputs, access);

    return Stack(
      children: [
        ListView(
          padding: EdgeInsets.fromLTRB(20, 20, 20, 20 + bottomPad),
          children: [
            if (_error != null)
              Padding(
                padding: const EdgeInsets.only(bottom: 16),
                child: MaterialBanner(
                  content: Text(_error!),
                  leading: const Icon(Icons.warning_amber),
                  backgroundColor: theme.colorScheme.error.withAlpha(38),
                  actions: [
                    TextButton(
                      onPressed: () => setState(() => _error = null),
                      child: const Text('Dismiss'),
                    ),
                  ],
                ),
              ),
            child,
            const SizedBox(height: 18),
            if (_lastUpdate != null)
              Text(
                'Last update: ${_lastUpdate!.toLocal()}',
                style: theme.textTheme.bodySmall?.copyWith(
                  color: theme.colorScheme.onSurface.withAlpha(153),
                ),
              ),
          ],
        ),
        if (_loading)
          const Align(
            alignment: Alignment.topCenter,
            child: LinearProgressIndicator(minHeight: 3),
          ),
      ],
    );
  }

  Widget _buildDashboardTab(
    ThemeData theme,
    MonitorSnapshot? monitor,
    ControlSnapshot? controls,
    Map<int, bool> outputs,
    Map<int, bool>? access,
  ) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _buildStatusCard(theme),
        const SizedBox(height: 18),
        _buildControlsCard(theme, monitor, controls),
        const SizedBox(height: 18),
        _buildMetricsSection(theme, monitor),
      ],
    );
  }

  Widget _buildManualTab(
    ThemeData theme,
    MonitorSnapshot? monitor,
    ControlSnapshot? controls,
    Map<int, bool> outputs,
    Map<int, bool>? access,
  ) {
    final manualMode = controls?.manualMode == true;
    final isUser = widget.role == UserRole.user;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Card(
          elevation: 0,
          color: theme.colorScheme.surface,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(18),
            side: BorderSide(color: theme.colorScheme.onSurface.withAlpha(20)),
          ),
          child: Padding(
            padding: const EdgeInsets.all(18),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Manual Control',
                  style: theme.textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.w600,
                  ),
                ),
                const SizedBox(height: 8),
                Text(
                  manualMode
                      ? 'Toggle outputs directly.'
                      : 'Enable Manual mode on the Dashboard to use manual controls.',
                  style: theme.textTheme.bodyMedium?.copyWith(
                    color: theme.colorScheme.onSurface.withAlpha(179),
                  ),
                ),
                if (isUser && access != null && access.values.every((v) => v != true))
                  Padding(
                    padding: const EdgeInsets.only(top: 8),
                    child: Text(
                      'No outputs are authorized for the User role. Ask an Admin to enable access.',
                      style: theme.textTheme.bodySmall?.copyWith(
                        color: theme.colorScheme.onSurface.withAlpha(179),
                      ),
                    ),
                  ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 18),
        _buildManualControlsCard(theme, monitor, controls),
        const SizedBox(height: 18),
        _buildOutputsSection(theme, outputs, access, manualMode),
        const SizedBox(height: 18),
        _buildMetricsSection(theme, monitor),
      ],
    );
  }

  Widget _buildManualControlsCard(
    ThemeData theme,
    MonitorSnapshot? monitor,
    ControlSnapshot? controls,
  ) {
    final manualMode = controls?.manualMode ?? false;
    final relayOn = monitor?.relay ?? controls?.relay ?? false;

    return Card(
      elevation: 0,
      color: theme.colorScheme.surface,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(18),
        side: BorderSide(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: Padding(
        padding: const EdgeInsets.all(18),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Manual actions',
              style: theme.textTheme.titleMedium?.copyWith(
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(height: 8),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Manual mode'),
              value: manualMode,
              onChanged: (value) => _sendControl('mode', value),
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Relay'),
              value: relayOn,
              onChanged: manualMode ? (value) => _sendControl('relay', value) : null,
            ),
            const SizedBox(height: 8),
            Text(
              'Fan speed: $_fanSpeed%',
              style: theme.textTheme.labelLarge,
            ),
            Slider(
              value: _fanSpeed.toDouble(),
              min: 0,
              max: 100,
              divisions: 100,
              label: '$_fanSpeed%',
              onChanged: manualMode
                  ? (value) => setState(() => _fanSpeed = value.round())
                  : null,
              onChangeEnd: manualMode
                  ? (value) => _sendControl('fanSpeed', value.round())
                  : null,
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildStatusCard(ThemeData theme) {
    final monitor = _monitor;
    final wifiLabel = monitor == null
        ? '--'
        : monitor.wifiSta
            ? (monitor.wifiConnected
                ? 'STA ${monitor.wifiRssi ?? '--'} dBm'
                : 'STA (offline)')
            : 'AP';

    return Card(
      elevation: 0,
      color: theme.colorScheme.surface,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(18),
        side: BorderSide(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: Padding(
        padding: const EdgeInsets.all(18),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Status',
              style: theme.textTheme.titleMedium?.copyWith(
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(height: 10),
            Wrap(
              spacing: 10,
              runSpacing: 10,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                Chip(
                  label: Text(_stateLabel),
                  backgroundColor: _stateColor(theme).withAlpha(38),
                  labelStyle: TextStyle(color: _stateColor(theme)),
                ),
                _buildFlagChip('Ready', _monitor?.ready == true),
                _buildFlagChip('Off', _monitor?.off == true),
                _buildFlagChip('Relay', _monitor?.relay == true),
                _buildFlagChip('AC', _monitor?.ac == true),
                Chip(label: Text(wifiLabel)),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildFlagChip(String label, bool active) {
    final theme = Theme.of(context);
    final color = active ? theme.colorScheme.primary : theme.colorScheme.outline;
    return Chip(
      label: Text(label),
      backgroundColor: color.withAlpha(38),
      labelStyle: TextStyle(color: color),
    );
  }

  Widget _buildControlsCard(
    ThemeData theme,
    MonitorSnapshot? monitor,
    ControlSnapshot? controls,
  ) {
    final manualMode = controls?.manualMode ?? false;
    final ledFeedback = controls?.ledFeedback ?? false;
    final buzzerMute = controls?.buzzerMute ?? false;
    final relayOn = monitor?.relay ?? controls?.relay ?? false;
    final running = _stateLabel.toLowerCase() == 'running';

    return Card(
      elevation: 0,
      color: theme.colorScheme.surface,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(18),
        side: BorderSide(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: Padding(
        padding: const EdgeInsets.all(18),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Controls',
              style: theme.textTheme.titleMedium?.copyWith(
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(height: 8),
            Wrap(
              spacing: 12,
              runSpacing: 12,
              children: [
                FilledButton(
                  onPressed: running ? null : () => _sendControl('systemStart', true),
                  child: const Text('Start'),
                ),
                OutlinedButton(
                  onPressed:
                      running ? () => _sendControl('systemShutdown', true) : null,
                  child: const Text('Stop'),
                ),
                OutlinedButton(
                  onPressed: () => _sendControl('reboot', true),
                  child: const Text('Reboot'),
                ),
              ],
            ),
            const SizedBox(height: 8),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Manual mode'),
              value: manualMode,
              onChanged: (value) => _sendControl('mode', value),
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Relay'),
              value: relayOn,
              onChanged: (value) => _sendControl('relay', value),
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('LED feedback'),
              value: ledFeedback,
              onChanged: (value) => _sendControl('ledFeedback', value),
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Buzzer mute'),
              value: buzzerMute,
              onChanged: (value) => _sendControl('buzzerMute', value),
            ),
            const SizedBox(height: 8),
            Text(
              'Fan speed: $_fanSpeed%',
              style: theme.textTheme.labelLarge,
            ),
            Slider(
              value: _fanSpeed.toDouble(),
              min: 0,
              max: 100,
              divisions: 100,
              label: '$_fanSpeed%',
              onChanged: (value) {
                setState(() => _fanSpeed = value.round());
              },
              onChangeEnd: (value) {
                _sendControl('fanSpeed', value.round());
              },
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildOutputsSection(
    ThemeData theme,
    Map<int, bool> outputs,
    Map<int, bool>? access,
    bool allowToggle,
  ) {
    final entries = <MapEntry<int, bool>>[];
    for (var i = 1; i <= 10; i++) {
      final allowed = access == null ? true : (access[i] ?? false);
      if (widget.role == UserRole.user && !allowed) continue;
      entries.add(MapEntry(i, outputs[i] == true));
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          'Outputs',
          style: theme.textTheme.titleMedium?.copyWith(
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(height: 8),
        LayoutBuilder(
          builder: (context, constraints) {
            int columns = 2;
            if (constraints.maxWidth > 900) {
              columns = 5;
            } else if (constraints.maxWidth > 620) {
              columns = 3;
            }
            return GridView.builder(
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                crossAxisCount: columns,
                crossAxisSpacing: 12,
                mainAxisSpacing: 12,
                childAspectRatio: 2.2,
              ),
              itemCount: entries.length,
              itemBuilder: (context, index) {
                final entry = entries[index];
                return OutputTile(
                  index: entry.key,
                  isOn: entry.value,
                  enabled: allowToggle && !_controlBusy,
                  onChanged: (value) =>
                      _sendControl('output${entry.key}', value),
                );
              },
            );
          },
        ),
      ],
    );
  }

  Widget _buildMetricsSection(ThemeData theme, MonitorSnapshot? monitor) {
    final temps = monitor?.temperatures ?? const [];
    final tempCards = <Widget>[
      InfoCard(
        title: 'Voltage',
        value: '${_formatNumber(monitor?.capVoltage, decimals: 1)} V',
        icon: Icons.bolt,
      ),
      InfoCard(
        title: 'Current',
        value: '${_formatNumber(monitor?.current, decimals: 1)} A',
        icon: Icons.flash_on,
      ),
      InfoCard(
        title: 'Board temp',
        value: _formatTemp(monitor?.boardTemp),
        icon: Icons.thermostat,
      ),
      InfoCard(
        title: 'Heatsink temp',
        value: _formatTemp(monitor?.heatsinkTemp),
        icon: Icons.thermostat_outlined,
      ),
    ];

    final extraTemps = <Widget>[];
    for (var i = 0; i < temps.length && i < 4; i++) {
      extraTemps.add(
        InfoCard(
          title: 'Temp ${i + 1}',
          value: _formatTemp(temps[i]),
          icon: Icons.sensors,
        ),
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          'Live metrics',
          style: theme.textTheme.titleMedium?.copyWith(
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(height: 8),
        LayoutBuilder(
          builder: (context, constraints) {
            int columns = 2;
            if (constraints.maxWidth > 900) {
              columns = 4;
            } else if (constraints.maxWidth > 620) {
              columns = 3;
            }
            return GridView.count(
              crossAxisCount: columns,
              crossAxisSpacing: 12,
              mainAxisSpacing: 12,
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              childAspectRatio: 2.3,
              children: [
                ...tempCards,
                ...extraTemps,
              ],
            );
          },
        ),
      ],
    );
  }
}

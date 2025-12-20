import 'dart:async';
import 'dart:io';
import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../api/powerboard_api.dart';
import '../l10n/app_strings.dart';
import '../models/control_snapshot.dart';
import '../models/monitor_snapshot.dart';
import 'admin/dialogs/calibration_dialog.dart';
import 'admin/dialogs/device_log_dialog.dart';
import 'admin/dialogs/last_event_dialog.dart';
import 'admin/dialogs/session_history_dialog.dart';
import 'admin/tabs/admin_settings_tab.dart';
import 'admin/tabs/dashboard_tab.dart';
import 'admin/tabs/device_settings_tab.dart';
import 'admin/tabs/live_tab.dart';
import 'admin/tabs/manual_control_tab.dart';
import 'admin/tabs/user_settings_tab.dart';
import 'admin/widgets/top_status_bar.dart';
import 'connection_screen.dart';
import 'login_screen.dart';

enum AdminTab {
  dashboard('Dashboard', Icons.dashboard_outlined),
  userSettings('User Settings', Icons.group_outlined),
  manualControl('Manual Control', Icons.tune_outlined),
  adminSettings('Admin Settings', Icons.admin_panel_settings_outlined),
  deviceSettings('Device Settings', Icons.settings_outlined),
  live('Live', Icons.bubble_chart_outlined);

  const AdminTab(this.labelKey, this.icon);
  final String labelKey;
  final IconData icon;
}

class AdminScreen extends StatefulWidget {
  const AdminScreen({super.key, required this.baseUrl});

  final String baseUrl;

  @override
  State<AdminScreen> createState() => _AdminScreenState();
}

class _AdminScreenState extends State<AdminScreen> with TickerProviderStateMixin {
  late final PowerBoardApi _api;
  AdminTab _tab = AdminTab.dashboard;

  MonitorSnapshot? _monitor;
  ControlSnapshot? _controls;
  String _stateLabel = 'Unknown';
  String? _error;
  bool _loading = true;
  bool _busy = false;

  Timer? _monitorTimer;
  Timer? _statusTimer;
  Timer? _controlsTimer;
  bool _monitorInFlight = false;
  bool _statusInFlight = false;
  bool _controlsInFlight = false;

  // User settings
  final TextEditingController _userCurrentPw = TextEditingController();
  final TextEditingController _userNewPw = TextEditingController();
  final TextEditingController _userNewId = TextEditingController();

  // Admin settings
  final TextEditingController _adminCurrentPw = TextEditingController();
  final TextEditingController _adminNewUser = TextEditingController();
  final TextEditingController _adminNewPw = TextEditingController();
  final TextEditingController _wifiSsid = TextEditingController();
  final TextEditingController _wifiPw = TextEditingController();

  // Manual controls
  int _fanSpeed = 0;

  late final AnimationController _idlePulse;
  late final AnimationController _errorBlink;

  @override
  void initState() {
    super.initState();
    _api = PowerBoardApi(baseUrl: widget.baseUrl);
    _idlePulse = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1500),
    )..repeat();
    _errorBlink = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 350),
    )..repeat();
    _loadInitial();
    _startPolling();
  }

  @override
  void dispose() {
    _monitorTimer?.cancel();
    _statusTimer?.cancel();
    _controlsTimer?.cancel();
    _api.dispose();
    _idlePulse.dispose();
    _errorBlink.dispose();
    _userCurrentPw.dispose();
    _userNewPw.dispose();
    _userNewId.dispose();
    _adminCurrentPw.dispose();
    _adminNewUser.dispose();
    _adminNewPw.dispose();
    _wifiSsid.dispose();
    _wifiPw.dispose();
    super.dispose();
  }

  void _startPolling() {
    _monitorTimer = Timer.periodic(
      const Duration(seconds: 1),
      (_) => _refreshMonitor(),
    );
    _statusTimer = Timer.periodic(
      const Duration(seconds: 2),
      (_) => _refreshStatus(),
    );
    _controlsTimer = Timer.periodic(
      const Duration(seconds: 3),
      (_) => _refreshControls(),
    );
  }

  Future<void> _loadInitial() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      await _refreshControls();
      await _refreshMonitor();
      await _refreshStatus();
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  Future<void> _refreshMonitor() async {
    if (_monitorInFlight) return;
    _monitorInFlight = true;
    try {
      final json = await _api.getJsonOptional('/monitor');
      if (json == null) return;
      final monitor = MonitorSnapshot.fromJson(json);
      if (!mounted) return;
      setState(() {
        _monitor = monitor;
        _fanSpeed = monitor.fanSpeed;
        _error = null;
      });
    } on UnauthorizedException {
      _handleUnauthorized();
    } on TimeoutException {
      // Ignore transient timeout.
    } on SocketException {
      // Ignore transient network errors.
    } catch (err) {
      if (mounted) setState(() => _error = err.toString());
    } finally {
      _monitorInFlight = false;
    }
  }

  Future<void> _refreshControls() async {
    if (_controlsInFlight) return;
    _controlsInFlight = true;
    try {
      final json = await _api.getJsonOptional('/load_controls');
      if (json == null) return;
      final controls = ControlSnapshot.fromJson(json);
      if (!mounted) return;
      setState(() {
        _controls = controls;
        _fanSpeed = controls.fanSpeed;
        if (_wifiSsid.text.isEmpty) _wifiSsid.text = controls.wifiSSID;
        _error = null;
      });
    } on UnauthorizedException {
      _handleUnauthorized();
    } on TimeoutException {
      // Ignore transient timeout.
    } on SocketException {
      // Ignore transient network errors.
    } catch (err) {
      if (mounted) setState(() => _error = err.toString());
    } finally {
      _controlsInFlight = false;
    }
  }

  Future<void> _refreshStatus() async {
    if (_statusInFlight) return;
    _statusInFlight = true;
    try {
      final s = await _api.fetchStatus();
      if (s == null) return;
      if (!mounted) return;
      setState(() {
        _stateLabel = s;
        _error = null;
      });
    } on UnauthorizedException {
      _handleUnauthorized();
    } on TimeoutException {
      // Ignore transient timeout.
    } on SocketException {
      // Ignore transient network errors.
    } catch (err) {
      if (mounted) setState(() => _error = err.toString());
    } finally {
      _statusInFlight = false;
    }
  }

  void _handleUnauthorized() {
    if (!mounted) return;
    Navigator.of(context).pushAndRemoveUntil(
      MaterialPageRoute(builder: (context) => LoginScreen(baseUrl: widget.baseUrl)),
      (route) => false,
    );
  }

  bool get _isMuted => _controls?.buzzerMute == true;

  String _powerLabel(AppStrings strings) {
    final state = _stateLabel;
    if (state == 'Shutdown') return strings.t('OFF');
    if (state == 'Idle') return strings.t('IDLE');
    if (state == 'Error') return strings.t('ERROR');
    if (state == 'Running') {
      final ready = _monitor?.ready == true;
      return ready ? strings.t('READY') : strings.t('RUN');
    }
    return state.toUpperCase();
  }

  Future<void> _sendControl(String target, dynamic value) async {
    final strings = context.strings;
    if (_busy) return;
    setState(() => _busy = true);
    try {
      final res = await _api.sendControlSet(target, value);
      if (!res.ok && mounted) {
        _toast(res.error ?? strings.t('Command failed'));
      }
      await _refreshControls();
      await _refreshMonitor();
    } on UnauthorizedException {
      _handleUnauthorized();
    } catch (err) {
      if (mounted) {
        _toast(strings.t('Command failed: {err}', {'err': err.toString()}));
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  void _toast(String msg) {
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  Future<void> _disconnect() async {
    try {
      await _api.disconnect();
    } catch (_) {}
    if (!mounted) return;
    Navigator.of(context).pushAndRemoveUntil(
      MaterialPageRoute(builder: (context) => LoginScreen(baseUrl: widget.baseUrl)),
      (route) => false,
    );
  }

  Future<void> _openConnectionPicker() async {
    try {
      await _api.disconnect();
    } catch (_) {}
    if (!mounted) return;
    Navigator.of(context).pushAndRemoveUntil(
      MaterialPageRoute(builder: (context) => const ConnectionScreen()),
      (route) => false,
    );
  }

  Future<void> _confirmReset() async {
    final strings = context.strings;
    final ok = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: Text(strings.t('Reset device?')),
        content: Text(strings.t('This performs a full system reset.')),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: Text(strings.t('Cancel')),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: Text(strings.t('Reset')),
          ),
        ],
      ),
    );
    if (ok == true) {
      await _sendControl('systemReset', true);
    }
  }

  Future<void> _openHistoryDialog() async {
    await showDialog<void>(
      context: context,
      builder: (context) => SessionHistoryDialog(api: _api),
    );
  }

  Future<void> _openLogDialog() async {
    await showDialog<void>(
      context: context,
      builder: (context) => DeviceLogDialog(api: _api),
    );
  }

  Future<void> _openErrorDialog({String? focus}) async {
    await showDialog<void>(
      context: context,
      builder: (context) => LastEventDialog(api: _api, focus: focus),
    );
  }

  Future<void> _openCalibrationDialog() async {
    await showDialog<void>(
      context: context,
      builder: (context) => CalibrationDialog(api: _api),
    );
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final strings = context.strings;
    return Scaffold(
      backgroundColor: theme.scaffoldBackgroundColor,
      body: SafeArea(
        child: Stack(
          children: [
            Column(
              children: [
                TopStatusBar(
                  monitor: _monitor,
                  controls: _controls,
                  onOpenWarnings: () {
                    _openErrorDialog(focus: 'warning');
                  },
                  onOpenErrors: () {
                    _openErrorDialog(focus: 'error');
                  },
                  onChangeConnection: () {
                    _openConnectionPicker();
                  },
                  onDisconnect: () {
                    _disconnect();
                  },
                ),
                Expanded(
                  child: Row(
                    children: [
                      _buildSidebar(theme),
                      Expanded(
                        child: Padding(
                          padding: const EdgeInsets.fromLTRB(18, 12, 18, 12),
                          child: Container(
                            decoration: BoxDecoration(
                              color: theme.colorScheme.surface.withAlpha(16),
                              borderRadius: BorderRadius.circular(18),
                              border: Border.all(
                                color: theme.colorScheme.onSurface.withAlpha(20),
                              ),
                            ),
                            child: ClipRRect(
                              borderRadius: BorderRadius.circular(18),
                              child: Scrollbar(
                                thumbVisibility: true,
                                child: ListView(
                                  padding: const EdgeInsets.all(18),
                                  children: [
                                    if (_error != null)
                                      Padding(
                                        padding: const EdgeInsets.only(bottom: 12),
                                        child: MaterialBanner(
                                          content: Text(_error!),
                                          leading: const Icon(Icons.warning_amber),
                                          backgroundColor:
                                              theme.colorScheme.error.withAlpha(38),
                                          actions: [
                                            TextButton(
                                              onPressed: () =>
                                                  setState(() => _error = null),
                                              child: Text(strings.t('Dismiss')),
                                            ),
                                          ],
                                        ),
                                      ),
                                    _buildTab(theme),
                                  ],
                                ),
                              ),
                            ),
                          ),
                        ),
                      ),
                    ],
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
        ),
      ),
    );
  }

  Widget _buildSidebar(ThemeData theme) {
    return Container(
      width: 185,
      decoration: BoxDecoration(
        gradient: RadialGradient(
          center: Alignment.topCenter,
          radius: 1.4,
          colors: [
            theme.colorScheme.primary.withAlpha(18),
            theme.scaffoldBackgroundColor.withAlpha(250),
          ],
        ),
        border: Border(right: BorderSide(color: theme.colorScheme.primary.withAlpha(31))),
      ),
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(12),
        child: Column(
          children: [
            for (final t in AdminTab.values) ...[
              _tabButton(theme, t),
              const SizedBox(height: 8),
            ],
            const SizedBox(height: 6),
            _sidebarControls(theme),
          ],
        ),
      ),
    );
  }

  Widget _tabButton(ThemeData theme, AdminTab tab) {
    final strings = context.strings;
    final active = _tab == tab;
    final accent = theme.colorScheme.primary;
    return InkWell(
      borderRadius: BorderRadius.circular(14),
      onTap: () => setState(() => _tab = tab),
      child: Container(
        width: double.infinity,
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
        margin: const EdgeInsets.symmetric(vertical: 4),
        decoration: BoxDecoration(
          gradient: RadialGradient(
            center: Alignment.topLeft,
            radius: 1.2,
            colors: [
              accent.withAlpha(active ? 41 : 15),
              theme.scaffoldBackgroundColor.withAlpha(active ? 250 : 230),
            ],
          ),
          borderRadius: BorderRadius.circular(14),
          border: Border.all(
            color: active
                ? accent.withAlpha(107)
                : theme.colorScheme.onSurface.withAlpha(13),
          ),
          boxShadow: active
              ? [
                  BoxShadow(
                    color: accent.withAlpha(102),
                    blurRadius: 20,
                  ),
                ]
              : null,
        ),
        child: Stack(
          children: [
            Align(
              alignment: Alignment.centerLeft,
              child: Text(
                strings.t(tab.labelKey),
                style: theme.textTheme.titleSmall?.copyWith(
                  fontWeight: FontWeight.w600,
                  color: active ? accent : theme.colorScheme.onSurface,
                ),
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
            ),
            if (active)
              Positioned(
                left: 10,
                right: 10,
                bottom: 4,
                child: Container(
                  height: 2,
                  decoration: BoxDecoration(
                    borderRadius: BorderRadius.circular(2),
                    gradient: RadialGradient(
                      radius: 2,
                      colors: [
                        accent.withAlpha(230),
                        accent.withAlpha(41),
                      ],
                    ),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }

  Widget _sidebarControls(ThemeData theme) {
    final strings = context.strings;
    final state = _stateLabel;
    final running = state == 'Running' || state == 'Error';
    final color = switch (state) {
      'Shutdown' => const Color(0xFFFF3B3B),
      'Idle' => const Color(0xFFFFB000),
      'Running' => theme.colorScheme.primary,
      'Error' => const Color(0xFFFF3B3B),
      _ => theme.colorScheme.primary,
    };

    return Column(
      children: [
        _roundIconButton(
          theme,
          tooltip: _isMuted ? strings.t('Unmute') : strings.t('Mute'),
          color: _isMuted ? theme.colorScheme.error : theme.colorScheme.primary,
          icon: _isMuted ? Icons.volume_off : Icons.volume_up,
          onTap: () {
            _sendControl('buzzerMute', !_isMuted);
          },
        ),
        const SizedBox(height: 14),
        _powerButton(
          theme,
          label: _powerLabel(strings),
          color: color,
          running: running,
          tooltip: strings.t('Power'),
        ),
        const SizedBox(height: 14),
        _roundIconButton(
          theme,
          tooltip: strings.t('Reboot'),
          color: theme.colorScheme.primary,
          icon: Icons.refresh,
          onTap: () {
            _sendControl('reboot', true);
          },
        ),
        const SizedBox(height: 14),
        _roundIconButton(
          theme,
          tooltip: strings.t('Reset'),
          color: theme.colorScheme.primary,
          icon: Icons.restart_alt,
          onTap: () {
            _confirmReset();
          },
        ),
      ],
    );
  }

  Widget _roundIconButton(
    ThemeData theme, {
    required String tooltip,
    required Color color,
    required IconData icon,
    required VoidCallback onTap,
  }) {
    return Tooltip(
      message: tooltip,
      child: InkWell(
        borderRadius: BorderRadius.circular(40),
        onTap: _busy ? null : onTap,
        child: Container(
          width: 56,
          height: 56,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            gradient: RadialGradient(
              center: Alignment.topCenter,
              radius: 1.2,
              colors: [
                theme.colorScheme.primary.withAlpha(20),
                const Color(0xFF050709).withAlpha(250),
              ],
            ),
            border: Border.all(color: color.withAlpha(41), width: 2),
            boxShadow: [
              BoxShadow(color: theme.colorScheme.primary.withAlpha(64), blurRadius: 12),
            ],
          ),
          child: Icon(icon, color: color, size: 28),
        ),
      ),
    );
  }

  Widget _powerButton(
    ThemeData theme, {
    required String label,
    required Color color,
    required bool running,
    required String tooltip,
  }) {
    final state = _stateLabel;
    return Tooltip(
      message: tooltip,
      child: InkWell(
        borderRadius: BorderRadius.circular(60),
        onTap: _busy
            ? null
            : () {
                _sendControl(
                  running ? 'systemShutdown' : 'systemStart',
                  true,
                );
              },
        child: AnimatedBuilder(
          animation: Listenable.merge([_idlePulse, _errorBlink]),
          builder: (context, _) {
            final isIdle = state == 'Idle';
            final isError = state == 'Error';
            final idlePulse = 0.5 + 0.5 * math.sin(2 * math.pi * _idlePulse.value);
            final blinkOn = _errorBlink.value < 0.5;
            final glowStrength = isIdle ? idlePulse : 1.0;
            final visible = isError ? blinkOn : true;

            return Opacity(
              opacity: visible ? 1.0 : 0.25,
              child: Container(
                width: 64,
                height: 64,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  gradient: RadialGradient(
                    center: Alignment.center,
                    radius: 0.95,
                    colors: [
                      theme.colorScheme.primary.withAlpha(46),
                      const Color(0xFF050709).withAlpha(250),
                    ],
                  ),
                  boxShadow: [
                    BoxShadow(
                      color: color.withAlpha((64 + 96 * glowStrength).round()),
                      blurRadius: 16 + (10 * glowStrength),
                    ),
                    const BoxShadow(
                      color: Color(0xC0000000),
                      blurRadius: 16,
                      offset: Offset(0, 4),
                    ),
                  ],
                ),
                child: Stack(
                  children: [
                    Positioned.fill(
                      child: Padding(
                        padding: const EdgeInsets.all(6),
                        child: DecoratedBox(
                          decoration: BoxDecoration(
                            shape: BoxShape.circle,
                            border: Border.all(color: color, width: 3),
                          ),
                        ),
                      ),
                    ),
                    Center(
                      child: Text(
                        label,
                        style: theme.textTheme.labelSmall?.copyWith(
                          fontWeight: FontWeight.w800,
                          letterSpacing: 0.6,
                          color: color.withAlpha(230),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            );
          },
        ),
      ),
    );
  }

  Widget _buildTab(ThemeData theme) {
    final strings = context.strings;
    switch (_tab) {
      case AdminTab.dashboard:
        return DashboardTab(
          monitor: _monitor,
          controls: _controls,
          isMuted: _isMuted,
          busy: _busy,
          onControl: _sendControl,
        );
      case AdminTab.userSettings:
        return UserSettingsTab(
          controls: _controls,
          busy: _busy,
          currentPw: _userCurrentPw,
          newPw: _userNewPw,
          newDeviceId: _userNewId,
          onSaveUser: () async {
            await _sendControl('userCredentials', {
              'current': _userCurrentPw.text,
              'newPass': _userNewPw.text,
              'newId': _userNewId.text,
            });
            _userCurrentPw.clear();
            _userNewPw.clear();
            _userNewId.clear();
          },
          onResetUser: () {
            _userCurrentPw.clear();
            _userNewPw.clear();
            _userNewId.clear();
          },
          onSetAccess: (idx, allowed) => _sendControl('Access$idx', allowed),
        );
      case AdminTab.manualControl:
        return ManualControlTab(
          monitor: _monitor,
          controls: _controls,
          busy: _busy,
          fanSpeed: _fanSpeed,
          onFanChanged: (v) => setState(() => _fanSpeed = v),
          onFanCommit: (v) {
            _sendControl('fanSpeed', v);
          },
          onSetRelay: (v) => _sendControl('relay', v),
          onSetOutput: (idx, v) => _sendControl('output$idx', v),
        );
      case AdminTab.adminSettings:
        return AdminSettingsTab(
          busy: _busy,
          currentPw: _adminCurrentPw,
          newUsername: _adminNewUser,
          newPassword: _adminNewPw,
          wifiSsid: _wifiSsid,
          wifiPassword: _wifiPw,
          onSave: () async {
            await _sendControl('adminCredentials', {
              'current': _adminCurrentPw.text,
              'username': _adminNewUser.text,
              'password': _adminNewPw.text,
              'wifiSSID': _wifiSsid.text,
              'wifiPassword': _wifiPw.text,
            });
            _adminCurrentPw.clear();
            _adminNewUser.clear();
            _adminNewPw.clear();
            _wifiPw.clear();
          },
          onReset: () {
            _adminCurrentPw.clear();
            _adminNewUser.clear();
            _adminNewPw.clear();
            _wifiPw.clear();
          },
        );
      case AdminTab.deviceSettings:
        return DeviceSettingsTab(
          controls: _controls,
          busy: _busy,
          onControl: _sendControl,
          onControlBatchComplete: () async {
            await _refreshControls();
            await _refreshMonitor();
            _toast(strings.t('Saved'));
          },
        );
      case AdminTab.live:
        return LiveTab(
          monitor: _monitor,
          onOpenHistory: () {
            _openHistoryDialog();
          },
          onOpenCalibration: () {
            _openCalibrationDialog();
          },
          onOpenError: () {
            _openErrorDialog();
          },
          onOpenLog: () {
            _openLogDialog();
          },
        );
    }
  }
}

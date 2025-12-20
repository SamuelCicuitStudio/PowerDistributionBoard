import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';

import '../l10n/app_strings.dart';
import '../platform/wifi_types.dart';
import '../platform/wifi_windows.dart';
import 'login_screen.dart';

enum ConnectionMode { ap, station }

class ConnectionScreen extends StatefulWidget {
  const ConnectionScreen({super.key});

  @override
  State<ConnectionScreen> createState() => _ConnectionScreenState();
}

class _ConnectionScreenState extends State<ConnectionScreen> {
  final TextEditingController _customController = TextEditingController();
  final TextEditingController _passwordController =
      TextEditingController(text: '1234567890');

  String? _customError;
  String? _error;

  List<WifiNetwork> _networks = const [];
  WifiNetwork? _selected;
  String? _currentSsid;
  bool _loading = false;
  bool _connecting = false;
  bool _disconnecting = false;
  bool _forgetting = false;
  ConnectionMode _mode = ConnectionMode.ap;
  bool _statusInFlight = false;
  Timer? _statusTimer;

  @override
  void initState() {
    super.initState();
    _refresh();
    if (Platform.isWindows) {
      _statusTimer = Timer.periodic(
        const Duration(seconds: 2),
        (_) => _refreshCurrentSsid(),
      );
    }
  }

  @override
  void dispose() {
    _customController.dispose();
    _passwordController.dispose();
    _statusTimer?.cancel();
    super.dispose();
  }

  String get _modeBaseUrl => _mode == ConnectionMode.ap
      ? 'http://192.168.4.1'
      : 'http://powerboard.local';

  void _openLogin(String baseUrl) {
    Navigator.of(context).push(
      MaterialPageRoute(
        builder: (context) => LoginScreen(baseUrl: baseUrl),
      ),
    );
  }

  String _normalizeUrl(String raw) {
    final trimmed = raw.trim();
    if (trimmed.isEmpty) return trimmed;
    if (trimmed.startsWith('http://') || trimmed.startsWith('https://')) {
      return trimmed;
    }
    return 'http://$trimmed';
  }

  Future<void> _refresh() async {
    if (!Platform.isWindows) return;
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final nets = await WifiWindows.scanNetworks();
      final current = await _loadCurrentSsid();
      if (!mounted) return;
      setState(() {
        _networks = nets;
        _currentSsid = current;
        if (_selected != null &&
            !_networks.any((n) => n.ssid == _selected!.ssid)) {
          _selected = null;
        }
        _selected ??= _pickLikelyBoardNetwork(nets);
        if (_selected == null && current != null && current.isNotEmpty) {
          for (final n in _networks) {
            if (n.ssid == current) {
              _selected = n;
              break;
            }
          }
        }
      });
    } catch (err) {
      if (mounted) setState(() => _error = err.toString());
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  WifiNetwork? _pickLikelyBoardNetwork(List<WifiNetwork> nets) {
    for (final n in nets) {
      final s = n.ssid.toLowerCase();
      if (s.contains('pdis') || s.contains('powerboard') || s.contains('pboard')) {
        return n;
      }
    }
    return null;
  }

  bool _needsPassword(WifiNetwork network) {
    final sec = (network.security ?? '').toLowerCase();
    if (sec.isEmpty) return false;
    return !sec.contains('open');
  }

  Future<void> _connectSelected() async {
    final strings = context.strings;
    if (!Platform.isWindows) {
      _openLogin(_modeBaseUrl);
      return;
    }

    final ssid = _selected?.ssid ?? '';
    if (ssid.isEmpty) {
      setState(() => _error = strings.t('Select a Wi-Fi network.'));
      return;
    }

    final password = _passwordController.text;
    if (_needsPassword(_selected!) && password.trim().length < 8) {
      setState(() => _error = strings.t('Wi-Fi password must be at least 8 characters.'));
      return;
    }

    setState(() {
      _connecting = true;
      _error = null;
    });

    if (_currentSsid != null &&
        _currentSsid!.isNotEmpty &&
        _currentSsid != ssid) {
      final disc = await WifiWindows.disconnect();
      if (!mounted) return;
      if (!disc.ok) {
        setState(() {
          _connecting = false;
          _error = disc.error ?? strings.t('Failed to disconnect from current Wi-Fi.');
        });
        return;
      }
      final cleared = await _waitForSsid(null, timeout: const Duration(seconds: 8));
      if (!mounted) return;
      if (!cleared) {
        setState(() {
          _connecting = false;
          _error = strings.t('Disconnect did not complete. Try again.');
        });
        return;
      }
    }

    final result = await WifiWindows.connect(ssid: ssid, password: password);
    if (!mounted) return;

    if (!result.ok) {
      setState(() {
        _connecting = false;
        _error = result.error ?? 'Failed to connect.';
      });
      return;
    }

    final connected = await _waitForSsid(ssid, timeout: const Duration(seconds: 15));
    if (!mounted) return;
    if (!connected) {
      setState(() {
        _connecting = false;
        _error = strings.t(
          'Still not connected to {ssid}. Check the password and signal.',
          {'ssid': ssid},
        );
      });
      return;
    }

    setState(() => _connecting = false);
    _openLogin(_modeBaseUrl);
  }

  Future<void> _disconnectCurrent() async {
    final strings = context.strings;
    if (!Platform.isWindows) return;
    if (_currentSsid == null || _currentSsid!.isEmpty) return;
    setState(() {
      _disconnecting = true;
      _error = null;
    });

    final result = await WifiWindows.disconnect();
    if (!mounted) return;
    if (!result.ok) {
      setState(() {
        _disconnecting = false;
        _error = result.error ?? strings.t('Failed to disconnect.');
      });
      return;
    }

    final cleared = await _waitForSsid(null, timeout: const Duration(seconds: 8));
    if (!mounted) return;
    if (!cleared) {
      setState(() {
        _disconnecting = false;
        _error = strings.t('Disconnect did not complete. Try again.');
      });
      return;
    }
    await _refresh();
    if (!mounted) return;
    setState(() => _disconnecting = false);
  }

  Future<void> _forgetSelected() async {
    final strings = context.strings;
    if (!Platform.isWindows) return;
    final ssid = _selected?.ssid ?? '';
    if (ssid.isEmpty) {
      setState(() => _error = strings.t('Select a Wi-Fi network.'));
      return;
    }
    setState(() {
      _forgetting = true;
      _error = null;
    });

    if (_currentSsid == ssid) {
      final disc = await WifiWindows.disconnect();
      if (!mounted) return;
      if (!disc.ok) {
        setState(() {
          _forgetting = false;
          _error = disc.error ?? strings.t('Failed to disconnect.');
        });
        return;
      }
      await _waitForSsid(null, timeout: const Duration(seconds: 8));
      if (!mounted) return;
    }

    final result = await WifiWindows.forgetProfile(ssid);
    if (!mounted) return;
    if (!result.ok) {
      setState(() {
        _forgetting = false;
        _error = result.error ?? strings.t('Failed to forget network.');
      });
      return;
    }

    await _refresh();
    if (!mounted) return;
    setState(() => _forgetting = false);
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(
          strings.t('Forgot network: {ssid}', {'ssid': ssid}),
        ),
      ),
    );
  }

  Future<String?> _loadCurrentSsid() async {
    if (!Platform.isWindows) return null;
    if (_statusInFlight) return _currentSsid;
    _statusInFlight = true;
    try {
      return await WifiWindows.currentSsid();
    } finally {
      _statusInFlight = false;
    }
  }

  Future<void> _refreshCurrentSsid() async {
    if (!Platform.isWindows) return;
    try {
      final current = await _loadCurrentSsid();
      if (!mounted) return;
      if (current != _currentSsid) {
        setState(() => _currentSsid = current);
      }
    } catch (_) {
      // Ignore background refresh errors.
    }
  }

  Future<bool> _waitForSsid(
    String? target, {
    Duration timeout = const Duration(seconds: 10),
  }) async {
    if (!Platform.isWindows) return true;
    final end = DateTime.now().add(timeout);
    final normalized = (target ?? '').trim();
    while (DateTime.now().isBefore(end)) {
      final current = await _loadCurrentSsid();
      if (!mounted) return false;
      if (current != _currentSsid) {
        setState(() => _currentSsid = current);
      }
      final match = normalized.isEmpty
          ? (current == null || current.isEmpty)
          : current == normalized;
      if (match) return true;
      await Future<void>.delayed(const Duration(milliseconds: 600));
    }
    return false;
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final windows = Platform.isWindows;
    final strings = context.strings;
    final language = context.language;

    return Scaffold(
      appBar: AppBar(
        title: Text(strings.t('Choose Connection')),
        actions: [
          PopupMenuButton<String>(
            tooltip: strings.t('Language'),
            icon: const Icon(Icons.language),
            onSelected: (value) => language.setLanguageCode(value),
            itemBuilder: (context) => [
              CheckedPopupMenuItem(
                value: 'en',
                checked: language.locale.languageCode == 'en',
                child: Text(strings.t('English')),
              ),
              CheckedPopupMenuItem(
                value: 'it',
                checked: language.locale.languageCode == 'it',
                child: Text(strings.t('Italian')),
              ),
            ],
          ),
        ],
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(24),
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 980),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  strings.t('Select how you want to connect'),
                  style: theme.textTheme.headlineSmall?.copyWith(
                    fontWeight: FontWeight.w600,
                  ),
                ),
                const SizedBox(height: 6),
                Text(
                  strings.t(
                    'Pick a Wi-Fi network, connect, then log in to the admin console.',
                  ),
                  style: theme.textTheme.bodyMedium?.copyWith(
                    color: theme.colorScheme.onSurface.withAlpha(179),
                  ),
                ),
                const SizedBox(height: 12),
                if (_error != null)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 12),
                    child: Text(
                      _error!,
                      style: theme.textTheme.bodySmall?.copyWith(
                        color: theme.colorScheme.error,
                      ),
                    ),
                  ),
                if (windows)
                  LayoutBuilder(
                    builder: (context, constraints) {
                      final stacked = constraints.maxWidth < 860;
                      if (stacked) {
                        return Column(
                          crossAxisAlignment: CrossAxisAlignment.stretch,
                          children: [
                            _buildNetworkList(theme),
                            const SizedBox(height: 16),
                            _buildConnectPanel(theme),
                          ],
                        );
                      }
                      return Row(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Expanded(child: _buildNetworkList(theme)),
                          const SizedBox(width: 16),
                          SizedBox(
                            width: 360,
                            child: _buildConnectPanel(theme),
                          ),
                        ],
                      );
                    },
                  )
                else
                  _buildNonWindowsPanel(theme),
                const SizedBox(height: 12),
                _buildCustomCard(theme),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildNonWindowsPanel(ThemeData theme) {
    final strings = context.strings;
    return Center(
      child: Text(
        strings.t(
          'On this platform, connect to the board using your system Wi-Fi settings, then continue to login.',
        ),
        style: theme.textTheme.bodyMedium?.copyWith(
          color: theme.colorScheme.onSurface.withAlpha(179),
        ),
      ),
    );
  }

  Widget _buildNetworkList(ThemeData theme) {
    final strings = context.strings;
    Widget list;
    if (_loading) {
      list = const Center(child: CircularProgressIndicator());
    } else if (_networks.isEmpty) {
      list = Center(
        child: Text(
          strings.t('No Wi-Fi networks found.'),
          style: theme.textTheme.bodyMedium?.copyWith(
            color: theme.colorScheme.onSurface.withAlpha(179),
          ),
        ),
      );
    } else {
      list = ListView.separated(
        shrinkWrap: true,
        physics: const NeverScrollableScrollPhysics(),
        itemCount: _networks.length,
        separatorBuilder: (context, index) => Divider(
          height: 1,
          color: theme.colorScheme.onSurface.withAlpha(20),
        ),
        itemBuilder: (context, index) {
          final n = _networks[index];
          final selected = _selected?.ssid == n.ssid;
          final signal = n.signalQuality;
          final isCurrent = _currentSsid != null && _currentSsid == n.ssid;
          final tags = <String>[
            if (signal != null)
              strings.t(
                'Signal {signal}%',
                {'signal': signal.toString()},
              ),
            if (n.security != null && n.security!.isNotEmpty) n.security!,
            if (isCurrent) strings.t('Connected'),
          ];
          return ListTile(
            title: Text(
              n.ssid.isEmpty ? strings.t('(hidden network)') : n.ssid,
            ),
            subtitle: Text(
              tags.join(' | '),
            ),
            trailing: isCurrent
                ? const Icon(Icons.wifi)
                : (selected ? const Icon(Icons.check) : null),
            selected: selected,
            onTap: () => setState(() => _selected = n),
          );
        },
      );
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Text(
              strings.t('Available networks'),
              style: theme.textTheme.titleSmall?.copyWith(
                fontWeight: FontWeight.w600,
              ),
            ),
            const Spacer(),
            IconButton(
              tooltip: strings.t('Refresh'),
              onPressed: !_loading ? _refresh : null,
              icon: const Icon(Icons.refresh),
            ),
          ],
        ),
        const SizedBox(height: 8),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
          decoration: BoxDecoration(
            color: theme.colorScheme.surface.withAlpha(76),
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: theme.colorScheme.onSurface.withAlpha(26)),
          ),
          child: Row(
            children: [
              Icon(
                _currentSsid == null ? Icons.wifi_off : Icons.wifi,
                size: 18,
                color: theme.colorScheme.onSurface.withAlpha(204),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  _currentSsid == null || _currentSsid!.isEmpty
                      ? strings.t('Not connected')
                      : strings.t(
                          'Connected to {ssid}',
                          {'ssid': _currentSsid!},
                        ),
                  style: theme.textTheme.bodyMedium?.copyWith(
                    color: theme.colorScheme.onSurface.withAlpha(204),
                  ),
                ),
              ),
              OutlinedButton(
                onPressed:
                    _disconnecting || _currentSsid == null || _currentSsid!.isEmpty
                        ? null
                        : _disconnectCurrent,
                child: _disconnecting
                    ? const SizedBox(
                        width: 16,
                        height: 16,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : Text(strings.t('Disconnect')),
              ),
            ],
          ),
        ),
        const SizedBox(height: 12),
        list,
      ],
    );
  }

  Widget _buildConnectPanel(ThemeData theme) {
    final strings = context.strings;
    final ssid = _selected?.ssid ?? '';
    return Card(
      elevation: 0,
      color: theme.colorScheme.surface,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(18),
        side: BorderSide(color: theme.colorScheme.onSurface.withAlpha(26)),
      ),
      child: Padding(
        padding: const EdgeInsets.all(18),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              strings.t('Connection mode'),
              style: theme.textTheme.titleSmall?.copyWith(
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(height: 8),
            Wrap(
              spacing: 8,
              children: [
                ChoiceChip(
                  label: Text(strings.t('AP Mode')),
                  selected: _mode == ConnectionMode.ap,
                  onSelected: (_) => setState(() => _mode = ConnectionMode.ap),
                ),
                ChoiceChip(
                  label: Text(strings.t('Station Mode')),
                  selected: _mode == ConnectionMode.station,
                  onSelected: (_) => setState(() => _mode = ConnectionMode.station),
                ),
              ],
            ),
            const SizedBox(height: 8),
            Text(
              strings.t('Base URL: {url}', {'url': _modeBaseUrl}),
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.onSurface.withAlpha(153),
              ),
            ),
            const SizedBox(height: 16),
            Text(
              strings.t('Selected network'),
              style: theme.textTheme.titleSmall?.copyWith(
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(height: 6),
            Text(
              ssid.isEmpty ? strings.t('None') : ssid,
              style: theme.textTheme.bodyMedium,
            ),
            const SizedBox(height: 16),
            TextField(
              controller: _passwordController,
              obscureText: true,
              decoration: InputDecoration(
                labelText: strings.t('Wi-Fi password'),
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 12),
            SizedBox(
              width: double.infinity,
              child: FilledButton(
                onPressed: _connecting ? null : _connectSelected,
                child: _connecting
                    ? const SizedBox(
                        width: 18,
                        height: 18,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : Text(strings.t('Connect & Continue')),
              ),
            ),
            const SizedBox(height: 8),
            SizedBox(
              width: double.infinity,
              child: OutlinedButton(
                onPressed: _forgetting || ssid.isEmpty ? null : _forgetSelected,
                child: _forgetting
                    ? const SizedBox(
                        width: 18,
                        height: 18,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : Text(strings.t('Forget network')),
              ),
            ),
            const SizedBox(height: 8),
            Align(
              alignment: Alignment.centerRight,
              child: TextButton(
                onPressed: () => _openLogin(_modeBaseUrl),
                child: Text(strings.t('Continue without switching Wi-Fi')),
              ),
            ),
            Text(
              strings.t('Tip: default AP password is 1234567890.'),
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.onSurface.withAlpha(153),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildCustomCard(ThemeData theme) {
    final strings = context.strings;
    return Card(
      elevation: 0,
      color: theme.colorScheme.surface,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(18),
        side: BorderSide(
          color: theme.colorScheme.onSurface.withAlpha(26),
        ),
      ),
      child: Padding(
        padding: const EdgeInsets.all(18),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              strings.t('Custom address'),
              style: theme.textTheme.titleMedium?.copyWith(
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(height: 8),
            TextField(
              controller: _customController,
              decoration: InputDecoration(
                hintText: 'http://192.168.0.25',
                errorText: _customError,
                border: const OutlineInputBorder(),
              ),
              onChanged: (_) {
                if (_customError != null) {
                  setState(() => _customError = null);
                }
              },
            ),
            const SizedBox(height: 12),
            Align(
              alignment: Alignment.centerRight,
              child: FilledButton(
                onPressed: () {
                  final raw = _customController.text.trim();
                  if (raw.isEmpty) {
                    setState(() {
                      _customError = strings.t('Enter an address or IP.');
                    });
                    return;
                  }
                  _openLogin(_normalizeUrl(raw));
                },
                child: Text(strings.t('Connect')),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

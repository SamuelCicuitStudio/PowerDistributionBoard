import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';

import '../platform/wifi_types.dart';
import '../platform/wifi_windows.dart';
import 'login_screen.dart';

class ApWifiSetupScreen extends StatefulWidget {
  const ApWifiSetupScreen({super.key});

  @override
  State<ApWifiSetupScreen> createState() => _ApWifiSetupScreenState();
}

class _ApWifiSetupScreenState extends State<ApWifiSetupScreen> {
  List<WifiNetwork> _networks = const [];
  bool _loading = false;
  String? _error;
  WifiNetwork? _selected;
  final TextEditingController _passwordController =
      TextEditingController(text: '1234567890');
  bool _connecting = false;

  @override
  void initState() {
    super.initState();
    _refresh();
  }

  @override
  void dispose() {
    _passwordController.dispose();
    super.dispose();
  }

  Future<void> _refresh() async {
    if (!Platform.isWindows) return;
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final nets = await WifiWindows.scanNetworks();
      if (!mounted) return;
      setState(() {
        _networks = nets;
        _selected ??= _pickLikelyBoardNetwork(nets);
      });
    } catch (err) {
      if (mounted) setState(() => _error = err.toString());
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  WifiNetwork? _pickLikelyBoardNetwork(List<WifiNetwork> nets) {
    WifiNetwork? best;
    for (final n in nets) {
      final s = n.ssid.toLowerCase();
      if (s.contains('pdis') || s.contains('powerboard') || s.contains('pboard')) {
        best = n;
        break;
      }
    }
    return best;
  }

  Future<void> _connect() async {
    if (!Platform.isWindows) {
      _goToLogin();
      return;
    }
    final ssid = _selected?.ssid ?? '';
    final password = _passwordController.text;
    if (ssid.isEmpty) {
      setState(() => _error = 'Select a Wi-Fi network.');
      return;
    }
    if (password.trim().length < 8) {
      setState(() => _error = 'Wi-Fi password must be at least 8 characters.');
      return;
    }

    setState(() {
      _connecting = true;
      _error = null;
    });

    final result = await WifiWindows.connect(ssid: ssid, password: password);
    if (!mounted) return;

    if (!result.ok) {
      setState(() {
        _connecting = false;
        _error = result.error ?? 'Failed to connect.';
      });
      return;
    }

    // Give Windows a moment to switch networks.
    await Future<void>.delayed(const Duration(seconds: 2));

    if (!mounted) return;
    setState(() => _connecting = false);
    _goToLogin();
  }

  void _goToLogin() {
    Navigator.of(context).pushReplacement(
      MaterialPageRoute(
        builder: (context) => const LoginScreen(baseUrl: 'http://192.168.4.1'),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final windows = Platform.isWindows;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Connect to Board AP'),
        actions: [
          IconButton(
            tooltip: 'Refresh',
            onPressed: windows && !_loading ? _refresh : null,
            icon: const Icon(Icons.refresh),
          ),
          const SizedBox(width: 8),
        ],
      ),
      body: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 920),
          child: Padding(
            padding: const EdgeInsets.all(24),
            child: Card(
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
                      'Step 1: Join the board Wi-Fi',
                      style: theme.textTheme.titleMedium?.copyWith(
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                    const SizedBox(height: 6),
                    Text(
                      windows
                          ? 'Select the board AP network, enter its password, then continue to login.'
                          : 'On this platform, please connect to the board AP using your system Wi-Fi settings, then continue to login.',
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
                    if (!windows) ...[
                      const Spacer(),
                      Align(
                        alignment: Alignment.centerRight,
                        child: FilledButton(
                          onPressed: _goToLogin,
                          child: const Text('Continue to Login'),
                        ),
                      ),
                    ] else ...[
                      Expanded(
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Expanded(
                              child: _buildNetworkList(theme),
                            ),
                            const SizedBox(width: 16),
                            SizedBox(
                              width: 360,
                              child: _buildConnectPanel(theme),
                            ),
                          ],
                        ),
                      ),
                    ],
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildNetworkList(ThemeData theme) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Text(
              'Available networks',
              style: theme.textTheme.titleSmall?.copyWith(
                fontWeight: FontWeight.w600,
              ),
            ),
            const Spacer(),
            IconButton(
              tooltip: 'Refresh',
              onPressed: !_loading ? _refresh : null,
              icon: const Icon(Icons.refresh),
            ),
          ],
        ),
        const SizedBox(height: 8),
        Expanded(
          child: _loading
              ? const Center(child: CircularProgressIndicator())
              : _networks.isEmpty
                  ? Center(
                      child: Text(
                        'No Wi-Fi networks found.',
                        style: theme.textTheme.bodyMedium?.copyWith(
                          color: theme.colorScheme.onSurface.withAlpha(179),
                        ),
                      ),
                    )
                  : ListView.separated(
                      itemCount: _networks.length,
                      separatorBuilder: (context, index) => Divider(
                        height: 1,
                        color: theme.colorScheme.onSurface.withAlpha(20),
                      ),
                      itemBuilder: (context, index) {
                        final n = _networks[index];
                        final selected = _selected?.ssid == n.ssid;
                        final signal = n.signalQuality;
                        return ListTile(
                          title: Text(n.ssid.isEmpty ? '(hidden network)' : n.ssid),
                          subtitle: Text(
                            [
                              if (signal != null) 'Signal $signal%',
                              if (n.security != null && n.security!.isNotEmpty)
                                n.security!,
                            ].join(' | '),
                          ),
                          trailing: selected ? const Icon(Icons.check) : null,
                          selected: selected,
                          onTap: () => setState(() => _selected = n),
                        );
                      },
                    ),
        ),
      ],
    );
  }

  Widget _buildConnectPanel(ThemeData theme) {
    final ssid = _selected?.ssid ?? '';
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          'Selected network',
          style: theme.textTheme.titleSmall?.copyWith(
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(height: 8),
        Text(
          ssid.isEmpty ? 'None' : ssid,
          style: theme.textTheme.bodyMedium,
        ),
        const SizedBox(height: 16),
        TextField(
          controller: _passwordController,
          obscureText: true,
          decoration: const InputDecoration(
            labelText: 'AP password',
            border: OutlineInputBorder(),
          ),
        ),
        const SizedBox(height: 12),
        SizedBox(
          width: double.infinity,
          child: FilledButton(
            onPressed: _connecting ? null : _connect,
            child: _connecting
                ? const SizedBox(
                    width: 18,
                    height: 18,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : const Text('Connect & Continue'),
          ),
        ),
        const SizedBox(height: 8),
        Text(
          'Tip: default AP password is 1234567890.',
          style: theme.textTheme.bodySmall?.copyWith(
            color: theme.colorScheme.onSurface.withAlpha(153),
          ),
        ),
      ],
    );
  }
}

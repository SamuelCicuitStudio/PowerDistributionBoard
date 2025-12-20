import 'package:flutter/material.dart';
import 'package:flutter_inappwebview/flutter_inappwebview.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const PowerBoardApp());
}

class PowerBoardApp extends StatelessWidget {
  const PowerBoardApp({super.key});

  @override
  Widget build(BuildContext context) {
    const scheme = ColorScheme(
      brightness: Brightness.dark,
      primary: Color(0xFF1FE1B0),
      onPrimary: Color(0xFF081214),
      secondary: Color(0xFF6BE2FF),
      onSecondary: Color(0xFF081214),
      surface: Color(0xFF131A1F),
      onSurface: Color(0xFFE6EEF2),
      background: Color(0xFF0B0F12),
      onBackground: Color(0xFFE6EEF2),
      error: Color(0xFFE85B4A),
      onError: Color(0xFF081214),
    );

    return MaterialApp(
      title: 'PowerBoard Admin',
      theme: ThemeData(
        colorScheme: scheme,
        scaffoldBackgroundColor: scheme.background,
        fontFamily: 'Inter',
        useMaterial3: true,
        appBarTheme: const AppBarTheme(
          backgroundColor: Color(0xFF131A1F),
          foregroundColor: Color(0xFFE6EEF2),
        ),
        filledButtonTheme: FilledButtonThemeData(
          style: FilledButton.styleFrom(
            padding: const EdgeInsets.symmetric(horizontal: 22, vertical: 14),
            textStyle: const TextStyle(
              fontSize: 15,
              fontWeight: FontWeight.w600,
            ),
          ),
        ),
      ),
      home: const OnboardingScreen(),
    );
  }
}

class OnboardingScreen extends StatelessWidget {
  const OnboardingScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      body: Container(
        decoration: const BoxDecoration(
          gradient: LinearGradient(
            colors: [Color(0xFF0B0F12), Color(0xFF0E1A1E)],
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
          ),
        ),
        child: SafeArea(
          child: Stack(
            children: [
              const _GlowOrb(
                color: Color(0xFF1FE1B0),
                alignment: Alignment(-0.9, -0.6),
                size: 240,
              ),
              const _GlowOrb(
                color: Color(0xFF6BE2FF),
                alignment: Alignment(0.9, 0.7),
                size: 220,
              ),
              Center(
                child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 820),
                  child: Padding(
                    padding: const EdgeInsets.all(28),
                    child: SingleChildScrollView(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            'PowerBoard Admin',
                            style: theme.textTheme.headlineMedium?.copyWith(
                              fontWeight: FontWeight.w700,
                              letterSpacing: 0.4,
                            ),
                          ),
                          const SizedBox(height: 8),
                          Text(
                            'Before you connect, run these quick steps on the control board.',
                            style: theme.textTheme.bodyLarge?.copyWith(
                              color: theme.colorScheme.onSurface.withOpacity(
                                0.8,
                              ),
                            ),
                          ),
                          const SizedBox(height: 26),
                          const _StepCard(
                            index: '01',
                            title: 'Press the Power button',
                            detail:
                                'Tap the Power button once, or restart the control board if it is already running.',
                          ),
                          const SizedBox(height: 16),
                          const _StepCard(
                            index: '02',
                            title: 'Wait for Ready',
                            detail:
                                'Confirm the Ready indicator is on before you continue to connect.',
                          ),
                          const SizedBox(height: 16),
                          const _StepCard(
                            index: '03',
                            title: 'Stay on the same network',
                            detail:
                                'Use Board AP mode or stay on your existing Wi-Fi and connect with powerboard.local.',
                          ),
                          const SizedBox(height: 28),
                          Align(
                            alignment: Alignment.centerRight,
                            child: FilledButton(
                              onPressed: () {
                                Navigator.of(context).push(
                                  MaterialPageRoute(
                                    builder: (context) =>
                                        const ConnectionScreen(),
                                  ),
                                );
                              },
                              child: const Text('Continue'),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class ConnectionScreen extends StatelessWidget {
  const ConnectionScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final targets = [
      BoardTarget(
        title: 'Board AP Mode',
        subtitle: 'Connect to the board hotspot Wi-Fi first.',
        loginUri: Uri.parse('http://192.168.4.1/login'),
        hint: 'Use this when your PC is connected to the board AP.',
        icon: Icons.wifi_tethering,
      ),
      BoardTarget(
        title: 'Existing Wi-Fi',
        subtitle: 'Stay connected and reach powerboard.local.',
        loginUri: Uri.parse('http://powerboard.local/login'),
        hint: 'Use this when the board is on your LAN.',
        icon: Icons.router,
      ),
    ];

    return Scaffold(
      appBar: AppBar(title: const Text('Choose Connection')),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 860),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'How do you want to connect?',
                  style: theme.textTheme.headlineSmall?.copyWith(
                    fontWeight: FontWeight.w600,
                  ),
                ),
                const SizedBox(height: 6),
                Text(
                  'Pick the matching connection, then log in to the admin console.',
                  style: theme.textTheme.bodyMedium?.copyWith(
                    color: theme.colorScheme.onSurface.withOpacity(0.7),
                  ),
                ),
                const SizedBox(height: 22),
                Expanded(
                  child: GridView.count(
                    crossAxisCount: 2,
                    crossAxisSpacing: 18,
                    mainAxisSpacing: 18,
                    childAspectRatio: 1.6,
                    children: targets
                        .map(
                          (target) => _ConnectionCard(
                            target: target,
                            onOpen: () {
                              Navigator.of(context).push(
                                MaterialPageRoute(
                                  builder: (context) =>
                                      AdminWebScreen(target: target),
                                ),
                              );
                            },
                          ),
                        )
                        .toList(),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class AdminWebScreen extends StatefulWidget {
  const AdminWebScreen({super.key, required this.target});

  final BoardTarget target;

  @override
  State<AdminWebScreen> createState() => _AdminWebScreenState();
}

class _AdminWebScreenState extends State<AdminWebScreen> {
  InAppWebViewController? _controller;
  bool _loading = true;
  String? _loadError;
  Uri? _currentUri;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Scaffold(
      appBar: AppBar(
        title: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text('PowerBoard Admin'),
            Text(
              widget.target.title,
              style: theme.textTheme.labelMedium?.copyWith(
                color: theme.colorScheme.onSurface.withOpacity(0.7),
              ),
            ),
          ],
        ),
        actions: [
          IconButton(
            tooltip: 'Back page',
            onPressed: () async {
              final canGoBack = await _controller?.canGoBack() ?? false;
              if (canGoBack) {
                await _controller?.goBack();
              }
            },
            icon: const Icon(Icons.arrow_back_ios_new),
          ),
          IconButton(
            tooltip: 'Forward page',
            onPressed: () async {
              final canGoForward = await _controller?.canGoForward() ?? false;
              if (canGoForward) {
                await _controller?.goForward();
              }
            },
            icon: const Icon(Icons.arrow_forward_ios),
          ),
          IconButton(
            tooltip: 'Reload',
            onPressed: () => _controller?.reload(),
            icon: const Icon(Icons.refresh),
          ),
          IconButton(
            tooltip: 'Login',
            onPressed: () {
              _controller?.loadUrl(
                urlRequest: URLRequest(
                  url: WebUri(widget.target.loginUri.toString()),
                ),
              );
            },
            icon: const Icon(Icons.home_outlined),
          ),
          TextButton(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text('Switch Board'),
          ),
          const SizedBox(width: 8),
        ],
      ),
      body: Stack(
        children: [
          InAppWebView(
            initialUrlRequest: URLRequest(
              url: WebUri(widget.target.loginUri.toString()),
            ),
            initialSettings: InAppWebViewSettings(
              javaScriptEnabled: true,
              mediaPlaybackRequiresUserGesture: false,
            ),
            onWebViewCreated: (controller) {
              _controller = controller;
            },
            onLoadStart: (controller, url) {
              setState(() {
                _loading = true;
                _loadError = null;
                _currentUri = Uri.tryParse(url?.toString() ?? '');
              });
            },
            onLoadStop: (controller, url) {
              setState(() {
                _loading = false;
                _currentUri = Uri.tryParse(url?.toString() ?? '');
              });
            },
            onReceivedError: (controller, request, error) {
              setState(() {
                _loading = false;
                _loadError = error.description;
              });
            },
          ),
          if (_loading)
            const Align(
              alignment: Alignment.topCenter,
              child: LinearProgressIndicator(minHeight: 3),
            ),
          if (_loadError != null) _buildErrorOverlay(context),
          if (_currentUri != null)
            Positioned(
              left: 16,
              bottom: 14,
              child: _LocationBadge(uri: _currentUri!),
            ),
        ],
      ),
    );
  }

  Widget _buildErrorOverlay(BuildContext context) {
    final theme = Theme.of(context);
    return Positioned.fill(
      child: Container(
        color: Colors.black.withOpacity(0.55),
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 420),
            child: Card(
              color: theme.colorScheme.surface,
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(18),
              ),
              child: Padding(
                padding: const EdgeInsets.all(22),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Unable to reach the board',
                      style: theme.textTheme.titleMedium?.copyWith(
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                    const SizedBox(height: 10),
                    Text(
                      _loadError ?? 'Unknown error',
                      style: theme.textTheme.bodyMedium?.copyWith(
                        color: theme.colorScheme.onSurface.withOpacity(0.7),
                      ),
                    ),
                    const SizedBox(height: 18),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.end,
                      children: [
                        TextButton(
                          onPressed: () => Navigator.of(context).pop(),
                          child: const Text('Switch Board'),
                        ),
                        const SizedBox(width: 8),
                        FilledButton(
                          onPressed: () => _controller?.reload(),
                          child: const Text('Retry'),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class BoardTarget {
  const BoardTarget({
    required this.title,
    required this.subtitle,
    required this.loginUri,
    required this.hint,
    required this.icon,
  });

  final String title;
  final String subtitle;
  final Uri loginUri;
  final String hint;
  final IconData icon;
}

class _ConnectionCard extends StatelessWidget {
  const _ConnectionCard({required this.target, required this.onOpen});

  final BoardTarget target;
  final VoidCallback onOpen;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(
      elevation: 0,
      color: theme.colorScheme.surface,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(18),
        side: BorderSide(color: theme.colorScheme.onSurface.withOpacity(0.1)),
      ),
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Container(
                  width: 42,
                  height: 42,
                  decoration: BoxDecoration(
                    color: theme.colorScheme.primary.withOpacity(0.15),
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Icon(target.icon, color: theme.colorScheme.primary),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    target.title,
                    style: theme.textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Text(
              target.subtitle,
              style: theme.textTheme.bodyMedium?.copyWith(
                color: theme.colorScheme.onSurface.withOpacity(0.75),
              ),
            ),
            const SizedBox(height: 10),
            Text(
              target.hint,
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.onSurface.withOpacity(0.55),
              ),
            ),
            const Spacer(),
            Align(
              alignment: Alignment.centerRight,
              child: FilledButton(
                onPressed: onOpen,
                child: const Text('Open Login'),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _StepCard extends StatelessWidget {
  const _StepCard({
    required this.index,
    required this.title,
    required this.detail,
  });

  final String index;
  final String title;
  final String detail;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(
      elevation: 0,
      color: theme.colorScheme.surface,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(18),
        side: BorderSide(color: theme.colorScheme.onSurface.withOpacity(0.08)),
      ),
      child: Padding(
        padding: const EdgeInsets.all(18),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Container(
              width: 48,
              height: 48,
              decoration: BoxDecoration(
                color: theme.colorScheme.primary.withOpacity(0.15),
                borderRadius: BorderRadius.circular(14),
              ),
              alignment: Alignment.center,
              child: Text(
                index,
                style: theme.textTheme.titleMedium?.copyWith(
                  fontWeight: FontWeight.w700,
                  color: theme.colorScheme.primary,
                ),
              ),
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    style: theme.textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                  const SizedBox(height: 6),
                  Text(
                    detail,
                    style: theme.textTheme.bodyMedium?.copyWith(
                      color: theme.colorScheme.onSurface.withOpacity(0.75),
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _GlowOrb extends StatelessWidget {
  const _GlowOrb({
    required this.color,
    required this.alignment,
    required this.size,
  });

  final Color color;
  final Alignment alignment;
  final double size;

  @override
  Widget build(BuildContext context) {
    return Align(
      alignment: alignment,
      child: Container(
        width: size,
        height: size,
        decoration: BoxDecoration(
          shape: BoxShape.circle,
          gradient: RadialGradient(
            colors: [color.withOpacity(0.18), color.withOpacity(0.0)],
          ),
        ),
      ),
    );
  }
}

class _LocationBadge extends StatelessWidget {
  const _LocationBadge({required this.uri});

  final Uri uri;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final host = uri.host.isEmpty ? uri.toString() : uri.host;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface.withOpacity(0.9),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(
          color: theme.colorScheme.onSurface.withOpacity(0.12),
        ),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.language, size: 16, color: theme.colorScheme.primary),
          const SizedBox(width: 6),
          Text(
            host,
            style: theme.textTheme.labelMedium?.copyWith(
              color: theme.colorScheme.onSurface.withOpacity(0.85),
            ),
          ),
        ],
      ),
    );
  }
}

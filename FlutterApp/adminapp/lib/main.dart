import 'dart:convert';
import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';
import 'package:webview_windows/webview_windows.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final assetHost = await AssetHost.prepare();
  runApp(AdminWebApp(assetHost: assetHost));
}

class AdminWebApp extends StatelessWidget {
  const AdminWebApp({super.key, required this.assetHost});

  final AssetHostConfig assetHost;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'PowerBoard Admin',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(),
      home: WebviewHost(assetHost: assetHost),
    );
  }
}

class WebviewHost extends StatefulWidget {
  const WebviewHost({super.key, required this.assetHost});

  final AssetHostConfig assetHost;

  @override
  State<WebviewHost> createState() => _WebviewHostState();
}

class _WebviewHostState extends State<WebviewHost> {
  final WebviewController _controller = WebviewController();
  StreamSubscription<LoadingState>? _loadingSub;
  bool _ready = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    _initWebview();
  }

  Future<void> _initWebview() async {
    if (!Platform.isWindows) {
      if (mounted) {
        setState(() => _error = 'WebView is only supported on Windows.');
      }
      return;
    }

    final version = await WebviewController.getWebViewVersion();
    if (version == null) {
      if (mounted) {
        setState(
          () =>
              _error =
                  'WebView2 Runtime not installed. Install from '
                  'https://go.microsoft.com/fwlink/p/?LinkId=2124703',
        );
      }
      return;
    }

    try {
      await _controller.initialize();
      _loadingSub = _controller.loadingState.listen((state) {
        if (!mounted) return;
        setState(() => _ready = state != LoadingState.loading);
      });
      await _controller.setBackgroundColor(Colors.transparent);
      await _controller.setPopupWindowPolicy(WebviewPopupWindowPolicy.deny);
      await _controller.loadUrl(widget.assetHost.entryUrl);
    } on PlatformException catch (e) {
      if (mounted) {
        setState(() => _error = e.message ?? e.code);
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_error != null) {
      return Scaffold(
        body: Center(child: Text('Failed to start webview: $_error')),
      );
    }

    final isInitialized = _controller.value.isInitialized;

    return Scaffold(
      body: Stack(
        children: [
          Positioned.fill(
            child: isInitialized ? Webview(_controller) : const SizedBox.shrink(),
          ),
          if (!isInitialized || !_ready)
            const Positioned.fill(
              child: ColoredBox(
                color: Colors.black,
                child: Center(
                  child: CircularProgressIndicator(),
                ),
              ),
            ),
        ],
      ),
    );
  }

  @override
  void dispose() {
    _loadingSub?.cancel();
    _controller.dispose();
    super.dispose();
  }
}

class AssetHostConfig {
  const AssetHostConfig({
    required this.entryUrl,
    required this.fileUrl,
    required this.assetsDir,
    required this.virtualHost,
    required this.useVirtualHost,
    required this.allowFileFallback,
  });

  final String entryUrl;
  final String fileUrl;
  final String assetsDir;
  final String virtualHost;
  final bool useVirtualHost;
  final bool allowFileFallback;
}

class AssetHost {
  static const String _virtualHost = 'appassets.powerboard.local';
  static _LocalAssetServer? _server;
  static const String _versionEnvKey = 'POWERBOARD_ASSET_VERSION';
  static const int _fnvOffset32 = 2166136261;
  static const int _fnvPrime32 = 16777619;

  static Future<AssetHostConfig> prepare() async {
    final allowFileFallback =
        Platform.environment['POWERBOARD_ALLOW_FILE_FALLBACK'] == '1';
    final manifest = await _loadManifest();
    final assets = manifest.keys
        .where((key) => key.startsWith('assets/'))
        .toList()
      ..sort();
    final assetVersion =
        Platform.environment[_versionEnvKey] ??
        await _computeAssetVersion(assets);

    final supportDir = await getApplicationSupportDirectory();
    final baseDir = Directory(
      '${supportDir.path}${Platform.pathSeparator}powerboard_admin_assets',
    );
    if (baseDir.existsSync()) {
      baseDir.deleteSync(recursive: true);
    }
    baseDir.createSync(recursive: true);

    for (final asset in assets) {
      final relative = asset.substring('assets/'.length);
      final outPath = baseDir.path +
          Platform.pathSeparator +
          relative.replaceAll('/', Platform.pathSeparator);
      final outFile = File(outPath);
      final outDir = outFile.parent;
      if (!outDir.existsSync()) {
        outDir.createSync(recursive: true);
      }

      final bytes = await rootBundle.load(asset);
      if (_shouldRewriteAsset(relative)) {
        final text = utf8.decode(bytes.buffer.asUint8List());
        final updated = _applyAssetVersion(text, relative, assetVersion);
        await outFile.writeAsString(updated, flush: true);
      } else {
        final data = bytes.buffer.asUint8List();
        await outFile.writeAsBytes(data, flush: true);
      }
    }

    final entryFile = File(
      baseDir.path + Platform.pathSeparator + 'login.html',
    );
    if (!entryFile.existsSync()) {
      throw StateError('Missing admin login.html asset.');
    }

    final fileUri = Uri.file(entryFile.path, windows: Platform.isWindows)
        .replace(queryParameters: {'v': assetVersion});
    final fileUrl = fileUri.toString();

    final serverUrl = await _startLocalServer(baseDir);
    if (serverUrl != null) {
      final entryUri = Uri.parse(serverUrl).replace(
        queryParameters: {'v': assetVersion},
      );
      return AssetHostConfig(
        entryUrl: entryUri.toString(),
        fileUrl: fileUrl,
        assetsDir: baseDir.path,
        virtualHost: _virtualHost,
        useVirtualHost: false,
        allowFileFallback: allowFileFallback,
      );
    }

    if (!allowFileFallback) {
      throw StateError('Local asset server failed and file fallback disabled.');
    }

    return AssetHostConfig(
      entryUrl: fileUrl,
      fileUrl: fileUrl,
      assetsDir: baseDir.path,
      virtualHost: _virtualHost,
      useVirtualHost: false,
      allowFileFallback: allowFileFallback,
    );
  }

  static Future<Map<String, dynamic>> _loadManifest() async {
    try {
      final manifestRaw = await rootBundle.loadString('AssetManifest.json');
      return jsonDecode(manifestRaw) as Map<String, dynamic>;
    } catch (_) {
      final data = await rootBundle.load('AssetManifest.bin');
      final decoded =
          const StandardMessageCodec().decodeMessage(data.buffer.asByteData());
      if (decoded is Map) {
        return decoded.map(
          (key, value) => MapEntry(key.toString(), value),
        );
      }
    }
    return <String, dynamic>{};
  }

  static Future<String?> _startLocalServer(
    Directory assetsDir,
  ) async {
    try {
      _server ??= _LocalAssetServer(assetsDir);
      final baseUri = await _server!.start();
      final entry = baseUri.replace(
        path: '/login.html',
      );
      return entry.toString();
    } catch (error) {
      return null;
    }
  }

  static bool _shouldRewriteAsset(String relative) {
    final lower = relative.toLowerCase();
    return lower.endsWith('.html') || lower.endsWith('.css');
  }

  static Future<String> _computeAssetVersion(List<String> assets) async {
    var hash = _fnvOffset32;
    for (final asset in assets) {
      hash = _fnv1a32(hash, utf8.encode(asset));
      hash = _fnv1a32(hash, const [0]);
      final bytes = await rootBundle.load(asset);
      hash = _fnv1a32(hash, bytes.buffer.asUint8List());
    }
    return hash.toRadixString(16).padLeft(8, '0');
  }

  static int _fnv1a32(int hash, List<int> data) {
    var h = hash;
    for (final byte in data) {
      h ^= byte;
      h = (h * _fnvPrime32) & 0xFFFFFFFF;
    }
    return h;
  }

  static String _applyAssetVersion(
    String input,
    String relative,
    String version,
  ) {
    final lower = relative.toLowerCase();
    if (lower.endsWith('.html')) {
      return _versionHtml(input, version);
    }
    if (lower.endsWith('.css')) {
      return _versionCss(input, version);
    }
    return input;
  }

  static String _versionHtml(String html, String version) {
    final pattern = RegExp(
      r'(\b(?:href|src|data-include)\b)(\s*=\s*)"([^"]+)"',
      caseSensitive: false,
    );
    return html.replaceAllMapped(pattern, (match) {
      final url = match.group(3) ?? '';
      final updated = _withVersion(url, version);
      if (updated == url) return match.group(0) ?? '';
      return '${match.group(1)}${match.group(2)}"$updated"';
    });
  }

  static String _versionCss(String css, String version) {
    final importPattern = RegExp(
      r"""@import\s+(?:url\()?\s*(['"]?)([^'")]+)\1\s*\)?\s*;""",
      caseSensitive: false,
    );
    var out = css.replaceAllMapped(importPattern, (match) {
      final url = match.group(2) ?? '';
      final updated = _withVersion(url, version);
      if (updated == url) return match.group(0) ?? '';
      return (match.group(0) ?? '').replaceFirst(url, updated);
    });

    final urlPattern = RegExp(r'url\(([^)]+)\)', caseSensitive: false);
    out = out.replaceAllMapped(urlPattern, (match) {
      var raw = (match.group(1) ?? '').trim();
      var quote = '';
      if (raw.startsWith('"') || raw.startsWith("'")) {
        quote = raw.substring(0, 1);
      }
      if (quote.isNotEmpty && raw.endsWith(quote) && raw.length > 1) {
        raw = raw.substring(1, raw.length - 1);
      }
      final updated = _withVersion(raw, version);
      if (updated == raw) return match.group(0) ?? '';
      final wrapped = quote.isNotEmpty ? '$quote$updated$quote' : updated;
      return 'url($wrapped)';
    });

    return out;
  }

  static String _withVersion(String url, String version) {
    if (!_shouldVersionUrl(url)) return url;
    return _appendVersion(url, version);
  }

  static bool _shouldVersionUrl(String url) {
    final lower = url.toLowerCase();
    if (lower.isEmpty) return false;
    if (lower.contains('?')) return false;
    if (lower.startsWith('http://') ||
        lower.startsWith('https://') ||
        lower.startsWith('data:') ||
        lower.startsWith('blob:') ||
        lower.startsWith('mailto:') ||
        lower.startsWith('//')) {
      return false;
    }
    return RegExp(
      r'\.(css|js|html|svg|png|jpg|jpeg|gif|webp|ico|ttf|otf|woff2?|map)$',
    ).hasMatch(lower);
  }

  static String _appendVersion(String url, String version) {
    final hashIndex = url.indexOf('#');
    if (hashIndex == -1) {
      return '$url?v=$version';
    }
    final base = url.substring(0, hashIndex);
    final hash = url.substring(hashIndex);
    return '$base?v=$version$hash';
  }
}

class _LocalAssetServer {
  _LocalAssetServer(this.assetsDir);

  final Directory assetsDir;
  HttpServer? _server;
  Uri? _baseUri;

  static const Map<String, String> _mimeTypes = {
    '.html': 'text/html; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.mjs': 'text/javascript; charset=utf-8',
    '.json': 'application/json; charset=utf-8',
    '.map': 'application/json; charset=utf-8',
    '.svg': 'image/svg+xml',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.jpeg': 'image/jpeg',
    '.gif': 'image/gif',
    '.webp': 'image/webp',
    '.ico': 'image/x-icon',
    '.ttf': 'font/ttf',
    '.otf': 'font/otf',
    '.woff': 'font/woff',
    '.woff2': 'font/woff2',
  };

  Future<Uri> start() async {
    if (_baseUri != null) return _baseUri!;
    _server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    _server!.listen(_handleRequest);
    _baseUri = Uri(
      scheme: 'http',
      host: _server!.address.address,
      port: _server!.port,
    );
    return _baseUri!;
  }

  Future<void> _handleRequest(HttpRequest request) async {
    final response = request.response;
    response.headers.set('Cache-Control', 'no-store, must-revalidate');
    response.headers.set('Pragma', 'no-cache');
    response.headers.set('Expires', '0');
    response.headers.set('Access-Control-Allow-Origin', '*');
    response.headers.set('Access-Control-Allow-Methods', 'GET,HEAD,OPTIONS');
    response.headers.set('Access-Control-Allow-Headers', '*');

    if (request.method.toUpperCase() == 'OPTIONS') {
      response.statusCode = HttpStatus.noContent;
      await response.close();
      return;
    }

    final relative = _sanitizePath(request.uri.path);
    if (relative == null) {
      response.statusCode = HttpStatus.forbidden;
      await response.close();
      return;
    }

    final file = File(
      '${assetsDir.path}${Platform.pathSeparator}$relative',
    );
    if (!file.existsSync()) {
      response.statusCode = HttpStatus.notFound;
      await response.close();
      return;
    }

    final ext = _extensionOf(file.path);
    final mime = _mimeTypes[ext];
    if (mime != null) {
      response.headers.set(HttpHeaders.contentTypeHeader, mime);
    }

    if (request.method.toUpperCase() == 'HEAD') {
      await response.close();
      return;
    }

    await response.addStream(file.openRead());
    await response.close();
  }

  String? _sanitizePath(String path) {
    var raw = Uri.decodeComponent(path);
    if (raw.isEmpty || raw == '/') {
      raw = '/login.html';
    }

    final parts = <String>[];
    for (final segment in raw.split('/')) {
      if (segment.isEmpty || segment == '.') continue;
      if (segment == '..') {
        if (parts.isNotEmpty) {
          parts.removeLast();
        }
        continue;
      }
      parts.add(segment);
    }
    return parts.join(Platform.pathSeparator);
  }

  String _extensionOf(String path) {
    final index = path.lastIndexOf('.');
    if (index == -1) return '';
    return path.substring(index).toLowerCase();
  }

}

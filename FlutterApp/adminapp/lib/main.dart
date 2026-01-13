import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';
import 'package:webview_windows/webview_windows.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final entryUrl = await AssetHost.prepare();
  runApp(AdminWebApp(entryUrl: entryUrl));
}

class AdminWebApp extends StatelessWidget {
  const AdminWebApp({super.key, required this.entryUrl});

  final String entryUrl;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'PowerBoard Admin',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(),
      home: WebviewHost(entryUrl: entryUrl),
    );
  }
}

class WebviewHost extends StatefulWidget {
  const WebviewHost({super.key, required this.entryUrl});

  final String entryUrl;

  @override
  State<WebviewHost> createState() => _WebviewHostState();
}

class _WebviewHostState extends State<WebviewHost> {
  final WebviewController _controller = WebviewController();
  bool _ready = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    _init();
  }

  Future<void> _init() async {
    try {
      await _controller.initialize();
      await _controller.setBackgroundColor(Colors.transparent);
      await _controller.loadUrl(widget.entryUrl);
      if (!mounted) return;
      setState(() => _ready = true);
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = e.toString());
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_error != null) {
      return Scaffold(
        body: Center(child: Text('Failed to start webview: $_error')),
      );
    }

    return Scaffold(
      body: Stack(
        children: [
          Positioned.fill(child: Webview(_controller)),
          if (!_ready)
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
}

class AssetHost {
  static Future<String> prepare() async {
    final manifest = await _loadManifest();
    final assets = manifest.keys
        .where((key) => key.startsWith('assets/admin/'))
        .toList();

    final supportDir = await getApplicationSupportDirectory();
    final baseDir = Directory(
      '${supportDir.path}${Platform.pathSeparator}powerboard_admin_assets',
    );
    if (!baseDir.existsSync()) {
      baseDir.createSync(recursive: true);
    }

    for (final asset in assets) {
      final relative = asset.substring('assets/admin/'.length);
      final outPath = baseDir.path +
          Platform.pathSeparator +
          relative.replaceAll('/', Platform.pathSeparator);
      final outFile = File(outPath);
      final outDir = outFile.parent;
      if (!outDir.existsSync()) {
        outDir.createSync(recursive: true);
      }

      final bytes = await rootBundle.load(asset);
      final data = bytes.buffer.asUint8List();
      if (!outFile.existsSync() || outFile.lengthSync() != data.length) {
        await outFile.writeAsBytes(data, flush: true);
      }
    }

    final entryFile = File(
      baseDir.path + Platform.pathSeparator + 'login.html',
    );
    if (!entryFile.existsSync()) {
      throw StateError('Missing admin login.html asset.');
    }

    final base = _normalizeBase(Platform.environment['POWERBOARD_BASE_URL']);
    final uri = Uri.file(entryFile.path, windows: Platform.isWindows);
    if (base.isNotEmpty) {
      return uri.replace(queryParameters: {'base': base}).toString();
    }
    return uri.toString();
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

  static String _normalizeBase(String? input) {
    var url = (input ?? '').trim();
    if (url.isEmpty) {
      return '';
    }
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
      url = 'http://$url';
    }
    while (url.endsWith('/')) {
      url = url.substring(0, url.length - 1);
    }
    return url;
  }
}

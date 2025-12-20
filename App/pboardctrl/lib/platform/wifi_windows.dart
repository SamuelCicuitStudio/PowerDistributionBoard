import 'dart:io';

import 'wifi_types.dart';

class WifiWindows {
  static Future<String?> currentSsid() async {
    final result = await Process.run(
      'netsh',
      const ['wlan', 'show', 'interfaces'],
      runInShell: true,
    );
    if (result.exitCode != 0) {
      throw Exception(
        'Failed to read Wi-Fi status: ${result.stderr ?? 'unknown error'}',
      );
    }

    final output = (result.stdout ?? '').toString();
    final lines = output.split(RegExp(r'\r?\n'));
    final ssidRe = RegExp(r'^\s*SSID\s*:\s*(.*)\s*$');

    for (final raw in lines) {
      final line = raw.trimRight();
      final ssidMatch = ssidRe.firstMatch(line);
      if (ssidMatch == null) continue;
      final ssid = (ssidMatch.group(1) ?? '').trim();
      if (ssid.isEmpty) continue;
      final lower = ssid.toLowerCase();
      if (lower.contains('not connected')) continue;
      return ssid;
    }

    return null;
  }

  static Future<List<WifiNetwork>> scanNetworks() async {
    final result = await Process.run(
      'netsh',
      const ['wlan', 'show', 'networks', 'mode=bssid'],
      runInShell: true,
    );
    if (result.exitCode != 0) {
      throw Exception(
        'Failed to scan Wi-Fi networks: ${result.stderr ?? 'unknown error'}',
      );
    }

    final output = (result.stdout ?? '').toString();
    final lines = output.split(RegExp(r'\r?\n'));

    final networks = <WifiNetwork>[];
    String? currentSsid;
    int? currentSignal;
    String? currentAuth;

    void flush() {
      final ssid = currentSsid?.trim() ?? '';
      if (ssid.isNotEmpty) {
        networks.add(
          WifiNetwork(
            ssid: ssid,
            signalQuality: currentSignal,
            security: currentAuth,
          ),
        );
      }
      currentSsid = null;
      currentSignal = null;
      currentAuth = null;
    }

    final ssidRe = RegExp(r'^\s*SSID\s+\d+\s*:\s*(.*)\s*$');
    final signalRe = RegExp(r'^\s*Signal\s*:\s*(\d+)\%\s*$');
    final authRe = RegExp(r'^\s*Authentication\s*:\s*(.*)\s*$');

    for (final raw in lines) {
      final line = raw.trimRight();

      final ssidMatch = ssidRe.firstMatch(line);
      if (ssidMatch != null) {
        flush();
        currentSsid = (ssidMatch.group(1) ?? '').trim();
        continue;
      }

      final signalMatch = signalRe.firstMatch(line);
      if (signalMatch != null) {
        currentSignal = int.tryParse(signalMatch.group(1) ?? '');
        continue;
      }

      final authMatch = authRe.firstMatch(line);
      if (authMatch != null) {
        currentAuth = (authMatch.group(1) ?? '').trim();
        continue;
      }
    }

    flush();

    networks.sort((a, b) {
      final as = a.signalQuality ?? -1;
      final bs = b.signalQuality ?? -1;
      return bs.compareTo(as);
    });

    final unique = <String, WifiNetwork>{};
    for (final n in networks) {
      if (n.ssid.isEmpty) continue;
      unique[n.ssid] = n;
    }
    return unique.values.toList();
  }

  static String _wifiProfileXml({
    required String ssid,
    required String password,
  }) {
    final escapedSsid = ssid
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&apos;');

    final escapedPassword = password
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&apos;');

    // WPA2-Personal + AES is what ESP SoftAP typically uses.
    return '''
<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>$escapedSsid</name>
  <SSIDConfig>
    <SSID>
      <name>$escapedSsid</name>
    </SSID>
  </SSIDConfig>
  <connectionType>ESS</connectionType>
  <connectionMode>auto</connectionMode>
  <MSM>
    <security>
      <authEncryption>
        <authentication>WPA2PSK</authentication>
        <encryption>AES</encryption>
        <useOneX>false</useOneX>
      </authEncryption>
      <sharedKey>
        <keyType>passPhrase</keyType>
        <protected>false</protected>
        <keyMaterial>$escapedPassword</keyMaterial>
      </sharedKey>
    </security>
  </MSM>
</WLANProfile>
''';
  }

  static Future<WifiConnectResult> connect({
    required String ssid,
    required String password,
  }) async {
    final tempDir = await Directory.systemTemp.createTemp('pboardctrl-');
    try {
      final profileFile = File('${tempDir.path}\\wifi-profile.xml');
      await profileFile.writeAsString(
        _wifiProfileXml(ssid: ssid, password: password),
      );

      final add = await Process.run(
        'netsh',
        ['wlan', 'add', 'profile', 'filename=${profileFile.path}', 'user=current'],
        runInShell: true,
      );
      if (add.exitCode != 0) {
        return WifiConnectResult(
          ok: false,
          error: 'Failed to add Wi-Fi profile: ${(add.stderr ?? add.stdout).toString().trim()}',
        );
      }

      final connect = await Process.run(
        'netsh',
        ['wlan', 'connect', 'name=$ssid', 'ssid=$ssid'],
        runInShell: true,
      );
      if (connect.exitCode != 0) {
        return WifiConnectResult(
          ok: false,
          error:
              'Failed to connect: ${(connect.stderr ?? connect.stdout).toString().trim()}',
        );
      }

      return const WifiConnectResult(ok: true);
    } catch (err) {
      return WifiConnectResult(ok: false, error: err.toString());
    } finally {
      try {
        await tempDir.delete(recursive: true);
      } catch (_) {}
    }
  }

  static Future<WifiConnectResult> disconnect() async {
    try {
      final result = await Process.run(
        'netsh',
        const ['wlan', 'disconnect'],
        runInShell: true,
      );
      if (result.exitCode != 0) {
        return WifiConnectResult(
          ok: false,
          error:
              'Failed to disconnect: ${(result.stderr ?? result.stdout).toString().trim()}',
        );
      }
      return const WifiConnectResult(ok: true);
    } catch (err) {
      return WifiConnectResult(ok: false, error: err.toString());
    }
  }

  static Future<WifiConnectResult> forgetProfile(String ssid) async {
    final name = ssid.trim();
    if (name.isEmpty) {
      return const WifiConnectResult(ok: false, error: 'SSID is empty.');
    }
    try {
      final result = await Process.run(
        'netsh',
        ['wlan', 'delete', 'profile', 'name="$name"'],
        runInShell: true,
      );
      if (result.exitCode != 0) {
        return WifiConnectResult(
          ok: false,
          error:
              'Failed to forget network: ${(result.stderr ?? result.stdout).toString().trim()}',
        );
      }
      return const WifiConnectResult(ok: true);
    } catch (err) {
      return WifiConnectResult(ok: false, error: err.toString());
    }
  }
}


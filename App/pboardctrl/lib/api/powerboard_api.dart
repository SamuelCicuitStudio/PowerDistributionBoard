import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:http/http.dart' as http;

enum UserRole { admin, user, unknown }

class ConnectResult {
  const ConnectResult._(this.ok, this.role, this.error);

  final bool ok;
  final UserRole role;
  final String? error;

  factory ConnectResult.success(UserRole role) =>
      ConnectResult._(true, role, null);

  factory ConnectResult.failure(String message) =>
      ConnectResult._(false, UserRole.unknown, message);
}

class ApiResult {
  const ApiResult({required this.ok, this.error, this.data});

  final bool ok;
  final String? error;
  final Map<String, dynamic>? data;
}

class ApiException implements Exception {
  ApiException(this.message);
  final String message;

  @override
  String toString() => message;
}

class UnauthorizedException extends ApiException {
  UnauthorizedException() : super('Not authenticated');
}

class PowerBoardApi {
  PowerBoardApi({
    required String baseUrl,
    http.Client? client,
    Duration? timeout,
  })  : baseUrl = _normalizeBaseUrl(baseUrl),
        _client = client ?? http.Client(),
        _timeout = timeout ?? const Duration(seconds: 5);

  final String baseUrl;
  final http.Client _client;
  final Duration _timeout;

  static String _normalizeBaseUrl(String value) {
    final trimmed = value.trim().replaceAll(RegExp(r'/+$'), '');
    if (trimmed.startsWith('http://') || trimmed.startsWith('https://')) {
      return trimmed;
    }
    return 'http://$trimmed';
  }

  Uri _uri(String path, [Map<String, String>? query]) {
    final uri = Uri.parse('$baseUrl$path');
    if (query == null || query.isEmpty) return uri;
    return uri.replace(queryParameters: query);
  }

  Future<ConnectResult> connect({
    required String username,
    required String password,
  }) async {
    final request = http.Request('POST', _uri('/connect'))
      ..followRedirects = false
      ..headers['Content-Type'] = 'application/json'
      ..body = jsonEncode({
        'username': username,
        'password': password,
      });

    late final http.StreamedResponse response;
    try {
      response = await _client.send(request).timeout(_timeout);
    } on TimeoutException {
      return ConnectResult.failure('Connection timed out.');
    } on SocketException {
      return ConnectResult.failure('Connection failed.');
    }
    final body = await response.stream.bytesToString();
    final status = response.statusCode;

    if (status >= 300 && status < 400) {
      final location = response.headers['location'] ?? '';
      if (location.contains('login_failed')) {
        return ConnectResult.failure('Invalid username or password.');
      }
      if (location.contains('admin')) {
        return ConnectResult.success(UserRole.admin);
      }
      if (location.contains('user')) {
        return ConnectResult.success(UserRole.user);
      }
      return ConnectResult.failure('Unexpected login response.');
    }

    if (status == 400 || status == 403 || status == 200) {
      if (body.isNotEmpty) {
        try {
          final data = jsonDecode(body) as Map<String, dynamic>;
          final error = data['error']?.toString();
          if (error != null && error.isNotEmpty) {
            return ConnectResult.failure(error);
          }
        } catch (_) {
          // Ignore non-JSON error responses.
        }
      }
      return ConnectResult.failure('Login failed');
    }

    return ConnectResult.failure('HTTP $status');
  }

  Future<Map<String, dynamic>> getJson(String path) async {
    final response = await _client.get(_uri(path)).timeout(_timeout);
    if (response.statusCode == 403) {
      throw UnauthorizedException();
    }
    if (response.statusCode != 200) {
      throw ApiException('HTTP ${response.statusCode}');
    }
    return jsonDecode(response.body) as Map<String, dynamic>;
  }

  Future<String> getText(String path) async {
    final response = await _client.get(_uri(path)).timeout(_timeout);
    if (response.statusCode == 403) {
      throw UnauthorizedException();
    }
    if (response.statusCode != 200) {
      throw ApiException('HTTP ${response.statusCode}');
    }
    return response.body;
  }

  Future<Map<String, dynamic>?> getJsonOptional(String path) async {
    late final http.Response response;
    try {
      response = await _client.get(_uri(path)).timeout(_timeout);
    } on TimeoutException {
      return null;
    } on SocketException {
      return null;
    }
    if (response.statusCode == 403) {
      throw UnauthorizedException();
    }
    if (response.statusCode == 503) {
      return null;
    }
    if (response.statusCode != 200) {
      throw ApiException('HTTP ${response.statusCode}');
    }
    return jsonDecode(response.body) as Map<String, dynamic>;
  }

  Future<Map<String, dynamic>> postJson(
    String path,
    Map<String, dynamic> payload, {
    bool includeEpoch = false,
  }) async {
    final data = Map<String, dynamic>.from(payload);
    if (includeEpoch) {
      data['epoch'] = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    }

    final response = await _client
        .post(
          _uri(path),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode(data),
        )
        .timeout(_timeout);

    if (response.statusCode == 403) {
      throw UnauthorizedException();
    }
    if (response.statusCode != 200) {
      throw ApiException('HTTP ${response.statusCode}');
    }
    if (response.body.isEmpty) return {};
    return jsonDecode(response.body) as Map<String, dynamic>;
  }

  Future<String?> fetchStatus() async {
    late final http.Response response;
    try {
      response = await _client
          .post(
            _uri('/control'),
            headers: {'Content-Type': 'application/json'},
            body: jsonEncode({'action': 'get', 'target': 'status'}),
          )
          .timeout(_timeout);
    } on TimeoutException {
      return null;
    } on SocketException {
      return null;
    }

    if (response.statusCode == 403) {
      throw UnauthorizedException();
    }
    if (response.statusCode != 200) {
      return null;
    }
    final data = jsonDecode(response.body) as Map<String, dynamic>;
    return data['state']?.toString();
  }

  Future<ApiResult> sendControlSet(String target, dynamic value) async {
    final payload = {
      'action': 'set',
      'target': target,
      if (value != null) 'value': value,
      'epoch': DateTime.now().millisecondsSinceEpoch ~/ 1000,
    };

    final response = await _client
        .post(
          _uri('/control'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode(payload),
        )
        .timeout(_timeout);

    if (response.statusCode == 403) {
      throw UnauthorizedException();
    }

    Map<String, dynamic> data = {};
    if (response.body.isNotEmpty) {
      try {
        data = jsonDecode(response.body) as Map<String, dynamic>;
      } catch (_) {
        data = {};
      }
    }

    if (response.statusCode != 200) {
      return ApiResult(ok: false, error: data['error']?.toString());
    }
    if (data['error'] != null) {
      return ApiResult(ok: false, error: data['error']?.toString());
    }
    return ApiResult(ok: true, data: data);
  }

  Future<void> disconnect() async {
    await _client
        .post(
          _uri('/disconnect'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({'action': 'disconnect'}),
        )
        .timeout(_timeout);
  }

  void dispose() {
    _client.close();
  }
}

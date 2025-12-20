import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';

import '../api/powerboard_api.dart';
import '../l10n/app_strings.dart';
import 'admin_screen.dart';
import 'connection_screen.dart';

class LoginScreen extends StatefulWidget {
  const LoginScreen({super.key, required this.baseUrl});

  final String baseUrl;

  @override
  State<LoginScreen> createState() => _LoginScreenState();
}

class _LoginScreenState extends State<LoginScreen> {
  final TextEditingController _usernameController = TextEditingController();
  final TextEditingController _passwordController = TextEditingController();
  bool _busy = false;
  String? _error;

  @override
  void dispose() {
    _usernameController.dispose();
    _passwordController.dispose();
    super.dispose();
  }

  Future<void> _submit() async {
    final strings = context.strings;
    final username = _usernameController.text.trim();
    final password = _passwordController.text.trim();
    if (username.isEmpty || password.isEmpty) {
      setState(() => _error = strings.t('Enter both username and password.'));
      return;
    }

    setState(() {
      _busy = true;
      _error = null;
    });

    final api = PowerBoardApi(baseUrl: widget.baseUrl);
    try {
      final result = await api.connect(username: username, password: password);
      if (!result.ok) {
        setState(() => _error = result.error ?? strings.t('Login failed.'));
        return;
      }
      if (result.role != UserRole.admin) {
        setState(
          () => _error = strings.t('Only Admin mode is supported in this app.'),
        );
        return;
      }
      if (!mounted) return;
      Navigator.of(context).pushReplacement(
        MaterialPageRoute(
          builder: (context) => AdminScreen(baseUrl: widget.baseUrl),
        ),
      );
    } on TimeoutException {
      setState(() => _error = strings.t('Connection timed out.'));
    } on SocketException {
      setState(() => _error = strings.t('Unable to reach the device.'));
    } catch (err) {
      setState(
        () => _error = strings.t('Login failed: {err}', {'err': err.toString()}),
      );
    } finally {
      api.dispose();
      if (mounted) {
        setState(() => _busy = false);
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final strings = context.strings;
    return Scaffold(
      appBar: AppBar(title: Text(strings.t('Login'))),
      body: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 420),
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
                padding: const EdgeInsets.all(22),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      strings.t('PowerBoard Admin'),
                      style: theme.textTheme.titleLarge?.copyWith(
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                    const SizedBox(height: 6),
                    Text(
                      widget.baseUrl,
                      style: theme.textTheme.bodySmall?.copyWith(
                        color: theme.colorScheme.onSurface.withAlpha(153),
                      ),
                    ),
                    const SizedBox(height: 18),
                    TextField(
                      controller: _usernameController,
                      decoration: InputDecoration(
                        labelText: strings.t('Username'),
                        border: OutlineInputBorder(),
                      ),
                    ),
                    const SizedBox(height: 12),
                    TextField(
                      controller: _passwordController,
                      obscureText: true,
                      decoration: InputDecoration(
                        labelText: strings.t('Password'),
                        border: OutlineInputBorder(),
                      ),
                      onSubmitted: (_) => _submit(),
                    ),
                    if (_error != null) ...[
                      const SizedBox(height: 12),
                      Text(
                        _error!,
                        style: theme.textTheme.bodySmall?.copyWith(
                          color: theme.colorScheme.error,
                        ),
                      ),
                    ],
                    const SizedBox(height: 18),
                    SizedBox(
                      width: double.infinity,
                      child: FilledButton(
                        onPressed: _busy ? null : _submit,
                        child: _busy
                            ? const SizedBox(
                                width: 18,
                                height: 18,
                                child: CircularProgressIndicator(strokeWidth: 2),
                              )
                            : Text(strings.t('Login')),
                      ),
                    ),
                    const SizedBox(height: 8),
                    Align(
                      alignment: Alignment.centerRight,
                      child: TextButton(
                        onPressed: _busy
                            ? null
                            : () {
                                Navigator.of(context).pushReplacement(
                                  MaterialPageRoute(
                                    builder: (context) => const ConnectionScreen(),
                                  ),
                                );
                              },
                        child: Text(strings.t('Change connection')),
                      ),
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

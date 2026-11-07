import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/board/board_address.dart';
import '../../../core/board/board_api.dart';

class SettingsScreen extends ConsumerStatefulWidget {
  const SettingsScreen({super.key});

  @override
  ConsumerState<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends ConsumerState<SettingsScreen> {
  late final TextEditingController _addressField = TextEditingController(
    text: ref.read(boardAddressProvider),
  );
  late final TextEditingController _tokenField = TextEditingController(
    text: ref.read(boardTokenProvider),
  );
  bool _isTesting = false;

  @override
  void dispose() {
    _addressField.dispose();
    _tokenField.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    await ref.read(boardAddressProvider.notifier).save(_addressField.text);
    await ref.read(boardTokenProvider.notifier).save(_tokenField.text);
    if (!mounted) return;
    ScaffoldMessenger.of(context)
        .showSnackBar(const SnackBar(content: Text('Saved')));
  }

  /// Asks the board for `/api/status` with the typed address and token.
  Future<void> _testConnection() async {
    final messenger = ScaffoldMessenger.of(context);
    final baseUri = boardBaseUri(_addressField.text);
    if (baseUri == null) {
      messenger.showSnackBar(
        const SnackBar(content: Text('Not a valid address')),
      );
      return;
    }
    setState(() => _isTesting = true);
    final client = ref.read(httpClientFactoryProvider)();
    String result;
    try {
      final status = await fetchBoardStatus(
        client,
        baseUri,
        normalizeToken(_tokenField.text),
      );
      result = '🟢 Connected: ${_describe(status)}';
    } on BoardAuthException {
      result = '🔴 Wrong token: see the board log or console "token"';
    } on BoardHttpException catch (error) {
      result = '🔴 $error (firmware too old?)';
    } on TimeoutException {
      result = '🔴 No answer in 3 s';
    } catch (error) {
      result = '🔴 Cannot reach the board: $error';
    } finally {
      client.close();
    }
    if (!mounted) return;
    setState(() => _isTesting = false);
    messenger.showSnackBar(SnackBar(content: Text(result)));
  }

  static String _describe(BoardStatus status) {
    final faces = status.faceCount == null
        ? 'recognition off'
        : '${status.faceCount} face${status.faceCount == 1 ? '' : 's'}';
    final door = status.isDoorOpen ? 'door open' : 'door locked';
    return '$faces, $door';
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Settings')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          TextField(
            controller: _addressField,
            decoration: const InputDecoration(
              labelText: 'Board address',
              hintText: '192.168.1.50 or 127.0.0.1:8080',
              helperText: 'IP from the board log or the console "status"',
              border: OutlineInputBorder(),
            ),
            keyboardType: TextInputType.url,
            autocorrect: false,
            onSubmitted: (_) => _save(),
          ),
          const SizedBox(height: 16),
          TextField(
            controller: _tokenField,
            decoration: const InputDecoration(
              labelText: 'Token',
              hintText: 'AB9A9JQ2',
              helperText: 'From the board log or the console "token"',
              border: OutlineInputBorder(),
            ),
            textCapitalization: TextCapitalization.characters,
            autocorrect: false,
            enableSuggestions: false,
            onSubmitted: (_) => _save(),
          ),
          const SizedBox(height: 16),
          Row(
            children: [
              FilledButton(onPressed: _save, child: const Text('Save')),
              const SizedBox(width: 16),
              OutlinedButton(
                onPressed: _isTesting ? null : _testConnection,
                child: Text(_isTesting ? 'Testing…' : 'Test connection'),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

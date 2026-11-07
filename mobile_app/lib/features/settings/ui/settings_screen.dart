import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/board/board_address.dart';
import '../../../core/board/board_api.dart';
import '../../../core/mqtt/broker_notifier.dart';
import 'board_settings_form.dart';
import 'broker_settings_screen.dart';

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
    // The typed address and token, not the saved ones: test before Save.
    final api = BoardApi(
      baseUri: baseUri,
      token: normalizeToken(_tokenField.text),
      newClient: ref.read(httpClientFactoryProvider),
    );
    String result;
    try {
      final status = await api.status();
      result = '🟢 Connected: ${_describe(status)}';
    } on BoardAuthException {
      result = '🔴 Wrong token: see the board log or console "token"';
    } on BoardHttpException catch (error) {
      result = '🔴 $error (firmware too old?)';
    } on TimeoutException {
      result = '🔴 No answer in 3 s';
    } catch (error) {
      result = '🔴 Cannot reach the board: $error';
    }
    if (!mounted) return;
    setState(() => _isTesting = false);
    messenger.showSnackBar(SnackBar(content: Text(result)));
  }

  void _open(Widget page) {
    Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => page));
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
          const SizedBox(height: 24),
          const Divider(),
          ListTile(
            leading: const Icon(Icons.cloud_sync),
            title: const Text('Broker (MQTT)'),
            subtitle: Text(describeBrokerState(ref.watch(brokerProvider))),
            trailing: const Icon(Icons.chevron_right),
            onTap: () => _open(const BrokerSettingsScreen()),
          ),
          ListTile(
            leading: const Icon(Icons.photo_camera),
            title: const Text('Camera picture'),
            subtitle: const Text('Brightness, exposure, flip, video quality'),
            trailing: const Icon(Icons.chevron_right),
            onTap: () => _open(
              const BoardSettingsForm(
                title: 'Camera picture',
                path: '/api/camera',
                numbers: cameraFields,
                switches: cameraSwitches,
              ),
            ),
          ),
          ListTile(
            leading: const Icon(Icons.rule),
            title: const Text('Unlock rules'),
            subtitle: const Text('Match needed, door open time, approval'),
            trailing: const Icon(Icons.chevron_right),
            onTap: () => _open(
              const BoardSettingsForm(
                title: 'Unlock rules',
                path: '/api/rules',
                numbers: rulesFields,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

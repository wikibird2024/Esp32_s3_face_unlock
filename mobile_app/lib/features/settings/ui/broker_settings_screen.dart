import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/mqtt/broker_connection.dart';
import '../../../core/mqtt/broker_messages.dart';
import '../../../core/mqtt/broker_notifier.dart';
import '../../../core/mqtt/broker_settings.dart';
import '../../../core/notify/door_notifications.dart';

/// Plain MQTT port, used when TLS is switched off.
const brokerPlainPort = 1883;

/// Settings → Broker (MQTT): where the app meets the board away from the
/// home Wi-Fi (`docs/mqtt_contract.md`).
class BrokerSettingsScreen extends ConsumerStatefulWidget {
  const BrokerSettingsScreen({super.key});

  @override
  ConsumerState<BrokerSettingsScreen> createState() =>
      _BrokerSettingsScreenState();
}

class _BrokerSettingsScreenState extends ConsumerState<BrokerSettingsScreen> {
  late final BrokerSettings _saved = ref.read(brokerSettingsProvider);
  late bool _isEnabled = _saved.isEnabled;
  late bool _useTls = _saved.useTls;
  late final _hostField = TextEditingController(text: _saved.host);
  late final _portField = TextEditingController(text: '${_saved.port}');
  late final _usernameField = TextEditingController(text: _saved.username);
  late final _passwordField = TextEditingController(text: _saved.password);
  late final _deviceField = TextEditingController(text: _saved.device);
  late final _prefixField = TextEditingController(text: _saved.topicPrefix);
  late final _phoneIdField = TextEditingController(text: _saved.phoneId);
  late final _phoneKeyField = TextEditingController(text: _saved.phoneKeyHex);
  bool _isTesting = false;

  List<TextEditingController> get _fields => [
    _hostField,
    _portField,
    _usernameField,
    _passwordField,
    _deviceField,
    _prefixField,
    _phoneIdField,
    _phoneKeyField,
  ];

  @override
  void dispose() {
    for (final field in _fields) {
      field.dispose();
    }
    super.dispose();
  }

  /// The settings as typed, not yet saved.
  BrokerSettings get _typed => BrokerSettings(
    isEnabled: _isEnabled,
    host: _hostField.text,
    port: int.tryParse(_portField.text.trim()) ?? 0,
    useTls: _useTls,
    username: _usernameField.text,
    password: _passwordField.text,
    device: _deviceField.text,
    topicPrefix: _prefixField.text,
    phoneId: _phoneIdField.text,
    phoneKeyHex: _phoneKeyField.text,
  ).trimmed();

  void _setTls(bool useTls) {
    // Follow the switch with the usual port, unless the user typed another.
    final port = int.tryParse(_portField.text.trim());
    if (useTls && port == brokerPlainPort) _portField.text = '$brokerTlsPort';
    if (!useTls && port == brokerTlsPort) _portField.text = '$brokerPlainPort';
    setState(() => _useTls = useTls);
  }

  Future<void> _save() async {
    final messenger = ScaffoldMessenger.of(context);
    final typed = _typed;
    final problem = brokerSettingsProblem(typed);
    if (typed.isEnabled && problem != null) {
      messenger.showSnackBar(SnackBar(content: Text('🔴 $problem')));
      return;
    }
    await ref.read(brokerSettingsProvider.notifier).save(typed);
    if (typed.isEnabled) {
      await ref.read(doorNotificationsProvider).requestPermission();
    }
    messenger.showSnackBar(const SnackBar(content: Text('Saved')));
  }

  /// Logs in and subscribes with the typed settings, then disconnects.
  Future<void> _testConnection() async {
    final messenger = ScaffoldMessenger.of(context);
    final typed = _typed;
    final problem = brokerSettingsProblem(typed);
    if (problem != null) {
      messenger.showSnackBar(SnackBar(content: Text('🔴 $problem')));
      return;
    }
    setState(() => _isTesting = true);
    final connection = ref.read(brokerConnectionFactoryProvider)(typed);
    String result;
    try {
      await connection.connect(subscribeTopics: typed.subscribeTopics);
      result = '🟢 Broker accepted the login and the topics';
    } on BrokerException catch (error) {
      result = '🔴 ${error.reason}';
    } catch (error) {
      result = '🔴 Cannot connect: $error';
    } finally {
      await connection.disconnect();
    }
    if (!mounted) return;
    setState(() => _isTesting = false);
    messenger.showSnackBar(SnackBar(content: Text(result)));
  }

  @override
  Widget build(BuildContext context) {
    final broker = ref.watch(brokerProvider);
    final typed = _typed;
    final problem = brokerSettingsProblem(typed);
    final defaultPrefix = defaultTopicPrefix(
      host: typed.host,
      username: typed.username,
      device: typed.device,
    );
    return Scaffold(
      appBar: AppBar(title: const Text('Broker (MQTT)')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Text(describeBrokerState(broker)),
          if (broker.throttleNotice != null)
            Text('🟡 Rate limit: ${broker.throttleNotice}'),
          const SizedBox(height: 8),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('Use MQTT'),
            subtitle: const Text('Requests and events away from home'),
            value: _isEnabled,
            onChanged: (value) => setState(() => _isEnabled = value),
          ),
          const _Heading('Broker'),
          _field(
            _hostField,
            'Host',
            hint: adafruitHost,
            keyboard: TextInputType.url,
          ),
          Row(
            children: [
              Expanded(
                child: _field(
                  _portField,
                  'Port',
                  keyboard: TextInputType.number,
                ),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  title: const Text('TLS'),
                  value: _useTls,
                  onChanged: _setTls,
                ),
              ),
            ],
          ),
          _field(
            _usernameField,
            'Username',
            helper: 'Adafruit IO: your account name',
          ),
          _field(
            _passwordField,
            'Password / AIO key',
            helper: 'Adafruit IO: My Key → Active Key',
            isSecret: true,
          ),
          const _Heading('Board'),
          _field(
            _deviceField,
            'Board id',
            hint: 'fu-a1b2c3',
            helper: 'fu- and the last 6 hex digits of the board MAC',
          ),
          _field(
            _prefixField,
            'Topic prefix',
            hint: defaultPrefix,
            helper: 'Empty: $defaultPrefix',
          ),
          const _Heading('This phone (signed answers)'),
          _field(
            _phoneIdField,
            'Phone id',
            hint: 'p1',
            helper: 'From pairing; empty: answers are not signed',
          ),
          _field(
            _phoneKeyField,
            'Phone key (hex)',
            helper: 'From pairing, at least 32 hex digits',
            isSecret: true,
          ),
          if (problem != null) ...[
            const SizedBox(height: 8),
            Text('🟡 $problem'),
          ],
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

  Widget _field(
    TextEditingController controller,
    String label, {
    String? hint,
    String? helper,
    TextInputType? keyboard,
    bool isSecret = false,
  }) => Padding(
    padding: const EdgeInsets.only(bottom: 12),
    child: TextField(
      controller: controller,
      decoration: InputDecoration(
        labelText: label,
        hintText: hint,
        helperText: helper,
        border: const OutlineInputBorder(),
      ),
      keyboardType: keyboard,
      obscureText: isSecret,
      autocorrect: false,
      enableSuggestions: false,
      onChanged: (_) => setState(() {}),
    ),
  );
}

class _Heading extends StatelessWidget {
  const _Heading(this.title);

  final String title;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(top: 16, bottom: 8),
    child: Text(title, style: Theme.of(context).textTheme.titleMedium),
  );
}

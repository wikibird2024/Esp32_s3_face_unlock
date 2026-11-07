import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math';

import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';

import 'broker_connection.dart';
import 'broker_settings.dart';

/// Seconds between MQTT pings while idle.
const brokerKeepAliveSeconds = 30;

/// Give up a connect attempt after this long.
const brokerConnectTimeout = Duration(seconds: 8);

/// [BrokerConnection] over TCP (TLS when the settings ask for it) with the
/// `mqtt_client` package. The only file that knows that package.
class MqttClientConnection implements BrokerConnection {
  MqttClientConnection(this.settings);

  final BrokerSettings settings;

  final _messages = StreamController<BrokerMessage>();
  MqttServerClient? _client;
  StreamSubscription<List<MqttReceivedMessage<MqttMessage>>>? _updates;

  @override
  Stream<BrokerMessage> get messages => _messages.stream;

  @override
  Future<void> connect({required List<String> subscribeTopics}) async {
    if (_client != null) throw StateError('connect() called twice');
    final client =
        MqttServerClient.withPort(settings.host, _newClientId(), settings.port)
          ..secure = settings.useTls
          ..keepAlivePeriod = brokerKeepAliveSeconds
          ..connectTimeoutPeriod = brokerConnectTimeout.inMilliseconds
          ..autoReconnect = false
          ..onDisconnected = _closeMessages;
    // Off: with logging on, the library prints the password.
    client.logging(on: false);
    client.setProtocolV311();
    _client = client;

    try {
      await client.connect(
        settings.username.isEmpty ? null : settings.username,
        settings.password.isEmpty ? null : settings.password,
      );
    } on Exception catch (error) {
      final reason = _describeFailure(client.connectionStatus, error);
      await disconnect();
      throw BrokerException(reason);
    }
    if (client.connectionStatus?.state != MqttConnectionState.connected) {
      final reason = _describeFailure(client.connectionStatus, null);
      await disconnect();
      throw BrokerException(reason);
    }

    _updates = client.updates?.listen(_onUpdates);
    try {
      for (final topic in subscribeTopics) {
        client.subscribe(topic, MqttQos.atLeastOnce);
      }
    } on Exception catch (error) {
      await disconnect();
      throw BrokerException('Subscribe failed: $error');
    }
  }

  @override
  Future<void> publish(String topic, String payload) async {
    final client = _client;
    if (client == null ||
        client.connectionStatus?.state != MqttConnectionState.connected) {
      throw const BrokerException('Not connected to the broker');
    }
    final data = MqttClientPayloadBuilder().addUTF8String(payload).payload!;
    try {
      client.publishMessage(topic, MqttQos.atLeastOnce, data);
    } on Exception catch (error) {
      throw BrokerException('Send failed: $error');
    }
  }

  @override
  Future<void> disconnect() async {
    final client = _client;
    if (client != null &&
        client.connectionStatus?.state != MqttConnectionState.disconnected) {
      client.disconnect();
    }
    _closeMessages();
  }

  void _onUpdates(List<MqttReceivedMessage<MqttMessage>> batch) {
    for (final received in batch) {
      final message = received.payload;
      if (message is! MqttPublishMessage || _messages.isClosed) continue;
      _messages.add(
        BrokerMessage(
          received.topic,
          utf8.decode(message.payload.message, allowMalformed: true),
        ),
      );
    }
  }

  void _closeMessages() {
    _updates?.cancel();
    _updates = null;
    if (!_messages.isClosed) _messages.close();
  }

  /// Brokers drop the older of two links with the same client id, so every
  /// connection gets a fresh random one.
  static String _newClientId() {
    final random = Random.secure();
    final suffix = List.generate(
      8,
      (_) => random.nextInt(16).toRadixString(16),
    ).join();
    return 'fu-app-$suffix';
  }

  static String _describeFailure(
    MqttClientConnectionStatus? status,
    Exception? error,
  ) {
    switch (status?.returnCode) {
      case MqttConnectReturnCode.badUsernameOrPassword:
      case MqttConnectReturnCode.notAuthorized:
        return 'Broker refused the username or key';
      case MqttConnectReturnCode.identifierRejected:
        return 'Broker refused the client id';
      case MqttConnectReturnCode.brokerUnavailable:
        return 'Broker unavailable, try later';
      default:
        break;
    }
    if (error is SocketException) return 'Cannot reach the broker';
    if (error is TlsException) {
      return 'TLS failed: check the port and the TLS switch';
    }
    return 'No answer from the broker';
  }
}

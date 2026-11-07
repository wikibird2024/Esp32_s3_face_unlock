import 'dart:async';
import 'dart:convert';

import 'package:face_unlock/core/board/board_address.dart';
import 'package:face_unlock/core/mqtt/broker_connection.dart';
import 'package:face_unlock/core/mqtt/broker_notifier.dart';
import 'package:face_unlock/core/mqtt/broker_settings.dart';
import 'package:face_unlock/core/notify/door_notifications.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Complete settings for a non-Adafruit test broker.
const testBrokerSettings = BrokerSettings(
  isEnabled: true,
  host: 'broker.test',
  port: 1883,
  useTls: false,
  device: 'fu-a1b2c3',
);

/// Topics of [testBrokerSettings].
const testRequestTopic = 'faceunlock/fu-a1b2c3/approval-request';
const testAnswerTopic = 'faceunlock/fu-a1b2c3/approval-answer';
const testEventTopic = 'faceunlock/fu-a1b2c3/event';
const testStatusTopic = 'faceunlock/fu-a1b2c3/status';

/// A [BrokerConnection] the test drives by hand: [receive] plays the
/// board, [drop] cuts the link, [published] records what the app sent.
class FakeBrokerConnection implements BrokerConnection {
  FakeBrokerConnection(this.settings, {this.connectFailure});

  final BrokerSettings settings;
  final BrokerException? connectFailure;
  final _messages = StreamController<BrokerMessage>();
  final published = <BrokerMessage>[];
  List<String> subscribedTopics = const [];
  bool isConnected = false;
  bool wasDisconnected = false;

  @override
  Future<void> connect({required List<String> subscribeTopics}) async {
    final failure = connectFailure;
    if (failure != null) throw failure;
    subscribedTopics = subscribeTopics;
    isConnected = true;
  }

  @override
  Stream<BrokerMessage> get messages => _messages.stream;

  @override
  Future<void> publish(String topic, String payload) async {
    if (!isConnected) throw const BrokerException('Not connected');
    published.add(BrokerMessage(topic, payload));
  }

  @override
  Future<void> disconnect() async {
    isConnected = false;
    wasDisconnected = true;
    if (!_messages.isClosed) await _messages.close();
  }

  void receive(String topic, String payload) =>
      _messages.add(BrokerMessage(topic, payload));

  void drop() {
    isConnected = false;
    _messages.close();
  }
}

/// Makes [FakeBrokerConnection]s and keeps them, newest last.
class FakeBrokerFactory {
  final connections = <FakeBrokerConnection>[];

  /// Connect attempts fail with this while set.
  BrokerException? failure;

  FakeBrokerConnection get latest => connections.last;

  BrokerConnection call(BrokerSettings settings) {
    final connection = FakeBrokerConnection(settings, connectFailure: failure);
    connections.add(connection);
    return connection;
  }
}

/// Records notifications instead of showing them.
class RecordingNotifications implements DoorNotifications {
  final shownRequests = <int>[];
  int clearCount = 0;

  @override
  Future<bool> requestPermission() async => true;

  @override
  Future<void> showVisitor({
    required int requestId,
    required String name,
  }) async => shownRequests.add(requestId);

  @override
  Future<void> clearVisitor() async => clearCount++;
}

/// A container with [settings] saved and the broker kept alive, like the app
/// root does. [preferences] adds other saved values (board address…).
Future<ProviderContainer> brokerTestContainer({
  required FakeBrokerFactory factory,
  BrokerSettings settings = testBrokerSettings,
  RecordingNotifications? notifications,
  Map<String, Object> preferences = const {},
  List<Override> overrides = const [],
}) async {
  SharedPreferences.setMockInitialValues({
    'broker_settings': jsonEncode(settings.toJson()),
    ...preferences,
  });
  final saved = await SharedPreferences.getInstance();
  final container = ProviderContainer(
    overrides: [
      sharedPreferencesProvider.overrideWithValue(saved),
      brokerConnectionFactoryProvider.overrideWithValue(factory.call),
      doorNotificationsProvider.overrideWithValue(
        notifications ?? RecordingNotifications(),
      ),
      ...overrides,
    ],
  );
  container.listen(brokerProvider, (_, _) {});
  return container;
}

/// `approval-request` payload as the board sends it.
String requestPayload({
  int requestId = 1234,
  String name = 'Hao',
  int expiresSeconds = 30,
}) => jsonEncode({
  'request': requestId,
  'face': 4,
  'name': name,
  'similarity': 0.71,
  'expires_s': expiresSeconds,
});

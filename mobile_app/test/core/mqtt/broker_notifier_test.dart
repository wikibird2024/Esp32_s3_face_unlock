import 'dart:convert';

import 'package:face_unlock/core/mqtt/broker_connection.dart';
import 'package:face_unlock/core/mqtt/broker_messages.dart';
import 'package:face_unlock/core/mqtt/broker_notifier.dart';
import 'package:face_unlock/core/mqtt/broker_settings.dart';
import 'package:fake_async/fake_async.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'fake_broker.dart';

/// Builds the container inside [time], so the notifier's timers are fake.
ProviderContainer _start(
  FakeAsync time,
  FakeBrokerFactory factory, {
  BrokerSettings settings = testBrokerSettings,
  RecordingNotifications? notifications,
}) {
  late ProviderContainer container;
  brokerTestContainer(
    factory: factory,
    settings: settings,
    notifications: notifications,
  ).then((created) => container = created);
  time.flushMicrotasks();
  return container;
}

void main() {
  test('switched off: no connection, state off', () {
    fakeAsync((time) {
      final factory = FakeBrokerFactory();
      final container = _start(
        time,
        factory,
        settings: testBrokerSettings.copyWith(isEnabled: false),
      );

      expect(container.read(brokerProvider).link, BrokerLink.off);
      expect(factory.connections, isEmpty);
      container.dispose();
    });
  });

  test('incomplete settings: off with the problem, no connection', () {
    fakeAsync((time) {
      final factory = FakeBrokerFactory();
      final container = _start(
        time,
        factory,
        settings: testBrokerSettings.copyWith(device: ''),
      );

      final state = container.read(brokerProvider);
      expect(state.link, BrokerLink.off);
      expect(state.problem, 'Enter the board id (fu-xxxxxx)');
      expect(factory.connections, isEmpty);
      container.dispose();
    });
  });

  test('connects and subscribes to request, event and status', () {
    fakeAsync((time) {
      final factory = FakeBrokerFactory();
      final container = _start(time, factory);

      expect(container.read(brokerProvider).link, BrokerLink.connected);
      expect(factory.latest.subscribedTopics, [
        testRequestTopic,
        testEventTopic,
        testStatusTopic,
      ]);
      container.dispose();
      expect(factory.latest.wasDisconnected, isTrue);
    });
  });

  test('a request is shown and notified, then expires', () {
    fakeAsync((time) {
      final factory = FakeBrokerFactory();
      final notifications = RecordingNotifications();
      final container = _start(time, factory, notifications: notifications);

      factory.latest.receive(
        testRequestTopic,
        requestPayload(expiresSeconds: 10),
      );
      time.flushMicrotasks();

      final waiting = container.read(brokerProvider).waiting;
      expect(waiting?.requestId, 1234);
      expect(waiting?.name, 'Hao');
      expect(notifications.shownRequests, [1234]);

      time.elapse(const Duration(seconds: 10));
      expect(container.read(brokerProvider).waiting, isNull);
      expect(notifications.clearCount, 1);
      container.dispose();
    });
  });

  test('an ending event clears the request and is kept, newest first', () {
    fakeAsync((time) {
      final factory = FakeBrokerFactory();
      final container = _start(time, factory);
      factory.latest
        ..receive(testRequestTopic, requestPayload())
        ..receive(
          testEventTopic,
          jsonEncode({'type': 'approval_requested', 'request': 1234}),
        )
        ..receive(
          testEventTopic,
          jsonEncode({'type': 'approval_denied', 'request': 1234}),
        );
      time.flushMicrotasks();

      final state = container.read(brokerProvider);
      expect(state.waiting, isNull);
      expect(state.events.map((event) => event.type), [
        'approval_denied',
        'approval_requested',
      ]);
      container.dispose();
    });
  });

  test('keeps at most $maxBrokerEvents events', () {
    fakeAsync((time) {
      final factory = FakeBrokerFactory();
      final container = _start(time, factory);
      for (var index = 0; index < maxBrokerEvents + 5; index++) {
        factory.latest.receive(
          testEventTopic,
          jsonEncode({'type': 'face_rejected', 'request': index + 1}),
        );
      }
      time.flushMicrotasks();

      final events = container.read(brokerProvider).events;
      expect(events, hasLength(maxBrokerEvents));
      expect(events.first.requestId, maxBrokerEvents + 5);
      container.dispose();
    });
  });

  test('status sets board online; bad payloads are counted, not fatal', () {
    fakeAsync((time) {
      final factory = FakeBrokerFactory();
      final container = _start(time, factory);
      factory.latest
        ..receive(testStatusTopic, '"online"')
        ..receive(testRequestTopic, 'garbage')
        ..receive(testEventTopic, '{}')
        ..receive(testStatusTopic, 'maybe');
      time.flushMicrotasks();

      final state = container.read(brokerProvider);
      expect(state.boardOnline, isTrue);
      expect(state.droppedMessages, 3);
      expect(state.link, BrokerLink.connected);
      container.dispose();
    });
  });

  test('answer publishes a signed payload and clears the request', () {
    fakeAsync((time) {
      final factory = FakeBrokerFactory();
      final container = _start(
        time,
        factory,
        settings: testBrokerSettings.copyWith(
          phoneId: 'p1',
          phoneKeyHex: '00112233445566778899aabbccddeeff',
        ),
      );
      factory.latest.receive(testRequestTopic, requestPayload());
      time.flushMicrotasks();

      container
          .read(brokerProvider.notifier)
          .answer(requestId: 1234, approve: true);
      time.flushMicrotasks();

      final sent = factory.latest.published.single;
      expect(sent.topic, testAnswerTopic);
      expect(jsonDecode(sent.payload), {
        'request': 1234,
        'approve': true,
        'phone': 'p1',
        'sig': signAnswer(
          phoneKeyHex: '00112233445566778899aabbccddeeff',
          device: 'fu-a1b2c3',
          requestId: 1234,
          approve: true,
        ),
      });
      expect(container.read(brokerProvider).waiting, isNull);
      container.dispose();
    });
  });

  test('answer while not connected throws BrokerException', () {
    fakeAsync((time) {
      final factory = FakeBrokerFactory()
        ..failure = const BrokerException('Cannot reach the broker');
      final container = _start(time, factory);

      Object? error;
      container
          .read(brokerProvider.notifier)
          .answer(requestId: 1, approve: true)
          .catchError((Object caught) {
            error = caught;
          });
      time.flushMicrotasks();

      expect(error, isA<BrokerException>());
      container.dispose();
    });
  });

  test('failed connect retries after 1 s, then 2 s, then connects', () {
    fakeAsync((time) {
      final factory = FakeBrokerFactory()
        ..failure = const BrokerException('Cannot reach the broker');
      final container = _start(time, factory);

      var state = container.read(brokerProvider);
      expect(state.link, BrokerLink.offline);
      expect(state.problem, 'Cannot reach the broker');
      expect(state.retryIn, const Duration(seconds: 1));
      expect(factory.connections, hasLength(1));

      time.elapse(const Duration(seconds: 1));
      expect(factory.connections, hasLength(2));
      expect(
        container.read(brokerProvider).retryIn,
        const Duration(seconds: 2),
      );

      factory.failure = null;
      time.elapse(const Duration(seconds: 2));
      state = container.read(brokerProvider);
      expect(factory.connections, hasLength(3));
      expect(state.link, BrokerLink.connected);
      expect(state.problem, isNull);
      container.dispose();
    });
  });

  test('a dropped link reconnects, and Retry skips the wait', () {
    fakeAsync((time) {
      final factory = FakeBrokerFactory();
      final container = _start(time, factory);

      factory.latest.drop();
      time.flushMicrotasks();
      expect(container.read(brokerProvider).link, BrokerLink.offline);
      expect(
        container.read(brokerProvider).problem,
        'Lost the link to the broker',
      );

      container.read(brokerProvider.notifier).retryNow();
      time.flushMicrotasks();
      expect(container.read(brokerProvider).link, BrokerLink.connected);
      expect(factory.connections, hasLength(2));
      container.dispose();
    });
  });

  test('new settings close the old link and open a new one', () {
    fakeAsync((time) {
      final factory = FakeBrokerFactory();
      final container = _start(time, factory);
      final first = factory.latest;

      container
          .read(brokerSettingsProvider.notifier)
          .save(testBrokerSettings.copyWith(device: 'fu-000001'));
      time.elapse(Duration.zero);

      expect(first.wasDisconnected, isTrue);
      expect(factory.connections, hasLength(2));
      expect(
        factory.latest.subscribedTopics.first,
        'faceunlock/fu-000001/approval-request',
      );
      expect(container.read(brokerProvider).link, BrokerLink.connected);
      container.dispose();
    });
  });

  test('describeBrokerState: one line, light first', () {
    expect(
      describeBrokerState(const BrokerState(BrokerLink.off)),
      '⚪ MQTT off',
    );
    expect(
      describeBrokerState(
        const BrokerState(BrokerLink.connected, boardOnline: true),
      ),
      '🟢 Broker connected, board online',
    );
    expect(
      describeBrokerState(
        const BrokerState(
          BrokerLink.offline,
          problem: 'Cannot reach the broker',
          retryIn: Duration(seconds: 4),
        ),
      ),
      '🔴 Cannot reach the broker, retry in 4 s',
    );
  });
}

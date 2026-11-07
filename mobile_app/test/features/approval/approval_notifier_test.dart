import 'dart:convert';

import 'package:face_unlock/core/board/board_address.dart';
import 'package:face_unlock/core/mqtt/broker_settings.dart';
import 'package:face_unlock/features/approval/approval_notifier.dart';
import 'package:fake_async/fake_async.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

import '../../core/mqtt/fake_broker.dart';

/// Plays the board's HTTP API: someone waits until an answer comes.
class _FakeBoardApi {
  int waitingRequest = 77;
  final requests = <http.Request>[];

  Future<http.Response> handle(http.Request request) async {
    requests.add(request);
    if (request.url.path == '/api/approval') {
      waitingRequest = 0;
      return http.Response('{"ok":true}', 202);
    }
    return http.Response(
      jsonEncode({
        'waiting_request': waitingRequest,
        'waiting_name': 'Hao',
        'waiting_left_ms': 20000,
      }),
      200,
    );
  }
}

ProviderContainer _start(
  FakeAsync time, {
  required FakeBrokerFactory broker,
  BrokerSettings brokerSettings = testBrokerSettings,
  String boardAddress = '',
  _FakeBoardApi? board,
}) {
  late ProviderContainer container;
  brokerTestContainer(
    factory: broker,
    settings: brokerSettings,
    preferences: {'board_address': boardAddress, 'board_token': 'AB9A9JQ2'},
    overrides: [
      httpClientFactoryProvider.overrideWithValue(
        () => MockClient(
          (request) => (board ?? _FakeBoardApi()).handle(request),
        ),
      ),
    ],
  ).then((created) => container = created);
  time.flushMicrotasks();
  // autoDispose: keep it alive like the open tab does.
  container.listen(approvalProvider, (_, _) {});
  time.flushMicrotasks();
  return container;
}

void main() {
  test('no address and no broker: channel none', () {
    fakeAsync((time) {
      final container = _start(
        time,
        broker: FakeBrokerFactory(),
        brokerSettings: testBrokerSettings.copyWith(isEnabled: false),
      );

      expect(container.read(approvalProvider).channel, ApprovalChannel.none);
      container.dispose();
    });
  });

  test('broker connected: request from MQTT, answer over MQTT, no HTTP', () {
    fakeAsync((time) {
      final broker = FakeBrokerFactory();
      final board = _FakeBoardApi();
      final container = _start(
        time,
        broker: broker,
        boardAddress: '10.0.0.5',
        board: board,
      );
      broker.latest.receive(testRequestTopic, requestPayload(requestId: 9));
      time.flushMicrotasks();

      var approval = container.read(approvalProvider);
      expect(approval.channel, ApprovalChannel.broker);
      expect(approval.pending?.requestId, 9);
      expect(approval.pending?.name, 'Hao');

      container.read(approvalProvider.notifier).answer(approve: false);
      time.flushMicrotasks();

      expect(broker.latest.published.single.topic, testAnswerTopic);
      expect(jsonDecode(broker.latest.published.single.payload), {
        'request': 9,
        'approve': false,
      });
      approval = container.read(approvalProvider);
      expect(approval.pending, isNull);
      expect(approval.isAnswering, isFalse);
      time.elapse(const Duration(seconds: 10));
      expect(board.requests, isEmpty, reason: 'no polling with the broker');
      container.dispose();
    });
  });

  test('broker off: polls the board every 2 s and answers over HTTP', () {
    fakeAsync((time) {
      final board = _FakeBoardApi();
      final container = _start(
        time,
        broker: FakeBrokerFactory(),
        brokerSettings: testBrokerSettings.copyWith(isEnabled: false),
        boardAddress: '10.0.0.5',
        board: board,
      );

      var approval = container.read(approvalProvider);
      expect(approval.channel, ApprovalChannel.board);
      expect(approval.isLoading, isFalse);
      expect(approval.pending?.requestId, 77);
      expect(board.requests, hasLength(1));

      time.elapse(approvalPollInterval);
      expect(board.requests, hasLength(2));

      container.read(approvalProvider.notifier).answer(approve: true);
      time.flushMicrotasks();

      final answer = board.requests.firstWhere(
        (request) => request.url.path == '/api/approval',
      );
      expect(jsonDecode(answer.body), {'request': 77, 'approve': true});
      approval = container.read(approvalProvider);
      expect(approval.pending, isNull, reason: 'polled again after answering');
      container.dispose();
    });
  });

  test('broker drops: falls back to polling the board', () {
    fakeAsync((time) {
      final broker = FakeBrokerFactory();
      final board = _FakeBoardApi();
      final container = _start(
        time,
        broker: broker,
        boardAddress: '10.0.0.5',
        board: board,
      );
      expect(container.read(approvalProvider).channel, ApprovalChannel.broker);

      broker.latest.drop();
      time.flushMicrotasks();
      time.elapse(Duration.zero);

      expect(container.read(approvalProvider).channel, ApprovalChannel.board);
      expect(board.requests, isNotEmpty);
      container.dispose();
    });
  });
}

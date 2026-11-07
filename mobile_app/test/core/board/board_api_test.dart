import 'dart:convert';

import 'package:face_unlock/core/board/board_address.dart';
import 'package:face_unlock/core/board/board_api.dart';
import 'package:face_unlock/features/faces/ui/enroll_dialog.dart';
import 'package:face_unlock/features/faces/ui/faces_screen.dart';
import 'package:face_unlock/features/history/ui/history_screen.dart';
import 'package:face_unlock/features/settings/ui/board_settings_form.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

void main() {
  final base = Uri.parse('http://10.0.0.5');

  group('BoardApi.status', () {
    BoardApi apiAnswering(http.Response Function(http.Request) answer) =>
        BoardApi(
          baseUri: base,
          token: 'AB9A9JQ2',
          newClient: () => MockClient((request) async => answer(request)),
        );

    test('sends the token to /api/status and reads the answer', () async {
      late http.Request requested;
      final api = apiAnswering((request) {
        requested = request;
        return http.Response(
          jsonEncode({
            'version': '1.2',
            'recognition': true,
            'state': 'IDLE',
            'waiting_request': 0,
            'door': 'locked',
            'faces': 1,
            'faces_max': 20,
            'ip': '10.0.0.5',
          }),
          200,
        );
      });

      final status = await api.status();

      expect(requested.url.toString(), 'http://10.0.0.5/api/status');
      expect(requested.headers['Authorization'], 'Bearer AB9A9JQ2');
      expect(status.state, 'IDLE');
      expect(status.isDoorOpen, isFalse);
      expect(status.isRecognitionRunning, isTrue);
      expect(status.faceCount, 1);
      expect(status.faceCapacity, 20);
      expect(status.waitingName, isNull);
    });

    test('no face store: faces are null', () async {
      final api = apiAnswering(
        (_) => http.Response(
          jsonEncode({'recognition': false, 'state': 'OFF', 'faces': null}),
          200,
        ),
      );

      final status = await api.status();

      expect(status.faceCount, isNull);
      expect(status.state, 'OFF');
    });

    test('a waiting approval has a name and time left', () async {
      final api = apiAnswering(
        (_) => http.Response(
          jsonEncode({
            'waiting_request': 77,
            'waiting_name': 'Hao',
            'waiting_left_ms': 12500,
          }),
          200,
        ),
      );

      final status = await api.status();

      expect(status.waitingRequest, 77);
      expect(status.waitingName, 'Hao');
      expect(status.waitingLeft, const Duration(milliseconds: 12500));
    });

    test('401 is a token problem', () {
      final api = apiAnswering((_) => http.Response('{}', 401));
      expect(api.status(), throwsA(isA<BoardAuthException>()));
    });

    test('404 (old firmware) is an HTTP error', () {
      final api = apiAnswering((_) => http.Response('', 404));
      expect(
        api.status(),
        throwsA(
          isA<BoardHttpException>().having((e) => e.statusCode, 'code', 404),
        ),
      );
    });
  });

  group('token helpers', () {
    test('normalizeToken forgives spaces, dashes and case', () {
      expect(normalizeToken(' ab9a-9jq2 '), 'AB9A9JQ2');
    });

    test('no token, no header', () {
      expect(boardAuthHeaders(''), isEmpty);
    });

    test('stream is on the API port + 1', () {
      expect(boardStreamUri(base).toString(), 'http://10.0.0.5:81');
      expect(
        boardStreamUri(Uri.parse('http://127.0.0.1:8080')).toString(),
        'http://127.0.0.1:8081',
      );
    });
  });

  group('BoardApi', () {
    late List<http.Request> requests;
    late http.Response Function(http.Request) answer;

    BoardApi api() => BoardApi(
      baseUri: base,
      token: 'AB9A9JQ2',
      newClient: () => MockClient((request) async {
        requests.add(request);
        return answer(request);
      }),
    );

    setUp(() {
      requests = [];
      answer = (_) => http.Response('{"ok":true}', 202);
    });

    test('enroll posts the name as JSON with the token', () async {
      await api().enroll('Hao');

      final request = requests.single;
      expect(request.method, 'POST');
      expect(request.url.toString(), 'http://10.0.0.5/api/faces/enroll');
      expect(request.headers['Authorization'], 'Bearer AB9A9JQ2');
      expect(jsonDecode(request.body), {'name': 'Hao'});
    });

    test('delete, unlock and approval hit the right paths', () async {
      await api().deleteFace(7);
      await api().unlock();
      await api().answerApproval(1234, approve: false);

      expect(requests.map((r) => '${r.method} ${r.url.path}'), [
        'DELETE /api/faces/7',
        'POST /api/door/unlock',
        'POST /api/approval',
      ]);
      expect(jsonDecode(requests.last.body), {
        'request': 1234,
        'approve': false,
      });
    });

    test('events after a seq are read oldest first', () async {
      answer = (_) => http.Response(
        jsonEncode({
          'now_ms': 10000,
          'last_seq': 5,
          'events': [
            {'seq': 4, 'time_ms': 4000, 'type': 'face_enrolled', 'name': 'Hao'},
            {'seq': 5, 'time_ms': 9000, 'type': 'door_opened', 'face_id': 1},
          ],
        }),
        200,
      );

      final events = await api().events(after: 3);

      expect(requests.single.url.queryParameters['after'], '3');
      expect(events.lastSeq, 5);
      expect(events.events.map((e) => e.seq), [4, 5]);
      final receivedAt = DateTime(2026, 1, 1, 12);
      expect(
        events.timeOf(events.events.first, receivedAt),
        receivedAt.subtract(const Duration(seconds: 6)),
      );
    });

    test('a 400 carries the board reason', () {
      answer = (_) =>
          http.Response('{"error":"value out of range"}', 400);
      expect(
        api().saveSettings('/api/rules', {'unlock_hold_ms': 1}),
        throwsA(
          isA<BoardHttpException>().having(
            (e) => e.toString(),
            'text',
            'Board answered HTTP 400: value out of range',
          ),
        ),
      );
    });

    test('settings are read and saved as JSON', () async {
      answer = (request) => http.Response(
        request.method == 'PUT' ? request.body : '{"brightness":1}',
        200,
      );

      expect(await api().settings('/api/camera'), {'brightness': 1});
      expect(
        await api().saveSettings('/api/camera', {'brightness': 2}),
        {'brightness': 2},
      );
      expect(requests.last.method, 'PUT');
    });
  });

  group('screen helpers', () {
    BoardEvent event(String type, {String name = 'Hao', int faceId = 1}) =>
        BoardEvent(
          seq: 1,
          timeMs: 0,
          type: type,
          faceId: faceId,
          name: name,
          requestId: 0,
        );

    test('describeEvent reads like a sentence', () {
      expect(describeEvent(event('door_opened')), 'Door opened for Hao');
      expect(
        describeEvent(event('door_opened', name: 'manual', faceId: 0)),
        'Door opened by hand (app or console)',
      );
      expect(describeEvent(event('face_rejected', name: '')),
          'Unknown face turned away');
      expect(describeEvent(event('something_new')), 'something_new');
    });

    test('enrollOutcome follows only the enrolled name', () {
      expect(enrollOutcome([event('enroll_started')], 'Hao'),
          EnrollOutcome.waiting);
      expect(enrollOutcome([event('face_enrolled')], 'Hao'),
          EnrollOutcome.saved);
      expect(enrollOutcome([event('enroll_failed')], 'Hao'),
          EnrollOutcome.failed);
      expect(enrollOutcome([event('face_enrolled', name: 'Lan')], 'Hao'),
          EnrollOutcome.waiting);
    });

    test('nameProblem counts bytes like the board', () {
      expect(nameProblem(''), 'Type a name');
      expect(nameProblem('Hao'), isNull);
      expect(nameProblem('a' * 31), isNull);
      expect(nameProblem('a' * 32), 'Too long');
      // 16 x "ạ" = 48 bytes in UTF-8, over the board's 31.
      expect(nameProblem('ạ' * 16), 'Too long');
    });

    test('timeAgo rounds down to the biggest unit', () {
      final now = DateTime(2026, 1, 1, 12);
      expect(timeAgo(now, now.subtract(const Duration(seconds: 30))),
          'just now');
      expect(timeAgo(now, now.subtract(const Duration(minutes: 5))),
          '5 min ago');
      expect(timeAgo(now, now.subtract(const Duration(hours: 3))), '3 h ago');
      expect(timeAgo(now, now.subtract(const Duration(days: 2))), '2 d ago');
    });

    test('sliders snap to the step and stay in range', () {
      const holdTime = NumberField(
        'unlock_hold_ms',
        'Door open time',
        500,
        60000,
        step: 500,
        unit: 's',
        divisor: 1000,
      );
      expect(snapToStep(holdTime, 3240), 3000);
      expect(snapToStep(holdTime, 99999), 60000);
      expect(holdTime.show(3000), '3 s');
      expect(holdTime.show(1500), '1.5 s');
    });
  });
}

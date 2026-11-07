import 'dart:convert';

import 'package:face_unlock/core/board/board_address.dart';
import 'package:face_unlock/core/board/board_api.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

void main() {
  final base = Uri.parse('http://10.0.0.5');

  group('fetchBoardStatus', () {
    test('sends the token to /api/status and reads the answer', () async {
      late http.Request requested;
      final client = MockClient((request) async {
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

      final status = await fetchBoardStatus(client, base, 'AB9A9JQ2');

      expect(requested.url.toString(), 'http://10.0.0.5/api/status');
      expect(requested.headers['Authorization'], 'Bearer AB9A9JQ2');
      expect(status.state, 'IDLE');
      expect(status.isDoorOpen, isFalse);
      expect(status.isRecognitionRunning, isTrue);
      expect(status.faceCount, 1);
      expect(status.faceCapacity, 20);
    });

    test('no face store: faces are null', () async {
      final client = MockClient(
        (_) async => http.Response(
          jsonEncode({'recognition': false, 'state': 'OFF', 'faces': null}),
          200,
        ),
      );

      final status = await fetchBoardStatus(client, base, 'AB9A9JQ2');

      expect(status.faceCount, isNull);
      expect(status.state, 'OFF');
    });

    test('401 is a token problem', () {
      final client = MockClient((_) async => http.Response('{}', 401));
      expect(
        fetchBoardStatus(client, base, 'WRONG123'),
        throwsA(isA<BoardAuthException>()),
      );
    });

    test('404 (old firmware) is an HTTP error', () {
      final client = MockClient((_) async => http.Response('', 404));
      expect(
        fetchBoardStatus(client, base, 'AB9A9JQ2'),
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
}

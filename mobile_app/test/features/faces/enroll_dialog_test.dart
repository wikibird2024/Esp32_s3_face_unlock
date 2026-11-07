import 'dart:async';
import 'dart:convert';

import 'package:face_unlock/core/board/board_api.dart';
import 'package:face_unlock/features/faces/ui/enroll_dialog.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

/// Opens the dialog from a button, like the Faces tab does.
Future<void> _openDialog(WidgetTester tester, BoardApi api) async {
  await tester.pumpWidget(
    MaterialApp(
      home: Builder(
        builder: (context) => TextButton(
          onPressed: () => showDialog<void>(
            context: context,
            barrierDismissible: false,
            builder: (_) => EnrollDialog(api: api, name: 'Hao'),
          ),
          child: const Text('open'),
        ),
      ),
    ),
  );
  await tester.tap(find.text('open'));
  await tester.pump();
}

String _events(int lastSeq, [List<Map<String, Object>> events = const []]) =>
    jsonEncode({'now_ms': 1000, 'last_seq': lastSeq, 'events': events});

void main() {
  late List<String> requests;

  BoardApi api(Future<http.Response> Function(http.Request) answer) =>
      BoardApi(
        baseUri: Uri.parse('http://10.0.0.5'),
        token: 'AB9A9JQ2',
        newClient: () => MockClient((request) {
          requests.add('${request.method} ${request.url.path}');
          return answer(request);
        }),
      );

  setUp(() => requests = []);

  testWidgets('Cancel before the board answers sends no enroll', (
    tester,
  ) async {
    final firstAnswer = Completer<http.Response>();
    await _openDialog(
      tester,
      api((request) {
        if (request.url.path == '/api/events') return firstAnswer.future;
        return Future.value(http.Response('{"ok":true}', 202));
      }),
    );

    await tester.tap(find.text('Cancel'));
    await tester.pump();
    firstAnswer.complete(http.Response(_events(3), 200));
    await tester.pump();
    await tester.pump(const Duration(seconds: 2));

    expect(requests, isNot(contains('POST /api/faces/enroll')));
    expect(requests, contains('POST /api/faces/enroll/cancel'));
  });

  testWidgets('a saved face ends the dialog with "saved"', (tester) async {
    var eventsCalls = 0;
    await _openDialog(
      tester,
      api((request) async {
        if (request.url.path != '/api/events') {
          return http.Response('{"ok":true}', 202);
        }
        eventsCalls++;
        return http.Response(
          eventsCalls == 1
              ? _events(3)
              : _events(4, [
                  {'seq': 4, 'type': 'face_enrolled', 'name': 'Hao'},
                ]),
          200,
        );
      }),
    );
    await tester.pump();
    expect(requests, contains('POST /api/faces/enroll'));

    await tester.pump(enrollPollInterval);
    await tester.pump();

    expect(find.text('Hao saved'), findsOneWidget);
  });
}

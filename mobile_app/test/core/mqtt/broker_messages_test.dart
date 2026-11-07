import 'dart:convert';

import 'package:face_unlock/core/mqtt/broker_messages.dart';
import 'package:flutter_test/flutter_test.dart';

/// Same vector as `tool/fake_mqtt_board.py --self-test` (and later the
/// firmware host test): both sides must sign the same way.
const _keyHex = '00112233445566778899aabbccddeeff';
const _signedOpen =
    '8ad156c76928fc07521359b09ffda63e1606787577cfdbcbdceee72ce6335734';
const _signedDeny =
    'cf85f519e79a9189242a27dc5f3deb2b76a4ecbddb9e5071cf5ce56b5aedf6ce';

void main() {
  final receivedAt = DateTime(2026, 11, 8, 9, 30);

  group('ApprovalRequest.tryParse', () {
    test('reads the contract payload', () {
      final request = ApprovalRequest.tryParse(
        '{"request":1234,"face":4,"name":"alice","similarity":0.71,'
        '"expires_s":30}',
        receivedAt,
      );

      expect(request, isNotNull);
      expect(request!.requestId, 1234);
      expect(request.faceId, 4);
      expect(request.name, 'alice');
      expect(request.similarity, 0.71);
      expect(request.deadline, receivedAt.add(const Duration(seconds: 30)));
    });

    test('drops payloads that are not a usable request', () {
      for (final payload in [
        'not json',
        '[1,2]',
        '{"request":0,"expires_s":30}',
        '{"request":"12","expires_s":30}',
        '{"request":12}',
        '{"request":12,"expires_s":-1}',
      ]) {
        expect(
          ApprovalRequest.tryParse(payload, receivedAt),
          isNull,
          reason: payload,
        );
      }
    });

    test('missing name and face fall back to empty and 0', () {
      final request = ApprovalRequest.tryParse(
        '{"request":7,"expires_s":5,"name":3}',
        receivedAt,
      )!;
      expect(request.name, '');
      expect(request.faceId, 0);
    });
  });

  group('BrokerEvent.tryParse', () {
    test('reads type, name, ids and time', () {
      final event = BrokerEvent.tryParse(
        '{"type":"door_opened","name":"alice","face":4,"request":1234,'
        '"time":"2026-10-22T08:02:11"}',
        receivedAt,
      )!;
      expect(event.type, 'door_opened');
      expect(event.name, 'alice');
      expect(event.faceId, 4);
      expect(event.requestId, 1234);
      expect(event.time, DateTime(2026, 10, 22, 8, 2, 11));
      expect(event.endsApproval, isTrue);
    });

    test('no or bad time: the receive time stands in', () {
      final event = BrokerEvent.tryParse(
        '{"type":"face_rejected","time":"soon"}',
        receivedAt,
      )!;
      expect(event.time, receivedAt);
      expect(event.endsApproval, isFalse);
    });

    test('no type: dropped', () {
      expect(BrokerEvent.tryParse('{"name":"x"}', receivedAt), isNull);
      expect(BrokerEvent.tryParse('{"type":""}', receivedAt), isNull);
    });
  });

  test('parseBoardOnline accepts the JSON string and the bare word', () {
    expect(parseBoardOnline('"online"'), isTrue);
    expect(parseBoardOnline(' offline '), isFalse);
    expect(parseBoardOnline('ONLINE'), isTrue);
    expect(parseBoardOnline('1'), isNull);
  });

  group('signAnswer', () {
    test('matches the shared test vector', () {
      expect(
        signAnswer(
          phoneKeyHex: _keyHex,
          device: 'fu-a1b2c3',
          requestId: 1234,
          approve: true,
        ),
        _signedOpen,
      );
      expect(
        signAnswer(
          phoneKeyHex: _keyHex.toUpperCase(),
          device: 'fu-a1b2c3',
          requestId: 1234,
          approve: false,
        ),
        _signedDeny,
      );
    });

    test('a key that is not hex throws', () {
      expect(
        () => signAnswer(
          phoneKeyHex: 'xyz1',
          device: 'fu-a1b2c3',
          requestId: 1,
          approve: true,
        ),
        throwsFormatException,
      );
    });
  });

  test('answerPayload leaves out phone and sig when not paired', () {
    expect(jsonDecode(answerPayload(requestId: 5, approve: false)), {
      'request': 5,
      'approve': false,
    });
    expect(
      jsonDecode(
        answerPayload(
          requestId: 5,
          approve: true,
          phoneId: 'p1',
          signature: 'ab',
        ),
      ),
      {'request': 5, 'approve': true, 'phone': 'p1', 'sig': 'ab'},
    );
  });

  test('hexToBytes rejects odd length and signs', () {
    expect(hexToBytes('0aFF'), [0x0a, 0xff]);
    expect(() => hexToBytes('abc'), throwsFormatException);
    expect(() => hexToBytes('+f'), throwsFormatException);
  });

  test('default topic prefix: Adafruit feeds vs plain topics', () {
    expect(
      defaultTopicPrefix(
        host: 'IO.adafruit.com ',
        username: 'hao',
        device: 'fu-a1b2c3',
      ),
      'hao/feeds/fu-a1b2c3-',
    );
    expect(
      defaultTopicPrefix(
        host: '192.168.1.9',
        username: '',
        device: 'fu-a1b2c3',
      ),
      'faceunlock/fu-a1b2c3/',
    );
  });
}

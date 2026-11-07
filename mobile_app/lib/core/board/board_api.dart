import 'dart:async';
import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:http/http.dart' as http;

import 'board_address.dart';

const boardRequestTimeout = Duration(seconds: 3);

/// The board refused the token (HTTP 401).
class BoardAuthException implements Exception {
  const BoardAuthException();

  @override
  String toString() => 'Wrong or missing token';
}

/// The board answered, but not with 2xx or 401.
class BoardHttpException implements Exception {
  const BoardHttpException(this.statusCode, [this.reason = '']);

  final int statusCode;

  /// The board's `{"error": ...}` text, if any.
  final String reason;

  @override
  String toString() => reason.isEmpty
      ? 'Board answered HTTP $statusCode'
      : 'Board answered HTTP $statusCode: $reason';
}

/// Short text for an error from the board, for a SnackBar or a screen.
String boardErrorText(Object error) {
  if (error is BoardAuthException) return 'Wrong token: check Settings';
  if (error is TimeoutException) return 'No answer from the board';
  if (error is BoardHttpException) return error.toString();
  return 'Cannot reach the board';
}

/// Answer of `GET /api/status` (firmware: components/board_api).
class BoardStatus {
  const BoardStatus({
    required this.version,
    required this.state,
    required this.isDoorOpen,
    required this.isRecognitionRunning,
    required this.faceCount,
    required this.faceCapacity,
    required this.waitingRequest,
    required this.ip,
    this.waitingName,
    this.waitingLeft,
  });

  factory BoardStatus.fromJson(Map<String, dynamic> json) {
    final leftMs = (json['waiting_left_ms'] as num?)?.toInt();
    return BoardStatus(
      version: json['version'] as String? ?? '',
      state: json['state'] as String? ?? '',
      isDoorOpen: json['door'] == 'open',
      isRecognitionRunning: json['recognition'] as bool? ?? false,
      faceCount: (json['faces'] as num?)?.toInt(),
      faceCapacity: (json['faces_max'] as num?)?.toInt(),
      waitingRequest: (json['waiting_request'] as num?)?.toInt() ?? 0,
      ip: json['ip'] as String? ?? '',
      waitingName: json['waiting_name'] as String?,
      waitingLeft: leftMs == null ? null : Duration(milliseconds: leftMs),
    );
  }

  final String version;

  /// Unlock state machine state, for example `IDLE`; `OFF` when face
  /// recognition did not start.
  final String state;
  final bool isDoorOpen;
  final bool isRecognitionRunning;

  /// Null when the face store is not available.
  final int? faceCount;
  final int? faceCapacity;

  /// Approval request the board waits for, 0 if none.
  final int waitingRequest;
  final String ip;

  /// Who waits for approval, and how long until it times out.
  final String? waitingName;
  final Duration? waitingLeft;
}

/// One entry of `GET /api/events`.
class BoardEvent {
  const BoardEvent({
    required this.seq,
    required this.timeMs,
    required this.type,
    required this.faceId,
    required this.name,
    required this.requestId,
  });

  factory BoardEvent.fromJson(Map<String, dynamic> json) => BoardEvent(
    seq: (json['seq'] as num?)?.toInt() ?? 0,
    timeMs: (json['time_ms'] as num?)?.toInt() ?? 0,
    type: json['type'] as String? ?? '',
    faceId: (json['face_id'] as num?)?.toInt() ?? 0,
    name: json['name'] as String? ?? '',
    requestId: (json['request_id'] as num?)?.toInt() ?? 0,
  );

  final int seq;

  /// Board uptime when it happened.
  final int timeMs;

  /// For example `door_opened`, `face_enrolled` (firmware: app_event_name).
  final String type;
  final int faceId;
  final String name;
  final int requestId;
}

/// Answer of `GET /api/events`.
class BoardEvents {
  const BoardEvents({
    required this.nowMs,
    required this.lastSeq,
    required this.events,
  });

  factory BoardEvents.fromJson(Map<String, dynamic> json) => BoardEvents(
    nowMs: (json['now_ms'] as num?)?.toInt() ?? 0,
    lastSeq: (json['last_seq'] as num?)?.toInt() ?? 0,
    events: [
      for (final item in json['events'] as List<dynamic>? ?? const [])
        BoardEvent.fromJson(item as Map<String, dynamic>),
    ],
  );

  /// Board uptime when it answered: event time on the phone =
  /// now - (nowMs - event.timeMs).
  final int nowMs;
  final int lastSeq;

  /// Oldest first.
  final List<BoardEvent> events;

  /// When [event] happened, on the phone's clock.
  DateTime timeOf(BoardEvent event, DateTime receivedAt) =>
      receivedAt.subtract(Duration(milliseconds: nowMs - event.timeMs));
}

/// One line for the History list and messages.
String describeEvent(BoardEvent event) {
  final name = event.name.isEmpty ? 'someone' : event.name;
  switch (event.type) {
    case 'door_opened':
      return event.faceId == 0
          ? 'Door opened by hand (app or console)'
          : 'Door opened for $name';
    case 'door_closed':
      return 'Door locked again';
    case 'face_rejected':
      return 'Unknown face turned away';
    case 'enroll_started':
      return 'Adding face: $name';
    case 'face_enrolled':
      return 'Face saved: $name';
    case 'enroll_failed':
      return 'Adding face failed: $name';
    case 'approval_requested':
      return '$name asked to come in';
    case 'approval_denied':
      return 'Denied: $name';
    case 'approval_timeout':
      return 'No answer in time: $name';
    default:
      return event.type;
  }
}

class BoardFace {
  const BoardFace({required this.id, required this.name});

  factory BoardFace.fromJson(Map<String, dynamic> json) => BoardFace(
    id: (json['id'] as num?)?.toInt() ?? 0,
    name: json['name'] as String? ?? '',
  );

  final int id;
  final String name;
}

Map<String, dynamic> _checkedJson(http.Response response) {
  if (response.statusCode == 401) throw const BoardAuthException();
  final decoded = response.body.isEmpty ? null : _tryJson(response.body);
  final json = decoded is Map<String, dynamic> ? decoded : <String, dynamic>{};
  if (response.statusCode < 200 || response.statusCode >= 300) {
    throw BoardHttpException(
      response.statusCode,
      json['error'] as String? ?? '',
    );
  }
  return json;
}

Object? _tryJson(String text) {
  try {
    return jsonDecode(text);
  } on FormatException {
    return null;
  }
}

/// The board's JSON API (port 80). Each call uses its own HTTP client, so
/// nothing stays open between calls.
class BoardApi {
  const BoardApi({
    required this.baseUri,
    required this.token,
    required this.newClient,
  });

  final Uri baseUri;
  final String token;
  final http.Client Function() newClient;

  Future<BoardStatus> status() async =>
      BoardStatus.fromJson(await _send('GET', '/api/status'));

  Future<BoardEvents> events({int after = 0}) async => BoardEvents.fromJson(
    await _send('GET', '/api/events', query: {'after': '$after'}),
  );

  Future<List<BoardFace>> faces() async {
    final json = await _send('GET', '/api/faces');
    return [
      for (final item in json['faces'] as List<dynamic>? ?? const [])
        BoardFace.fromJson(item as Map<String, dynamic>),
    ];
  }

  /// The board saves the next face it sees under [name].
  Future<void> enroll(String name) =>
      _send('POST', '/api/faces/enroll', body: {'name': name});

  Future<void> cancelEnroll() => _send('POST', '/api/faces/enroll/cancel');

  Future<void> deleteFace(int id) => _send('DELETE', '/api/faces/$id');

  /// Opens the door for the hold time; the board relocks it by itself.
  Future<void> unlock() => _send('POST', '/api/door/unlock');

  Future<void> answerApproval(int requestId, {required bool approve}) => _send(
    'POST',
    '/api/approval',
    body: {'request': requestId, 'approve': approve},
  );

  /// Unlock rules or camera settings ([settingsPath]: `/api/rules` or
  /// `/api/camera`), as the board sends them.
  Future<Map<String, dynamic>> settings(String settingsPath) =>
      _send('GET', settingsPath);

  /// Saves the given keys; the answer is the full settings in use.
  Future<Map<String, dynamic>> saveSettings(
    String settingsPath,
    Map<String, Object> changes,
  ) => _send('PUT', settingsPath, body: changes);

  Future<Map<String, dynamic>> _send(
    String method,
    String path, {
    Map<String, Object>? body,
    Map<String, String>? query,
  }) async {
    final client = newClient();
    try {
      final request = http.Request(
        method,
        baseUri.replace(path: path, queryParameters: query),
      )..headers.addAll(boardAuthHeaders(token));
      if (body != null) {
        request.headers['Content-Type'] = 'application/json';
        request.body = jsonEncode(body);
      }
      final response = await http.Response.fromStream(
        await client.send(request).timeout(boardRequestTimeout),
      ).timeout(boardRequestTimeout);
      return _checkedJson(response);
    } finally {
      client.close();
    }
  }
}

/// Null until a board address is set in Settings.
final boardApiProvider = Provider<BoardApi?>((ref) {
  final baseUri = boardBaseUri(ref.watch(boardAddressProvider));
  if (baseUri == null) return null;
  return BoardApi(
    baseUri: baseUri,
    token: ref.watch(boardTokenProvider),
    newClient: ref.watch(httpClientFactoryProvider),
  );
});

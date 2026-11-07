import 'dart:async';
import 'dart:convert';

import 'package:http/http.dart' as http;

import 'board_address.dart';

const boardRequestTimeout = Duration(seconds: 3);

/// The board refused the token (HTTP 401).
class BoardAuthException implements Exception {
  const BoardAuthException();

  @override
  String toString() => 'Wrong or missing token';
}

/// The board answered, but not with 200 or 401.
class BoardHttpException implements Exception {
  const BoardHttpException(this.statusCode);

  final int statusCode;

  @override
  String toString() => 'Board answered HTTP $statusCode';
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
  });

  factory BoardStatus.fromJson(Map<String, dynamic> json) => BoardStatus(
    version: json['version'] as String? ?? '',
    state: json['state'] as String? ?? '',
    isDoorOpen: json['door'] == 'open',
    isRecognitionRunning: json['recognition'] as bool? ?? false,
    faceCount: (json['faces'] as num?)?.toInt(),
    faceCapacity: (json['faces_max'] as num?)?.toInt(),
    waitingRequest: (json['waiting_request'] as num?)?.toInt() ?? 0,
    ip: json['ip'] as String? ?? '',
  );

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
}

/// `GET /api/status`. Throws [BoardAuthException], [BoardHttpException],
/// [TimeoutException] or a network error.
Future<BoardStatus> fetchBoardStatus(
  http.Client client,
  Uri baseUri,
  String token,
) async {
  final response = await client
      .get(
        baseUri.replace(path: '/api/status'),
        headers: boardAuthHeaders(token),
      )
      .timeout(boardRequestTimeout);
  if (response.statusCode == 401) throw const BoardAuthException();
  if (response.statusCode != 200) {
    throw BoardHttpException(response.statusCode);
  }
  return BoardStatus.fromJson(
    jsonDecode(response.body) as Map<String, dynamic>,
  );
}

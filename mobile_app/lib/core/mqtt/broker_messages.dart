import 'dart:convert';

import 'package:crypto/crypto.dart';

/// The topics of `docs/mqtt_contract.md`. A full topic is the settings'
/// topic prefix followed by [feedName].
enum BrokerTopic {
  approvalRequest('approval-request'),
  approvalAnswer('approval-answer'),
  event('event'),
  status('status');

  const BrokerTopic(this.feedName);

  /// Lowercase letters and dashes only, so it is also a valid Adafruit IO
  /// feed key.
  final String feedName;
}

/// The host of the Adafruit IO broker, which only accepts topics of the
/// form `<username>/feeds/<feed key>`.
const adafruitHost = 'io.adafruit.com';

bool isAdafruitHost(String host) => host.trim().toLowerCase() == adafruitHost;

/// Topic prefix used when the user leaves it empty:
/// `hao/feeds/fu-a1b2c3-` on Adafruit IO, `faceunlock/fu-a1b2c3/` elsewhere.
String defaultTopicPrefix({
  required String host,
  required String username,
  required String device,
}) => isAdafruitHost(host)
    ? '${username.trim()}/feeds/${device.trim()}-'
    : 'faceunlock/${device.trim()}/';

/// A known face waits at the door for Open / Deny (topic
/// `approval-request`).
class ApprovalRequest {
  const ApprovalRequest({
    required this.requestId,
    required this.faceId,
    required this.name,
    required this.similarity,
    required this.expiresIn,
    required this.receivedAt,
  });

  /// Null when [payload] is not a valid request: the board's payload is
  /// outside input, so a bad one is dropped instead of crashing.
  static ApprovalRequest? tryParse(String payload, DateTime receivedAt) {
    final json = _tryJsonObject(payload);
    if (json == null) return null;
    final requestId = _positiveInt(json['request']);
    final expiresSeconds = _positiveInt(json['expires_s']);
    if (requestId == null || expiresSeconds == null) return null;
    return ApprovalRequest(
      requestId: requestId,
      faceId: _positiveInt(json['face']) ?? 0,
      name: json['name'] is String ? json['name'] as String : '',
      similarity: json['similarity'] is num
          ? (json['similarity'] as num).toDouble()
          : 0,
      expiresIn: Duration(seconds: expiresSeconds),
      receivedAt: receivedAt,
    );
  }

  /// The id the board's state machine waits for; the answer must repeat it.
  final int requestId;
  final int faceId;
  final String name;
  final double similarity;
  final Duration expiresIn;

  /// Phone clock when the message arrived.
  final DateTime receivedAt;

  /// After this the board stops waiting and the answer is ignored.
  DateTime get deadline => receivedAt.add(expiresIn);

  @override
  bool operator ==(Object other) =>
      other is ApprovalRequest &&
      other.requestId == requestId &&
      other.faceId == faceId &&
      other.name == name &&
      other.similarity == similarity &&
      other.expiresIn == expiresIn &&
      other.receivedAt == receivedAt;

  @override
  int get hashCode => Object.hash(
    requestId,
    faceId,
    name,
    similarity,
    expiresIn,
    receivedAt,
  );
}

/// Something that happened on the board (topic `event`). Types are the
/// same as in the HTTP event log (firmware: app_event_name).
class BrokerEvent {
  const BrokerEvent({
    required this.type,
    required this.name,
    required this.faceId,
    required this.requestId,
    required this.time,
  });

  /// Null when [payload] is not a valid event. [receivedAt] stands in when
  /// the board sends no usable `time` (no clock set yet).
  static BrokerEvent? tryParse(String payload, DateTime receivedAt) {
    final json = _tryJsonObject(payload);
    final type = json?['type'];
    if (json == null || type is! String || type.isEmpty) return null;
    final timeText = json['time'];
    return BrokerEvent(
      type: type,
      name: json['name'] is String ? json['name'] as String : '',
      faceId: _positiveInt(json['face']) ?? 0,
      requestId: _positiveInt(json['request']) ?? 0,
      time: (timeText is String ? DateTime.tryParse(timeText) : null) ??
          receivedAt,
    );
  }

  final String type;
  final String name;

  /// 0 when no face is involved (for example the door opened from the app).
  final int faceId;

  /// 0 when the event is not about an approval request.
  final int requestId;
  final DateTime time;

  /// True for the events that end a waiting approval request.
  bool get endsApproval =>
      type == 'door_opened' ||
      type == 'approval_denied' ||
      type == 'approval_timeout';
}

/// Payload of topic `status`: true for "online", false for "offline",
/// null for anything else. Accepts the JSON string form (`"online"`) of the
/// contract and the bare word that Adafruit IO dashboards send.
bool? parseBoardOnline(String payload) {
  var text = payload.trim();
  if (text.length >= 2 && text.startsWith('"') && text.endsWith('"')) {
    text = text.substring(1, text.length - 1);
  }
  switch (text.toLowerCase()) {
    case 'online':
      return true;
    case 'offline':
      return false;
    default:
      return null;
  }
}

/// `HMAC-SHA256(phone key, "<device>|<request>|<approve>")` as lowercase hex,
/// the signature the board checks before it opens the door.
/// Throws [FormatException] when [phoneKeyHex] is not hex.
String signAnswer({
  required String phoneKeyHex,
  required String device,
  required int requestId,
  required bool approve,
}) {
  final hmac = Hmac(sha256, hexToBytes(phoneKeyHex));
  return hmac.convert(utf8.encode('$device|$requestId|$approve')).toString();
}

/// JSON for topic `approval-answer`. [phoneId] and [signature] are left out
/// when empty (phone not paired yet); the board then refuses to open.
String answerPayload({
  required int requestId,
  required bool approve,
  String phoneId = '',
  String signature = '',
}) => jsonEncode({
  'request': requestId,
  'approve': approve,
  if (phoneId.isNotEmpty) 'phone': phoneId,
  if (signature.isNotEmpty) 'sig': signature,
});

/// `"0a1B"` → `[0x0a, 0x1b]`. Throws [FormatException] for odd length or a
/// character that is not hex.
List<int> hexToBytes(String hex) {
  final text = hex.trim();
  if (text.length.isOdd || !_hexDigits.hasMatch(text)) {
    throw const FormatException('Not hex, or an odd number of digits');
  }
  return [
    for (var index = 0; index < text.length; index += 2)
      int.parse(text.substring(index, index + 2), radix: 16),
  ];
}

final _hexDigits = RegExp(r'^[0-9a-fA-F]*$');

Map<String, dynamic>? _tryJsonObject(String payload) {
  try {
    final decoded = jsonDecode(payload);
    return decoded is Map<String, dynamic> ? decoded : null;
  } on FormatException {
    return null;
  }
}

int? _positiveInt(Object? value) =>
    value is int && value > 0 ? value : null;

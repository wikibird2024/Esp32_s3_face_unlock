import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../board/board_address.dart';
import 'broker_messages.dart';

/// Default MQTT port with TLS (Adafruit IO: 8883; plain 1883).
const brokerTlsPort = 8883;

/// Shortest phone key accepted, in bytes (128 bits).
const minimumPhoneKeyBytes = 16;

/// Where and how the app reaches the MQTT broker, as saved in Settings.
class BrokerSettings {
  const BrokerSettings({
    this.isEnabled = false,
    this.host = adafruitHost,
    this.port = brokerTlsPort,
    this.useTls = true,
    this.username = '',
    this.password = '',
    this.device = '',
    this.topicPrefix = '',
    this.phoneId = '',
    this.phoneKeyHex = '',
  });

  /// Reads what [toJson] wrote; missing or wrong-typed keys get defaults,
  /// so settings from an older app version still load.
  factory BrokerSettings.fromJson(Map<String, dynamic> json) {
    const defaults = BrokerSettings();
    String text(String key, String fallback) =>
        json[key] is String ? json[key] as String : fallback;
    return BrokerSettings(
      isEnabled: json['enabled'] is bool
          ? json['enabled'] as bool
          : defaults.isEnabled,
      host: text('host', defaults.host),
      port: json['port'] is int ? json['port'] as int : defaults.port,
      useTls: json['tls'] is bool ? json['tls'] as bool : defaults.useTls,
      username: text('username', defaults.username),
      password: text('password', defaults.password),
      device: text('device', defaults.device),
      topicPrefix: text('topic_prefix', defaults.topicPrefix),
      phoneId: text('phone_id', defaults.phoneId),
      phoneKeyHex: text('phone_key', defaults.phoneKeyHex),
    );
  }

  /// Off: the app uses only the board's HTTP API, as before.
  final bool isEnabled;
  final String host;
  final int port;
  final bool useTls;

  /// Adafruit IO: the account name.
  final String username;

  /// Adafruit IO: the AIO key.
  final String password;

  /// The board's id, `fu-` + last 6 hex digits of its MAC (`fu-a1b2c3`).
  /// Part of the default topics and of the answer signature.
  final String device;

  /// Empty: [defaultTopicPrefix] is used.
  final String topicPrefix;

  /// This phone's id and key, given by pairing (contract phase C).
  final String phoneId;
  final String phoneKeyHex;

  bool get isAdafruit => isAdafruitHost(host);

  /// The prefix in use: the typed one, or the default for this broker.
  String get effectiveTopicPrefix => topicPrefix.trim().isNotEmpty
      ? topicPrefix.trim()
      : defaultTopicPrefix(host: host, username: username, device: device);

  String topicFor(BrokerTopic topic) =>
      '$effectiveTopicPrefix${topic.feedName}';

  /// Adafruit IO tells about rate limit hits here; other brokers have none.
  String? get throttleTopic =>
      isAdafruit ? '${username.trim()}/throttle' : null;

  /// Everything the app listens to.
  List<String> get subscribeTopics => [
    topicFor(BrokerTopic.approvalRequest),
    topicFor(BrokerTopic.event),
    topicFor(BrokerTopic.status),
    ?throttleTopic,
  ];

  /// Answers can be signed: the phone is paired.
  bool get canSign =>
      phoneId.trim().isNotEmpty && phoneKeyHex.trim().isNotEmpty;

  BrokerSettings copyWith({
    bool? isEnabled,
    String? host,
    int? port,
    bool? useTls,
    String? username,
    String? password,
    String? device,
    String? topicPrefix,
    String? phoneId,
    String? phoneKeyHex,
  }) => BrokerSettings(
    isEnabled: isEnabled ?? this.isEnabled,
    host: host ?? this.host,
    port: port ?? this.port,
    useTls: useTls ?? this.useTls,
    username: username ?? this.username,
    password: password ?? this.password,
    device: device ?? this.device,
    topicPrefix: topicPrefix ?? this.topicPrefix,
    phoneId: phoneId ?? this.phoneId,
    phoneKeyHex: phoneKeyHex ?? this.phoneKeyHex,
  );

  /// Same values with spaces around the texts removed.
  BrokerSettings trimmed() => BrokerSettings(
    isEnabled: isEnabled,
    host: host.trim(),
    port: port,
    useTls: useTls,
    username: username.trim(),
    password: password.trim(),
    device: device.trim().toLowerCase(),
    topicPrefix: topicPrefix.trim(),
    phoneId: phoneId.trim(),
    phoneKeyHex: phoneKeyHex.trim().toLowerCase(),
  );

  Map<String, Object> toJson() => {
    'enabled': isEnabled,
    'host': host,
    'port': port,
    'tls': useTls,
    'username': username,
    'password': password,
    'device': device,
    'topic_prefix': topicPrefix,
    'phone_id': phoneId,
    'phone_key': phoneKeyHex,
  };

  @override
  bool operator ==(Object other) =>
      other is BrokerSettings &&
      other.isEnabled == isEnabled &&
      other.host == host &&
      other.port == port &&
      other.useTls == useTls &&
      other.username == username &&
      other.password == password &&
      other.device == device &&
      other.topicPrefix == topicPrefix &&
      other.phoneId == phoneId &&
      other.phoneKeyHex == phoneKeyHex;

  @override
  int get hashCode => Object.hash(
    isEnabled,
    host,
    port,
    useTls,
    username,
    password,
    device,
    topicPrefix,
    phoneId,
    phoneKeyHex,
  );
}

final _deviceIdPattern = RegExp(r'^[a-z0-9-]+$');

/// The first thing that stops [settings] from working, or null when they
/// can be used to connect. Shown under the form in Settings.
String? brokerSettingsProblem(BrokerSettings settings) {
  final trimmed = settings.trimmed();
  if (trimmed.host.isEmpty) return 'Enter the broker host';
  if (trimmed.port < 1 || trimmed.port > 65535) {
    return 'Port must be 1 to 65535';
  }
  if (trimmed.device.isEmpty) return 'Enter the board id (fu-xxxxxx)';
  if (!_deviceIdPattern.hasMatch(trimmed.device)) {
    return 'Board id: only a-z, 0-9 and "-"';
  }
  if (trimmed.isAdafruit &&
      (trimmed.username.isEmpty || trimmed.password.isEmpty)) {
    return 'Adafruit IO needs the username and the AIO key';
  }
  if (trimmed.effectiveTopicPrefix.contains(RegExp(r'[#+\s]'))) {
    return 'Topic prefix cannot contain #, + or spaces';
  }
  if (trimmed.phoneKeyHex.isNotEmpty) {
    final List<int> keyBytes;
    try {
      keyBytes = hexToBytes(trimmed.phoneKeyHex);
    } on FormatException {
      return 'Phone key must be hex (0-9, a-f)';
    }
    if (keyBytes.length < minimumPhoneKeyBytes) {
      return 'Phone key needs at least ${minimumPhoneKeyBytes * 2} hex digits';
    }
    if (trimmed.phoneId.isEmpty) return 'Enter the phone id for the key';
  }
  return null;
}

/// The saved broker settings. Changing them reconnects the broker.
final brokerSettingsProvider =
    NotifierProvider<BrokerSettingsStore, BrokerSettings>(
      BrokerSettingsStore.new,
    );

/// shortcut: the AIO key and phone key sit in SharedPreferences
/// (app-private on Android), not secure storage; move them there with
/// phone pairing (contract phase C).
class BrokerSettingsStore extends Notifier<BrokerSettings> {
  static const _storageKey = 'broker_settings';

  @override
  BrokerSettings build() {
    final saved = ref.watch(sharedPreferencesProvider).getString(_storageKey);
    if (saved == null) return const BrokerSettings();
    try {
      final decoded = jsonDecode(saved);
      return decoded is Map<String, dynamic>
          ? BrokerSettings.fromJson(decoded)
          : const BrokerSettings();
    } on FormatException {
      return const BrokerSettings();
    }
  }

  Future<void> save(BrokerSettings settings) async {
    final cleaned = settings.trimmed();
    await ref
        .read(sharedPreferencesProvider)
        .setString(_storageKey, jsonEncode(cleaned.toJson()));
    state = cleaned;
  }
}

import 'dart:async';

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../notify/door_notifications.dart';
import '../retry_delay.dart';
import 'broker_connection.dart';
import 'broker_messages.dart';
import 'broker_settings.dart';
import 'mqtt_client_connection.dart';

/// How many broker events the app keeps (History shows them).
const maxBrokerEvents = 50;

enum BrokerLink {
  /// MQTT switched off, or the settings are not complete.
  off,
  connecting,
  connected,

  /// The link failed or dropped; a retry is scheduled.
  offline,
}

/// What the app knows from the broker.
class BrokerState {
  const BrokerState(
    this.link, {
    this.problem,
    this.retryIn,
    this.boardOnline,
    this.waiting,
    this.events = const [],
    this.throttleNotice,
    this.droppedMessages = 0,
  });

  final BrokerLink link;

  /// Why the link is off or offline, for the user.
  final String? problem;

  /// Wait before the next connect attempt, while offline.
  final Duration? retryIn;

  /// From topic `status`; null until the board said something.
  final bool? boardOnline;

  /// The approval request the board waits for right now.
  final ApprovalRequest? waiting;

  /// Newest first, at most [maxBrokerEvents], since the app started.
  final List<BrokerEvent> events;

  /// Last rate limit notice from Adafruit IO.
  final String? throttleNotice;

  /// Messages on our topics that could not be read.
  final int droppedMessages;

  /// Nullable fields take a function, so they can also be set to null.
  BrokerState copyWith({
    BrokerLink? link,
    String? Function()? problem,
    Duration? Function()? retryIn,
    bool? Function()? boardOnline,
    ApprovalRequest? Function()? waiting,
    List<BrokerEvent>? events,
    String? Function()? throttleNotice,
    int? droppedMessages,
  }) => BrokerState(
    link ?? this.link,
    problem: problem == null ? this.problem : problem(),
    retryIn: retryIn == null ? this.retryIn : retryIn(),
    boardOnline: boardOnline == null ? this.boardOnline : boardOnline(),
    waiting: waiting == null ? this.waiting : waiting(),
    events: events ?? this.events,
    throttleNotice: throttleNotice == null
        ? this.throttleNotice
        : throttleNotice(),
    droppedMessages: droppedMessages ?? this.droppedMessages,
  );
}

/// One line about the broker link for the screens, status light first.
String describeBrokerState(BrokerState state) {
  switch (state.link) {
    case BrokerLink.off:
      final problem = state.problem;
      return problem == null ? '⚪ MQTT off' : '🟡 MQTT off: $problem';
    case BrokerLink.connecting:
      return '🟡 Connecting to the broker…';
    case BrokerLink.connected:
      final board = switch (state.boardOnline) {
        true => 'board online',
        false => 'board offline',
        null => 'board not heard yet',
      };
      return '🟢 Broker connected, $board';
    case BrokerLink.offline:
      final retrySeconds = state.retryIn?.inSeconds;
      final retry = retrySeconds == null ? '' : ', retry in $retrySeconds s';
      return '🔴 ${state.problem ?? 'Broker offline'}$retry';
  }
}

typedef BrokerConnectionFactory =
    BrokerConnection Function(BrokerSettings settings);

/// Makes the link to the broker; tests swap in a fake.
final brokerConnectionFactoryProvider = Provider<BrokerConnectionFactory>(
  (ref) => MqttClientConnection.new,
);

/// The live link to the broker. Not autoDispose: the app root listens to it,
/// so it stays connected while the app runs, on every tab.
final brokerProvider = NotifierProvider<BrokerNotifier, BrokerState>(
  BrokerNotifier.new,
);

/// Keeps one [BrokerConnection] up while MQTT is on, reconnects with a
/// growing wait, and turns the messages of `docs/mqtt_contract.md` into
/// [BrokerState]. Rebuilt (new link) whenever the settings change.
class BrokerNotifier extends Notifier<BrokerState> {
  late BrokerSettings _settings;
  BrokerConnection? _connection;
  StreamSubscription<BrokerMessage>? _messageSubscription;
  Timer? _retryTimer;
  Timer? _expiryTimer;
  int _failedAttempts = 0;

  @override
  BrokerState build() {
    _settings = ref.watch(brokerSettingsProvider);
    ref.onDispose(_close);
    if (!_settings.isEnabled) return const BrokerState(BrokerLink.off);
    final problem = brokerSettingsProblem(_settings);
    if (problem != null) return BrokerState(BrokerLink.off, problem: problem);
    // State cannot change inside build: connect right after it.
    Future.microtask(() {
      if (ref.mounted) _connect();
    });
    return const BrokerState(BrokerLink.connecting);
  }

  /// "Retry now" while offline: skips the rest of the wait.
  void retryNow() {
    if (state.link != BrokerLink.offline) return;
    _failedAttempts = 0;
    _connect();
  }

  /// Sends Open / Deny for [requestId] on topic `approval-answer`, signed
  /// when the phone has a key. Throws [BrokerException] when not connected
  /// or the send fails.
  Future<void> answer({required int requestId, required bool approve}) async {
    final connection = _connection;
    if (connection == null || state.link != BrokerLink.connected) {
      throw const BrokerException('Not connected to the broker');
    }
    final settings = _settings;
    final payload = settings.canSign
        ? answerPayload(
            requestId: requestId,
            approve: approve,
            phoneId: settings.phoneId,
            signature: signAnswer(
              phoneKeyHex: settings.phoneKeyHex,
              device: settings.device,
              requestId: requestId,
              approve: approve,
            ),
          )
        : answerPayload(requestId: requestId, approve: approve);
    await connection.publish(
      settings.topicFor(BrokerTopic.approvalAnswer),
      payload,
    );
    if (ref.mounted) _clearWaiting(requestId);
  }

  Future<void> _connect() async {
    _closeConnection();
    final connection = ref.read(brokerConnectionFactoryProvider)(_settings);
    _connection = connection;
    state = state.copyWith(link: BrokerLink.connecting, retryIn: () => null);
    try {
      await connection.connect(subscribeTopics: _settings.subscribeTopics);
    } on BrokerException catch (error) {
      if (_isCurrent(connection)) _failAndRetry(error.reason);
      return;
    } catch (error) {
      if (_isCurrent(connection)) _failAndRetry('Cannot connect: $error');
      return;
    }
    if (!_isCurrent(connection)) {
      // Settings changed or Retry pressed while connecting.
      await connection.disconnect();
      return;
    }
    _failedAttempts = 0;
    _messageSubscription = connection.messages.listen(
      _onMessage,
      onDone: () {
        if (_isCurrent(connection)) {
          _failAndRetry('Lost the link to the broker');
        }
      },
    );
    state = state.copyWith(link: BrokerLink.connected, problem: () => null);
  }

  bool _isCurrent(BrokerConnection connection) =>
      ref.mounted && identical(_connection, connection);

  void _onMessage(BrokerMessage message) {
    final receivedAt = DateTime.now();
    final topic = message.topic;
    if (topic == _settings.topicFor(BrokerTopic.approvalRequest)) {
      final request = ApprovalRequest.tryParse(message.payload, receivedAt);
      if (request == null) {
        _countBadMessage();
      } else {
        _setWaiting(request);
      }
    } else if (topic == _settings.topicFor(BrokerTopic.event)) {
      final event = BrokerEvent.tryParse(message.payload, receivedAt);
      if (event == null) {
        _countBadMessage();
      } else {
        _addEvent(event);
      }
    } else if (topic == _settings.topicFor(BrokerTopic.status)) {
      final online = parseBoardOnline(message.payload);
      if (online == null) {
        _countBadMessage();
      } else {
        state = state.copyWith(boardOnline: () => online);
      }
    } else if (topic == _settings.throttleTopic) {
      state = state.copyWith(throttleNotice: () => message.payload.trim());
    }
  }

  void _setWaiting(ApprovalRequest request) {
    _expiryTimer?.cancel();
    _expiryTimer = Timer(
      request.expiresIn,
      () => _clearWaiting(request.requestId),
    );
    state = state.copyWith(waiting: () => request);
    ref
        .read(doorNotificationsProvider)
        .showVisitor(requestId: request.requestId, name: request.name);
  }

  void _addEvent(BrokerEvent event) {
    state = state.copyWith(
      events: [event, ...state.events.take(maxBrokerEvents - 1)],
    );
    if (event.endsApproval) _clearWaiting(event.requestId);
  }

  /// Forgets the waiting request if it is [requestId].
  void _clearWaiting(int requestId) {
    if (state.waiting?.requestId != requestId) return;
    _expiryTimer?.cancel();
    _expiryTimer = null;
    state = state.copyWith(waiting: () => null);
    ref.read(doorNotificationsProvider).clearVisitor();
  }

  void _countBadMessage() {
    state = state.copyWith(droppedMessages: state.droppedMessages + 1);
  }

  /// Shows [reason], then tries again after a growing wait.
  void _failAndRetry(String reason) {
    _closeConnection();
    final delay = retryDelay(_failedAttempts);
    _failedAttempts++;
    state = state.copyWith(
      link: BrokerLink.offline,
      problem: () => reason,
      retryIn: () => delay,
    );
    _retryTimer = Timer(delay, _connect);
  }

  void _closeConnection() {
    _retryTimer?.cancel();
    _retryTimer = null;
    // Cancel first, so the stream's onDone does not schedule a retry.
    _messageSubscription?.cancel();
    _messageSubscription = null;
    _connection?.disconnect();
    _connection = null;
  }

  void _close() {
    _closeConnection();
    _expiryTimer?.cancel();
    _expiryTimer = null;
  }
}

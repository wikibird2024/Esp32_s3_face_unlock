/// One message received from the broker, payload decoded as UTF-8.
class BrokerMessage {
  const BrokerMessage(this.topic, this.payload);

  final String topic;
  final String payload;
}

/// Connecting, subscribing or publishing failed. [reason] is short text for
/// the user, never containing the password.
class BrokerException implements Exception {
  const BrokerException(this.reason);

  final String reason;

  @override
  String toString() => reason;
}

/// One link to the MQTT broker: connect once, then use it until
/// [messages] ends. A dropped link is not reused; the owner makes a new one.
///
/// The seam between the app and the MQTT library, so the logic above it is
/// tested with a fake instead of a broker.
abstract interface class BrokerConnection {
  /// Connects and subscribes to [subscribeTopics] (QoS 1).
  /// Throws [BrokerException].
  Future<void> connect({required List<String> subscribeTopics});

  /// Messages on the subscribed topics. Single listener; messages that
  /// arrive before it listens are kept. Ends when the link drops or after
  /// [disconnect].
  Stream<BrokerMessage> get messages;

  /// Sends [payload] to [topic] with QoS 1. Throws [BrokerException].
  Future<void> publish(String topic, String payload);

  /// Closes the link; [messages] ends. Safe to call more than once.
  Future<void> disconnect();
}

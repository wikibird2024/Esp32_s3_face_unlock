/// Longest wait between reconnect attempts (video stream, MQTT broker).
const maxRetryDelay = Duration(seconds: 30);

/// Wait before reconnect attempt [attempt] (0-based): 1, 2, 4, 8, 16, 30, 30… s.
Duration retryDelay(int attempt) {
  final seconds = 1 << attempt.clamp(0, 5);
  final delay = Duration(seconds: seconds);
  return delay > maxRetryDelay ? maxRetryDelay : delay;
}

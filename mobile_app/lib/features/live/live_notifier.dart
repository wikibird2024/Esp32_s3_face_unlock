import 'dart:async';
import 'dart:collection';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:http/http.dart' as http;

import '../../core/board/board_address.dart';
import '../../core/board/mjpeg_reader.dart';
import '../../core/retry_delay.dart';

enum LiveStatus { stopped, noAddress, connecting, live, offline }

class LiveState {
  const LiveState(
    this.status, {
    this.frame,
    this.message,
    this.retryIn,
    this.framesPerSecond = 0,
  });

  final LiveStatus status;

  /// Last JPEG received. Kept when the stream drops, so the screen can
  /// still show (and snapshot) the last picture.
  final Uint8List? frame;
  final String? message;

  /// Set while waiting to reconnect after going offline.
  final Duration? retryIn;

  /// Frames received in the last second.
  final int framesPerSecond;
}

const connectTimeout = Duration(seconds: 5);

/// Longer than this without a frame counts as offline.
const frameTimeout = Duration(seconds: 5);

/// Live view state. Disposed when the Live tab is left, which closes the
/// stream: the board serves one viewer at a time.
final liveProvider = NotifierProvider.autoDispose<LiveNotifier, LiveState>(
  LiveNotifier.new,
);

class LiveNotifier extends Notifier<LiveState> {
  http.Client? _client;
  StreamSubscription<Uint8List>? _frameSubscription;
  Timer? _frameTimer;
  Timer? _retryTimer;
  int _failedAttempts = 0;
  final _recentFrameTimes = Queue<DateTime>();

  /// The user wants to watch: false after Stop, so coming back from the
  /// background does not restart a stream the user stopped.
  bool _wantsStream = false;

  @override
  LiveState build() {
    ref.onDispose(_close);
    return const LiveState(LiveStatus.stopped);
  }

  /// Start watching (Start button, opening the tab). Resets the retry wait.
  Future<void> start() {
    _wantsStream = true;
    _failedAttempts = 0;
    return _connect();
  }

  /// Stop button: stop and stay stopped.
  void stop() {
    _wantsStream = false;
    _close();
    state = LiveState(LiveStatus.stopped, frame: state.frame);
  }

  /// App went to the background: free the board's viewer slot, but come
  /// back on [resume] if the user was watching.
  void pause() {
    _close();
    if (_wantsStream) {
      state = LiveState(LiveStatus.stopped, frame: state.frame);
    }
  }

  void resume() {
    if (_wantsStream) start();
  }

  Future<void> _connect() async {
    _close();
    final baseUri = boardBaseUri(ref.read(boardAddressProvider));
    if (baseUri == null) {
      state = const LiveState(LiveStatus.noAddress);
      return;
    }
    state = LiveState(LiveStatus.connecting, frame: state.frame);
    final client = ref.read(httpClientFactoryProvider)();
    _client = client;
    final request = http.Request(
      'GET',
      boardStreamUri(baseUri).replace(path: '/stream'),
    )..headers.addAll(boardAuthHeaders(ref.read(boardTokenProvider)));
    try {
      final response = await client.send(request).timeout(connectTimeout);
      if (_client != client) return; // stopped while connecting
      if (response.statusCode == 401) {
        _failAndRetry(LiveStatus.offline, 'Wrong token: check Settings');
        return;
      }
      if (response.statusCode != 200) {
        _failAndRetry(
          LiveStatus.offline,
          'Board answered HTTP ${response.statusCode}',
        );
        return;
      }
      final boundary =
          boundaryFromContentType(response.headers['content-type']) ??
          defaultBoundary;
      _frameSubscription = MjpegReader(boundary)
          .frames(response.stream)
          .listen(
            _onFrame,
            onError: (Object error) =>
                _failAndRetry(LiveStatus.offline, 'Stream error: $error'),
            onDone: () =>
                _failAndRetry(LiveStatus.offline, 'Board closed the stream'),
          );
      _restartFrameTimer();
    } on TimeoutException {
      // The board's stream server is one task: while another viewer
      // streams, it does not answer at all.
      if (_client == client) {
        _failAndRetry(
          LiveStatus.offline,
          'No answer (offline, or someone watching)',
        );
      }
    } catch (error) {
      if (_client == client) {
        _failAndRetry(LiveStatus.offline, 'Cannot reach the board: $error');
      }
    }
  }

  void _onFrame(Uint8List frame) {
    _failedAttempts = 0;
    final now = DateTime.now();
    _recentFrameTimes.addLast(now);
    while (now.difference(_recentFrameTimes.first) >
        const Duration(seconds: 1)) {
      _recentFrameTimes.removeFirst();
    }
    state = LiveState(
      LiveStatus.live,
      frame: frame,
      framesPerSecond: _recentFrameTimes.length,
    );
    _restartFrameTimer();
  }

  /// While live, the board's stream server task is busy with the stream, so
  /// the snapshot is the current frame. Otherwise ask the board for `/jpg`.
  Future<Uint8List> takeSnapshot() async {
    final frame = state.frame;
    if (state.status == LiveStatus.live && frame != null) return frame;
    final baseUri = boardBaseUri(ref.read(boardAddressProvider));
    if (baseUri == null) {
      throw StateError('Set the board address in Settings');
    }
    final client = ref.read(httpClientFactoryProvider)();
    try {
      final response = await client
          .get(
            boardStreamUri(baseUri).replace(path: '/jpg'),
            headers: boardAuthHeaders(ref.read(boardTokenProvider)),
          )
          .timeout(connectTimeout);
      if (response.statusCode != 200) {
        throw StateError('Board answered HTTP ${response.statusCode}');
      }
      return response.bodyBytes;
    } finally {
      client.close();
    }
  }

  void _restartFrameTimer() {
    _frameTimer?.cancel();
    _frameTimer = Timer(
      frameTimeout,
      () => _failAndRetry(LiveStatus.offline, 'No frames for 5 s'),
    );
  }

  /// Show the problem, then try again after a growing wait.
  void _failAndRetry(LiveStatus status, String message) {
    _close();
    final delay = retryDelay(_failedAttempts);
    _failedAttempts++;
    state = LiveState(
      status,
      frame: state.frame,
      message: message,
      retryIn: delay,
    );
    _retryTimer = Timer(delay, _connect);
  }

  void _close() {
    _retryTimer?.cancel();
    _retryTimer = null;
    _frameTimer?.cancel();
    _frameTimer = null;
    _frameSubscription?.cancel();
    _frameSubscription = null;
    _client?.close();
    _client = null;
    _recentFrameTimes.clear();
  }
}

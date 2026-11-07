import 'dart:async';

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/board/board_api.dart';
import '../../core/mqtt/broker_connection.dart';
import '../../core/mqtt/broker_notifier.dart';

/// How often the tab asks the board whether someone waits, when the broker
/// is not connected.
const approvalPollInterval = Duration(seconds: 2);

/// Where the Approve tab gets requests from and sends answers to.
enum ApprovalChannel {
  /// No board address and no broker: nothing to show.
  none,

  /// Board HTTP API on the home Wi-Fi, polled.
  board,

  /// MQTT broker, pushed; also works away from home.
  broker,
}

/// A known face waiting for Open / Deny.
class PendingApproval {
  const PendingApproval({
    required this.requestId,
    required this.name,
    this.deadline,
  });

  final int requestId;

  /// Empty when the board did not send a name.
  final String name;

  /// Phone clock time when the board stops waiting; null if unknown.
  final DateTime? deadline;

  @override
  bool operator ==(Object other) =>
      other is PendingApproval &&
      other.requestId == requestId &&
      other.name == name &&
      other.deadline == deadline;

  @override
  int get hashCode => Object.hash(requestId, name, deadline);
}

class ApprovalState {
  const ApprovalState(
    this.channel, {
    this.pending,
    this.error,
    this.isLoading = false,
    this.isAnswering = false,
  });

  final ApprovalChannel channel;
  final PendingApproval? pending;

  /// Last polling error (board channel only).
  final String? error;

  /// First poll not answered yet.
  final bool isLoading;
  final bool isAnswering;

  /// Nullable fields take a function, so they can also be set to null.
  ApprovalState copyWith({
    PendingApproval? Function()? pending,
    String? Function()? error,
    bool? isLoading,
    bool? isAnswering,
  }) => ApprovalState(
    channel,
    pending: pending == null ? this.pending : pending(),
    error: error == null ? this.error : error(),
    isLoading: isLoading ?? this.isLoading,
    isAnswering: isAnswering ?? this.isAnswering,
  );
}

/// Short text for a failed answer, for a SnackBar.
String approvalErrorText(Object error) =>
    error is BrokerException ? error.reason : boardErrorText(error);

/// The Approve tab's logic. Uses the broker while it is connected (instant,
/// works away from home), else polls the board's HTTP API. Lives only while
/// the tab is open, so polling stops when the tab is left.
final approvalProvider =
    NotifierProvider.autoDispose<ApprovalNotifier, ApprovalState>(
      ApprovalNotifier.new,
    );

class ApprovalNotifier extends Notifier<ApprovalState> {
  @override
  ApprovalState build() {
    final isBrokerConnected = ref.watch(
      brokerProvider.select((broker) => broker.link == BrokerLink.connected),
    );
    if (isBrokerConnected) {
      final waiting = ref.watch(brokerProvider.select((b) => b.waiting));
      return ApprovalState(
        ApprovalChannel.broker,
        pending: waiting == null
            ? null
            : PendingApproval(
                requestId: waiting.requestId,
                name: waiting.name,
                deadline: waiting.deadline,
              ),
      );
    }
    if (ref.watch(boardApiProvider) == null) {
      return const ApprovalState(ApprovalChannel.none);
    }
    final pollTimer = Timer.periodic(approvalPollInterval, (_) => _poll());
    ref.onDispose(pollTimer.cancel);
    Future.microtask(() {
      if (ref.mounted) _poll();
    });
    return const ApprovalState(ApprovalChannel.board, isLoading: true);
  }

  /// Open ([approve] true) or Deny the pending request. Throws on failure;
  /// [approvalErrorText] turns the error into text.
  Future<void> answer({required bool approve}) async {
    final pending = state.pending;
    if (pending == null || state.isAnswering) return;
    final channel = state.channel;
    state = state.copyWith(isAnswering: true);
    try {
      switch (channel) {
        case ApprovalChannel.broker:
          await ref
              .read(brokerProvider.notifier)
              .answer(requestId: pending.requestId, approve: approve);
        case ApprovalChannel.board:
          await ref
              .read(boardApiProvider)
              ?.answerApproval(pending.requestId, approve: approve);
        case ApprovalChannel.none:
          return;
      }
    } finally {
      // A broker answer clears the request, which rebuilds this notifier.
      if (ref.mounted) state = state.copyWith(isAnswering: false);
    }
    if (channel == ApprovalChannel.board && ref.mounted) await _poll();
  }

  Future<void> _poll() async {
    final api = ref.read(boardApiProvider);
    if (api == null) return;
    try {
      final status = await api.status();
      if (!ref.mounted) return;
      final left = status.waitingLeft;
      state = state.copyWith(
        isLoading: false,
        error: () => null,
        pending: () => status.waitingRequest == 0
            ? null
            : PendingApproval(
                requestId: status.waitingRequest,
                name: status.waitingName ?? '',
                deadline: left == null ? null : DateTime.now().add(left),
              ),
      );
    } catch (error) {
      if (!ref.mounted) return;
      state = state.copyWith(
        isLoading: false,
        error: () => boardErrorText(error),
      );
    }
  }
}

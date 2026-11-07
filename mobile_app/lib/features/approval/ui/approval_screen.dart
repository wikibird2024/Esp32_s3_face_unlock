import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/mqtt/broker_notifier.dart';
import '../approval_notifier.dart';

/// A known face at the door waits here for Open / Deny (when "Wait for
/// approval" is above 0 in Settings → Unlock rules). The logic is in
/// [ApprovalNotifier]; this widget only draws it.
class ApprovalScreen extends ConsumerStatefulWidget {
  const ApprovalScreen({super.key});

  @override
  ConsumerState<ApprovalScreen> createState() => _ApprovalScreenState();
}

class _ApprovalScreenState extends ConsumerState<ApprovalScreen> {
  Future<void> _answer({required bool approve}) async {
    final messenger = ScaffoldMessenger.of(context);
    try {
      await ref.read(approvalProvider.notifier).answer(approve: approve);
      messenger.showSnackBar(
        SnackBar(content: Text(approve ? 'Door opening' : 'Denied')),
      );
    } catch (error) {
      messenger.showSnackBar(
        SnackBar(content: Text(approvalErrorText(error))),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final approval = ref.watch(approvalProvider);
    final broker = ref.watch(brokerProvider);
    return Scaffold(
      appBar: AppBar(title: const Text('Approve')),
      body: Column(
        children: [
          if (broker.link != BrokerLink.off || broker.problem != null)
            ListTile(
              dense: true,
              title: Text(describeBrokerState(broker)),
              trailing: broker.link == BrokerLink.offline
                  ? TextButton(
                      onPressed: ref.read(brokerProvider.notifier).retryNow,
                      child: const Text('Retry'),
                    )
                  : null,
            ),
          Expanded(
            child: Center(
              child: Padding(
                padding: const EdgeInsets.all(24),
                child: _buildBody(context, approval),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildBody(BuildContext context, ApprovalState approval) {
    if (approval.channel == ApprovalChannel.none) {
      return const Text(
        'Set the board address or the broker in Settings ⚙',
        textAlign: TextAlign.center,
      );
    }
    final pending = approval.pending;
    if (pending == null) {
      if (approval.isLoading) return const CircularProgressIndicator();
      return _NoOneWaiting(error: approval.error);
    }
    return _VisitorCard(
      pending: pending,
      isAnswering: approval.isAnswering,
      onAnswer: _answer,
    );
  }
}

class _NoOneWaiting extends StatelessWidget {
  const _NoOneWaiting({this.error});

  final String? error;

  @override
  Widget build(BuildContext context) {
    final error = this.error;
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(
          Icons.how_to_reg,
          size: 64,
          color: Theme.of(context).disabledColor,
        ),
        const SizedBox(height: 16),
        Text(
          'No one is waiting',
          style: Theme.of(context).textTheme.titleLarge,
        ),
        const SizedBox(height: 8),
        const Text(
          'With "Wait for approval" above 0 (Settings → Unlock rules), a '
          'known face waits here for your answer.',
          textAlign: TextAlign.center,
        ),
        if (error != null) ...[const SizedBox(height: 16), Text('🔴 $error')],
      ],
    );
  }
}

class _VisitorCard extends StatelessWidget {
  const _VisitorCard({
    required this.pending,
    required this.isAnswering,
    required this.onAnswer,
  });

  final PendingApproval pending;
  final bool isAnswering;
  final Future<void> Function({required bool approve}) onAnswer;

  @override
  Widget build(BuildContext context) {
    final deadline = pending.deadline;
    final who = pending.name.isEmpty ? 'A known face' : pending.name;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.doorbell, size: 64),
            const SizedBox(height: 16),
            Text(
              '$who is at the door',
              style: Theme.of(context).textTheme.titleLarge,
              textAlign: TextAlign.center,
            ),
            if (deadline != null) ...[
              const SizedBox(height: 8),
              _Countdown(deadline: deadline),
            ],
            const SizedBox(height: 24),
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                FilledButton.icon(
                  onPressed: isAnswering ? null : () => onAnswer(approve: true),
                  icon: const Icon(Icons.lock_open),
                  label: const Text('Open'),
                ),
                const SizedBox(width: 16),
                OutlinedButton.icon(
                  onPressed: isAnswering
                      ? null
                      : () => onAnswer(approve: false),
                  icon: const Icon(Icons.block),
                  label: const Text('Deny'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

/// "Answer within N s", redrawn every second while shown.
class _Countdown extends StatefulWidget {
  const _Countdown({required this.deadline});

  final DateTime deadline;

  @override
  State<_Countdown> createState() => _CountdownState();
}

class _CountdownState extends State<_Countdown> {
  Timer? _tickTimer;

  @override
  void initState() {
    super.initState();
    _tickTimer = Timer.periodic(
      const Duration(seconds: 1),
      (_) => setState(() {}),
    );
  }

  @override
  void dispose() {
    _tickTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final seconds = widget.deadline.difference(DateTime.now()).inSeconds;
    return Text('Answer within ${seconds < 0 ? 0 : seconds} s');
  }
}

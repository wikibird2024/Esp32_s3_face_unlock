import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/board/board_api.dart';

/// How often the tab asks the board whether someone waits.
const approvalPollInterval = Duration(seconds: 2);

/// A known face at the door waits here for Open / Deny (when "Wait for
/// approval" is above 0 in Settings → Unlock rules). Polls only while the
/// tab is open.
class ApprovalScreen extends ConsumerStatefulWidget {
  const ApprovalScreen({super.key});

  @override
  ConsumerState<ApprovalScreen> createState() => _ApprovalScreenState();
}

class _ApprovalScreenState extends ConsumerState<ApprovalScreen> {
  Timer? _pollTimer;
  BoardStatus? _status;
  String? _error;
  bool _isAnswering = false;

  @override
  void initState() {
    super.initState();
    _refresh();
    _pollTimer = Timer.periodic(approvalPollInterval, (_) => _refresh());
  }

  @override
  void dispose() {
    _pollTimer?.cancel();
    super.dispose();
  }

  Future<void> _refresh() async {
    final api = ref.read(boardApiProvider);
    if (api == null) return;
    try {
      final status = await api.status();
      if (!mounted) return;
      setState(() {
        _status = status;
        _error = null;
      });
    } catch (error) {
      if (!mounted) return;
      setState(() => _error = boardErrorText(error));
    }
  }

  Future<void> _answer(int requestId, {required bool approve}) async {
    final api = ref.read(boardApiProvider);
    if (api == null) return;
    final messenger = ScaffoldMessenger.of(context);
    setState(() => _isAnswering = true);
    try {
      await api.answerApproval(requestId, approve: approve);
      messenger.showSnackBar(
        SnackBar(content: Text(approve ? 'Door opening' : 'Denied')),
      );
    } catch (error) {
      messenger.showSnackBar(SnackBar(content: Text(boardErrorText(error))));
    }
    if (!mounted) return;
    setState(() => _isAnswering = false);
    await _refresh();
  }

  @override
  Widget build(BuildContext context) {
    final hasAddress = ref.watch(boardApiProvider) != null;
    return Scaffold(
      appBar: AppBar(title: const Text('Approve')),
      body: Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: hasAddress
              ? _buildBody(context)
              : const Text('Set the board address in Settings ⚙'),
        ),
      ),
    );
  }

  Widget _buildBody(BuildContext context) {
    final status = _status;
    if (status == null) {
      return _error == null
          ? const CircularProgressIndicator()
          : Text('🔴 $_error');
    }
    if (status.waitingRequest == 0) {
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
          if (_error != null) ...[
            const SizedBox(height: 16),
            Text('🔴 $_error'),
          ],
        ],
      );
    }
    final left = status.waitingLeft;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.doorbell, size: 64),
            const SizedBox(height: 16),
            Text(
              '${status.waitingName ?? 'A known face'} is at the door',
              style: Theme.of(context).textTheme.titleLarge,
              textAlign: TextAlign.center,
            ),
            if (left != null) ...[
              const SizedBox(height: 8),
              Text('Answer within ${left.inSeconds} s'),
            ],
            const SizedBox(height: 24),
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                FilledButton.icon(
                  onPressed: _isAnswering
                      ? null
                      : () => _answer(status.waitingRequest, approve: true),
                  icon: const Icon(Icons.lock_open),
                  label: const Text('Open'),
                ),
                const SizedBox(width: 16),
                OutlinedButton.icon(
                  onPressed: _isAnswering
                      ? null
                      : () => _answer(status.waitingRequest, approve: false),
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

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/board/board_api.dart';

/// The board's last 50 events, newest first. The board keeps them in RAM,
/// so the list starts empty after a board restart.
class HistoryScreen extends ConsumerStatefulWidget {
  const HistoryScreen({super.key});

  @override
  ConsumerState<HistoryScreen> createState() => _HistoryScreenState();
}

class _HistoryScreenState extends ConsumerState<HistoryScreen> {
  BoardEvents? _events;
  DateTime _receivedAt = DateTime.now();
  String? _error;

  @override
  void initState() {
    super.initState();
    _refresh();
  }

  Future<void> _refresh() async {
    final api = ref.read(boardApiProvider);
    if (api == null) return;
    try {
      final events = await api.events();
      if (!mounted) return;
      setState(() {
        _events = events;
        _receivedAt = DateTime.now();
        _error = null;
      });
    } catch (error) {
      if (!mounted) return;
      setState(() => _error = boardErrorText(error));
    }
  }

  @override
  Widget build(BuildContext context) {
    final hasAddress = ref.watch(boardApiProvider) != null;
    return Scaffold(
      appBar: AppBar(
        title: const Text('History'),
        actions: [
          IconButton(
            tooltip: 'Refresh',
            onPressed: hasAddress ? _refresh : null,
            icon: const Icon(Icons.refresh),
          ),
        ],
      ),
      body: hasAddress
          ? RefreshIndicator(onRefresh: _refresh, child: _buildList())
          : const Center(child: Text('Set the board address in Settings ⚙')),
    );
  }

  Widget _buildList() {
    final events = _events;
    if (events == null || events.events.isEmpty) {
      final message = events == null
          ? (_error == null ? null : '🔴 $_error')
          : 'Nothing yet since the board started.';
      return ListView(
        children: [
          const SizedBox(height: 120),
          Center(
            child: message == null
                ? const CircularProgressIndicator()
                : Text(message),
          ),
        ],
      );
    }
    final newestFirst = events.events.reversed.toList();
    return ListView.builder(
      itemCount: newestFirst.length,
      itemBuilder: (context, index) {
        final event = newestFirst[index];
        return ListTile(
          leading: Icon(iconForEvent(event.type)),
          title: Text(describeEvent(event)),
          subtitle: Text(timeAgo(_receivedAt, events.timeOf(event, _receivedAt))),
        );
      },
    );
  }
}

IconData iconForEvent(String type) {
  switch (type) {
    case 'door_opened':
      return Icons.lock_open;
    case 'door_closed':
      return Icons.lock;
    case 'face_rejected':
    case 'approval_denied':
    case 'approval_timeout':
      return Icons.block;
    case 'enroll_started':
    case 'face_enrolled':
      return Icons.person_add;
    case 'enroll_failed':
      return Icons.person_off;
    case 'approval_requested':
      return Icons.doorbell;
    default:
      return Icons.info_outline;
  }
}

/// "just now", "5 min ago", "2 h ago", "3 d ago".
String timeAgo(DateTime now, DateTime then) {
  final age = now.difference(then);
  if (age.inSeconds < 60) return 'just now';
  if (age.inMinutes < 60) return '${age.inMinutes} min ago';
  if (age.inHours < 24) return '${age.inHours} h ago';
  return '${age.inDays} d ago';
}

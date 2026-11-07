import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/board/board_api.dart';
import '../../../core/mqtt/broker_messages.dart';
import '../../../core/mqtt/broker_notifier.dart';

/// The board's last 50 events, newest first, and above them the events the
/// broker sent since the app started. The board keeps its list in RAM, so it
/// starts empty after a board restart.
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
    final brokerEvents = ref.watch(brokerProvider.select((b) => b.events));
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
      body: hasAddress || brokerEvents.isNotEmpty
          ? RefreshIndicator(
              onRefresh: _refresh,
              child: ListView(
                children: [
                  if (brokerEvents.isNotEmpty) ...[
                    const _SectionHeader(
                      'From the broker, since the app started',
                    ),
                    for (final event in brokerEvents) _brokerTile(event),
                    if (hasAddress) const _SectionHeader('From the board'),
                  ],
                  if (hasAddress) ..._boardTiles(),
                ],
              ),
            )
          : const Center(child: Text('Set the board address in Settings ⚙')),
    );
  }

  Widget _brokerTile(BrokerEvent event) {
    final asBoardEvent = BoardEvent(
      seq: 0,
      timeMs: 0,
      type: event.type,
      faceId: event.faceId,
      name: event.name,
      requestId: event.requestId,
    );
    return ListTile(
      leading: Icon(iconForEvent(event.type)),
      title: Text(describeEvent(asBoardEvent)),
      subtitle: Text(timeAgo(DateTime.now(), event.time)),
    );
  }

  List<Widget> _boardTiles() {
    final events = _events;
    if (events == null || events.events.isEmpty) {
      final message = events == null
          ? (_error == null ? null : '🔴 $_error')
          : 'Nothing yet since the board started.';
      return [
        const SizedBox(height: 120),
        Center(
          child: message == null
              ? const CircularProgressIndicator()
              : Text(message),
        ),
      ];
    }
    return [
      for (final event in events.events.reversed)
        ListTile(
          leading: Icon(iconForEvent(event.type)),
          title: Text(describeEvent(event)),
          subtitle: Text(
            timeAgo(_receivedAt, events.timeOf(event, _receivedAt)),
          ),
        ),
    ];
  }
}

class _SectionHeader extends StatelessWidget {
  const _SectionHeader(this.title);

  final String title;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(16, 16, 16, 4),
    child: Text(title, style: Theme.of(context).textTheme.titleSmall),
  );
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

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/board/board_api.dart';
import '../../../core/device/keep_screen_on.dart';
import '../live_notifier.dart';
import 'live_full_screen.dart';
import 'live_video.dart';

class LiveScreen extends ConsumerStatefulWidget {
  const LiveScreen({super.key});

  @override
  ConsumerState<LiveScreen> createState() => _LiveScreenState();
}

class _LiveScreenState extends ConsumerState<LiveScreen> {
  late final AppLifecycleListener _lifecycleListener;
  late final Future<void> Function(bool) _keepScreenOn;

  @override
  void initState() {
    super.initState();
    _keepScreenOn = ref.read(keepScreenOnProvider);
    // Free the board's single viewer slot while the app is in the background.
    _lifecycleListener = AppLifecycleListener(
      onHide: () => ref.read(liveProvider.notifier).pause(),
      onShow: () => ref.read(liveProvider.notifier).resume(),
    );
    Future.microtask(() => ref.read(liveProvider.notifier).start());
  }

  @override
  void dispose() {
    _lifecycleListener.dispose();
    _keepScreenOn(false);
    super.dispose();
  }

  Future<void> _showSnapshot() async {
    final messenger = ScaffoldMessenger.of(context);
    try {
      final jpeg = await ref.read(liveProvider.notifier).takeSnapshot();
      if (!mounted) return;
      await showDialog<void>(
        context: context,
        builder: (context) =>
            Dialog(child: InteractiveViewer(child: Image.memory(jpeg))),
      );
    } catch (error) {
      messenger.showSnackBar(
        SnackBar(content: Text('Snapshot failed: $error')),
      );
    }
  }

  /// Asks first: this opens the real door.
  Future<void> _unlock() async {
    final api = ref.read(boardApiProvider);
    if (api == null) return;
    final messenger = ScaffoldMessenger.of(context);
    final isConfirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Open the door?'),
        content: const Text('The board locks it again by itself.'),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Open'),
          ),
        ],
      ),
    );
    if (isConfirmed != true) return;
    try {
      await api.unlock();
      messenger.showSnackBar(const SnackBar(content: Text('Door opening')));
    } catch (error) {
      messenger.showSnackBar(SnackBar(content: Text(boardErrorText(error))));
    }
  }

  void _openFullScreen() {
    Navigator.of(
      context,
      rootNavigator: true,
    ).push(MaterialPageRoute<void>(builder: (_) => const LiveFullScreen()));
  }

  @override
  Widget build(BuildContext context) {
    // Screen stays on only while video is coming in.
    ref.listen(liveProvider.select((live) => live.status == LiveStatus.live), (
      _,
      isLive,
    ) {
      _keepScreenOn(isLive);
    });

    final live = ref.watch(liveProvider);
    final notifier = ref.read(liveProvider.notifier);
    final isRunning =
        live.status == LiveStatus.live ||
        live.status == LiveStatus.connecting ||
        live.retryIn != null;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Live'),
        actions: [
          StatusDot(status: live.status),
          IconButton(
            tooltip: 'Full screen',
            onPressed: live.frame == null ? null : _openFullScreen,
            icon: const Icon(Icons.fullscreen),
          ),
        ],
      ),
      body: Column(
        children: [
          Expanded(
            child: GestureDetector(
              onTap: live.frame == null ? null : _openFullScreen,
              child: Center(child: LiveVideo(live: live)),
            ),
          ),
          Padding(
            padding: const EdgeInsets.all(16),
            // Wrap: three buttons do not fit one row on a narrow phone.
            child: Wrap(
              alignment: WrapAlignment.center,
              spacing: 16,
              runSpacing: 8,
              children: [
                FilledButton.icon(
                  onPressed: isRunning ? notifier.stop : notifier.start,
                  icon: Icon(isRunning ? Icons.stop : Icons.play_arrow),
                  label: Text(isRunning ? 'Stop' : 'Start'),
                ),
                OutlinedButton.icon(
                  onPressed: live.status == LiveStatus.noAddress
                      ? null
                      : _showSnapshot,
                  icon: const Icon(Icons.photo_camera),
                  label: const Text('Snapshot'),
                ),
                OutlinedButton.icon(
                  onPressed: live.status == LiveStatus.noAddress
                      ? null
                      : _unlock,
                  icon: const Icon(Icons.lock_open),
                  label: const Text('Unlock'),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

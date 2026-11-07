import 'dart:async';

import 'package:flutter/material.dart';

import '../../../core/board/board_api.dart';

/// How often the dialog asks the board for news while enrolling.
const enrollPollInterval = Duration(seconds: 1);

/// Longer than the board's longest enroll timeout (120 s) plus margin.
const enrollGiveUpAfter = Duration(seconds: 150);

/// Result of an enroll, from the board's events after it started.
enum EnrollOutcome { waiting, saved, failed }

/// Looks for the end of the enroll of [name] in [events].
EnrollOutcome enrollOutcome(List<BoardEvent> events, String name) {
  for (final event in events) {
    if (event.name != name) continue;
    if (event.type == 'face_enrolled') return EnrollOutcome.saved;
    if (event.type == 'enroll_failed') return EnrollOutcome.failed;
  }
  return EnrollOutcome.waiting;
}

/// Starts an enroll on the board and follows it until the face is saved,
/// fails or is cancelled. The person stands in front of the door camera.
class EnrollDialog extends StatefulWidget {
  const EnrollDialog({super.key, required this.api, required this.name});

  final BoardApi api;
  final String name;

  @override
  State<EnrollDialog> createState() => _EnrollDialogState();
}

class _EnrollDialogState extends State<EnrollDialog> {
  Timer? _pollTimer;
  final _started = Stopwatch();
  int _afterSeq = 0;
  EnrollOutcome _outcome = EnrollOutcome.waiting;
  String? _error;

  @override
  void initState() {
    super.initState();
    _start();
  }

  @override
  void dispose() {
    _pollTimer?.cancel();
    super.dispose();
  }

  Future<void> _start() async {
    try {
      // Only events after this point belong to this enroll.
      _afterSeq = (await widget.api.events(after: 1 << 30)).lastSeq;
      // Cancelled meanwhile: never send the enroll (whoever is at the door
      // would be saved).
      if (!mounted) return;
      await widget.api.enroll(widget.name);
      if (!mounted) {
        // Cancelled while the enroll was on its way: stop it on the board.
        await widget.api.cancelEnroll();
        return;
      }
      _started.start();
      _pollTimer = Timer.periodic(enrollPollInterval, (_) => _poll());
    } catch (error) {
      _finish(EnrollOutcome.failed, boardErrorText(error));
    }
  }

  Future<void> _poll() async {
    if (_started.elapsed > enrollGiveUpAfter) {
      _finish(EnrollOutcome.failed, 'No answer from the board');
      return;
    }
    try {
      final events = await widget.api.events(after: _afterSeq);
      final outcome = enrollOutcome(events.events, widget.name);
      if (outcome != EnrollOutcome.waiting) {
        _finish(
          outcome,
          outcome == EnrollOutcome.failed
              ? 'Timed out, cancelled or the board was busy'
              : null,
        );
      }
    } catch (_) {
      // One missed poll is fine; the next one tries again.
    }
  }

  void _finish(EnrollOutcome outcome, String? error) {
    _pollTimer?.cancel();
    if (!mounted) return;
    setState(() {
      _outcome = outcome;
      _error = error;
    });
  }

  Future<void> _cancel() async {
    _pollTimer?.cancel();
    final navigator = Navigator.of(context);
    try {
      await widget.api.cancelEnroll();
    } catch (_) {
      // The board times the enroll out by itself.
    }
    navigator.pop();
  }

  @override
  Widget build(BuildContext context) {
    switch (_outcome) {
      case EnrollOutcome.waiting:
        return AlertDialog(
          title: Text('Adding ${widget.name}'),
          content: const Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              CircularProgressIndicator(),
              SizedBox(height: 24),
              Text(
                'Stand in front of the door camera and look at it. '
                'The screen on the door shows "Enroll".',
                textAlign: TextAlign.center,
              ),
            ],
          ),
          actions: [
            TextButton(onPressed: _cancel, child: const Text('Cancel')),
          ],
        );
      case EnrollOutcome.saved:
        return AlertDialog(
          icon: const Icon(Icons.check_circle, size: 48),
          title: Text('${widget.name} saved'),
          content: const Text('Add another photo for better matching.'),
          actions: [
            FilledButton(
              onPressed: () => Navigator.of(context).pop(),
              child: const Text('Done'),
            ),
          ],
        );
      case EnrollOutcome.failed:
        return AlertDialog(
          icon: const Icon(Icons.error, size: 48),
          title: const Text('Not saved'),
          content: Text(_error ?? 'Try again'),
          actions: [
            FilledButton(
              onPressed: () => Navigator.of(context).pop(),
              child: const Text('OK'),
            ),
          ],
        );
    }
  }
}

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../core/mqtt/broker_notifier.dart';
import 'router.dart';
import 'theme.dart';

class FaceUnlockApp extends ConsumerWidget {
  const FaceUnlockApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // Listen, not watch: keeps the broker link up on every tab without
    // rebuilding the whole app on each broker message.
    ref.listen(brokerProvider, (_, _) {});
    return MaterialApp.router(
      title: 'Face Unlock',
      theme: lightTheme,
      darkTheme: darkTheme,
      routerConfig: ref.watch(routerProvider),
    );
  }
}

import 'package:flutter/foundation.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

/// Phone notifications about the door. Works while the app runs, also in
/// the background; a closed app gets nothing (needs push, contract phase D).
abstract interface class DoorNotifications {
  /// Asks for the notification permission (Android 13+). False when the
  /// user said no or the platform has no notifications.
  Future<bool> requestPermission();

  /// "<name> is at the door" for approval request [requestId].
  Future<void> showVisitor({required int requestId, required String name});

  /// Removes the visitor notification once answered or expired.
  Future<void> clearVisitor();
}

/// A provider so tests swap in a fake instead of the plugin.
final doorNotificationsProvider = Provider<DoorNotifications>(
  (ref) => PluginDoorNotifications(),
);

/// [DoorNotifications] with `flutter_local_notifications`.
class PluginDoorNotifications implements DoorNotifications {
  static const _visitorNotificationId = 1;
  static const _channelId = 'door_visitor';

  final _plugin = FlutterLocalNotificationsPlugin();
  Future<bool>? _ready;

  @override
  Future<bool> requestPermission() async {
    if (!await _ensureReady()) return false;
    final android = _plugin
        .resolvePlatformSpecificImplementation<
          AndroidFlutterLocalNotificationsPlugin
        >();
    if (android == null) return true;
    return await android.requestNotificationsPermission() ?? false;
  }

  @override
  Future<void> showVisitor({
    required int requestId,
    required String name,
  }) async {
    if (!await _ensureReady()) return;
    final who = name.isEmpty ? 'A known face' : name;
    try {
      await _plugin.show(
        id: _visitorNotificationId,
        title: '$who is at the door',
        body: 'Open the app to let them in or deny',
        notificationDetails: const NotificationDetails(
          android: AndroidNotificationDetails(
            _channelId,
            'Visitor at the door',
            channelDescription: 'Someone waits for your approval',
            importance: Importance.high,
            priority: Priority.high,
            category: AndroidNotificationCategory.call,
          ),
          linux: LinuxNotificationDetails(),
        ),
        payload: '$requestId',
      );
    } catch (error) {
      // No permission or no notification service: the Approve tab still
      // shows the request.
      debugPrint('Visitor notification failed: $error');
    }
  }

  @override
  Future<void> clearVisitor() async {
    if (!await _ensureReady()) return;
    try {
      await _plugin.cancel(id: _visitorNotificationId);
    } catch (error) {
      debugPrint('Clearing the visitor notification failed: $error');
    }
  }

  /// Initialises the plugin once; false where it cannot run.
  Future<bool> _ensureReady() => _ready ??= _initialise();

  Future<bool> _initialise() async {
    try {
      final initialised = await _plugin.initialize(
        settings: const InitializationSettings(
          android: AndroidInitializationSettings('@mipmap/ic_launcher'),
          linux: LinuxInitializationSettings(defaultActionName: 'Open'),
        ),
      );
      return initialised ?? false;
    } catch (error) {
      debugPrint('Notifications not available: $error');
      return false;
    }
  }
}

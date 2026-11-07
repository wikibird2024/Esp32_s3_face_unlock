import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';

/// Loaded once in main() and passed in with an override.
final sharedPreferencesProvider = Provider<SharedPreferences>(
  (ref) => throw UnimplementedError('override sharedPreferencesProvider'),
);

/// Makes a new HTTP client. Each stream gets its own client, so closing the
/// client is how a stream is stopped. Tests swap in a MockClient.
final httpClientFactoryProvider = Provider<http.Client Function()>(
  (ref) => http.Client.new,
);

/// The board's address as the user typed it, for example `192.168.1.50`
/// or `127.0.0.1:8080`. Empty until set in Settings.
final boardAddressProvider = NotifierProvider<BoardAddress, String>(
  BoardAddress.new,
);

class BoardAddress extends Notifier<String> {
  static const _storageKey = 'board_address';

  @override
  String build() =>
      ref.watch(sharedPreferencesProvider).getString(_storageKey) ?? '';

  Future<void> save(String address) async {
    final trimmed = address.trim();
    await ref.read(sharedPreferencesProvider).setString(_storageKey, trimmed);
    state = trimmed;
  }
}

/// The app token the board prints in its boot log (console: `token`).
/// Empty until set in Settings.
final boardTokenProvider = NotifierProvider<BoardToken, String>(
  BoardToken.new,
);

/// shortcut: kept in SharedPreferences (app-private on Android), not secure
/// storage; move it there with phone pairing.
class BoardToken extends Notifier<String> {
  static const _storageKey = 'board_token';

  @override
  String build() =>
      ref.watch(sharedPreferencesProvider).getString(_storageKey) ?? '';

  Future<void> save(String token) async {
    final cleaned = normalizeToken(token);
    await ref.read(sharedPreferencesProvider).setString(_storageKey, cleaned);
    state = cleaned;
  }
}

/// ` ab9a-9jq2 ` → `AB9A9JQ2`: forgives how people type a token off a log.
String normalizeToken(String token) =>
    token.replaceAll(RegExp(r'[\s-]'), '').toUpperCase();

/// Login header for every board request; empty when no token is set.
Map<String, String> boardAuthHeaders(String token) =>
    token.isEmpty ? const {} : {'Authorization': 'Bearer $token'};

/// The video stream has its own server at the API port + 1 (board 80 → 81,
/// fake board 8080 → 8081), so an open stream never blocks the API.
Uri boardStreamUri(Uri baseUri) => baseUri.replace(port: baseUri.port + 1);

/// `192.168.1.50` → `http://192.168.1.50`. Null when empty or not a URL.
Uri? boardBaseUri(String address) {
  final trimmed = address.trim();
  if (trimmed.isEmpty) return null;
  final withScheme = trimmed.contains('://') ? trimmed : 'http://$trimmed';
  final uri = Uri.tryParse(withScheme);
  if (uri == null || uri.host.isEmpty) return null;
  return Uri(
    scheme: uri.scheme,
    host: uri.host,
    port: uri.hasPort ? uri.port : null,
  );
}

import 'package:face_unlock/core/board/board_address.dart';
import 'package:face_unlock/core/mqtt/broker_messages.dart';
import 'package:face_unlock/core/mqtt/broker_settings.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

const _adafruit = BrokerSettings(
  isEnabled: true,
  username: 'hao',
  password: 'aio_key',
  device: 'fu-a1b2c3',
);

void main() {
  test('defaults: Adafruit IO over TLS, switched off', () {
    const settings = BrokerSettings();
    expect(settings.host, adafruitHost);
    expect(settings.port, 8883);
    expect(settings.useTls, isTrue);
    expect(settings.isEnabled, isFalse);
  });

  test('Adafruit topics are feeds, plus the throttle topic', () {
    expect(_adafruit.subscribeTopics, [
      'hao/feeds/fu-a1b2c3-approval-request',
      'hao/feeds/fu-a1b2c3-event',
      'hao/feeds/fu-a1b2c3-status',
      'hao/throttle',
    ]);
    expect(
      _adafruit.topicFor(BrokerTopic.approvalAnswer),
      'hao/feeds/fu-a1b2c3-approval-answer',
    );
  });

  test('a typed prefix wins over the default; no throttle elsewhere', () {
    final settings = _adafruit.copyWith(
      host: 'broker.test',
      topicPrefix: ' home/door/ ',
    );
    expect(settings.topicFor(BrokerTopic.status), 'home/door/status');
    expect(settings.throttleTopic, isNull);
  });

  test('toJson and fromJson round trip; junk keys get defaults', () {
    final settings = _adafruit.copyWith(phoneId: 'p1', phoneKeyHex: 'ab' * 16);
    expect(BrokerSettings.fromJson(settings.toJson()), settings);
    expect(
      BrokerSettings.fromJson({'port': 'x', 'tls': 1, 'host': 5}),
      const BrokerSettings(),
    );
  });

  group('brokerSettingsProblem', () {
    test('complete Adafruit settings have none', () {
      expect(brokerSettingsProblem(_adafruit), isNull);
    });

    test('names the first problem', () {
      expect(
        brokerSettingsProblem(_adafruit.copyWith(host: ' ')),
        'Enter the broker host',
      );
      expect(
        brokerSettingsProblem(_adafruit.copyWith(port: 70000)),
        'Port must be 1 to 65535',
      );
      expect(
        brokerSettingsProblem(_adafruit.copyWith(device: '')),
        'Enter the board id (fu-xxxxxx)',
      );
      expect(
        brokerSettingsProblem(_adafruit.copyWith(device: 'fu a1')),
        'Board id: only a-z, 0-9 and "-"',
      );
      expect(
        brokerSettingsProblem(_adafruit.copyWith(password: '')),
        'Adafruit IO needs the username and the AIO key',
      );
      expect(
        brokerSettingsProblem(_adafruit.copyWith(topicPrefix: 'a/#/')),
        'Topic prefix cannot contain #, + or spaces',
      );
    });

    test('phone key: hex, long enough, with a phone id', () {
      expect(
        brokerSettingsProblem(_adafruit.copyWith(phoneKeyHex: 'zz' * 16)),
        'Phone key must be hex (0-9, a-f)',
      );
      expect(
        brokerSettingsProblem(_adafruit.copyWith(phoneKeyHex: 'ab' * 8)),
        'Phone key needs at least 32 hex digits',
      );
      expect(
        brokerSettingsProblem(_adafruit.copyWith(phoneKeyHex: 'ab' * 16)),
        'Enter the phone id for the key',
      );
    });
  });

  test('the store saves trimmed settings and loads them back', () async {
    SharedPreferences.setMockInitialValues({});
    final preferences = await SharedPreferences.getInstance();
    final container = ProviderContainer(
      overrides: [sharedPreferencesProvider.overrideWithValue(preferences)],
    );
    addTearDown(container.dispose);

    await container
        .read(brokerSettingsProvider.notifier)
        .save(_adafruit.copyWith(device: ' FU-A1B2C3 ', username: ' hao '));

    expect(container.read(brokerSettingsProvider).device, 'fu-a1b2c3');
    final reloaded = ProviderContainer(
      overrides: [sharedPreferencesProvider.overrideWithValue(preferences)],
    );
    addTearDown(reloaded.dispose);
    expect(reloaded.read(brokerSettingsProvider), _adafruit);
  });

  test('broken saved JSON loads the defaults', () async {
    SharedPreferences.setMockInitialValues({'broker_settings': '{oops'});
    final preferences = await SharedPreferences.getInstance();
    final container = ProviderContainer(
      overrides: [sharedPreferencesProvider.overrideWithValue(preferences)],
    );
    addTearDown(container.dispose);
    expect(container.read(brokerSettingsProvider), const BrokerSettings());
  });
}

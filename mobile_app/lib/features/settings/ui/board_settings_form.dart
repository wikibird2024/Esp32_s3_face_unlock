import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/board/board_api.dart';

/// One whole-number setting with the range the firmware accepts.
class NumberField {
  const NumberField(
    this.key,
    this.label,
    this.min,
    this.max, {
    this.step = 1,
    this.unit = '',
    this.divisor = 1,
    this.help = '',
  });

  /// JSON key on the board.
  final String key;
  final String label;
  final int min;
  final int max;
  final int step;

  /// Shown after the value, for example 's' or '%'.
  final String unit;

  /// Shown value = board value / divisor (1000 shows ms as s).
  final int divisor;
  final String help;

  String show(int value) {
    final shown = value / divisor;
    final text = divisor == 1 || shown == shown.roundToDouble()
        ? shown.round().toString()
        : shown.toStringAsFixed(1);
    return unit.isEmpty ? text : '$text $unit';
  }
}

class SwitchField {
  const SwitchField(this.key, this.label);

  final String key;
  final String label;
}

/// Ranges as in components/camera_driver/camera_settings.h.
const cameraFields = [
  NumberField('brightness', 'Brightness', -3, 3),
  NumberField('contrast', 'Contrast', -3, 3),
  NumberField('saturation', 'Colour', -4, 4),
  NumberField('sharpness', 'Sharpness', -3, 3),
  NumberField(
    'exposure',
    'Exposure',
    -5,
    5,
    help: 'Raise in a dark hallway, lower against bright light.',
  ),
  NumberField('denoise', 'Noise filter', 0, 8, help: '0 = off.'),
  NumberField(
    'jpeg_quality',
    'Video quality',
    10,
    100,
    step: 5,
    help: 'Only the live view; higher looks better but is slower.',
  ),
];
const cameraSwitches = [
  SwitchField('vflip', 'Flip upside down'),
  SwitchField('hmirror', 'Mirror left-right'),
];

/// Ranges as in components/system_logic/unlock_rules.h.
const rulesFields = [
  NumberField(
    'min_similarity_percent',
    'Match needed',
    30,
    95,
    unit: '%',
    help: 'Higher: fewer wrong people let in, more retries for you.',
  ),
  NumberField(
    'confirm_frames',
    'Frames to confirm',
    1,
    10,
    help: 'Same result this many frames in a row before acting.',
  ),
  NumberField(
    'unlock_hold_ms',
    'Door open time',
    500,
    60000,
    step: 500,
    unit: 's',
    divisor: 1000,
  ),
  NumberField(
    'cooldown_ms',
    'Pause after a result',
    0,
    60000,
    step: 500,
    unit: 's',
    divisor: 1000,
  ),
  NumberField(
    'enroll_timeout_ms',
    'Time to add a face',
    1000,
    120000,
    step: 1000,
    unit: 's',
    divisor: 1000,
  ),
  NumberField(
    'approval_timeout_ms',
    'Wait for approval',
    0,
    120000,
    step: 5000,
    unit: 's',
    divisor: 1000,
    help: '0 = a known face opens at once. Above 0 = it waits for Open in '
        'the Approve tab.',
  ),
];

/// Loads settings from [path] (`/api/camera` or `/api/rules`), shows them
/// as sliders and switches, and saves them back.
class BoardSettingsForm extends ConsumerStatefulWidget {
  const BoardSettingsForm({
    super.key,
    required this.title,
    required this.path,
    required this.numbers,
    this.switches = const [],
  });

  final String title;
  final String path;
  final List<NumberField> numbers;
  final List<SwitchField> switches;

  @override
  ConsumerState<BoardSettingsForm> createState() => _BoardSettingsFormState();
}

class _BoardSettingsFormState extends ConsumerState<BoardSettingsForm> {
  final Map<String, Object> _values = {};
  bool _isLoaded = false;
  bool _isSaving = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final api = ref.read(boardApiProvider);
    if (api == null) {
      // Called from initState: the first build shows it, no setState.
      _error = 'Set the board address in Settings first';
      return;
    }
    try {
      final json = await api.settings(widget.path);
      if (!mounted) return;
      setState(() {
        _values.clear();
        for (final field in widget.numbers) {
          final value = json[field.key];
          if (value is num) _values[field.key] = value.toInt();
        }
        for (final field in widget.switches) {
          final value = json[field.key];
          if (value is bool) _values[field.key] = value;
        }
        _isLoaded = true;
        _error = null;
      });
    } catch (error) {
      if (!mounted) return;
      setState(() => _error = boardErrorText(error));
    }
  }

  Future<void> _save() async {
    final api = ref.read(boardApiProvider);
    if (api == null) return;
    final messenger = ScaffoldMessenger.of(context);
    setState(() => _isSaving = true);
    try {
      await api.saveSettings(widget.path, Map.of(_values));
      messenger.showSnackBar(const SnackBar(content: Text('Saved on the board')));
    } catch (error) {
      messenger.showSnackBar(SnackBar(content: Text(boardErrorText(error))));
    }
    if (!mounted) return;
    setState(() => _isSaving = false);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text(widget.title)),
      body: _isLoaded
          ? ListView(
              padding: const EdgeInsets.all(16),
              children: [
                for (final field in widget.numbers)
                  if (_values[field.key] is int) _buildSlider(field),
                for (final field in widget.switches)
                  if (_values[field.key] is bool)
                    SwitchListTile(
                      title: Text(field.label),
                      value: _values[field.key]! as bool,
                      onChanged: (value) =>
                          setState(() => _values[field.key] = value),
                    ),
                const SizedBox(height: 16),
                FilledButton(
                  onPressed: _isSaving ? null : _save,
                  child: Text(_isSaving ? 'Saving…' : 'Save on the board'),
                ),
              ],
            )
          : Center(
              child: _error == null
                  ? const CircularProgressIndicator()
                  : Text('🔴 $_error'),
            ),
    );
  }

  Widget _buildSlider(NumberField field) {
    final value = (_values[field.key]! as int).clamp(field.min, field.max);
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(child: Text(field.label)),
              Text(field.show(value)),
            ],
          ),
          Slider(
            value: value.toDouble(),
            min: field.min.toDouble(),
            max: field.max.toDouble(),
            divisions: (field.max - field.min) ~/ field.step,
            label: field.show(value),
            onChanged: (newValue) => setState(
              () => _values[field.key] = snapToStep(field, newValue),
            ),
          ),
          if (field.help.isNotEmpty)
            Text(field.help, style: Theme.of(context).textTheme.bodySmall),
        ],
      ),
    );
  }
}

/// Slider position → a whole board value on the field's step grid.
int snapToStep(NumberField field, double position) {
  final steps = ((position - field.min) / field.step).round();
  return (field.min + steps * field.step).clamp(field.min, field.max);
}

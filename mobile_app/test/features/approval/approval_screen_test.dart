import 'package:face_unlock/core/board/board_address.dart';
import 'package:face_unlock/features/approval/approval_notifier.dart';
import 'package:face_unlock/features/approval/ui/approval_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Shows a fixed state and records the answers.
class _FakeApprovalNotifier extends ApprovalNotifier {
  _FakeApprovalNotifier(this._initial);

  final ApprovalState _initial;
  final answers = <bool>[];

  @override
  ApprovalState build() => _initial;

  @override
  Future<void> answer({required bool approve}) async => answers.add(approve);
}

Future<_FakeApprovalNotifier> _pumpScreen(
  WidgetTester tester,
  ApprovalState state,
) async {
  SharedPreferences.setMockInitialValues({});
  final preferences = await SharedPreferences.getInstance();
  final notifier = _FakeApprovalNotifier(state);
  await tester.pumpWidget(
    ProviderScope(
      overrides: [
        sharedPreferencesProvider.overrideWithValue(preferences),
        approvalProvider.overrideWith(() => notifier),
      ],
      child: const MaterialApp(home: ApprovalScreen()),
    ),
  );
  await tester.pump();
  return notifier;
}

void main() {
  testWidgets('a waiting visitor: Open and Deny call the notifier', (
    tester,
  ) async {
    final notifier = await _pumpScreen(
      tester,
      const ApprovalState(
        ApprovalChannel.broker,
        pending: PendingApproval(requestId: 9, name: 'Hao'),
      ),
    );

    expect(find.text('Hao is at the door'), findsOneWidget);
    await tester.tap(find.text('Open'));
    await tester.pump();
    await tester.tap(find.text('Deny'));
    await tester.pump();

    expect(notifier.answers, [true, false]);
    expect(find.text('Door opening'), findsOneWidget);
  });

  testWidgets('while answering, both buttons are off', (tester) async {
    await _pumpScreen(
      tester,
      const ApprovalState(
        ApprovalChannel.board,
        pending: PendingApproval(requestId: 9, name: ''),
        isAnswering: true,
      ),
    );

    expect(find.text('A known face is at the door'), findsOneWidget);
    final open = tester.widget<ButtonStyleButton>(
      find.ancestor(
        of: find.text('Open'),
        matching: find.bySubtype<ButtonStyleButton>(),
      ),
    );
    expect(open.onPressed, isNull);
  });

  testWidgets('nobody waiting shows the hint and the poll error', (
    tester,
  ) async {
    await _pumpScreen(
      tester,
      const ApprovalState(ApprovalChannel.board, error: 'No answer'),
    );

    expect(find.text('No one is waiting'), findsOneWidget);
    expect(find.text('🔴 No answer'), findsOneWidget);
  });
}

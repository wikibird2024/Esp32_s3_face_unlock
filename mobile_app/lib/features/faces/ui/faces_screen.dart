import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/board/board_api.dart';
import 'enroll_dialog.dart';

/// The board allows names of up to 31 bytes (FACE_DB_NAME_MAX_LEN).
const faceNameMaxBytes = 31;

class FacesScreen extends ConsumerStatefulWidget {
  const FacesScreen({super.key});

  @override
  ConsumerState<FacesScreen> createState() => _FacesScreenState();
}

class _FacesScreenState extends ConsumerState<FacesScreen> {
  List<BoardFace>? _faces;
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
      final faces = await api.faces();
      if (!mounted) return;
      setState(() {
        _faces = faces;
        _error = null;
      });
    } catch (error) {
      if (!mounted) return;
      setState(() => _error = boardErrorText(error));
    }
  }

  Future<void> _addFace() async {
    final api = ref.read(boardApiProvider);
    if (api == null) return;
    final name = await showDialog<String>(
      context: context,
      builder: (_) => const _NameDialog(),
    );
    if (name == null) return;
    if (!mounted) return;
    await showDialog<void>(
      context: context,
      barrierDismissible: false,
      builder: (_) => EnrollDialog(api: api, name: name),
    );
    await _refresh();
  }

  Future<bool> _confirmDelete(BoardFace face) async {
    final api = ref.read(boardApiProvider);
    if (api == null) return false;
    final messenger = ScaffoldMessenger.of(context);
    final isConfirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: Text('Delete ${face.name}?'),
        content: Text(
          'Face #${face.id} can no longer open the door. Other photos of '
          'the same person stay.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Delete'),
          ),
        ],
      ),
    );
    if (isConfirmed != true) return false;
    try {
      await api.deleteFace(face.id);
      messenger.showSnackBar(SnackBar(content: Text('${face.name} deleted')));
      return true;
    } catch (error) {
      messenger.showSnackBar(SnackBar(content: Text(boardErrorText(error))));
      return false;
    }
  }

  @override
  Widget build(BuildContext context) {
    final hasAddress = ref.watch(boardApiProvider) != null;
    return Scaffold(
      appBar: AppBar(
        title: const Text('Faces'),
        actions: [
          IconButton(
            tooltip: 'Refresh',
            onPressed: hasAddress ? _refresh : null,
            icon: const Icon(Icons.refresh),
          ),
        ],
      ),
      floatingActionButton: hasAddress
          ? FloatingActionButton.extended(
              onPressed: _addFace,
              icon: const Icon(Icons.person_add),
              label: const Text('Add face'),
            )
          : null,
      body: hasAddress
          ? RefreshIndicator(onRefresh: _refresh, child: _buildList(context))
          : const Center(child: Text('Set the board address in Settings ⚙')),
    );
  }

  Widget _buildList(BuildContext context) {
    final faces = _faces;
    if (faces == null) {
      return ListView(
        children: [
          const SizedBox(height: 120),
          Center(
            child: _error == null
                ? const CircularProgressIndicator()
                : Text('🔴 $_error'),
          ),
        ],
      );
    }
    if (faces.isEmpty) {
      return ListView(
        children: const [
          SizedBox(height: 120),
          Center(child: Text('No faces yet. Tap "Add face".')),
        ],
      );
    }
    return ListView(
      padding: const EdgeInsets.only(bottom: 96),
      children: [
        const Padding(
          padding: EdgeInsets.fromLTRB(16, 8, 16, 0),
          child: Text(
            'Tip: add 3-5 photos per person (different angles and light). '
            'Swipe left to delete.',
          ),
        ),
        for (final face in faces)
          Dismissible(
            key: ValueKey(face.id),
            direction: DismissDirection.endToStart,
            confirmDismiss: (_) => _confirmDelete(face),
            onDismissed: (_) => setState(
              () => _faces = [
                for (final other in faces)
                  if (other.id != face.id) other,
              ],
            ),
            background: Container(
              color: Theme.of(context).colorScheme.errorContainer,
              alignment: Alignment.centerRight,
              padding: const EdgeInsets.only(right: 24),
              child: const Icon(Icons.delete),
            ),
            child: ListTile(
              leading: CircleAvatar(
                child: Text(
                  face.name.isEmpty ? '?' : face.name[0].toUpperCase(),
                ),
              ),
              title: Text(face.name),
              subtitle: Text('#${face.id}'),
            ),
          ),
      ],
    );
  }
}

/// Asks for the name; returns it trimmed, or null when cancelled.
class _NameDialog extends StatefulWidget {
  const _NameDialog();

  @override
  State<_NameDialog> createState() => _NameDialogState();
}

class _NameDialogState extends State<_NameDialog> {
  final _nameField = TextEditingController();
  String? _problem;

  @override
  void dispose() {
    _nameField.dispose();
    super.dispose();
  }

  void _submit() {
    final name = _nameField.text.trim();
    final problem = nameProblem(name);
    if (problem != null) {
      setState(() => _problem = problem);
      return;
    }
    Navigator.of(context).pop(name);
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('Add face'),
      content: TextField(
        controller: _nameField,
        autofocus: true,
        decoration: InputDecoration(
          labelText: 'Name',
          hintText: 'Hao',
          errorText: _problem,
        ),
        textCapitalization: TextCapitalization.words,
        onSubmitted: (_) => _submit(),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Cancel'),
        ),
        FilledButton(onPressed: _submit, child: const Text('Next')),
      ],
    );
  }
}

/// Why the board would refuse [name], or null when it is fine.
String? nameProblem(String name) {
  if (name.isEmpty) return 'Type a name';
  if (_utf8Length(name) > faceNameMaxBytes) return 'Too long';
  return null;
}

/// Bytes the name takes on the board (accented letters take 2-3).
int _utf8Length(String text) {
  var length = 0;
  for (final rune in text.runes) {
    length += rune < 0x80
        ? 1
        : rune < 0x800
        ? 2
        : rune < 0x10000
        ? 3
        : 4;
  }
  return length;
}

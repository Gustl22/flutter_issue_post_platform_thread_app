import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

/// Channel implemented in windows/runner/flutter_window.cpp.
const MethodChannel _channel = MethodChannel('post_platform_thread');

void main() {
  runApp(const MaterialApp(home: PostTaskPage()));
}

class PostTaskPage extends StatefulWidget {
  const PostTaskPage({super.key});

  @override
  State<PostTaskPage> createState() => _PostTaskPageState();
}

class _PostTaskPageState extends State<PostTaskPage> {
  String _status = 'Press the button to post a task to the platform thread.';

  @override
  void initState() {
    super.initState();
    // Post once on startup so the bug reproduces without any interaction.
    _postTask();
  }

  Future<void> _postTask() async {
    setState(() => _status = 'Posting task...');
    String status;
    try {
      // The runner posts a task via FlutterEngine::PostPlatformThreadTask and
      // replies from inside that task.
      status = await _channel.invokeMethod<String>('postTask') ?? 'null';
    } on PlatformException catch (e) {
      status = 'PlatformException: ${e.code} ${e.message}';
    } on MissingPluginException catch (e) {
      status = 'MissingPluginException: ${e.message}';
    }
    if (mounted) {
      setState(() => _status = status);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('PostPlatformThreadTask repro')),
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: <Widget>[
            Text(_status),
            const SizedBox(height: 16),
            FilledButton(
              onPressed: _postTask,
              child: const Text('Post task'),
            ),
          ],
        ),
      ),
    );
  }
}

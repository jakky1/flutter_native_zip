part of 'native_zip.dart';

class _DartNotifyMessage {
  final int taskId;
  final DartNotifyAction action;
  final int errCode;
  String? errMsg;

  _DartNotifyMessage._(NativeNotifyMessage msg)
      : taskId = msg.taskId,
        action = DartNotifyAction.fromValue(msg.action),
        errCode = msg.errCode,
        errMsg = msg.errMsg == nullptr ? null : msg.errMsg.toDartString();
}

void _taskNotifyIsolateFunc(SendPort sendPort) {
  // run in isolate
  while (true) {
    var msg = _bindings.getDartMessage();
    if (msg == nullptr) {
      //log("isolate ticks");
      continue;
    }

    if (msg.ref.action == DartNotifyAction.TASK_LOG.value) {
      log(msg.ref.errMsg.toDartString());
    } else {
      sendPort.send(_DartNotifyMessage._(msg.ref));
    }
  }
}

bool _isIsolateInited = false;
Future<void> _startTaskNotifyIsolate() async {
  if (_isIsolateInited) return;
  _isIsolateInited = true;

  final receivePort = ReceivePort();
  await Isolate.spawn(_taskNotifyIsolateFunc, receivePort.sendPort);

  receivePort.listen((msg) {
    if (msg is _DartNotifyMessage) {
      var completer = _taskNotifyMap.remove(msg.taskId);
      if (completer == null) {
        log("[NativeZip] task id not found: ${msg.taskId} , ignored");
        return;
      }

      switch (msg.action) {
        case DartNotifyAction.TASK_FINISH:
          completer.complete();
          break;
        case DartNotifyAction.TASK_ERROR:
          Future.delayed(Duration.zero, () {
            completer.completeError(_getExceptionByErrorCode(msg.errCode));
          });
          break;
        case DartNotifyAction.TASK_WARNING:
          // TODO: ...
          break;
        case DartNotifyAction.TASK_LOG:
          break;
      }
    }
  });
}

final _taskNotifyMap = HashMap<int, Completer<void>>();
void _registerTask(int taskId, Completer<void> completer) {
  _startTaskNotifyIsolate();
  _taskNotifyMap[taskId] = completer;
}

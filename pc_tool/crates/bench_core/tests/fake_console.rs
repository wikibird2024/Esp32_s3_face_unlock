//! End-to-end check against the fake board's serial console. Skipped unless
//! the pty link exists:
//!   python3 mobile_app/tool/fake_board.py --port 8080 --pty-link /tmp/face_unlock_tty --no-events
//!   FAKE_CONSOLE_TTY=/tmp/face_unlock_tty cargo test -p bench_core --test fake_console -- --nocapture

use bench_core::events::RecordingSink;
use bench_core::serial::SerialManager;
use board_protocol::parse_status_reply;
use std::sync::Arc;
use std::time::{Duration, Instant};

fn pty_path() -> Option<String> {
    let path =
        std::env::var("FAKE_CONSOLE_TTY").unwrap_or_else(|_| "/tmp/face_unlock_tty".to_string());
    std::path::Path::new(&path).exists().then_some(path)
}

async fn wait_for_event(
    sink: &RecordingSink,
    name: &str,
    timeout: Duration,
) -> Option<serde_json::Value> {
    let deadline = Instant::now() + timeout;
    while Instant::now() < deadline {
        if let Some((_, payload)) = sink
            .events
            .lock()
            .unwrap()
            .iter()
            .find(|(event, _)| event == name)
        {
            return Some(payload.clone());
        }
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
    None
}

#[tokio::test]
async fn talks_to_the_fake_console() {
    let Some(path) = pty_path() else {
        eprintln!("fake console pty not found, skipping");
        return;
    };
    let sink = Arc::new(RecordingSink::default());
    let manager = SerialManager::new(sink.clone());
    manager.connect(&path, 115200).expect("open pty");
    // Let the banner (if any) go by, then ask.
    tokio::time::sleep(Duration::from_millis(300)).await;

    let reply = manager
        .run_command("status json", 3000)
        .await
        .expect("status json");
    assert!(
        !reply.timed_out,
        "no prompt after status json: {:?}",
        reply.lines
    );
    let status_line = reply
        .lines
        .iter()
        .find(|line| line.starts_with("@status "))
        .expect("@status line");
    let status = parse_status_reply(status_line).expect("status parses");
    assert!(!status.version.app.is_empty());

    let reply = manager
        .run_command("list json", 3000)
        .await
        .expect("list json");
    assert!(
        reply.lines.iter().any(|line| line.starts_with("@list ")),
        "{:?}",
        reply.lines
    );

    let reply = manager
        .run_command("nonsense", 3000)
        .await
        .expect("bad command");
    assert!(reply.is_error, "{:?}", reply.lines);

    manager
        .run_command("stats 200", 3000)
        .await
        .expect("stats on");
    assert!(
        wait_for_event(&sink, "telemetry:sample", Duration::from_secs(3))
            .await
            .is_some(),
        "no telemetry line"
    );
    manager
        .run_command("stats 0", 3000)
        .await
        .expect("stats off");

    manager.send_text("panic").expect("send panic");
    let panic = wait_for_event(&sink, "board:panic", Duration::from_secs(5))
        .await
        .expect("panic block");
    assert!(panic["frames"]
        .as_array()
        .map(|frames| !frames.is_empty())
        .unwrap_or(false));
    assert!(
        wait_for_event(&sink, "board:boot", Duration::from_secs(5))
            .await
            .is_some(),
        "no reboot banner"
    );

    manager.disconnect();
    let names: Vec<String> = sink
        .events
        .lock()
        .unwrap()
        .iter()
        .map(|(name, _)| name.clone())
        .collect();
    assert!(names.contains(&"serial:lines".to_string()));
    assert!(names.contains(&"board:status".to_string()));
}

#[tokio::test]
async fn streams_from_the_fake_board_http() {
    let base_url =
        std::env::var("FAKE_BOARD_URL").unwrap_or_else(|_| "http://127.0.0.1:8080".to_string());
    if std::net::TcpStream::connect_timeout(
        &base_url.trim_start_matches("http://").parse().unwrap(),
        Duration::from_millis(300),
    )
    .is_err()
    {
        eprintln!("fake board HTTP not reachable, skipping");
        return;
    }
    let sink = Arc::new(RecordingSink::default());
    let relay = bench_core::camera::CameraRelay::new(sink.clone());
    relay.start(&base_url);
    let deadline = Instant::now() + Duration::from_secs(5);
    while relay.latest_frame().sequence < 3 && Instant::now() < deadline {
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
    let frame = relay.latest_frame();
    relay.stop();
    assert!(frame.sequence >= 3, "only {} frames", frame.sequence);
    assert_eq!(&frame.bytes[..2], &[0xff, 0xd8], "not a JPEG");
}

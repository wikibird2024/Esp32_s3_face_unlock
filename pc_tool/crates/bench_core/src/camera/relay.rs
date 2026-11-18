//! One HTTP connection to `http://<ip>/stream`, the newest JPEG kept in
//! memory. The board serves a single client, so nothing else in the tool
//! may open a second request while the relay runs.

use crate::events::{
    self, CameraConnectionState, CameraFramePayload, CameraStatePayload, SharedSink,
};
use board_protocol::{boundary_from_content_type, MjpegParser, DEFAULT_BOUNDARY};
use futures_util::StreamExt;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

const CONNECT_TIMEOUT_MS: u64 = 5000;
const READ_TIMEOUT_MS: u64 = 5000;
const BACKOFF_STEPS_MS: [u64; 6] = [1000, 2000, 4000, 8000, 16000, 30000];

#[derive(Debug, Default, Clone)]
pub struct LatestFrame {
    pub sequence: u64,
    pub bytes: Vec<u8>,
}

struct RunningStream {
    stop: Arc<AtomicBool>,
    handle: tokio::task::JoinHandle<()>,
}

pub struct CameraRelay {
    sink: SharedSink,
    latest: Arc<Mutex<LatestFrame>>,
    running: Mutex<Option<RunningStream>>,
}

impl CameraRelay {
    pub fn new(sink: SharedSink) -> Self {
        Self {
            sink,
            latest: Arc::new(Mutex::new(LatestFrame::default())),
            running: Mutex::new(None),
        }
    }

    pub fn latest_frame(&self) -> LatestFrame {
        self.latest.lock().expect("frame lock").clone()
    }

    /// Start streaming from `base_url` (e.g. `http://192.168.1.50` or
    /// `http://127.0.0.1:8080`). Must be called inside a tokio runtime.
    pub fn start(&self, base_url: &str) {
        self.stop();
        let stop = Arc::new(AtomicBool::new(false));
        let handle = tokio::spawn(run_stream(
            base_url.trim_end_matches('/').to_string(),
            Arc::clone(&self.sink),
            Arc::clone(&self.latest),
            Arc::clone(&stop),
        ));
        *self.running.lock().expect("running lock") = Some(RunningStream { stop, handle });
    }

    pub fn stop(&self) {
        if let Some(running) = self.running.lock().expect("running lock").take() {
            running.stop.store(true, Ordering::SeqCst);
            running.handle.abort();
            events::emit(
                &self.sink,
                events::CAMERA_STATE,
                &CameraStatePayload {
                    state: CameraConnectionState::Stopped,
                    board_ip: String::new(),
                    error_text: None,
                },
            );
        }
    }

    pub fn is_running(&self) -> bool {
        self.running.lock().expect("running lock").is_some()
    }
}

async fn run_stream(
    base_url: String,
    sink: SharedSink,
    latest: Arc<Mutex<LatestFrame>>,
    stop: Arc<AtomicBool>,
) {
    let mut backoff_index = 0usize;
    let emit_state = |state: CameraConnectionState, error_text: Option<String>| {
        events::emit(
            &sink,
            events::CAMERA_STATE,
            &CameraStatePayload {
                state,
                board_ip: base_url.clone(),
                error_text,
            },
        );
    };
    while !stop.load(Ordering::SeqCst) {
        emit_state(CameraConnectionState::Connecting, None);
        match stream_once(&base_url, &sink, &latest, &stop, &emit_state).await {
            Ok(()) => {
                backoff_index = 0;
                if stop.load(Ordering::SeqCst) {
                    return;
                }
                emit_state(CameraConnectionState::Offline, Some("stream ended".into()));
            }
            Err(error) => {
                if stop.load(Ordering::SeqCst) {
                    return;
                }
                emit_state(CameraConnectionState::Offline, Some(error));
            }
        }
        let delay = BACKOFF_STEPS_MS[backoff_index.min(BACKOFF_STEPS_MS.len() - 1)];
        backoff_index += 1;
        tokio::time::sleep(Duration::from_millis(delay)).await;
    }
}

async fn stream_once(
    base_url: &str,
    sink: &SharedSink,
    latest: &Arc<Mutex<LatestFrame>>,
    stop: &Arc<AtomicBool>,
    emit_state: &(dyn Fn(CameraConnectionState, Option<String>) + Sync),
) -> Result<(), String> {
    let client = reqwest::Client::builder()
        .connect_timeout(Duration::from_millis(CONNECT_TIMEOUT_MS))
        .read_timeout(Duration::from_millis(READ_TIMEOUT_MS))
        .build()
        .map_err(|error| error.to_string())?;
    let response = client
        .get(format!("{base_url}/stream"))
        .send()
        .await
        .map_err(|error| error.to_string())?;
    if !response.status().is_success() {
        return Err(format!("HTTP {}", response.status()));
    }
    let boundary = response
        .headers()
        .get(reqwest::header::CONTENT_TYPE)
        .and_then(|value| value.to_str().ok())
        .and_then(boundary_from_content_type)
        .unwrap_or_else(|| DEFAULT_BOUNDARY.to_string());
    let mut parser = MjpegParser::new(&boundary);
    let mut body = response.bytes_stream();
    let mut fps_window_start = Instant::now();
    let mut fps_window_count = 0u32;
    let mut received_fps = 0.0;
    let mut announced = false;
    while let Some(chunk) = body.next().await {
        if stop.load(Ordering::SeqCst) {
            return Ok(());
        }
        let chunk = chunk.map_err(|error| error.to_string())?;
        for frame in parser.push(&chunk) {
            if !announced {
                announced = true;
                emit_state(CameraConnectionState::Streaming, None);
            }
            fps_window_count += 1;
            if fps_window_start.elapsed() >= Duration::from_secs(1) {
                received_fps = fps_window_count as f64 / fps_window_start.elapsed().as_secs_f64();
                fps_window_start = Instant::now();
                fps_window_count = 0;
            }
            let sequence = {
                let mut guard = latest.lock().expect("frame lock");
                guard.sequence += 1;
                guard.bytes = frame;
                guard.sequence
            };
            let byte_count = latest.lock().expect("frame lock").bytes.len();
            events::emit(
                sink,
                events::CAMERA_FRAME,
                &CameraFramePayload {
                    sequence,
                    byte_count,
                    received_fps,
                },
            );
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::events::RecordingSink;
    use std::io::Write;
    use std::net::TcpListener;

    /// A tiny MJPEG server that sends two frames then closes.
    fn serve_two_frames() -> String {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        std::thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            let mut request = [0u8; 1024];
            let _ = std::io::Read::read(&mut socket, &mut request);
            let mut body = b"HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace;boundary=frameboundary\r\n\r\n".to_vec();
            for seed in [1u8, 2] {
                let frame = vec![0xff, 0xd8, seed, seed, 0xff, 0xd9];
                body.extend_from_slice(
                    format!("\r\n--frameboundary\r\nContent-Type: image/jpeg\r\nContent-Length: {}\r\n\r\n", frame.len())
                        .as_bytes(),
                );
                body.extend_from_slice(&frame);
            }
            socket.write_all(&body).unwrap();
            socket.flush().unwrap();
        });
        format!("http://{address}")
    }

    #[tokio::test]
    async fn relay_receives_frames_from_a_fake_server() {
        let sink = Arc::new(RecordingSink::default());
        let relay = CameraRelay::new(sink.clone());
        relay.start(&serve_two_frames());
        let deadline = Instant::now() + Duration::from_secs(5);
        while relay.latest_frame().sequence < 2 && Instant::now() < deadline {
            tokio::time::sleep(Duration::from_millis(20)).await;
        }
        let latest = relay.latest_frame();
        assert_eq!(latest.sequence, 2);
        assert_eq!(latest.bytes, vec![0xff, 0xd8, 2, 2, 0xff, 0xd9]);
        relay.stop();
        assert!(!relay.is_running());
        let names: Vec<String> = sink
            .events
            .lock()
            .unwrap()
            .iter()
            .map(|(name, _)| name.clone())
            .collect();
        assert!(names.contains(&"camera:frame".to_string()));
        assert!(names.contains(&"camera:state".to_string()));
    }
}

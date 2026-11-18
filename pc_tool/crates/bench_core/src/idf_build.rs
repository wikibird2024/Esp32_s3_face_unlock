//! Runs `idf.py build` in the repo and streams its output.

use crate::events::{self, BuildDonePayload, BuildOutputPayload, SharedSink};
use std::process::Stdio;
use std::sync::{Arc, Mutex};
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::{Child, Command};

#[derive(Debug, thiserror::Error)]
pub enum BuildError {
    #[error("a build is already running")]
    AlreadyRunning,
    #[error("{0}")]
    Io(#[from] std::io::Error),
}

#[derive(Default)]
pub struct BuildRunner {
    child: Arc<Mutex<Option<Child>>>,
}

impl BuildRunner {
    pub fn is_running(&self) -> bool {
        self.child.lock().expect("child lock").is_some()
    }

    /// Spawn `bash -lc '. <idf>/export.sh && idf.py <args>'`; lines go out
    /// as `build:output`, the exit code as `build:done`.
    pub async fn start(
        &self,
        repo_path: &str,
        idf_path: &str,
        extra_args: &[String],
        sink: SharedSink,
    ) -> Result<(), BuildError> {
        if self.is_running() {
            return Err(BuildError::AlreadyRunning);
        }
        let script = format!(
            ". \"{}/export.sh\" >/dev/null 2>&1 && idf.py {}",
            idf_path.trim_end_matches('/'),
            if extra_args.is_empty() {
                "build".to_string()
            } else {
                extra_args.join(" ")
            }
        );
        let mut child = Command::new("bash")
            .arg("-lc")
            .arg(&script)
            .current_dir(repo_path)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .kill_on_drop(true)
            .spawn()?;
        let stdout = child.stdout.take().expect("piped stdout");
        let stderr = child.stderr.take().expect("piped stderr");
        *self.child.lock().expect("child lock") = Some(child);
        let child_slot = Arc::clone(&self.child);

        let sink_out = sink.clone();
        let out_task = tokio::spawn(async move {
            let mut lines = BufReader::new(stdout).lines();
            while let Ok(Some(line)) = lines.next_line().await {
                events::emit(
                    &sink_out,
                    events::BUILD_OUTPUT,
                    &BuildOutputPayload { line },
                );
            }
        });
        let sink_err = sink.clone();
        let err_task = tokio::spawn(async move {
            let mut lines = BufReader::new(stderr).lines();
            while let Ok(Some(line)) = lines.next_line().await {
                events::emit(
                    &sink_err,
                    events::BUILD_OUTPUT,
                    &BuildOutputPayload { line },
                );
            }
        });
        tokio::spawn(async move {
            let _ = out_task.await;
            let _ = err_task.await;
            let exit_code = {
                let child = child_slot.lock().expect("child lock").take();
                match child {
                    Some(mut child) => child
                        .wait()
                        .await
                        .ok()
                        .and_then(|status| status.code())
                        .unwrap_or(-1),
                    None => -1,
                }
            };
            events::emit(&sink, events::BUILD_DONE, &BuildDonePayload { exit_code });
        });
        Ok(())
    }

    pub async fn stop(&self) {
        let child = self.child.lock().expect("child lock").take();
        if let Some(mut child) = child {
            let _ = child.kill().await;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::events::RecordingSink;

    #[tokio::test]
    async fn streams_output_and_exit_code() {
        let sink = Arc::new(RecordingSink::default());
        let runner = BuildRunner::default();
        let temp = std::env::temp_dir().join(format!("bench-core-build-{}", std::process::id()));
        std::fs::create_dir_all(&temp).unwrap();
        std::fs::write(
            temp.join("export.sh"),
            "idf.py() { echo building \"$@\"; return 3; }\nexport -f idf.py\n",
        )
        .unwrap();
        runner
            .start(
                temp.to_str().unwrap(),
                temp.to_str().unwrap(),
                &["build".into()],
                sink.clone(),
            )
            .await
            .unwrap();
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(10);
        loop {
            let done = sink
                .events
                .lock()
                .unwrap()
                .iter()
                .any(|(name, _)| name == "build:done");
            if done || std::time::Instant::now() > deadline {
                break;
            }
            tokio::time::sleep(std::time::Duration::from_millis(20)).await;
        }
        let events = sink.events.lock().unwrap();
        let done = events
            .iter()
            .find(|(name, _)| name == "build:done")
            .expect("build:done");
        assert_eq!(done.1["exitCode"], 3);
        assert!(events
            .iter()
            .any(|(name, payload)| name == "build:output" && payload["line"] == "building build"));
        let _ = std::fs::remove_dir_all(&temp);
    }
}

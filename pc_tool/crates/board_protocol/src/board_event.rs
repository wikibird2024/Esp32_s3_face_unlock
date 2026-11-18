//! Face / door events and boot notices taken from log lines.
//!
//! Every `APP_EVENTS` post is logged by `net_comm` as
//! `publish face_unlock/<event> {"id":4,"name":"alice","similarity":0.712,"request":0}`.
//! A few human lines from `system_logic` carry extra detail (rejection
//! similarity, approval ids) and are parsed too.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum BoardEventKind {
    DoorOpened,
    DoorClosed,
    FaceRejected,
    EnrollStarted,
    FaceEnrolled,
    EnrollFailed,
    ApprovalRequested,
    ApprovalDenied,
    ApprovalTimeout,
}

impl BoardEventKind {
    pub fn from_topic_name(name: &str) -> Option<Self> {
        Some(match name {
            "door_opened" => Self::DoorOpened,
            "door_closed" => Self::DoorClosed,
            "face_rejected" => Self::FaceRejected,
            "enroll_started" => Self::EnrollStarted,
            "face_enrolled" => Self::FaceEnrolled,
            "enroll_failed" => Self::EnrollFailed,
            "approval_requested" => Self::ApprovalRequested,
            "approval_denied" => Self::ApprovalDenied,
            "approval_timeout" => Self::ApprovalTimeout,
            _ => return None,
        })
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BoardEvent {
    pub kind: BoardEventKind,
    pub face_id: u32,
    pub name: String,
    pub similarity: f64,
    pub request_id: u64,
}

#[derive(Debug, Clone, Deserialize)]
struct PublishPayload {
    #[serde(default)]
    id: u32,
    #[serde(default)]
    name: String,
    #[serde(default)]
    similarity: f64,
    #[serde(default)]
    request: u64,
}

/// Parse the message part of a `net_comm` log line.
pub fn parse_publish_message(message: &str) -> Option<BoardEvent> {
    let rest = message.strip_prefix("publish face_unlock/")?;
    let (topic, payload_text) = match rest.split_once(' ') {
        Some(pair) => pair,
        None => (rest, "{}"),
    };
    let kind = BoardEventKind::from_topic_name(topic.trim())?;
    let payload: PublishPayload =
        serde_json::from_str(payload_text.trim()).unwrap_or(PublishPayload {
            id: 0,
            name: String::new(),
            similarity: 0.0,
            request: 0,
        });
    Some(BoardEvent {
        kind,
        face_id: payload.id,
        name: payload.name,
        similarity: payload.similarity,
        request_id: payload.request,
    })
}

/// Parse the human result lines from `system_logic` that add detail the
/// publish line lacks (rejection similarity, approval wait).
pub fn parse_system_logic_message(message: &str) -> Option<BoardEvent> {
    if let Some(rest) = message.strip_prefix("REJECT unknown face (best similarity ") {
        let similarity: f64 = rest.trim_end_matches(')').parse().ok()?;
        return Some(BoardEvent {
            kind: BoardEventKind::FaceRejected,
            face_id: 0,
            name: String::new(),
            similarity,
            request_id: 0,
        });
    }
    if let Some(rest) = message.strip_prefix("WAIT for approval #") {
        // WAIT for approval #123 for 'alice' (similarity 0.71): approve 123 / deny 123
        let (request_text, rest) = rest.split_once(" for '")?;
        let request_id: u64 = request_text.parse().ok()?;
        let (name, rest) = rest.split_once("' (similarity ")?;
        let similarity: f64 = rest.split(')').next()?.parse().ok()?;
        return Some(BoardEvent {
            kind: BoardEventKind::ApprovalRequested,
            face_id: 0,
            name: name.to_string(),
            similarity,
            request_id,
        });
    }
    if let Some(rest) = message.strip_prefix("OPEN for '") {
        // OPEN for 'alice' (id 4, similarity 0.71)
        let (name, rest) = rest.split_once("' (id ")?;
        let (id_text, rest) = rest.split_once(", similarity ")?;
        let face_id: u32 = id_text.parse().ok()?;
        let similarity: f64 = rest.trim_end_matches(')').parse().ok()?;
        return Some(BoardEvent {
            kind: BoardEventKind::DoorOpened,
            face_id,
            name: name.to_string(),
            similarity,
            request_id: 0,
        });
    }
    None
}

/// Lines about the board itself rather than faces.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", tag = "kind")]
pub enum BoardNotice {
    /// `app_main: Face unlock <version> (IDF <idf>)`: the board just booted.
    Boot {
        version: String,
        idf_version: String,
    },
    /// `net_comm: Wi-Fi connected, IP 192.168.1.50` or the `live view` line.
    Ip { ip: String },
    /// `app_main: Ready. Type 'help' in the console.`
    Ready,
}

pub fn parse_board_notice(tag: &str, message: &str) -> Option<BoardNotice> {
    if tag == "app_main" {
        if let Some(rest) = message.strip_prefix("Face unlock ") {
            let (version, rest) = rest.split_once(" (IDF ")?;
            let idf_version = rest.trim_end_matches(')').to_string();
            return Some(BoardNotice::Boot {
                version: version.to_string(),
                idf_version,
            });
        }
        if message.starts_with("Ready.") {
            return Some(BoardNotice::Ready);
        }
    }
    if let Some(ip) = message.strip_prefix("Wi-Fi connected, IP ") {
        return Some(BoardNotice::Ip {
            ip: ip.trim().to_string(),
        });
    }
    if let Some(rest) = message.strip_prefix("Live view on http://") {
        let ip = rest.split('/').next()?.trim();
        if !ip.is_empty() && ip != "<ip>" {
            return Some(BoardNotice::Ip { ip: ip.to_string() });
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_publish_with_payload() {
        let event = parse_publish_message(
            r#"publish face_unlock/door_opened {"id":4,"name":"alice","similarity":0.712,"request":0}"#,
        )
        .unwrap();
        assert_eq!(event.kind, BoardEventKind::DoorOpened);
        assert_eq!(event.face_id, 4);
        assert_eq!(event.name, "alice");
        assert_eq!(event.similarity, 0.712);
    }

    #[test]
    fn parses_publish_with_empty_payload_and_unknown_topic() {
        let event = parse_publish_message("publish face_unlock/door_closed {}").unwrap();
        assert_eq!(event.kind, BoardEventKind::DoorClosed);
        assert_eq!(event.name, "");
        assert!(parse_publish_message("publish face_unlock/nope {}").is_none());
        assert!(parse_publish_message("Wi-Fi connected, IP 1.2.3.4").is_none());
    }

    #[test]
    fn parses_human_lines() {
        let reject =
            parse_system_logic_message("REJECT unknown face (best similarity 0.40)").unwrap();
        assert_eq!(reject.kind, BoardEventKind::FaceRejected);
        assert_eq!(reject.similarity, 0.40);

        let wait = parse_system_logic_message(
            "WAIT for approval #123 for 'alice' (similarity 0.71): approve 123 / deny 123",
        )
        .unwrap();
        assert_eq!(wait.kind, BoardEventKind::ApprovalRequested);
        assert_eq!(wait.request_id, 123);
        assert_eq!(wait.name, "alice");

        let open = parse_system_logic_message("OPEN for 'manual' (id 0, similarity 0.00)").unwrap();
        assert_eq!(open.kind, BoardEventKind::DoorOpened);
        assert_eq!(open.name, "manual");
        assert!(parse_system_logic_message("Locked").is_none());
    }

    #[test]
    fn parses_notices() {
        assert_eq!(
            parse_board_notice("app_main", "Face unlock fcb63ff-dirty (IDF v5.5.1)"),
            Some(BoardNotice::Boot {
                version: "fcb63ff-dirty".into(),
                idf_version: "v5.5.1".into()
            })
        );
        assert_eq!(
            parse_board_notice("net_comm", "Wi-Fi connected, IP 192.168.1.50"),
            Some(BoardNotice::Ip {
                ip: "192.168.1.50".into()
            })
        );
        assert_eq!(
            parse_board_notice("video_stream", "Live view on http://10.0.0.7/ (port 80)"),
            Some(BoardNotice::Ip {
                ip: "10.0.0.7".into()
            })
        );
        assert_eq!(
            parse_board_notice("app_main", "Ready. Type 'help' in the console."),
            Some(BoardNotice::Ready)
        );
        assert_eq!(parse_board_notice("app_main", "Lock on GPIO 2"), None);
    }
}

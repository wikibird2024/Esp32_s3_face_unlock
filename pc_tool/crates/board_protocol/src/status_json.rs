//! `@status {...}` and `@list [...]` replies (docs/console_protocol.md §3-4).
//! Deserialized from the board's snake_case keys, serialized to the frontend
//! as camelCase.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all(deserialize = "snake_case", serialize = "camelCase"))]
pub struct LastMatch {
    pub id: u32,
    pub similarity: f64,
    pub threshold: f64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all(deserialize = "snake_case", serialize = "camelCase"))]
pub struct Approval {
    pub request_id: u64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all(deserialize = "snake_case", serialize = "camelCase"))]
pub struct WifiStatus {
    pub enabled: bool,
    pub connected: bool,
    pub ip: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all(deserialize = "snake_case", serialize = "camelCase"))]
pub struct FacesStatus {
    pub count: u32,
    pub capacity: u32,
    pub skipped: u32,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all(deserialize = "snake_case", serialize = "camelCase"))]
pub struct HeapStatus {
    pub internal: u64,
    pub psram: u64,
    #[serde(default)]
    pub internal_min: u64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all(deserialize = "snake_case", serialize = "camelCase"))]
pub struct VersionStatus {
    pub app: String,
    pub idf: String,
    #[serde(default)]
    pub compile_date: String,
    #[serde(default)]
    pub compile_time: String,
    #[serde(default)]
    pub elf_sha256_short: String,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all(deserialize = "snake_case", serialize = "camelCase"))]
pub struct BoardStatus {
    pub state: String,
    pub state_id: u32,
    pub frames: u64,
    pub frames_with_face: u64,
    pub inference_ms: u64,
    pub last_match: LastMatch,
    pub approval: Option<Approval>,
    pub door: String,
    pub wifi: WifiStatus,
    pub faces: Option<FacesStatus>,
    pub heap: HeapStatus,
    pub version: VersionStatus,
    #[serde(default)]
    pub uptime_ms: u64,
}

impl BoardStatus {
    pub fn is_door_open(&self) -> bool {
        self.door == "open"
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all(deserialize = "snake_case", serialize = "camelCase"))]
pub struct FaceEntry {
    pub id: u32,
    pub name: String,
}

/// Parse a `@status {...}` line (the marker is optional).
pub fn parse_status_reply(line: &str) -> Result<BoardStatus, serde_json::Error> {
    let json = line.trim().strip_prefix("@status").unwrap_or(line).trim();
    serde_json::from_str(json)
}

/// Parse a `@list [...]` line (the marker is optional).
pub fn parse_list_reply(line: &str) -> Result<Vec<FaceEntry>, serde_json::Error> {
    let json = line.trim().strip_prefix("@list").unwrap_or(line).trim();
    serde_json::from_str(json)
}

#[cfg(test)]
mod tests {
    use super::*;

    const STATUS: &str = r#"@status {"state":"idle","state_id":0,"frames":1234,"frames_with_face":56,"inference_ms":85,"last_match":{"id":1,"similarity":0.712,"threshold":0.55},"approval":null,"door":"locked","wifi":{"enabled":true,"connected":true,"ip":"192.168.1.20"},"faces":{"count":2,"capacity":20,"skipped":0},"heap":{"internal":123456,"psram":7000000,"internal_min":110000},"version":{"app":"fcb63ff-dirty","idf":"v5.5.1","compile_date":"Nov  4 2026","compile_time":"16:42:10","elf_sha256_short":"1a2b3c4d"},"uptime_ms":123456}"#;

    #[test]
    fn parses_contract_example() {
        let status = parse_status_reply(STATUS).unwrap();
        assert_eq!(status.state, "idle");
        assert_eq!(status.last_match.threshold, 0.55);
        assert!(status.approval.is_none());
        assert!(!status.is_door_open());
        assert_eq!(status.wifi.ip.as_deref(), Some("192.168.1.20"));
        assert_eq!(status.faces.as_ref().unwrap().capacity, 20);
        assert_eq!(status.version.elf_sha256_short, "1a2b3c4d");
        assert_eq!(status.uptime_ms, 123456);
    }

    #[test]
    fn serializes_camel_case_for_frontend() {
        let status = parse_status_reply(STATUS).unwrap();
        let json = serde_json::to_string(&status).unwrap();
        assert!(json.contains("\"stateId\":0"));
        assert!(json.contains("\"elfSha256Short\""));
        assert!(!json.contains("state_id"));
    }

    #[test]
    fn parses_waiting_and_nulls() {
        let line = r#"{"state":"waiting approval","state_id":5,"frames":1,"frames_with_face":1,"inference_ms":90,"last_match":{"id":3,"similarity":0.6,"threshold":0.55},"approval":{"request_id":99},"door":"open","wifi":{"enabled":false,"connected":false,"ip":null},"faces":null,"heap":{"internal":1,"psram":2},"version":{"app":"v0.1.0","idf":"v5.5.1"}}"#;
        let status = parse_status_reply(line).unwrap();
        assert_eq!(status.approval.as_ref().unwrap().request_id, 99);
        assert!(status.is_door_open());
        assert!(status.faces.is_none());
        assert!(status.wifi.ip.is_none());
        assert_eq!(status.heap.internal_min, 0);
    }

    #[test]
    fn parses_list() {
        let faces =
            parse_list_reply(r#"@list [{"id":1,"name":"alice"},{"id":2,"name":"bob"}]"#).unwrap();
        assert_eq!(faces.len(), 2);
        assert_eq!(faces[1].name, "bob");
        assert!(parse_list_reply("@list []").unwrap().is_empty());
        assert!(parse_list_reply("@list nope").is_err());
    }
}

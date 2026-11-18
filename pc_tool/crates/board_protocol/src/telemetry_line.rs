//! `>fps:9.8,infer_ms:85,...` telemetry and `>box:x,y,w,h,id,sim` lines.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TelemetrySample {
    /// Key/value pairs in the order printed.
    pub values: Vec<(String, f64)>,
}

impl TelemetrySample {
    pub fn get(&self, key: &str) -> Option<f64> {
        self.values
            .iter()
            .find(|(name, _)| name == key)
            .map(|(_, value)| *value)
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FaceBox {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    /// Best match id, 0 = unknown.
    pub face_id: u32,
    pub similarity: f64,
}

#[derive(Debug, Clone, PartialEq)]
pub enum TelemetryLine {
    Sample(TelemetrySample),
    Box(FaceBox),
}

/// Parse a line starting with `>`; `None` for anything else or malformed.
pub fn parse_telemetry_line(line: &str) -> Option<TelemetryLine> {
    let body = line.strip_prefix('>')?;
    if let Some(box_text) = body.strip_prefix("box:") {
        return parse_box(box_text).map(TelemetryLine::Box);
    }
    let mut values = Vec::new();
    for pair in body.split(',') {
        let (key, value) = pair.split_once(':')?;
        let key = key.trim();
        if key.is_empty() {
            return None;
        }
        let value: f64 = value.trim().parse().ok()?;
        values.push((key.to_string(), value));
    }
    if values.is_empty() {
        return None;
    }
    Some(TelemetryLine::Sample(TelemetrySample { values }))
}

fn parse_box(text: &str) -> Option<FaceBox> {
    let fields: Vec<&str> = text.split(',').map(str::trim).collect();
    if fields.len() != 6 {
        return None;
    }
    Some(FaceBox {
        x: fields[0].parse().ok()?,
        y: fields[1].parse().ok()?,
        width: fields[2].parse().ok()?,
        height: fields[3].parse().ok()?,
        face_id: fields[4].parse().ok()?,
        similarity: fields[5].parse().ok()?,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_sample_with_all_keys() {
        let line = ">fps:9.8,infer_ms:85,heap_free:123456,psram_free:7000000,rssi:-60,state:2,uptime_s:123";
        match parse_telemetry_line(line).unwrap() {
            TelemetryLine::Sample(sample) => {
                assert_eq!(sample.values.len(), 7);
                assert_eq!(sample.get("fps"), Some(9.8));
                assert_eq!(sample.get("rssi"), Some(-60.0));
                assert_eq!(sample.get("missing"), None);
            }
            other => panic!("unexpected {other:?}"),
        }
    }

    #[test]
    fn sample_without_rssi_is_fine() {
        let line = ">fps:0.0,infer_ms:0,heap_free:1,psram_free:2,state:0,uptime_s:3";
        let TelemetryLine::Sample(sample) = parse_telemetry_line(line).unwrap() else {
            panic!("not a sample");
        };
        assert_eq!(sample.get("rssi"), None);
    }

    #[test]
    fn parses_box() {
        let parsed = parse_telemetry_line(">box:112,40,96,120,1,0.712").unwrap();
        assert_eq!(
            parsed,
            TelemetryLine::Box(FaceBox {
                x: 112,
                y: 40,
                width: 96,
                height: 120,
                face_id: 1,
                similarity: 0.712
            })
        );
    }

    #[test]
    fn rejects_malformed() {
        assert!(parse_telemetry_line("fps:1").is_none());
        assert!(parse_telemetry_line(">").is_none());
        assert!(parse_telemetry_line(">fps:abc").is_none());
        assert!(parse_telemetry_line(">box:1,2,3").is_none());
        assert!(parse_telemetry_line(">fps").is_none());
    }
}

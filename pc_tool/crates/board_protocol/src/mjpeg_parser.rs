//! Splits an MJPEG body (`multipart/x-mixed-replace`) into JPEG frames.
//! Port of `mobile_app/lib/core/board/mjpeg_reader.dart`.
//!
//! The board sends each frame as
//! `\r\n--frameboundary\r\nContent-Type: image/jpeg\r\nContent-Length: N\r\n\r\n<N bytes>`.
//! Parts without Content-Length end at the next boundary.

/// Boundary the firmware uses (`PART_BOUNDARY` in video_stream.c).
pub const DEFAULT_BOUNDARY: &str = "frameboundary";

const HEADER_END: &[u8] = b"\r\n\r\n";

/// Reads `boundary=...` from a multipart Content-Type header.
pub fn boundary_from_content_type(content_type: &str) -> Option<String> {
    let lower = content_type.to_ascii_lowercase();
    let start = lower.find("boundary=")? + "boundary=".len();
    let rest = &content_type[start..];
    let value = rest.split(';').next()?.trim().trim_matches('"').trim();
    if value.is_empty() {
        None
    } else {
        Some(value.to_string())
    }
}

#[derive(Debug)]
pub struct MjpegParser {
    marker: Vec<u8>,
    pending: Vec<u8>,
}

impl MjpegParser {
    pub fn new(boundary: &str) -> Self {
        Self {
            marker: format!("--{boundary}").into_bytes(),
            pending: Vec::new(),
        }
    }

    /// Feed a chunk; returns every complete JPEG frame.
    pub fn push(&mut self, chunk: &[u8]) -> Vec<Vec<u8>> {
        self.pending.extend_from_slice(chunk);
        let mut frames = Vec::new();
        loop {
            let Some(part_start) = find(&self.pending, &self.marker, 0) else {
                // Keep a tail in case the marker is split across chunks.
                let keep = self.marker.len() - 1;
                if self.pending.len() > keep {
                    self.pending.drain(..self.pending.len() - keep);
                }
                break;
            };
            let Some(header_end) = find(&self.pending, HEADER_END, part_start + self.marker.len())
            else {
                self.pending.drain(..part_start);
                break;
            };
            let headers =
                String::from_utf8_lossy(&self.pending[part_start + self.marker.len()..header_end])
                    .to_string();
            let frame_start = header_end + HEADER_END.len();
            let frame_end = match content_length(&headers) {
                Some(length) => {
                    let end = frame_start + length;
                    if self.pending.len() < end {
                        self.pending.drain(..part_start);
                        break;
                    }
                    end
                }
                None => {
                    let Some(next_part) = find(&self.pending, &self.marker, frame_start) else {
                        self.pending.drain(..part_start);
                        break;
                    };
                    let mut end = next_part;
                    if end - frame_start >= 2 && &self.pending[end - 2..end] == b"\r\n" {
                        end -= 2;
                    }
                    end
                }
            };
            frames.push(self.pending[frame_start..frame_end].to_vec());
            self.pending.drain(..frame_end);
        }
        frames
    }
}

fn content_length(headers: &str) -> Option<usize> {
    headers
        .lines()
        .filter_map(|line| line.split_once(':'))
        .find(|(name, _)| name.trim().eq_ignore_ascii_case("content-length"))
        .and_then(|(_, value)| value.trim().parse().ok())
}

fn find(haystack: &[u8], needle: &[u8], from: usize) -> Option<usize> {
    if needle.is_empty() || haystack.len() < needle.len() {
        return None;
    }
    (from..=haystack.len() - needle.len())
        .find(|&start| &haystack[start..start + needle.len()] == needle)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn part(frame: &[u8], with_length: bool) -> Vec<u8> {
        let mut out = b"\r\n--frameboundary\r\nContent-Type: image/jpeg\r\n".to_vec();
        if with_length {
            out.extend_from_slice(format!("Content-Length: {}\r\n", frame.len()).as_bytes());
        }
        out.extend_from_slice(b"\r\n");
        out.extend_from_slice(frame);
        out
    }

    fn fake_jpeg(seed: u8, length: usize) -> Vec<u8> {
        let mut bytes = vec![0xff, 0xd8];
        bytes.extend((0..length).map(|index| (index as u8).wrapping_mul(seed)));
        bytes.extend([0xff, 0xd9]);
        bytes
    }

    #[test]
    fn whole_body_in_one_chunk() {
        let first = fake_jpeg(3, 100);
        let second = fake_jpeg(7, 50);
        let mut body = part(&first, true);
        body.extend(part(&second, true));
        let mut parser = MjpegParser::new(DEFAULT_BOUNDARY);
        let frames = parser.push(&body);
        assert_eq!(frames, vec![first, second]);
    }

    #[test]
    fn chunk_splits_inside_marker_header_and_jpeg() {
        let first = fake_jpeg(3, 300);
        let second = fake_jpeg(5, 200);
        let mut body = part(&first, true);
        body.extend(part(&second, true));
        for chunk_size in [1usize, 5, 17, 64, 333] {
            let mut parser = MjpegParser::new(DEFAULT_BOUNDARY);
            let mut frames = Vec::new();
            for chunk in body.chunks(chunk_size) {
                frames.extend(parser.push(chunk));
            }
            assert_eq!(
                frames,
                vec![first.clone(), second.clone()],
                "chunk size {chunk_size}"
            );
        }
    }

    #[test]
    fn part_without_content_length_ends_at_next_boundary() {
        let first = fake_jpeg(3, 80);
        let second = fake_jpeg(9, 40);
        let mut body = part(&first, false);
        body.extend(part(&second, true));
        let mut parser = MjpegParser::new(DEFAULT_BOUNDARY);
        let frames = parser.push(&body);
        assert_eq!(frames, vec![first, second]);
    }

    #[test]
    fn reads_boundary_from_header() {
        assert_eq!(
            boundary_from_content_type("multipart/x-mixed-replace;boundary=frameboundary"),
            Some("frameboundary".into())
        );
        assert_eq!(
            boundary_from_content_type("multipart/x-mixed-replace; boundary=\"abc\"; charset=x"),
            Some("abc".into())
        );
        assert_eq!(boundary_from_content_type("image/jpeg"), None);
    }
}

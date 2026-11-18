//! Serial port discovery, ranked so the UART bridge (the only port that
//! takes console input) comes first.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum SerialPortKind {
    /// CP210x, CH34x, FTDI: console input and output.
    UartBridge,
    /// ESP32-S3 built-in USB-Serial-JTAG: log output only on this firmware.
    UsbJtag,
    Other,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SerialPortInfo {
    pub path: String,
    pub kind: SerialPortKind,
    pub vendor_id: Option<u16>,
    pub product_id: Option<u16>,
    pub product_name: Option<String>,
    pub serial_number: Option<String>,
}

pub fn classify_usb(vendor_id: u16, product_id: u16) -> SerialPortKind {
    match (vendor_id, product_id) {
        (0x303a, 0x1001) => SerialPortKind::UsbJtag,
        (0x10c4, _) | (0x1a86, _) | (0x0403, _) | (0x067b, _) => SerialPortKind::UartBridge,
        _ => SerialPortKind::Other,
    }
}

fn rank(kind: SerialPortKind) -> u8 {
    match kind {
        SerialPortKind::UartBridge => 0,
        SerialPortKind::UsbJtag => 1,
        SerialPortKind::Other => 2,
    }
}

pub fn list_serial_ports() -> Vec<SerialPortInfo> {
    let mut ports: Vec<SerialPortInfo> = serialport::available_ports()
        .unwrap_or_default()
        .into_iter()
        .map(|port| match port.port_type {
            serialport::SerialPortType::UsbPort(usb) => SerialPortInfo {
                path: port.port_name,
                kind: classify_usb(usb.vid, usb.pid),
                vendor_id: Some(usb.vid),
                product_id: Some(usb.pid),
                product_name: usb.product,
                serial_number: usb.serial_number,
            },
            _ => SerialPortInfo {
                path: port.port_name,
                kind: SerialPortKind::Other,
                vendor_id: None,
                product_id: None,
                product_name: None,
                serial_number: None,
            },
        })
        .filter(|port| !port.path.starts_with("/dev/ttyS"))
        .collect();
    ports.sort_by_key(|port| (rank(port.kind), port.path.clone()));
    ports
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn classifies_known_chips() {
        assert_eq!(classify_usb(0x10c4, 0xea60), SerialPortKind::UartBridge);
        assert_eq!(classify_usb(0x1a86, 0x55d4), SerialPortKind::UartBridge);
        assert_eq!(classify_usb(0x303a, 0x1001), SerialPortKind::UsbJtag);
        assert_eq!(classify_usb(0x1234, 0x0001), SerialPortKind::Other);
    }
}

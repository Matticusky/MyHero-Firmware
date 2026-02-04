# MyHero Device - BLE Integration Guide

## Overview

This document describes the Bluetooth Low Energy (BLE) interface for integrating mobile applications with the MyHero audio recording/playback device.

**Device Name:** `MyHero`
**BLE Stack:** NimBLE (ESP32)
**Connection Mode:** Single connection only (device stops advertising when connected)

---

## Table of Contents

1. [Connection Flow](#1-connection-flow)
2. [Services Overview](#2-services-overview)
3. [Authentication](#3-authentication)
4. [File Operations](#4-file-operations)
5. [File Transfer Protocol](#5-file-transfer-protocol)
6. [Standard Services](#6-standard-services)
7. [Data Formats](#7-data-formats)
8. [Error Handling](#8-error-handling)
9. [Example Flows](#9-example-flows)

---

## 1. Connection Flow

### Discovery
1. Scan for BLE devices with name `MyHero`
2. Device advertises as General Discoverable with TX power level
3. Connect to the device (single connection only)

### Post-Connection
1. Perform service discovery
2. Subscribe to notifications on relevant characteristics
3. Authenticate using the Auth Service
4. Once authenticated, file operations become available

### Disconnection Behavior
- Authentication session is cleared on disconnect
- Device automatically resumes advertising after disconnect
- Any ongoing file transfer is cancelled

---

## 2. Services Overview

### Custom UUID Base Format
```
xxxxxxxx-4D59-4842-8000-00805F9B34FB
```
Where `4D59-4842` = "MYHB" (ASCII for "MyHero Board")

### Available Services

| Service | UUID | Description |
|---------|------|-------------|
| Auth Service | `00000001-4D59-4842-8000-00805F9B34FB` | Authentication management |
| File Service | `00000002-4D59-4842-8000-00805F9B34FB` | File operations and transfer |
| Battery Service | `0x180F` (Standard) | Battery level reporting |
| GAP Service | `0x1800` (Standard) | Device name, appearance |
| GATT Service | `0x1801` (Standard) | Service changed |

---

## 3. Authentication

The device uses app-level authentication (not BLE pairing/bonding). A 32-byte key is used for authentication.

### Auth Service UUID
```
00000001-4D59-4842-8000-00805F9B34FB
```

### Characteristics

#### 3.1 Auth Key Write
| Property | Value |
|----------|-------|
| UUID | `00000101-4D59-4842-8000-00805F9B34FB` |
| Properties | Write |
| Value | 32 bytes (authentication key) |

**Usage:**
- Write exactly 32 bytes to authenticate
- On first pairing (no stored key), any 32-byte key is accepted and stored
- On subsequent connections, the key must match the stored key

#### 3.2 Auth Status
| Property | Value |
|----------|-------|
| UUID | `00000102-4D59-4842-8000-00805F9B34FB` |
| Properties | Read, Notify |
| Value | 1 byte: `0x00` = not authenticated, `0x01` = authenticated |

**Usage:**
- Subscribe to notifications to receive authentication state changes
- Read to check current authentication status

#### 3.3 Auth Key Clear (Factory Reset)
| Property | Value |
|----------|-------|
| UUID | `00000103-4D59-4842-8000-00805F9B34FB` |
| Properties | Write |
| Requirement | Must be authenticated |
| Value | Write any byte to trigger |

**Usage:**
- Clears the stored authentication key
- Device enters first-pairing mode
- Use for factory reset or pairing with a new phone

### Authentication Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    FIRST PAIRING                            │
├─────────────────────────────────────────────────────────────┤
│ 1. App generates random 32-byte key                         │
│ 2. App writes key to Auth Key Write characteristic          │
│ 3. Device stores key in NVS flash                           │
│ 4. Auth Status notifies 0x01 (authenticated)                │
│ 5. App securely stores the key for future use               │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                 SUBSEQUENT CONNECTIONS                       │
├─────────────────────────────────────────────────────────────┤
│ 1. App retrieves stored 32-byte key                         │
│ 2. App writes key to Auth Key Write characteristic          │
│ 3. Device compares with stored key                          │
│ 4. If match: Auth Status notifies 0x01                      │
│ 5. If no match: Auth Status remains 0x00                    │
└─────────────────────────────────────────────────────────────┘
```

---

## 4. File Operations

**Requirement:** All file operations require authentication.

### File Service UUID
```
00000002-4D59-4842-8000-00805F9B34FB
```

### Characteristics

#### 4.1 File List
| Property | Value |
|----------|-------|
| UUID | `00000201-4D59-4842-8000-00805F9B34FB` |
| Properties | Read, Notify |

**Usage:**
- Read triggers the device to send file list via notifications
- Each notification contains one file entry
- End of list is marked with a special entry

**Entry Format (per notification):**
```
[type:1][size:4][filename\0]
```

| Field | Size | Description |
|-------|------|-------------|
| type | 1 byte | `0x00` = file, `0x01` = directory, `0xFF` = end of list |
| size | 4 bytes | File size in bytes (little-endian), 0 for directories |
| filename | variable | Null-terminated filename only (no path) |

**Note:** Only the filename is sent (e.g., `recording_0001.aac`), not the full path. The device storage path (`/Storage/`) is internal and not relevant to the phone app.

**Example:**
```
Notification 1: 00 80 1A 06 00 74 65 73 74 2E 61 61 63 00
                │  └──────────┘ └────────────────────────┘
                │     400000         "test.aac\0"
                └─ type=file

Notification 2: FF 00 00 00 00
                └─ type=end (no more files)
```

#### 4.2 File Delete
| Property | Value |
|----------|-------|
| UUID | `00000202-4D59-4842-8000-00805F9B34FB` |
| Properties | Write |
| Value | Null-terminated file path (max 127 bytes) |

**Usage:**
- Write the file path to delete
- Path can be relative (e.g., `recording_0001.aac`) or absolute (e.g., `/Storage/recording_0001.aac`)
- Relative paths are prefixed with `/Storage/`

---

## 5. File Transfer Protocol

File transfer uses raw binary data for efficient transmission over BLE. The device negotiates a higher MTU (512 bytes) for optimal throughput.

### Transfer Characteristics

#### 5.1 Transfer Control
| Property | Value |
|----------|-------|
| UUID | `00000203-4D59-4842-8000-00805F9B34FB` |
| Properties | Write, Notify |

**Write Commands:**

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x00` | Cancel | `[0x00]` | Cancel ongoing transfer |
| `0x01` | Upload | `[0x01][size:4][filename\0]` | Start upload (phone → device) |
| `0x02` | Download | `[0x02][filename\0]` | Start download (device → phone) |

**Notify Responses:**

| Status | Value | Format | Description |
|--------|-------|--------|-------------|
| Error | `0x00` | `[0x00][error_code:4]` | Transfer failed |
| Ready | `0x01` | `[0x01][size:4]` | Ready for transfer, includes file size |
| Complete | `0x02` | `[0x02][size:4]` | Transfer completed successfully |

#### 5.2 Transfer Data
| Property | Value |
|----------|-------|
| UUID | `00000204-4D59-4842-8000-00805F9B34FB` |
| Properties | Write, Read, Notify |
| Chunk Size | Max 490 bytes (raw binary) |

**Usage:**
- **Upload (Write):** App writes raw binary chunks to this characteristic
- **Download (Notify + Read):** Device notifies when chunk is ready, app reads to retrieve data

**Encoding:**
- All data is **raw binary** (no encoding)
- Max 490 bytes per chunk
- With MTU 512, max ATT payload is 509 bytes; 490 leaves margin for protocol overhead

#### 5.3 Transfer Progress
| Property | Value |
|----------|-------|
| UUID | `00000205-4D59-4842-8000-00805F9B34FB` |
| Properties | Read, Notify |
| Format | `[transferred:4][total:4]` (little-endian) |

### Upload Flow (Phone → Device)

```
┌──────────────────────────────────────────────────────────────────┐
│                        UPLOAD FLOW                               │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Phone                                    Device                 │
│    │                                         │                   │
│    │  1. Write Transfer Control              │                   │
│    │     [0x01][size:4][filename\0]          │                   │
│    │ ─────────────────────────────────────►  │                   │
│    │                                         │                   │
│    │  2. Notify Transfer Control             │                   │
│    │     [0x01][size:4] (Ready)              │                   │
│    │ ◄─────────────────────────────────────  │                   │
│    │                                         │                   │
│    │  3. Write Transfer Data                 │                   │
│    │     [raw binary chunk 1]                │                   │
│    │ ─────────────────────────────────────►  │                   │
│    │                                         │                   │
│    │  4. Notify Transfer Progress            │                   │
│    │     [transferred:4][total:4]            │                   │
│    │ ◄─────────────────────────────────────  │                   │
│    │                                         │                   │
│    │  ... repeat steps 3-4 for all chunks    │                   │
│    │                                         │                   │
│    │  5. Notify Transfer Control             │                   │
│    │     [0x02][size:4] (Complete)           │                   │
│    │ ◄─────────────────────────────────────  │                   │
│    │                                         │                   │
└──────────────────────────────────────────────────────────────────┘
```

### Download Flow (Device → Phone)

The download flow uses a **read-based** approach where the app controls the pace by reading chunks when ready.

```
┌──────────────────────────────────────────────────────────────────┐
│                       DOWNLOAD FLOW                              │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Phone                                    Device                 │
│    │                                         │                   │
│    │  1. Write Transfer Control              │                   │
│    │     [0x02][filename\0]                  │                   │
│    │ ─────────────────────────────────────►  │                   │
│    │                                         │                   │
│    │  2. Notify Transfer Data                │                   │
│    │     [0x01][size:4] (Ready + file size)  │                   │
│    │ ◄─────────────────────────────────────  │                   │
│    │                                         │                   │
│    │  3. Read Transfer Data                  │                   │
│    │ ─────────────────────────────────────►  │                   │
│    │  Response: [raw binary chunk 1]         │                   │
│    │ ◄─────────────────────────────────────  │                   │
│    │                                         │                   │
│    │  4. Notify Transfer Data                │                   │
│    │     [0x01][chunk_len:4] (Next ready)    │                   │
│    │ ◄─────────────────────────────────────  │                   │
│    │                                         │                   │
│    │  5. Notify Transfer Progress            │                   │
│    │     [transferred:4][total:4]            │                   │
│    │ ◄─────────────────────────────────────  │                   │
│    │                                         │                   │
│    │  6. Read Transfer Data                  │                   │
│    │ ─────────────────────────────────────►  │                   │
│    │  Response: [raw binary chunk 2]         │                   │
│    │ ◄─────────────────────────────────────  │                   │
│    │                                         │                   │
│    │  ... repeat steps 4-6 for all chunks    │                   │
│    │                                         │                   │
│    │  7. Notify Transfer Control             │                   │
│    │     [0x02][0:4] (Complete)              │                   │
│    │ ◄─────────────────────────────────────  │                   │
│    │                                         │                   │
└──────────────────────────────────────────────────────────────────┘
```

**Transfer Data Notifications (Download):**

| Status | Value | Format | Description |
|--------|-------|--------|-------------|
| Error | `0x00` | `[0x00][0:4]` | Transfer failed |
| Ready | `0x01` | `[0x01][size:4]` | Chunk ready, size = file size (first) or chunk length (subsequent) |

### Transfer Cancellation

To cancel an ongoing transfer:
1. Write `[0x00]` to Transfer Control characteristic
2. Device closes file and cleans up
3. Partial uploads are deleted from storage

---

## 6. Standard Services

### 6.1 Battery Service (0x180F)

| Characteristic | UUID | Properties | Format |
|---------------|------|------------|--------|
| Battery Level | `0x2A19` | Read, Notify | uint8 (0-100%) |

**Usage:**
- Read returns current battery percentage (0-100)
- Subscribe for battery level change notifications

### 6.2 GAP Service (0x1800)

| Characteristic | UUID | Properties | Value |
|---------------|------|------------|-------|
| Device Name | `0x2A00` | Read | "MyHero" |
| Appearance | `0x2A01` | Read | Generic |

---

## 7. Data Formats

### Byte Order
All multi-byte integers are **little-endian**.

### String Encoding
All strings are **null-terminated UTF-8**.

### Binary Transfer
- Data is transferred as raw binary (no encoding)
- Max chunk size: 490 bytes
- MTU negotiated to 512 bytes for optimal throughput
- Connection interval optimized to 7.5-15ms for fast transfer

### File Paths
- Storage root: `/Storage/`
- Supported audio format: AAC (`.aac` extension)
- Recording filename pattern: `recording_NNNN.aac`
- Relative paths are auto-prefixed with `/Storage/`

---

## 8. Error Handling

### BLE ATT Errors

| Error | Code | Description |
|-------|------|-------------|
| Invalid Length | `0x0D` | Wrong data length for characteristic |
| Insufficient Authentication | `0x05` | Operation requires authentication |
| Unlikely Error | `0x0E` | General operation failure |
| Insufficient Resources | `0x11` | Out of memory |

### Transfer Errors

When Transfer Control notifies with status `0x00` (Error):
- File not found
- Storage full
- File system error
- Invalid filename

### Recommended Error Handling

1. Always check Auth Status before file operations
2. Implement retry logic for failed writes
3. Handle disconnection during transfer (cleanup partial state)
4. Validate file sizes before upload (storage has limits)

---

## 9. Example Flows

### Complete Session Example

```
1. SCAN & CONNECT
   - Scan for "MyHero"
   - Connect to device
   - Discover services

2. SETUP NOTIFICATIONS
   - Subscribe to Auth Status (0x00000102-...)
   - Subscribe to File List (0x00000201-...)
   - Subscribe to Transfer Control (0x00000203-...)
   - Subscribe to Transfer Data (0x00000204-...)
   - Subscribe to Transfer Progress (0x00000205-...)

3. AUTHENTICATE
   - Write 32-byte key to Auth Key Write
   - Wait for Auth Status notification (0x01 = success)

4. LIST FILES
   - Read File List characteristic
   - Collect notifications until type=0xFF received

5. DOWNLOAD FILE
   - Write [0x02]["recording_0001.aac\0"] to Transfer Control
   - Receive Ready notification on Transfer Data with file size
   - Read Transfer Data to get raw binary chunk
   - Wait for next Ready notification, then read again
   - Write raw chunks directly to local file
   - Receive Complete notification on Transfer Control

6. UPLOAD FILE
   - Write [0x01][file_size:4]["newfile.aac\0"] to Transfer Control
   - Wait for Ready notification
   - Send raw binary chunks (max 490 bytes each)
   - Write chunks to Transfer Data
   - Wait for Complete notification

7. DELETE FILE
   - Write ["recording_0001.aac\0"] to File Delete

8. DISCONNECT
   - Disconnect gracefully
   - Device resumes advertising
```

### Sample Code (Pseudocode)

```javascript
// Authentication
async function authenticate(key32bytes) {
    await writeCharacteristic(AUTH_KEY_WRITE_UUID, key32bytes);
    // Wait for notification on AUTH_STATUS_UUID
    // Returns true if status byte is 0x01
}

// List Files
async function listFiles() {
    const files = [];

    subscribeToNotifications(FILE_LIST_UUID, (data) => {
        const type = data[0];
        if (type === 0xFF) {
            // End of list
            return;
        }
        const size = readUint32LE(data, 1);
        const path = readNullTerminatedString(data, 5);
        files.push({ type, size, path });
    });

    await readCharacteristic(FILE_LIST_UUID); // Triggers listing
    return files;
}

// Upload File
async function uploadFile(filename, fileData) {
    const size = fileData.length;
    const command = new Uint8Array([0x01, ...uint32ToLE(size), ...stringToBytes(filename), 0]);

    await writeCharacteristic(TRANSFER_CONTROL_UUID, command);
    // Wait for Ready notification

    // Send raw binary chunks (max 490 bytes each)
    for (let i = 0; i < fileData.length; i += 490) {
        const chunk = fileData.slice(i, i + 490);
        await writeCharacteristic(TRANSFER_DATA_UUID, chunk);
    }

    // Wait for Complete notification
}

// Download File (read-based flow)
async function downloadFile(filename) {
    const chunks = [];
    let transferComplete = false;
    let fileSize = 0;

    // Subscribe to Transfer Data notifications (ready signals)
    subscribeToNotifications(TRANSFER_DATA_UUID, async (data) => {
        const status = data[0];
        if (status === 0x01) {
            // Ready - read the chunk (raw binary)
            const size = readUint32LE(data, 1);
            if (fileSize === 0) fileSize = size; // First notification has file size

            const chunkData = await readCharacteristic(TRANSFER_DATA_UUID);
            chunks.push(chunkData); // Raw binary, no decoding needed
        }
    });

    // Subscribe to Transfer Control for completion
    subscribeToNotifications(TRANSFER_CONTROL_UUID, (data) => {
        if (data[0] === 0x02) {
            transferComplete = true;
        }
    });

    // Start download
    const command = new Uint8Array([0x02, ...stringToBytes(filename), 0]);
    await writeCharacteristic(TRANSFER_CONTROL_UUID, command);

    // Wait for Complete notification
    while (!transferComplete) {
        await delay(10);
    }
    return concatenateArrays(chunks);
}
```

---

## Appendix: UUID Reference

### Custom Service UUIDs

| Name | UUID |
|------|------|
| Auth Service | `00000001-4D59-4842-8000-00805F9B34FB` |
| File Service | `00000002-4D59-4842-8000-00805F9B34FB` |

### Custom Characteristic UUIDs

| Name | UUID | Service |
|------|------|---------|
| Auth Key Write | `00000101-4D59-4842-8000-00805F9B34FB` | Auth |
| Auth Status | `00000102-4D59-4842-8000-00805F9B34FB` | Auth |
| Auth Key Clear | `00000103-4D59-4842-8000-00805F9B34FB` | Auth |
| File List | `00000201-4D59-4842-8000-00805F9B34FB` | File |
| File Delete | `00000202-4D59-4842-8000-00805F9B34FB` | File |
| Transfer Control | `00000203-4D59-4842-8000-00805F9B34FB` | File |
| Transfer Data | `00000204-4D59-4842-8000-00805F9B34FB` | File |
| Transfer Progress | `00000205-4D59-4842-8000-00805F9B34FB` | File |

### Standard UUIDs

| Name | UUID |
|------|------|
| Battery Service | `0x180F` |
| Battery Level | `0x2A19` |
| GAP Service | `0x1800` |
| Device Name | `0x2A00` |

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.2 | 2026-02-04 | Removed Base64 encoding for faster transfer. Now uses raw binary (490 bytes/chunk). MTU increased to 512. Optimized connection parameters (7.5-15ms interval). |
| 1.1 | 2026-02-04 | Changed download flow to read-based (app reads chunks instead of receiving via notify). Changed audio format from WAV to AAC. |
| 1.0 | 2026-02-01 | Initial release |

---

## Contact

For questions about this integration, please contact the firmware development team.

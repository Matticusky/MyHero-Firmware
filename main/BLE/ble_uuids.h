#ifndef BLE_UUIDS_H
#define BLE_UUIDS_H

#include "host/ble_uuid.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Custom UUID Base: xxxxxxxx-4D59-4842-8000-00805F9B34FB
// "MYHB" = 4D59-4842 (ASCII for MyHero Board)
// ============================================================================

// ============ Auth Service (0x0001) ============
// Service UUID: 00000001-4D59-4842-8000-00805F9B34FB
#define BLE_UUID_AUTH_SERVICE \
    BLE_UUID128_DECLARE(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
                        0x42, 0x48, 0x59, 0x4D, 0x01, 0x00, 0x00, 0x00)

// Auth Key Write Characteristic: 00000101-4D59-4842-8000-00805F9B34FB
// Write 32-byte key for authentication
#define BLE_UUID_AUTH_KEY_WRITE \
    BLE_UUID128_DECLARE(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
                        0x42, 0x48, 0x59, 0x4D, 0x01, 0x01, 0x00, 0x00)

// Auth Status Characteristic: 00000102-4D59-4842-8000-00805F9B34FB
// Read/Notify: 0x00 = not authenticated, 0x01 = authenticated
#define BLE_UUID_AUTH_STATUS \
    BLE_UUID128_DECLARE(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
                        0x42, 0x48, 0x59, 0x4D, 0x02, 0x01, 0x00, 0x00)

// Auth Key Clear Characteristic: 00000103-4D59-4842-8000-00805F9B34FB
// Write (requires auth): clears stored key for factory reset
#define BLE_UUID_AUTH_KEY_CLEAR \
    BLE_UUID128_DECLARE(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
                        0x42, 0x48, 0x59, 0x4D, 0x03, 0x01, 0x00, 0x00)

// ============ File Service (0x0002) ============
// Service UUID: 00000002-4D59-4842-8000-00805F9B34FB
#define BLE_UUID_FILE_SERVICE \
    BLE_UUID128_DECLARE(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
                        0x42, 0x48, 0x59, 0x4D, 0x02, 0x00, 0x00, 0x00)

// File List Characteristic: 00000201-4D59-4842-8000-00805F9B34FB
// Read/Notify: Lists files with format [type:1][size:4][path\0]
#define BLE_UUID_FILE_LIST \
    BLE_UUID128_DECLARE(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
                        0x42, 0x48, 0x59, 0x4D, 0x01, 0x02, 0x00, 0x00)

// File Delete Characteristic: 00000202-4D59-4842-8000-00805F9B34FB
// Write: Delete file by path
#define BLE_UUID_FILE_DELETE \
    BLE_UUID128_DECLARE(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
                        0x42, 0x48, 0x59, 0x4D, 0x02, 0x02, 0x00, 0x00)

// Transfer Control Characteristic: 00000203-4D59-4842-8000-00805F9B34FB
// Write/Notify: Upload [0x01][size:4][filename] or Download [0x02][filename]
// Notify response: [status:1][size:4] where status: 0x01=ready, 0x00=error, 0x02=complete
#define BLE_UUID_TRANSFER_CONTROL \
    BLE_UUID128_DECLARE(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
                        0x42, 0x48, 0x59, 0x4D, 0x03, 0x02, 0x00, 0x00)

// Transfer Data Characteristic: 00000204-4D59-4842-8000-00805F9B34FB
// Write/Notify: Base64 encoded file data chunks (up to 240 bytes = 180 raw bytes)
#define BLE_UUID_TRANSFER_DATA \
    BLE_UUID128_DECLARE(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
                        0x42, 0x48, 0x59, 0x4D, 0x04, 0x02, 0x00, 0x00)

// Transfer Progress Characteristic: 00000205-4D59-4842-8000-00805F9B34FB
// Read/Notify: [transferred:4][total:4] in little-endian
#define BLE_UUID_TRANSFER_PROGRESS \
    BLE_UUID128_DECLARE(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
                        0x42, 0x48, 0x59, 0x4D, 0x05, 0x02, 0x00, 0x00)

// ============ Standard Services ============
// Battery Service: 0x180F (standard BLE SIG)
// Battery Level Characteristic: 0x2A19 (uint8_t 0-100%)

// Device Information Service: 0x180A (standard BLE SIG)
// These are handled by NimBLE's built-in services

// ============ Transfer Status Codes ============
#define BLE_TRANSFER_STATUS_ERROR     0x00
#define BLE_TRANSFER_STATUS_READY     0x01
#define BLE_TRANSFER_STATUS_COMPLETE  0x02

// ============ Transfer Operation Codes ============
#define BLE_TRANSFER_OP_CANCEL   0x00
#define BLE_TRANSFER_OP_UPLOAD   0x01
#define BLE_TRANSFER_OP_DOWNLOAD 0x02

// ============ File List Entry Types ============
#define BLE_FILE_TYPE_FILE      0x00
#define BLE_FILE_TYPE_DIRECTORY 0x01
#define BLE_FILE_TYPE_END       0xFF

// ============ Auth Key Size ============
#define BLE_AUTH_KEY_SIZE 32

#ifdef __cplusplus
}
#endif

#endif // BLE_UUIDS_H

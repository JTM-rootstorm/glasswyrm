//! Temporary, deliberately narrow C ABI bridge for GWIPC migration.
//!
//! The Rust exports use an internal prefix. A small C forwarding shim supplies
//! the legacy public names and GNU symbol versions for compatibility tests.
//! Nothing in this crate is installed in place of native `libgwipc` yet.

use core::ffi::{c_char, c_void};
use core::{mem, ptr, slice};

const GWIPC_STATUS_OK: i32 = 0;
const GWIPC_STATUS_INVALID_ARGUMENT: i32 = 4;
const GWIPC_STATUS_OUT_OF_MEMORY: i32 = 6;
const GWIPC_STATUS_PROTOCOL_ERROR: i32 = 8;

const GWIPC_MESSAGE_SURFACE_REMOVE: u16 = 0x0111;

const STATUS_NAMES: [&[u8]; 14] = [
    b"Ok\0",
    b"WouldBlock\0",
    b"InProgress\0",
    b"Disconnected\0",
    b"InvalidArgument\0",
    b"InvalidState\0",
    b"OutOfMemory\0",
    b"LimitExceeded\0",
    b"ProtocolError\0",
    b"CredentialRejected\0",
    b"VersionMismatch\0",
    b"RoleRejected\0",
    b"CapabilityMismatch\0",
    b"SystemError\0",
];
const UNKNOWN_STATUS: &[u8] = b"UnknownStatus\0";

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GwipcApiVersion {
    major: u16,
    minor: u16,
    patch: u16,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GwipcWireVersion {
    major: u16,
    minor: u16,
}

#[repr(C)]
pub struct GwipcSurfaceRemove {
    struct_size: usize,
    surface_id: u64,
    reserved: [u64; 4],
}

#[repr(C)]
pub struct GwipcContractPayload {
    bytes: [u8; 8],
}

#[repr(C)]
pub struct GwipcDecodedContract {
    message_type: u16,
    surface_remove: GwipcSurfaceRemove,
}

unsafe extern "C" {
    fn malloc(size: usize) -> *mut c_void;
    fn free(pointer: *mut c_void);
}

fn allocate<T>(value: T) -> *mut T {
    // SAFETY: `malloc` is called with the exact size and the successful result
    // is initialized as a `T` before it is exposed to callers.
    let allocation = unsafe { malloc(mem::size_of::<T>()) }.cast::<T>();
    if allocation.is_null() {
        return ptr::null_mut();
    }
    // SAFETY: the allocation above is non-null, suitably aligned for the C
    // allocator, and large enough for one `T`.
    unsafe { allocation.write(value) };
    allocation
}

fn reserved_is_zero(reserved: &[u64; 4]) -> bool {
    reserved.iter().all(|value| *value == 0)
}

#[unsafe(no_mangle)]
pub extern "C" fn gwipc_rust_get_api_version() -> GwipcApiVersion {
    GwipcApiVersion {
        major: 0,
        minor: 9,
        patch: 0,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn gwipc_rust_get_max_wire_version() -> GwipcWireVersion {
    GwipcWireVersion { major: 1, minor: 0 }
}

#[unsafe(no_mangle)]
pub extern "C" fn gwipc_rust_status_string(status: i32) -> *const c_char {
    usize::try_from(status)
        .ok()
        .and_then(|index| STATUS_NAMES.get(index))
        .copied()
        .unwrap_or(UNKNOWN_STATUS)
        .as_ptr()
        .cast()
}

/// Encodes the covered `gwipc_surface_remove` contract.
///
/// # Safety
///
/// `value` must be null or point to a readable `GwipcSurfaceRemove` and
/// `out_payload` must be null or point to writable pointer storage. A returned
/// payload must be released with `gwipc_rust_contract_payload_destroy`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gwipc_rust_contract_encode_surface_remove(
    value: *const GwipcSurfaceRemove,
    out_payload: *mut *mut GwipcContractPayload,
) -> i32 {
    if value.is_null() || out_payload.is_null() {
        return GWIPC_STATUS_INVALID_ARGUMENT;
    }

    // SAFETY: caller obligations above require a readable input structure.
    let value = unsafe { &*value };
    if value.struct_size < mem::size_of::<GwipcSurfaceRemove>()
        || !reserved_is_zero(&value.reserved)
    {
        return GWIPC_STATUS_INVALID_ARGUMENT;
    }
    // Native libgwipc initializes the output only after the public structure
    // itself has passed its size/reserved-field checks.
    // SAFETY: caller obligations above require writable pointer storage.
    unsafe { out_payload.write(ptr::null_mut()) };
    if value.surface_id == 0 {
        return GWIPC_STATUS_INVALID_ARGUMENT;
    }

    let payload = allocate(GwipcContractPayload {
        bytes: value.surface_id.to_le_bytes(),
    });
    if payload.is_null() {
        return GWIPC_STATUS_OUT_OF_MEMORY;
    }
    // SAFETY: caller obligations above require writable pointer storage.
    unsafe { out_payload.write(payload) };
    GWIPC_STATUS_OK
}

/// Returns the borrowed payload bytes.
///
/// # Safety
///
/// `payload` must be null or a live pointer returned by this crate.
/// `out_size`, when non-null, must point to writable `usize` storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gwipc_rust_contract_payload_data(
    payload: *const GwipcContractPayload,
    out_size: *mut usize,
) -> *const u8 {
    if !out_size.is_null() {
        // SAFETY: the caller promises writable storage when non-null.
        unsafe { out_size.write(if payload.is_null() { 0 } else { 8 }) };
    }
    if payload.is_null() {
        return ptr::null();
    }
    // SAFETY: the caller promises a live payload returned by this crate.
    unsafe { (*payload).bytes.as_ptr() }
}

/// Destroys a payload returned by this crate.
///
/// # Safety
///
/// `payload` must be null or a live, not-yet-destroyed pointer returned by this
/// crate.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gwipc_rust_contract_payload_destroy(payload: *mut GwipcContractPayload) {
    if !payload.is_null() {
        // SAFETY: the caller promises this allocation came from this crate.
        unsafe { free(payload.cast()) };
    }
}

/// Decodes one covered contract from message parts obtained by the C shim.
///
/// # Safety
///
/// `bytes` must describe `size` readable bytes when `size` is nonzero.
/// `out_contract` must be null or point to writable pointer storage. A returned
/// contract must be released with `gwipc_rust_decoded_contract_destroy`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gwipc_rust_contract_decode_parts(
    message_type: u16,
    bytes: *const u8,
    size: usize,
    out_contract: *mut *mut GwipcDecodedContract,
) -> i32 {
    if out_contract.is_null() || (size != 0 && bytes.is_null()) {
        return GWIPC_STATUS_INVALID_ARGUMENT;
    }
    // SAFETY: caller obligations above require writable pointer storage.
    unsafe { out_contract.write(ptr::null_mut()) };
    if message_type != GWIPC_MESSAGE_SURFACE_REMOVE {
        return GWIPC_STATUS_INVALID_ARGUMENT;
    }
    if size != 8 {
        return GWIPC_STATUS_PROTOCOL_ERROR;
    }

    // SAFETY: caller obligations above guarantee eight readable bytes.
    let bytes = unsafe { slice::from_raw_parts(bytes, size) };
    let mut encoded_id = [0_u8; 8];
    encoded_id.copy_from_slice(bytes);
    let surface_id = u64::from_le_bytes(encoded_id);
    if surface_id == 0 {
        return GWIPC_STATUS_PROTOCOL_ERROR;
    }

    let contract = allocate(GwipcDecodedContract {
        message_type,
        surface_remove: GwipcSurfaceRemove {
            struct_size: mem::size_of::<GwipcSurfaceRemove>(),
            surface_id,
            reserved: [0; 4],
        },
    });
    if contract.is_null() {
        return GWIPC_STATUS_OUT_OF_MEMORY;
    }
    // SAFETY: caller obligations above require writable pointer storage.
    unsafe { out_contract.write(contract) };
    GWIPC_STATUS_OK
}

/// Returns the decoded contract type or zero for a null contract.
///
/// # Safety
///
/// `contract` must be null or a live pointer returned by this crate.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gwipc_rust_decoded_contract_type(
    contract: *const GwipcDecodedContract,
) -> u16 {
    if contract.is_null() {
        0
    } else {
        // SAFETY: the caller promises a live decoded contract.
        unsafe { (*contract).message_type }
    }
}

/// Returns a borrowed covered contract view.
///
/// # Safety
///
/// `contract` must be null or a live pointer returned by this crate. The
/// returned pointer is valid only until the decoded contract is destroyed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gwipc_rust_decoded_surface_remove(
    contract: *const GwipcDecodedContract,
) -> *const GwipcSurfaceRemove {
    if contract.is_null() {
        return ptr::null();
    }
    // SAFETY: the caller promises a live decoded contract.
    let contract = unsafe { &*contract };
    if contract.message_type != GWIPC_MESSAGE_SURFACE_REMOVE {
        ptr::null()
    } else {
        &contract.surface_remove
    }
}

/// Destroys a decoded contract returned by this crate.
///
/// # Safety
///
/// `contract` must be null or a live, not-yet-destroyed pointer returned by
/// this crate.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gwipc_rust_decoded_contract_destroy(contract: *mut GwipcDecodedContract) {
    if !contract.is_null() {
        // SAFETY: the caller promises this allocation came from this crate.
        unsafe { free(contract.cast()) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn versions_and_status_names_match_api_zero_nine() {
        assert_eq!(
            gwipc_rust_get_api_version(),
            GwipcApiVersion {
                major: 0,
                minor: 9,
                patch: 0
            }
        );
        assert_eq!(
            gwipc_rust_get_max_wire_version(),
            GwipcWireVersion { major: 1, minor: 0 }
        );
        // SAFETY: the status string functions return static NUL-terminated bytes.
        unsafe {
            assert_eq!(
                std::ffi::CStr::from_ptr(gwipc_rust_status_string(0)).to_bytes(),
                b"Ok"
            );
            assert_eq!(
                std::ffi::CStr::from_ptr(gwipc_rust_status_string(99)).to_bytes(),
                b"UnknownStatus"
            );
        }
    }

    #[test]
    fn surface_remove_round_trips_and_rejects_malformed_bytes() {
        let value = GwipcSurfaceRemove {
            struct_size: mem::size_of::<GwipcSurfaceRemove>(),
            surface_id: 0x0102_0304_0506_0708,
            reserved: [0; 4],
        };
        let mut payload = ptr::null_mut();
        assert_eq!(
            // SAFETY: all pointers refer to live local storage.
            unsafe { gwipc_rust_contract_encode_surface_remove(&value, &mut payload) },
            GWIPC_STATUS_OK
        );
        let mut size = 0;
        // SAFETY: payload is live and `size` is writable.
        let bytes = unsafe { gwipc_rust_contract_payload_data(payload, &mut size) };
        assert_eq!(size, 8);
        assert_eq!(
            // SAFETY: the payload reports eight readable bytes.
            unsafe { slice::from_raw_parts(bytes, size) },
            &value.surface_id.to_le_bytes()
        );

        let mut decoded = ptr::null_mut();
        assert_eq!(
            // SAFETY: the payload bytes and output pointer are valid.
            unsafe {
                gwipc_rust_contract_decode_parts(
                    GWIPC_MESSAGE_SURFACE_REMOVE,
                    bytes,
                    size,
                    &mut decoded,
                )
            },
            GWIPC_STATUS_OK
        );
        // SAFETY: decoded is a live object of the covered type.
        let round_trip = unsafe { &*gwipc_rust_decoded_surface_remove(decoded) };
        assert_eq!(round_trip.surface_id, value.surface_id);

        assert_eq!(
            // SAFETY: the output pointer is writable; seven bytes are readable.
            unsafe {
                gwipc_rust_contract_decode_parts(
                    GWIPC_MESSAGE_SURFACE_REMOVE,
                    bytes,
                    size - 1,
                    &mut ptr::null_mut(),
                )
            },
            GWIPC_STATUS_PROTOCOL_ERROR
        );
        // SAFETY: each object is destroyed exactly once.
        unsafe {
            gwipc_rust_decoded_contract_destroy(decoded);
            gwipc_rust_contract_payload_destroy(payload);
        }
    }

    #[test]
    fn surface_remove_preserves_legacy_output_initialization_order() {
        let invalid_structure = GwipcSurfaceRemove {
            struct_size: 0,
            surface_id: 1,
            reserved: [0; 4],
        };
        let sentinel = ptr::NonNull::<GwipcContractPayload>::dangling().as_ptr();
        let mut payload = sentinel;
        assert_eq!(
            // SAFETY: both pointers refer to live local storage.
            unsafe { gwipc_rust_contract_encode_surface_remove(&invalid_structure, &mut payload) },
            GWIPC_STATUS_INVALID_ARGUMENT
        );
        assert_eq!(payload, sentinel);

        let invalid_value = GwipcSurfaceRemove {
            struct_size: mem::size_of::<GwipcSurfaceRemove>(),
            surface_id: 0,
            reserved: [0; 4],
        };
        assert_eq!(
            // SAFETY: both pointers refer to live local storage.
            unsafe { gwipc_rust_contract_encode_surface_remove(&invalid_value, &mut payload) },
            GWIPC_STATUS_INVALID_ARGUMENT
        );
        assert!(payload.is_null());
    }
}

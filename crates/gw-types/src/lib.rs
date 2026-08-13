//! Shared, policy-neutral types used by Glasswyrm's Rust crates.
//!
//! These types describe values, not their wire representation. Wire codecs must
//! encode fields explicitly rather than serializing Rust memory layouts.

#![forbid(unsafe_code)]

use core::fmt;

/// The GWIPC wire version implemented by the legacy M14 stack.
pub const GWIPC_WIRE_VERSION: WireVersion = WireVersion::new(1, 0);

/// A negotiated GWIPC wire protocol version.
#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct WireVersion {
    pub major: u16,
    pub minor: u16,
}

impl WireVersion {
    #[must_use]
    pub const fn new(major: u16, minor: u16) -> Self {
        Self { major, minor }
    }
}

macro_rules! id_type {
    ($(#[$meta:meta])* $name:ident, $raw:ty) => {
        $(#[$meta])*
        #[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
        #[repr(transparent)]
        pub struct $name(pub $raw);

        impl $name {
            #[must_use]
            pub const fn new(value: $raw) -> Self {
                Self(value)
            }

            #[must_use]
            pub const fn get(self) -> $raw {
                self.0
            }
        }

        impl From<$raw> for $name {
            fn from(value: $raw) -> Self {
                Self(value)
            }
        }

        impl From<$name> for $raw {
            fn from(value: $name) -> Self {
                value.0
            }
        }
    };
}

id_type!(/// A GWIPC connection identifier.
    ConnectionId, u64);
id_type!(/// A monotonically increasing GWIPC message sequence number.
    Sequence, u64);
id_type!(/// A compositor output identifier.
    OutputId, u64);
id_type!(/// A compositor surface identifier.
    SurfaceId, u64);
id_type!(/// A shared-buffer identifier.
    BufferId, u64);
id_type!(/// An X11 window identifier.
    WindowId, u32);
id_type!(/// A state or snapshot generation.
    Generation, u64);
id_type!(/// A frame or policy commit identifier.
    CommitId, u64);
id_type!(/// A snapshot identifier.
    SnapshotId, u64);

/// A GWIPC message type.
///
/// This is intentionally a newtype rather than an enum: the legacy decoder
/// preserves unknown `u16` values so later validation can decide whether a
/// message is supported.
#[derive(Clone, Copy, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
#[repr(transparent)]
pub struct MessageType(u16);

impl MessageType {
    pub const HELLO: Self = Self(0x0001);
    pub const WELCOME: Self = Self(0x0002);
    pub const REJECT: Self = Self(0x0003);
    pub const PING: Self = Self(0x0004);
    pub const PONG: Self = Self(0x0005);
    pub const PROTOCOL_ERROR: Self = Self(0x0006);
    pub const SNAPSHOT_BEGIN: Self = Self(0x0010);
    pub const SNAPSHOT_END: Self = Self(0x0011);
    pub const SNAPSHOT_ABORT: Self = Self(0x0012);
    pub const OUTPUT_UPSERT: Self = Self(0x0100);
    pub const OUTPUT_REMOVE: Self = Self(0x0101);
    pub const OUTPUT_DESCRIPTOR_UPSERT: Self = Self(0x0102);
    pub const OUTPUT_MODE_UPSERT: Self = Self(0x0103);
    pub const OUTPUT_VRR_CAPABILITY_UPSERT: Self = Self(0x0104);
    pub const OUTPUT_VRR_POLICY_UPSERT: Self = Self(0x0105);
    pub const OUTPUT_VRR_STATE_UPSERT: Self = Self(0x0106);
    pub const SURFACE_UPSERT: Self = Self(0x0110);
    pub const SURFACE_REMOVE: Self = Self(0x0111);
    pub const SURFACE_POLICY_UPSERT: Self = Self(0x0112);
    pub const SURFACE_OUTPUT_STATE: Self = Self(0x0113);
    pub const SURFACE_VRR_STATE: Self = Self(0x0114);
    pub const BUFFER_ATTACH: Self = Self(0x0120);
    pub const BUFFER_DETACH: Self = Self(0x0121);
    pub const BUFFER_RELEASE: Self = Self(0x0122);
    pub const SURFACE_DAMAGE: Self = Self(0x0130);
    pub const FRAME_COMMIT: Self = Self(0x0140);
    pub const FRAME_ACKNOWLEDGED: Self = Self(0x0141);
    pub const PRESENTATION_TIMING: Self = Self(0x0142);
    pub const POLICY_CONTEXT_UPSERT: Self = Self(0x0200);
    pub const POLICY_WINDOW_UPSERT: Self = Self(0x0201);
    pub const POLICY_WINDOW_REMOVE: Self = Self(0x0202);
    pub const POLICY_LIFECYCLE_WINDOW_UPSERT: Self = Self(0x0203);
    pub const POLICY_OUTPUT_UPSERT: Self = Self(0x0204);
    pub const POLICY_WINDOW_OUTPUT_HINT: Self = Self(0x0205);
    pub const POLICY_WINDOW_VRR_UPSERT: Self = Self(0x0206);
    pub const POLICY_OUTPUT_VRR_UPSERT: Self = Self(0x0207);
    pub const POLICY_COMMIT: Self = Self(0x0210);
    pub const POLICY_WINDOW_STATE: Self = Self(0x0211);
    pub const POLICY_ACKNOWLEDGED: Self = Self(0x0212);
    pub const POLICY_BINDINGS_UPSERT: Self = Self(0x0213);
    pub const POLICY_WINDOW_VRR_STATE: Self = Self(0x0214);
    pub const POLICY_OUTPUT_VRR_STATE: Self = Self(0x0215);
    pub const SYNTHETIC_MOTION: Self = Self(0x0300);
    pub const SYNTHETIC_BUTTON: Self = Self(0x0301);
    pub const SYNTHETIC_KEY: Self = Self(0x0302);
    pub const SYNTHETIC_BARRIER: Self = Self(0x0303);
    pub const SYNTHETIC_INPUT_ACKNOWLEDGED: Self = Self(0x0310);
    pub const SESSION_STATE_CHANGE: Self = Self(0x0400);
    pub const SESSION_STATE_ACKNOWLEDGED: Self = Self(0x0401);
    pub const OUTPUT_STATE_QUERY: Self = Self(0x0500);
    pub const OUTPUT_CONFIGURATION_COMMIT: Self = Self(0x0501);
    pub const OUTPUT_CONFIGURATION_ACKNOWLEDGED: Self = Self(0x0502);

    #[must_use]
    pub const fn new(value: u16) -> Self {
        Self(value)
    }

    #[must_use]
    pub const fn get(self) -> u16 {
        self.0
    }
}

impl fmt::Debug for MessageType {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "MessageType({:#06x})", self.0)
    }
}

impl From<u16> for MessageType {
    fn from(value: u16) -> Self {
        Self(value)
    }
}

impl From<MessageType> for u16 {
    fn from(value: MessageType) -> Self {
        value.0
    }
}

/// Valid flags in a GWIPC envelope.
#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
#[repr(transparent)]
pub struct MessageFlags(u32);

impl MessageFlags {
    pub const REPLY: Self = Self(1 << 0);
    pub const ERROR: Self = Self(1 << 1);
    pub const ACK_REQUIRED: Self = Self(1 << 2);
    pub const SNAPSHOT_ITEM: Self = Self(1 << 3);
    pub const CRITICAL: Self = Self(1 << 4);
    pub const KNOWN_MASK: u32 = 0x1f;

    #[must_use]
    pub const fn from_bits(bits: u32) -> Option<Self> {
        if bits & !Self::KNOWN_MASK == 0 {
            Some(Self(bits))
        } else {
            None
        }
    }

    #[must_use]
    pub const fn bits(self) -> u32 {
        self.0
    }

    #[must_use]
    pub const fn contains(self, flag: Self) -> bool {
        self.0 & flag.0 == flag.0
    }

    #[must_use]
    pub const fn with(self, flag: Self) -> Self {
        Self(self.0 | flag.0)
    }
}

/// The role of a GWIPC peer.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
#[repr(u16)]
pub enum Role {
    Unknown = 0,
    ProtocolServer = 1,
    WindowManager = 2,
    Compositor = 3,
    TestProducer = 4,
    TestConsumer = 5,
    DiagnosticTool = 6,
}

/// Reason a GWIPC peer handshake was rejected.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
#[repr(u16)]
pub enum RejectReason {
    IncompatibleVersion = 1,
    RoleNotAllowed = 2,
    CapabilityMismatch = 3,
    CredentialRejected = 4,
    InvalidHello = 5,
    ServerBusy = 6,
    InternalError = 7,
}

/// Reason a peer reported a GWIPC protocol violation.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
#[repr(u16)]
pub enum ProtocolErrorCode {
    MalformedEnvelope = 1,
    MalformedPayload = 2,
    UnsupportedMessage = 3,
    MissingCapability = 4,
    InvalidDescriptorCount = 5,
    InvalidDescriptor = 6,
    OutOfOrderSequence = 7,
    UnexpectedReply = 8,
    SnapshotViolation = 9,
    LimitExceeded = 10,
    InternalError = 11,
}

impl TryFrom<u16> for ProtocolErrorCode {
    type Error = ();

    fn try_from(value: u16) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(Self::MalformedEnvelope),
            2 => Ok(Self::MalformedPayload),
            3 => Ok(Self::UnsupportedMessage),
            4 => Ok(Self::MissingCapability),
            5 => Ok(Self::InvalidDescriptorCount),
            6 => Ok(Self::InvalidDescriptor),
            7 => Ok(Self::OutOfOrderSequence),
            8 => Ok(Self::UnexpectedReply),
            9 => Ok(Self::SnapshotViolation),
            10 => Ok(Self::LimitExceeded),
            11 => Ok(Self::InternalError),
            _ => Err(()),
        }
    }
}

/// State domain carried by a GWIPC snapshot transaction.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
#[repr(u16)]
pub enum SnapshotDomain {
    Outputs = 1,
    Surfaces = 2,
    WindowPolicy = 3,
    CompleteSession = 4,
    Test = 5,
}

impl TryFrom<u16> for SnapshotDomain {
    type Error = ();

    fn try_from(value: u16) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(Self::Outputs),
            2 => Ok(Self::Surfaces),
            3 => Ok(Self::WindowPolicy),
            4 => Ok(Self::CompleteSession),
            5 => Ok(Self::Test),
            _ => Err(()),
        }
    }
}

impl TryFrom<u16> for RejectReason {
    type Error = ();

    fn try_from(value: u16) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(Self::IncompatibleVersion),
            2 => Ok(Self::RoleNotAllowed),
            3 => Ok(Self::CapabilityMismatch),
            4 => Ok(Self::CredentialRejected),
            5 => Ok(Self::InvalidHello),
            6 => Ok(Self::ServerBusy),
            7 => Ok(Self::InternalError),
            _ => Err(()),
        }
    }
}

impl TryFrom<u16> for Role {
    type Error = ();

    fn try_from(value: u16) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::Unknown),
            1 => Ok(Self::ProtocolServer),
            2 => Ok(Self::WindowManager),
            3 => Ok(Self::Compositor),
            4 => Ok(Self::TestProducer),
            5 => Ok(Self::TestConsumer),
            6 => Ok(Self::DiagnosticTool),
            _ => Err(()),
        }
    }
}

/// Negotiated GWIPC capabilities.
#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
#[repr(transparent)]
pub struct Capabilities(u64);

impl Capabilities {
    pub const FD_PASSING: Self = Self(1 << 0);
    pub const SNAPSHOTS: Self = Self(1 << 1);
    pub const OUTPUT_STATE: Self = Self(1 << 2);
    pub const SURFACE_STATE: Self = Self(1 << 3);
    pub const MEMFD_BUFFERS: Self = Self(1 << 4);
    pub const DAMAGE_REGIONS: Self = Self(1 << 5);
    pub const SCALE_METADATA: Self = Self(1 << 6);
    pub const SDR_COLOR_METADATA: Self = Self(1 << 7);
    pub const FRAME_ACKNOWLEDGEMENT: Self = Self(1 << 8);
    pub const TRACE_METADATA: Self = Self(1 << 9);
    pub const WINDOW_POLICY: Self = Self(1 << 10);
    pub const WINDOW_LIFECYCLE: Self = Self(1 << 11);
    pub const SYNTHETIC_INPUT: Self = Self(1 << 12);
    pub const SESSION_STATE: Self = Self(1 << 13);
    pub const INTERACTIVE_POLICY: Self = Self(1 << 14);
    pub const CURSOR_SURFACE: Self = Self(1 << 15);
    pub const CPU_BUFFER_SYNCHRONIZATION: Self = Self(1 << 16);
    pub const OUTPUT_MANAGEMENT: Self = Self(1 << 17);
    pub const MULTI_OUTPUT_POLICY: Self = Self(1 << 18);
    pub const SURFACE_OUTPUT_MEMBERSHIP: Self = Self(1 << 19);
    pub const SCALE_AWARE_SURFACES: Self = Self(1 << 20);
    pub const OUTPUT_CONTROL: Self = Self(1 << 21);
    pub const VRR_METADATA: Self = Self(1 << 22);
    pub const VRR_POLICY: Self = Self(1 << 23);
    pub const PRESENTATION_TIMING: Self = Self(1 << 24);
    pub const KNOWN_MASK: u64 = 0x01ff_ffff;

    #[must_use]
    pub const fn from_bits(bits: u64) -> Option<Self> {
        if bits & !Self::KNOWN_MASK == 0 {
            Some(Self(bits))
        } else {
            None
        }
    }

    /// Preserves capability bits for negotiation with a newer peer.
    #[must_use]
    pub const fn from_bits_retain(bits: u64) -> Self {
        Self(bits)
    }

    #[must_use]
    pub const fn bits(self) -> u64 {
        self.0
    }

    #[must_use]
    pub const fn contains(self, capability: Self) -> bool {
        self.0 & capability.0 == capability.0
    }

    #[must_use]
    pub const fn with(self, capability: Self) -> Self {
        Self(self.0 | capability.0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn typed_ids_do_not_change_numeric_values() {
        let output = OutputId::new(0xfeed_beef);
        assert_eq!(output.get(), 0xfeed_beef);
        assert_eq!(u64::from(output), 0xfeed_beef);
    }

    #[test]
    fn message_type_preserves_unknown_values() {
        let future = MessageType::new(0xf001);
        assert_eq!(future.get(), 0xf001);
        assert_eq!(format!("{future:?}"), "MessageType(0xf001)");
    }

    #[test]
    fn flags_reject_unknown_bits() {
        assert!(MessageFlags::from_bits(MessageFlags::KNOWN_MASK).is_some());
        assert!(MessageFlags::from_bits(1 << 31).is_none());
    }

    #[test]
    fn capabilities_compose_without_untyped_integers() {
        let capabilities = Capabilities::default()
            .with(Capabilities::FD_PASSING)
            .with(Capabilities::VRR_POLICY);
        assert!(capabilities.contains(Capabilities::FD_PASSING));
        assert!(capabilities.contains(Capabilities::VRR_POLICY));
        assert!(!capabilities.contains(Capabilities::PRESENTATION_TIMING));
        assert!(Capabilities::from_bits(1 << 63).is_none());
    }
}

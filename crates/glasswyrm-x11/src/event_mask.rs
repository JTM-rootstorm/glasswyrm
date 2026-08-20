use core::ops::{BitOr, BitOrAssign};

#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
#[repr(transparent)]
pub struct EventMask(u32);

impl EventMask {
    pub const KEY_PRESS: Self = Self(1 << 0);
    pub const KEY_RELEASE: Self = Self(1 << 1);
    pub const BUTTON_PRESS: Self = Self(1 << 2);
    pub const BUTTON_RELEASE: Self = Self(1 << 3);
    pub const ENTER_WINDOW: Self = Self(1 << 4);
    pub const LEAVE_WINDOW: Self = Self(1 << 5);
    pub const POINTER_MOTION: Self = Self(1 << 6);
    pub const POINTER_MOTION_HINT: Self = Self(1 << 7);
    pub const BUTTON1_MOTION: Self = Self(1 << 8);
    pub const BUTTON2_MOTION: Self = Self(1 << 9);
    pub const BUTTON3_MOTION: Self = Self(1 << 10);
    pub const BUTTON4_MOTION: Self = Self(1 << 11);
    pub const BUTTON5_MOTION: Self = Self(1 << 12);
    pub const BUTTON_MOTION: Self = Self(1 << 13);
    pub const KEYMAP_STATE: Self = Self(1 << 14);
    pub const EXPOSURE: Self = Self(1 << 15);
    pub const VISIBILITY_CHANGE: Self = Self(1 << 16);
    pub const STRUCTURE_NOTIFY: Self = Self(1 << 17);
    pub const RESIZE_REDIRECT: Self = Self(1 << 18);
    pub const SUBSTRUCTURE_NOTIFY: Self = Self(1 << 19);
    pub const SUBSTRUCTURE_REDIRECT: Self = Self(1 << 20);
    pub const FOCUS_CHANGE: Self = Self(1 << 21);
    pub const PROPERTY_CHANGE: Self = Self(1 << 22);
    pub const COLORMAP_CHANGE: Self = Self(1 << 23);
    pub const OWNER_GRAB_BUTTON: Self = Self(1 << 24);
    pub const ALL: Self = Self(0x01ff_ffff);
    pub const DO_NOT_PROPAGATE: Self = Self(
        Self::KEY_PRESS.0
            | Self::KEY_RELEASE.0
            | Self::BUTTON_PRESS.0
            | Self::BUTTON_RELEASE.0
            | Self::POINTER_MOTION.0
            | Self::BUTTON_MOTION.0,
    );

    #[must_use]
    pub const fn from_bits(bits: u32) -> Option<Self> {
        if bits & !Self::ALL.0 == 0 {
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
    pub const fn contains(self, other: Self) -> bool {
        self.0 & other.0 == other.0
    }

    #[must_use]
    pub const fn intersects(self, other: Self) -> bool {
        self.0 & other.0 != 0
    }
}

impl BitOr for EventMask {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

impl BitOrAssign for EventMask {
    fn bitor_assign(&mut self, rhs: Self) {
        self.0 |= rhs.0;
    }
}

impl From<EventMask> for u32 {
    fn from(value: EventMask) -> Self {
        value.bits()
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
#[repr(transparent)]
pub struct StateMask(u16);

impl StateMask {
    pub const SHIFT: Self = Self(1 << 0);
    pub const LOCK: Self = Self(1 << 1);
    pub const CONTROL: Self = Self(1 << 2);
    pub const MOD1: Self = Self(1 << 3);
    pub const MOD2: Self = Self(1 << 4);
    pub const MOD3: Self = Self(1 << 5);
    pub const MOD4: Self = Self(1 << 6);
    pub const MOD5: Self = Self(1 << 7);
    pub const BUTTON1: Self = Self(1 << 8);
    pub const BUTTON2: Self = Self(1 << 9);
    pub const BUTTON3: Self = Self(1 << 10);
    pub const BUTTON4: Self = Self(1 << 11);
    pub const BUTTON5: Self = Self(1 << 12);
    pub const ALL: Self = Self(0x1fff);

    #[must_use]
    pub const fn from_bits(bits: u16) -> Option<Self> {
        if bits & !Self::ALL.0 == 0 {
            Some(Self(bits))
        } else {
            None
        }
    }

    #[must_use]
    pub const fn bits(self) -> u16 {
        self.0
    }

    #[must_use]
    pub const fn contains(self, other: Self) -> bool {
        self.0 & other.0 == other.0
    }
}

impl BitOr for StateMask {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

impl BitOrAssign for StateMask {
    fn bitor_assign(&mut self, rhs: Self) {
        self.0 |= rhs.0;
    }
}

impl From<StateMask> for u16 {
    fn from(value: StateMask) -> Self {
        value.bits()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn event_mask_values_match_the_legacy_protocol_header() {
        assert_eq!(EventMask::KEY_PRESS.bits(), 1 << 0);
        assert_eq!(EventMask::OWNER_GRAB_BUTTON.bits(), 1 << 24);
        assert_eq!(EventMask::ALL.bits(), 0x01ff_ffff);
        assert_eq!(EventMask::DO_NOT_PROPAGATE.bits(), 0x0000_204f);
        assert!(EventMask::from_bits(EventMask::STRUCTURE_NOTIFY.bits()).is_some());
        assert!(EventMask::from_bits(1 << 25).is_none());
        assert!((EventMask::KEY_PRESS | EventMask::KEY_RELEASE).contains(EventMask::KEY_PRESS));
    }

    #[test]
    fn state_mask_values_match_the_legacy_protocol_header() {
        assert_eq!(StateMask::SHIFT.bits(), 1 << 0);
        assert_eq!(StateMask::BUTTON5.bits(), 1 << 12);
        assert_eq!(StateMask::ALL.bits(), 0x1fff);
        assert!(StateMask::from_bits(0x1fff).is_some());
        assert!(StateMask::from_bits(0x2000).is_none());
    }
}

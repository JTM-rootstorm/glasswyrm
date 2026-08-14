#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LifecycleSerialSource {
    next: u64,
    exhausted: bool,
}

impl LifecycleSerialSource {
    pub const fn new(next: u64) -> Self {
        Self {
            next,
            exhausted: false,
        }
    }

    pub const fn exhausted(self) -> bool {
        self.exhausted
    }

    pub fn take(&mut self) -> Option<u64> {
        if self.exhausted || self.next == 0 {
            self.exhausted = true;
            return None;
        }
        let result = self.next;
        if self.next == u64::MAX {
            self.exhausted = true;
        } else {
            self.next += 1;
        }
        Some(result)
    }
}

impl Default for LifecycleSerialSource {
    fn default() -> Self {
        Self::new(1)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn starts_at_one_and_is_monotonic() {
        let mut source = LifecycleSerialSource::default();
        assert_eq!(source.take(), Some(1));
        assert_eq!(source.take(), Some(2));
        assert!(!source.exhausted());
    }

    #[test]
    fn zero_and_maximum_never_wrap() {
        let mut zero = LifecycleSerialSource::new(0);
        assert_eq!(zero.take(), None);
        assert!(zero.exhausted());

        let mut maximum = LifecycleSerialSource::new(u64::MAX);
        assert_eq!(maximum.take(), Some(u64::MAX));
        assert_eq!(maximum.take(), None);
        assert!(maximum.exhausted());
    }
}

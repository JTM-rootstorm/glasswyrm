use std::fmt;

/// A stable test identity suitable for directory and artifact names.
#[derive(Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct TestId {
    name: String,
    seed: u64,
}

impl TestId {
    /// Creates an identity using a stable seed derived from `name`.
    pub fn new(name: impl Into<String>) -> Self {
        let name = name.into();
        let seed = deterministic_seed(name.as_bytes());
        Self { name, seed }
    }

    /// Creates an identity with an explicit reproducibility seed.
    pub fn with_seed(name: impl Into<String>, seed: u64) -> Self {
        Self {
            name: name.into(),
            seed,
        }
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn seed(&self) -> u64 {
        self.seed
    }

    /// Returns a stable, filesystem-safe representation.
    pub fn slug(&self) -> String {
        let mut slug = String::with_capacity(self.name.len() + 17);
        let mut last_was_separator = false;

        for character in self.name.chars() {
            let mapped = if character.is_ascii_alphanumeric() {
                character.to_ascii_lowercase()
            } else {
                '-'
            };
            if mapped == '-' {
                if last_was_separator || slug.is_empty() {
                    continue;
                }
                last_was_separator = true;
            } else {
                last_was_separator = false;
            }
            slug.push(mapped);
        }

        while slug.ends_with('-') {
            slug.pop();
        }
        if slug.is_empty() {
            slug.push_str("test");
        }
        if slug.len() > 80 {
            slug.truncate(80);
            while slug.ends_with('-') {
                slug.pop();
            }
        }

        format!("{slug}-{:016x}", self.seed)
    }
}

impl fmt::Display for TestId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.slug())
    }
}

/// FNV-1a provides a small, specified, cross-platform deterministic seed.
pub fn deterministic_seed(input: &[u8]) -> u64 {
    let mut hash = 0xcbf29ce484222325_u64;
    for byte in input {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x100000001b3);
    }
    hash
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ids_are_stable_and_safe() {
        let first = TestId::new("M14 restart / replay");
        let second = TestId::new("M14 restart / replay");
        assert_eq!(first, second);
        assert_eq!(first.slug(), second.slug());
        assert!(first.slug().starts_with("m14-restart-replay-"));
        assert!(
            first
                .slug()
                .chars()
                .all(|c| c.is_ascii_alphanumeric() || c == '-')
        );
    }

    #[test]
    fn explicit_seed_is_visible() {
        assert_eq!(
            TestId::with_seed("fixture", 0x1234).slug(),
            "fixture-0000000000001234"
        );
    }
}

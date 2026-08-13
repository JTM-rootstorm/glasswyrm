/// A signed-origin, unsigned-extent half-open rectangle.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Rectangle {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}

impl Rectangle {
    #[must_use]
    pub const fn new(x: i32, y: i32, width: u32, height: u32) -> Self {
        Self {
            x,
            y,
            width,
            height,
        }
    }

    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.width == 0 || self.height == 0
    }

    #[must_use]
    pub fn has_valid_extents(self) -> bool {
        self.right().is_some() && self.bottom().is_some()
    }

    fn right(self) -> Option<i32> {
        i64::from(self.x)
            .checked_add(i64::from(self.width))?
            .try_into()
            .ok()
    }

    fn bottom(self) -> Option<i32> {
        i64::from(self.y)
            .checked_add(i64::from(self.height))?
            .try_into()
            .ok()
    }

    #[must_use]
    pub fn intersection(self, other: Self) -> Option<Self> {
        if self.is_empty()
            || other.is_empty()
            || !self.has_valid_extents()
            || !other.has_valid_extents()
        {
            return None;
        }

        let x1 = self.x.max(other.x);
        let y1 = self.y.max(other.y);
        let x2 = self.right()?.min(other.right()?);
        let y2 = self.bottom()?.min(other.bottom()?);
        if x1 >= x2 || y1 >= y2 {
            return None;
        }

        Some(Self::new(
            x1,
            y1,
            u32::try_from(i64::from(x2) - i64::from(x1)).ok()?,
            u32::try_from(i64::from(y2) - i64::from(y1)).ok()?,
        ))
    }

    #[must_use]
    pub fn translate(self, dx: i32, dy: i32) -> Option<Self> {
        if !self.has_valid_extents() {
            return None;
        }

        let x = i64::from(self.x).checked_add(i64::from(dx))?;
        let y = i64::from(self.y).checked_add(i64::from(dy))?;
        let right = i64::from(self.right()?).checked_add(i64::from(dx))?;
        let bottom = i64::from(self.bottom()?).checked_add(i64::from(dy))?;
        let x = i32::try_from(x).ok()?;
        let y = i32::try_from(y).ok()?;
        i32::try_from(right).ok()?;
        i32::try_from(bottom).ok()?;
        Some(Self::new(x, y, self.width, self.height))
    }

    #[must_use]
    pub fn overlaps_or_is_compatibly_adjacent(self, other: Self) -> bool {
        if self.is_empty()
            || other.is_empty()
            || !self.has_valid_extents()
            || !other.has_valid_extents()
        {
            return false;
        }

        let Some(self_right) = self.right() else {
            return false;
        };
        let Some(self_bottom) = self.bottom() else {
            return false;
        };
        let Some(other_right) = other.right() else {
            return false;
        };
        let Some(other_bottom) = other.bottom() else {
            return false;
        };
        let overlaps = self.x < other_right
            && other.x < self_right
            && self.y < other_bottom
            && other.y < self_bottom;
        let horizontal = self.y == other.y
            && self.height == other.height
            && self_right >= other.x
            && other_right >= self.x;
        let vertical = self.x == other.x
            && self.width == other.width
            && self_bottom >= other.y
            && other_bottom >= self.y;
        overlaps || horizontal || vertical
    }

    #[must_use]
    pub fn bounding_union(self, other: Self) -> Option<Self> {
        if !self.has_valid_extents() || !other.has_valid_extents() {
            return None;
        }
        if self.is_empty() {
            return Some(other);
        }
        if other.is_empty() {
            return Some(self);
        }

        let x1 = self.x.min(other.x);
        let y1 = self.y.min(other.y);
        let x2 = self.right()?.max(other.right()?);
        let y2 = self.bottom()?.max(other.bottom()?);
        Some(Self::new(
            x1,
            y1,
            u32::try_from(i64::from(x2) - i64::from(x1)).ok()?,
            u32::try_from(i64::from(y2) - i64::from(y1)).ok()?,
        ))
    }
}

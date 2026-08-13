use crate::geometry::Rectangle;

/// A bounded, deterministic damage region clipped to one output.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DamageRegion {
    output_bounds: Rectangle,
    rectangles: Vec<Rectangle>,
    full_output: bool,
}

impl DamageRegion {
    /// The maximum complexity representable by the public GWIPC contract.
    pub const MAXIMUM_RECTANGLES: usize = 1024;

    #[must_use]
    pub const fn new(output_bounds: Rectangle) -> Self {
        Self {
            output_bounds,
            rectangles: Vec::new(),
            full_output: false,
        }
    }

    pub fn add(&mut self, rectangle: Rectangle) {
        if self.full_output {
            return;
        }
        let Some(mut candidate) = rectangle.intersection(self.output_bounds) else {
            return;
        };

        let mut index = 0;
        while index < self.rectangles.len() {
            if !candidate.overlaps_or_is_compatibly_adjacent(self.rectangles[index]) {
                index += 1;
                continue;
            }
            let Some(united) = candidate.bounding_union(self.rectangles[index]) else {
                self.add_full_output();
                return;
            };
            candidate = united;
            self.rectangles.remove(index);
            index = 0;
        }

        self.rectangles.push(candidate);
        if self.rectangles.len() > Self::MAXIMUM_RECTANGLES {
            self.add_full_output();
            return;
        }
        self.normalize();
    }

    pub fn add_full_output(&mut self) {
        self.rectangles.clear();
        if !self.output_bounds.is_empty() && self.output_bounds.has_valid_extents() {
            self.rectangles.push(self.output_bounds);
        }
        self.full_output = true;
    }

    #[must_use]
    pub fn rectangles(&self) -> &[Rectangle] {
        &self.rectangles
    }

    #[must_use]
    pub const fn is_full_output(&self) -> bool {
        self.full_output
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.rectangles.is_empty()
    }

    fn normalize(&mut self) {
        self.rectangles
            .sort_by_key(|rectangle| (rectangle.y, rectangle.x, rectangle.height, rectangle.width));
    }
}

use crate::ByteOrder;

pub const CORE_REQUEST_HEADER_SIZE: usize = 4;
pub const MAXIMUM_CORE_REQUEST_LENGTH_UNITS: u16 = u16::MAX;
pub const MAXIMUM_CORE_REQUEST_SIZE: usize = MAXIMUM_CORE_REQUEST_LENGTH_UNITS as usize * 4;
pub const BIG_REQUEST_HEADER_SIZE: usize = 8;
pub const MAXIMUM_BIG_REQUEST_SIZE: usize = 16 * 1024 * 1024;
pub const MAXIMUM_BIG_REQUEST_LENGTH_UNITS: u32 = (MAXIMUM_BIG_REQUEST_SIZE / 4) as u32;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FramedRequest {
    pub opcode: u8,
    pub data: u8,
    pub length_units: u32,
    pub header_size: usize,
    pub bytes: Vec<u8>,
}

impl Default for FramedRequest {
    fn default() -> Self {
        Self {
            opcode: 0,
            data: 0,
            length_units: 0,
            header_size: CORE_REQUEST_HEADER_SIZE,
            bytes: Vec::new(),
        }
    }
}

impl FramedRequest {
    #[must_use]
    pub fn body(&self) -> &[u8] {
        &self.bytes[self.header_size..]
    }

    #[must_use]
    pub fn core_size(&self) -> usize {
        self.bytes.len() - self.header_size + CORE_REQUEST_HEADER_SIZE
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RequestFrameStatus {
    NeedMore,
    Complete,
    ZeroLength,
    TooLarge,
    TruncatedInput,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RequestFrameResult {
    pub status: RequestFrameStatus,
    pub consumed: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RequestFramerConfigError {
    ZeroOrdinaryLimit,
    InvalidBigRequestsLimit,
}

#[derive(Debug)]
pub struct RequestFramer {
    order: ByteOrder,
    maximum_length_units: u16,
    maximum_big_length_units: u32,
    big_requests_enabled: bool,
    expected_size: usize,
    status: RequestFrameStatus,
    request: FramedRequest,
}

impl RequestFramer {
    pub fn new(
        order: ByteOrder,
        maximum_length_units: u16,
    ) -> Result<Self, RequestFramerConfigError> {
        if maximum_length_units == 0 {
            return Err(RequestFramerConfigError::ZeroOrdinaryLimit);
        }
        let mut request = FramedRequest::default();
        request.bytes.reserve(CORE_REQUEST_HEADER_SIZE);
        Ok(Self {
            order,
            maximum_length_units,
            maximum_big_length_units: MAXIMUM_BIG_REQUEST_LENGTH_UNITS,
            big_requests_enabled: false,
            expected_size: CORE_REQUEST_HEADER_SIZE,
            status: RequestFrameStatus::NeedMore,
            request,
        })
    }

    pub fn with_default_limit(order: ByteOrder) -> Self {
        Self::new(order, MAXIMUM_CORE_REQUEST_LENGTH_UNITS)
            .expect("the protocol maximum is nonzero")
    }

    pub fn consume(&mut self, input: &[u8]) -> RequestFrameResult {
        if self.status != RequestFrameStatus::NeedMore {
            return RequestFrameResult {
                status: self.status,
                consumed: 0,
            };
        }

        let mut consumed = 0;
        while consumed < input.len() && self.status == RequestFrameStatus::NeedMore {
            let needed = self.expected_size - self.request.bytes.len();
            let copy_size = needed.min(input.len() - consumed);
            self.request
                .bytes
                .extend_from_slice(&input[consumed..consumed + copy_size]);
            consumed += copy_size;

            if self.request.bytes.len() == CORE_REQUEST_HEADER_SIZE
                && self.expected_size == CORE_REQUEST_HEADER_SIZE
            {
                self.status = self.inspect_header();
            }
            if self.status == RequestFrameStatus::NeedMore
                && self.request.header_size == BIG_REQUEST_HEADER_SIZE
                && self.request.bytes.len() == BIG_REQUEST_HEADER_SIZE
                && self.expected_size == BIG_REQUEST_HEADER_SIZE
            {
                self.status = self.inspect_extended_header();
            }
            if self.status == RequestFrameStatus::NeedMore
                && self.request.bytes.len() == self.expected_size
            {
                self.status = RequestFrameStatus::Complete;
            }
        }
        RequestFrameResult {
            status: self.status,
            consumed,
        }
    }

    #[must_use]
    pub const fn eof(&self) -> RequestFrameStatus {
        if matches!(self.status, RequestFrameStatus::NeedMore) && !self.request.bytes.is_empty() {
            RequestFrameStatus::TruncatedInput
        } else {
            self.status
        }
    }

    #[must_use]
    pub const fn request(&self) -> &FramedRequest {
        &self.request
    }

    #[must_use]
    pub const fn expected_size(&self) -> usize {
        self.expected_size
    }

    pub fn enable_big_requests(
        &mut self,
        maximum_length_units: u32,
    ) -> Result<(), RequestFramerConfigError> {
        if !(2..=MAXIMUM_BIG_REQUEST_LENGTH_UNITS).contains(&maximum_length_units) {
            return Err(RequestFramerConfigError::InvalidBigRequestsLimit);
        }
        self.big_requests_enabled = true;
        self.maximum_big_length_units = maximum_length_units;
        Ok(())
    }

    pub fn enable_big_requests_with_default_limit(&mut self) {
        self.big_requests_enabled = true;
        self.maximum_big_length_units = MAXIMUM_BIG_REQUEST_LENGTH_UNITS;
    }

    #[must_use]
    pub const fn big_requests_enabled(&self) -> bool {
        self.big_requests_enabled
    }

    pub fn reset(&mut self) {
        self.expected_size = CORE_REQUEST_HEADER_SIZE;
        self.status = RequestFrameStatus::NeedMore;
        self.request = FramedRequest::default();
        self.request.bytes.reserve(CORE_REQUEST_HEADER_SIZE);
    }

    fn inspect_header(&mut self) -> RequestFrameStatus {
        self.request.opcode = self.request.bytes[0];
        self.request.data = self.request.bytes[1];
        self.request.length_units = u32::from(
            self.order
                .read_u16([self.request.bytes[2], self.request.bytes[3]]),
        );
        if self.request.length_units == 0 {
            if !self.big_requests_enabled {
                return RequestFrameStatus::ZeroLength;
            }
            self.request.header_size = BIG_REQUEST_HEADER_SIZE;
            self.expected_size = BIG_REQUEST_HEADER_SIZE;
            self.request.bytes.reserve(BIG_REQUEST_HEADER_SIZE);
            return RequestFrameStatus::NeedMore;
        }
        if self.request.length_units > u32::from(self.maximum_length_units) {
            return RequestFrameStatus::TooLarge;
        }
        let Some(size) = usize::try_from(self.request.length_units)
            .ok()
            .and_then(|units| units.checked_mul(4))
        else {
            return RequestFrameStatus::TooLarge;
        };
        if size < CORE_REQUEST_HEADER_SIZE {
            return RequestFrameStatus::TooLarge;
        }
        self.expected_size = size;
        self.request.bytes.reserve(size - self.request.bytes.len());
        RequestFrameStatus::NeedMore
    }

    fn inspect_extended_header(&mut self) -> RequestFrameStatus {
        self.request.length_units = self.order.read_u32([
            self.request.bytes[4],
            self.request.bytes[5],
            self.request.bytes[6],
            self.request.bytes[7],
        ]);
        if self.request.length_units < 2
            || self.request.length_units > self.maximum_big_length_units
        {
            return RequestFrameStatus::TooLarge;
        }
        let Some(size) = usize::try_from(self.request.length_units)
            .ok()
            .and_then(|units| units.checked_mul(4))
        else {
            return RequestFrameStatus::TooLarge;
        };
        if size < BIG_REQUEST_HEADER_SIZE {
            return RequestFrameStatus::TooLarge;
        }
        self.expected_size = size;
        self.request.bytes.reserve(size - self.request.bytes.len());
        RequestFrameStatus::NeedMore
    }
}

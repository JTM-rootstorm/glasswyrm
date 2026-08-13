use gwcomp_core::{
    DamageRegion, FULL_OPACITY, FramebufferView, ImageView, Pixel, PixelFormat, Rectangle,
    RenderResult, SoftwareFrame, SoftwareFrameError, blend, clear, composite,
    hash_visible_xrgb8888_measured, pack_xrgb8888, unpack_argb8888, unpack_xrgb8888,
};

#[test]
fn rectangle_and_damage_match_the_legacy_reference() {
    assert_eq!(
        Rectangle::new(-2, -2, 5, 5).intersection(Rectangle::new(0, 0, 4, 4)),
        Some(Rectangle::new(0, 0, 3, 3))
    );
    assert_eq!(
        Rectangle::new(0, 0, 1, 1).intersection(Rectangle::new(1, 0, 1, 1)),
        None
    );
    assert!(!Rectangle::new(i32::MAX, 0, 1, 1).has_valid_extents());
    assert_eq!(Rectangle::new(i32::MAX - 1, 0, 1, 1).translate(1, 0), None);

    let mut region = DamageRegion::new(Rectangle::new(0, 0, 100, 100));
    region.add(Rectangle::new(-10, 5, 20, 10));
    region.add(Rectangle::new(10, 5, 5, 10));
    assert_eq!(region.rectangles(), &[Rectangle::new(0, 5, 15, 10)]);
    region.add(Rectangle::new(50, 40, 2, 2));
    assert_eq!(
        region.rectangles(),
        &[Rectangle::new(0, 5, 15, 10), Rectangle::new(50, 40, 2, 2)]
    );

    let mut complex = DamageRegion::new(Rectangle::new(0, 0, 10_000, 2));
    for index in 0..=DamageRegion::MAXIMUM_RECTANGLES {
        complex.add(Rectangle::new((index * 2) as i32, 0, 1, 1));
    }
    assert!(complex.is_full_output());
    assert_eq!(complex.rectangles(), &[Rectangle::new(0, 0, 10_000, 2)]);
}

#[test]
fn software_pixel_math_matches_the_legacy_reference() {
    assert_eq!(
        unpack_xrgb8888(0x0012_3456),
        Pixel::new(0x12, 0x34, 0x56, 255)
    );
    assert_eq!(
        unpack_argb8888(0x8010_2030),
        Pixel::new(0x10, 0x20, 0x30, 0x80)
    );
    assert_eq!(pack_xrgb8888(Pixel::new(1, 2, 3, 0)), 0xff01_0203);
    assert!(Pixel::new(64, 32, 1, 64).is_premultiplied());
    assert!(!Pixel::new(65, 0, 0, 64).is_premultiplied());
    assert_eq!(
        blend(
            Pixel::new(255, 255, 255, 255),
            Pixel::new(0, 0, 0, 255),
            FULL_OPACITY / 2
        ),
        Pixel::new(128, 128, 128, 255)
    );
    assert_eq!(
        blend(
            Pixel::new(128, 0, 0, 128),
            Pixel::new(0, 0, 255, 255),
            FULL_OPACITY
        ),
        Pixel::new(128, 0, 127, 255)
    );
}

#[test]
fn compositing_validates_premultiplication_before_writing() {
    let mut framebuffer = [0_u8; 16];
    let mut target = FramebufferView {
        bytes: &mut framebuffer,
        width: 2,
        height: 2,
        stride: 8,
    };
    assert_eq!(
        clear(&mut target, Rectangle::new(0, 0, 2, 2)),
        RenderResult::Success
    );
    let source_bytes = 0x8080_0000_u32.to_ne_bytes();
    let source = ImageView {
        bytes: &source_bytes,
        width: 1,
        height: 1,
        stride: 4,
        format: PixelFormat::Argb8888Premultiplied,
    };
    assert_eq!(
        composite(
            &mut target,
            &source,
            Rectangle::new(0, 0, 1, 1),
            1,
            0,
            FULL_OPACITY
        ),
        RenderResult::Success
    );
    assert_eq!(
        u32::from_ne_bytes(framebuffer[4..8].try_into().unwrap()),
        0xff80_0000
    );

    let before = framebuffer;
    let invalid_bytes = 0x407f_0000_u32.to_ne_bytes();
    let invalid = ImageView {
        bytes: &invalid_bytes,
        ..source
    };
    assert_eq!(
        composite(
            &mut FramebufferView {
                bytes: &mut framebuffer,
                width: 2,
                height: 2,
                stride: 8,
            },
            &invalid,
            Rectangle::new(0, 0, 1, 1),
            0,
            0,
            FULL_OPACITY
        ),
        RenderResult::InvalidPremultipliedPixel
    );
    assert_eq!(framebuffer, before);
}

#[test]
fn software_frame_preserves_bounds_state_and_canonical_hash() {
    let mut frame = SoftwareFrame::default();
    assert_eq!(
        frame.configure(0, 2, 2),
        Err(SoftwareFrameError::ZeroOutputId)
    );
    frame.configure(7, 2, 2).unwrap();
    assert!(frame.is_enabled());
    assert_eq!(frame.spec(60_000).output_id, 7);
    frame
        .pixels_mut()
        .copy_from_slice(&[0x0011_2233, 0x8044_5566, 0xff77_8899, 0x00aa_bbcc]);
    assert_eq!(frame.visible_hash(), 0x4d14_16c2_7558_38b5);
    let measured = hash_visible_xrgb8888_measured(frame.pixels());
    assert_eq!(measured.hash, frame.visible_hash());
    assert_eq!(measured.bytes, 12);

    let old = frame.clone();
    assert_eq!(
        frame.configure(8, SoftwareFrame::MAXIMUM_WIDTH + 1, 1),
        Err(SoftwareFrameError::DimensionsOutOfRange)
    );
    assert_eq!(frame, old);
    frame.disable();
    assert!(!frame.is_enabled());
    assert!(frame.pixels().is_empty());
}

use std::collections::BTreeMap;

use gwcomp_core::{
    LogicalExtent, LogicalPoint, OutputMapping, OutputTransform, PhysicalExtent, PhysicalPoint,
    PhysicalRectangle, PixelFormat, RationalScale, Rectangle, Scene, SceneOutput, SceneSurface,
    SoftwareRenderRequest, SurfaceBuffer, SurfaceOutputMembership, SurfacePresentation,
    map_logical_point_to_native, map_logical_rectangle_to_native,
    map_native_pixel_center_to_logical, render_software_scene, transform_boundary,
    transform_rectangle,
};

fn output(
    id: u64,
    logical: Rectangle,
    physical_width: u32,
    physical_height: u32,
    scale: RationalScale,
    transform: OutputTransform,
) -> SceneOutput {
    SceneOutput {
        output_id: id,
        enabled: true,
        logical,
        physical_width,
        physical_height,
        refresh_millihertz: 60_000,
        scale,
        transform,
    }
}

fn surface(id: u64, logical: Rectangle, client_scale: u32, pixels: Vec<u32>) -> SceneSurface {
    let width = logical.width * client_scale;
    let height = logical.height * client_scale;
    SceneSurface {
        surface_id: id,
        output_id: 1,
        logical,
        stacking: 0,
        visible: true,
        clip: None,
        opacity: 65_536,
        client_buffer_scale: client_scale,
        presentation: SurfacePresentation::Ordinary,
        buffer: SurfaceBuffer {
            width,
            height,
            stride_pixels: width,
            format: PixelFormat::Xrgb8888,
            pixels,
        },
    }
}

fn membership(
    primary_output_id: u64,
    output_ids: Vec<u64>,
    scale: RationalScale,
    client_scale: u32,
    generation: u64,
) -> SurfaceOutputMembership {
    SurfaceOutputMembership {
        primary_output_id,
        output_ids,
        preferred_scale: scale,
        client_buffer_scale: client_scale,
        layout_generation: generation,
    }
}

fn render(scene: &Scene) -> gwcomp_core::SoftwareRenderResult {
    let damage = scene
        .outputs
        .values()
        .map(|output| {
            (
                output.output_id,
                vec![Rectangle::new(
                    0,
                    0,
                    output.physical_width,
                    output.physical_height,
                )],
            )
        })
        .collect();
    render_software_scene(SoftwareRenderRequest {
        scene,
        damage: &damage,
        previous: None,
        commit_id: 11,
        generation: 12,
        ordinal: 13,
    })
    .expect("M13 software scene should render")
}

#[test]
fn output_mapping_matches_fractional_and_transform_tables() {
    let native = PhysicalExtent {
        width: 5,
        height: 3,
    };
    let transforms = [
        OutputTransform::Normal,
        OutputTransform::Rotate90,
        OutputTransform::Rotate180,
        OutputTransform::Rotate270,
        OutputTransform::Flipped,
        OutputTransform::Flipped90,
        OutputTransform::Flipped180,
        OutputTransform::Flipped270,
    ];
    let expected_points = [
        PhysicalPoint { x: 1, y: 2 },
        PhysicalPoint { x: 3, y: 1 },
        PhysicalPoint { x: 4, y: 1 },
        PhysicalPoint { x: 2, y: 2 },
        PhysicalPoint { x: 4, y: 2 },
        PhysicalPoint { x: 3, y: 2 },
        PhysicalPoint { x: 1, y: 1 },
        PhysicalPoint { x: 2, y: 1 },
    ];
    let expected_rectangles = [
        PhysicalRectangle {
            x: 1,
            y: 0,
            width: 2,
            height: 1,
        },
        PhysicalRectangle {
            x: 4,
            y: 1,
            width: 1,
            height: 2,
        },
        PhysicalRectangle {
            x: 2,
            y: 2,
            width: 2,
            height: 1,
        },
        PhysicalRectangle {
            x: 0,
            y: 0,
            width: 1,
            height: 2,
        },
        PhysicalRectangle {
            x: 2,
            y: 0,
            width: 2,
            height: 1,
        },
        PhysicalRectangle {
            x: 4,
            y: 0,
            width: 1,
            height: 2,
        },
        PhysicalRectangle {
            x: 1,
            y: 2,
            width: 2,
            height: 1,
        },
        PhysicalRectangle {
            x: 0,
            y: 1,
            width: 1,
            height: 2,
        },
    ];
    for ((transform, expected_point), expected_rectangle) in transforms
        .into_iter()
        .zip(expected_points)
        .zip(expected_rectangles)
    {
        assert_eq!(
            transform_boundary(PhysicalPoint { x: 1, y: 2 }, native, transform),
            Some(expected_point)
        );
        assert_eq!(
            transform_rectangle(
                PhysicalRectangle {
                    x: 1,
                    y: 0,
                    width: 2,
                    height: 1,
                },
                native,
                transform,
            ),
            Some(expected_rectangle)
        );
    }

    let mapping = OutputMapping {
        logical_origin: LogicalPoint { x: 7, y: 9 },
        logical_extent: LogicalExtent {
            width: 4,
            height: 1,
        },
        physical_extent: PhysicalExtent {
            width: 5,
            height: 1,
        },
        scale: RationalScale {
            numerator: 5,
            denominator: 4,
        },
        transform: OutputTransform::Normal,
    };
    assert_eq!(
        map_logical_rectangle_to_native(mapping, Rectangle::new(8, 9, 2, 1)),
        Some(PhysicalRectangle {
            x: 1,
            y: 0,
            width: 3,
            height: 1,
        })
    );
    assert_eq!(
        map_logical_point_to_native(mapping, LogicalPoint { x: 11, y: 10 }),
        Some(PhysicalPoint { x: 5, y: 1 })
    );
    assert_eq!(
        map_native_pixel_center_to_logical(mapping, PhysicalPoint { x: 1, y: 0 }),
        Some(gwcomp_core::LogicalSamplePoint {
            x_numerator: 82,
            y_numerator: 94,
            denominator: 10,
        })
    );
}

#[test]
fn multi_output_frame_set_matches_the_m13_golden_hashes() {
    let mut scene = Scene {
        primary_output_id: 1,
        configuration_generation: 9,
        ..Scene::default()
    };
    scene.outputs.insert(
        1,
        output(
            1,
            Rectangle::new(0, 0, 2, 2),
            2,
            2,
            RationalScale::default(),
            OutputTransform::Normal,
        ),
    );
    scene.outputs.insert(
        2,
        output(
            2,
            Rectangle::new(2, 0, 2, 1),
            4,
            2,
            RationalScale {
                numerator: 2,
                denominator: 1,
            },
            OutputTransform::Normal,
        ),
    );
    scene.surfaces.insert(
        10,
        surface(
            10,
            Rectangle::new(0, 0, 4, 2),
            1,
            vec![
                0xffff_0000,
                0xff00_ff00,
                0xff00_00ff,
                0xffff_ffff,
                0xff00_ffff,
                0xffff_00ff,
                0xffff_ff00,
                0xff10_1010,
            ],
        ),
    );
    scene.surface_outputs.insert(
        10,
        membership(1, vec![1, 2], RationalScale::default(), 1, 9),
    );

    let rendered = render(&scene);
    assert_eq!(
        rendered.frames.outputs()[&1].frame.pixels(),
        &[0xffff_0000, 0xff00_ff00, 0xff00_ffff, 0xffff_00ff]
    );
    assert_eq!(
        rendered.frames.outputs()[&2].frame.pixels(),
        &[
            0xff00_00ff,
            0xff00_00ff,
            0xffff_ffff,
            0xffff_ffff,
            0xff00_00ff,
            0xff00_00ff,
            0xffff_ffff,
            0xffff_ffff,
        ]
    );
    assert_eq!(
        rendered.frames.outputs()[&1].visible_hash,
        0xecfd_32b5_1350_9bef
    );
    assert_eq!(
        rendered.frames.outputs()[&2].visible_hash,
        0x3a1c_ba14_0a20_0a45
    );
    assert_eq!(rendered.frames.aggregate_hash(), 0xe340_c21b_8eab_4293);
    assert!(rendered.metrics[&1].used_direct);
    assert!(rendered.metrics[&2].used_nearest);
}

#[test]
fn fractional_and_scaled_client_bilinear_pixels_match_m13() {
    let cases = [
        (
            Rectangle::new(0, 0, 4, 1),
            5,
            RationalScale {
                numerator: 5,
                denominator: 4,
            },
            1,
            vec![0xff00_0000, 0xffff_0000, 0xff00_ff00, 0xff00_00ff],
            vec![
                0xff00_0000,
                0xffb3_0000,
                0xff80_8000,
                0xff00_b34d,
                0xff00_00ff,
            ],
        ),
        (
            Rectangle::new(0, 0, 2, 1),
            2,
            RationalScale::default(),
            2,
            vec![
                0xff00_0000,
                0xffff_0000,
                0xff00_ff00,
                0xff00_00ff,
                0xff00_0000,
                0xffff_0000,
                0xff00_ff00,
                0xff00_00ff,
            ],
            vec![0xff80_0000, 0xff00_8080],
        ),
    ];
    for (logical, physical_width, scale, client_scale, pixels, expected) in cases {
        let mut scene = Scene {
            primary_output_id: 1,
            configuration_generation: 2,
            ..Scene::default()
        };
        scene.outputs.insert(
            1,
            output(
                1,
                logical,
                physical_width,
                1,
                scale,
                OutputTransform::Normal,
            ),
        );
        scene
            .surfaces
            .insert(1, surface(1, logical, client_scale, pixels));
        scene
            .surface_outputs
            .insert(1, membership(1, vec![1], scale, client_scale, 2));
        let rendered = render(&scene);
        assert_eq!(rendered.frames.outputs()[&1].frame.pixels(), expected);
        assert!(rendered.metrics[&1].used_bilinear);
    }
}

#[test]
fn all_output_transforms_match_native_orientation_goldens() {
    let cases = [
        (OutputTransform::Normal, 2, 3, vec![1, 2, 3, 4, 5, 6]),
        (OutputTransform::Rotate90, 3, 2, vec![5, 3, 1, 6, 4, 2]),
        (OutputTransform::Rotate180, 2, 3, vec![6, 5, 4, 3, 2, 1]),
        (OutputTransform::Rotate270, 3, 2, vec![2, 4, 6, 1, 3, 5]),
        (OutputTransform::Flipped, 2, 3, vec![2, 1, 4, 3, 6, 5]),
        (OutputTransform::Flipped90, 3, 2, vec![6, 4, 2, 5, 3, 1]),
        (OutputTransform::Flipped180, 2, 3, vec![5, 6, 3, 4, 1, 2]),
        (OutputTransform::Flipped270, 3, 2, vec![1, 3, 5, 2, 4, 6]),
    ];
    for (transform, width, height, expected) in cases {
        let mut scene = Scene {
            primary_output_id: 1,
            configuration_generation: 3,
            ..Scene::default()
        };
        scene.outputs.insert(
            1,
            output(
                1,
                Rectangle::new(0, 0, 2, 3),
                width,
                height,
                RationalScale::default(),
                transform,
            ),
        );
        scene.surfaces.insert(
            1,
            surface(
                1,
                Rectangle::new(0, 0, 2, 3),
                1,
                (1..=6).map(|value| 0xff00_0000 | value).collect(),
            ),
        );
        scene
            .surface_outputs
            .insert(1, membership(1, vec![1], RationalScale::default(), 1, 3));
        let actual: Vec<_> = render(&scene).frames.outputs()[&1]
            .frame
            .pixels()
            .iter()
            .map(|value| value & 0xff)
            .collect();
        assert_eq!(actual, expected, "transform {transform:?}");
    }
}

#[test]
fn previous_frames_are_preserved_outside_damage_and_hashes_are_reused() {
    let mut scene = Scene {
        primary_output_id: 1,
        configuration_generation: 4,
        ..Scene::default()
    };
    scene.outputs.insert(
        1,
        output(
            1,
            Rectangle::new(0, 0, 2, 1),
            2,
            1,
            RationalScale::default(),
            OutputTransform::Normal,
        ),
    );
    scene.surfaces.insert(
        1,
        surface(
            1,
            Rectangle::new(0, 0, 2, 1),
            1,
            vec![0xff11_2233, 0xff44_5566],
        ),
    );
    scene
        .surface_outputs
        .insert(1, membership(1, vec![1], RationalScale::default(), 1, 4));
    let first = render(&scene);
    scene.surfaces.get_mut(&1).unwrap().buffer.pixels.fill(0);
    let damage = BTreeMap::new();
    let second = render_software_scene(SoftwareRenderRequest {
        scene: &scene,
        damage: &damage,
        previous: Some(&first.frames),
        commit_id: 14,
        generation: 15,
        ordinal: 16,
    })
    .unwrap();
    assert_eq!(
        second.frames.aggregate_hash(),
        first.frames.aggregate_hash()
    );
    assert_eq!(
        second.frames.outputs()[&1].frame.pixels(),
        first.frames.outputs()[&1].frame.pixels()
    );
    assert!(second.frames.outputs()[&1].frame_hash_reused);

    let damage = BTreeMap::from([(1, vec![Rectangle::new(1, 0, 1, 1)])]);
    let third = render_software_scene(SoftwareRenderRequest {
        scene: &scene,
        damage: &damage,
        previous: Some(&second.frames),
        commit_id: 17,
        generation: 18,
        ordinal: 19,
    })
    .unwrap();
    assert_eq!(
        third.frames.outputs()[&1].frame.pixels(),
        &[0xff11_2233, 0xff00_0000]
    );
    assert_eq!(third.metrics[&1].rendered_pixels, 1);
}

use gwcomp_core::{
    OutputTransform, PixelFormat, RationalScale, Rectangle, Scene, SceneOutput, SceneSurface,
    SurfaceBuffer, SurfaceOutputMembership, SurfacePresentation,
};

fn output(id: u64, x: i32, width: u32, height: u32) -> SceneOutput {
    SceneOutput {
        output_id: id,
        enabled: true,
        logical: Rectangle::new(x, 0, width, height),
        physical_width: width,
        physical_height: height,
        refresh_millihertz: 60_000,
        scale: RationalScale::default(),
        transform: OutputTransform::Normal,
    }
}

fn surface(id: u64, logical: Rectangle, presentation: SurfacePresentation) -> SceneSurface {
    SceneSurface {
        surface_id: id,
        output_id: 1,
        logical,
        stacking: 0,
        visible: true,
        clip: None,
        opacity: 65_536,
        client_buffer_scale: 1,
        presentation,
        buffer: SurfaceBuffer {
            width: logical.width,
            height: logical.height,
            stride_pixels: logical.width,
            format: PixelFormat::Xrgb8888,
            pixels: vec![0xff00_0000; (logical.width * logical.height) as usize],
        },
    }
}

fn membership(
    output_ids: Vec<u64>,
    preferred_scale: RationalScale,
    client_buffer_scale: u32,
    generation: u64,
) -> SurfaceOutputMembership {
    SurfaceOutputMembership {
        primary_output_id: 1,
        output_ids,
        preferred_scale,
        client_buffer_scale,
        layout_generation: generation,
    }
}

fn base_scene() -> Scene {
    Scene {
        outputs: [(1, output(1, 0, 4, 4))].into(),
        primary_output_id: 1,
        configuration_generation: 7,
        ..Scene::default()
    }
}

#[test]
fn metadata_only_surface_requires_no_membership_but_orphan_membership_rejects() {
    let mut metadata = base_scene();
    metadata.surfaces.insert(
        10,
        surface(
            10,
            Rectangle::new(0, 0, 1, 1),
            SurfacePresentation::MetadataOnly,
        ),
    );
    assert!(metadata.validate().is_ok());

    let mut orphan = base_scene();
    orphan
        .surface_outputs
        .insert(99, membership(vec![1], RationalScale::default(), 1, 7));
    assert!(orphan.validate().is_err());
}

#[test]
fn membership_is_unique_geometric_and_sorted_like_the_legacy_oracle() {
    let mut scene = base_scene();
    scene.outputs.insert(2, output(2, 4, 4, 4));
    scene.surfaces.insert(
        10,
        surface(
            10,
            Rectangle::new(0, 0, 4, 4),
            SurfacePresentation::Ordinary,
        ),
    );
    scene
        .surface_outputs
        .insert(10, membership(vec![1, 1], RationalScale::default(), 1, 7));
    assert!(scene.validate().is_err(), "duplicate membership IDs reject");

    scene
        .surface_outputs
        .insert(10, membership(vec![1, 2], RationalScale::default(), 1, 7));
    assert!(
        scene.validate().is_err(),
        "non-intersecting membership IDs reject"
    );

    scene.surfaces.get_mut(&10).unwrap().logical = Rectangle::new(0, 0, 8, 4);
    scene.surfaces.get_mut(&10).unwrap().buffer.width = 8;
    scene.surfaces.get_mut(&10).unwrap().buffer.stride_pixels = 8;
    scene.surfaces.get_mut(&10).unwrap().buffer.pixels = vec![0xff00_0000; 32];
    assert!(
        scene.validate().is_ok(),
        "canonical geometric order accepts"
    );

    scene.surface_outputs.get_mut(&10).unwrap().output_ids = vec![2, 1];
    assert!(
        scene.validate().is_err(),
        "non-canonical geometry order rejects"
    );
}

#[test]
fn membership_scale_client_scale_and_generation_match_the_scene() {
    let mut scene = base_scene();
    scene.surfaces.insert(
        10,
        surface(
            10,
            Rectangle::new(0, 0, 2, 2),
            SurfacePresentation::Ordinary,
        ),
    );
    scene
        .surface_outputs
        .insert(10, membership(vec![1], RationalScale::default(), 1, 7));
    assert!(scene.validate().is_ok());

    let mut bad_scale = scene.clone();
    bad_scale
        .surface_outputs
        .get_mut(&10)
        .unwrap()
        .preferred_scale = RationalScale {
        numerator: 2,
        denominator: 1,
    };
    assert!(bad_scale.validate().is_err());

    let mut bad_client_scale = scene.clone();
    bad_client_scale
        .surface_outputs
        .get_mut(&10)
        .unwrap()
        .client_buffer_scale = 2;
    assert!(bad_client_scale.validate().is_err());

    let mut stale = scene;
    stale
        .surface_outputs
        .get_mut(&10)
        .unwrap()
        .layout_generation = 6;
    assert!(stale.validate().is_err());
}

#[test]
fn hidden_surface_accepts_empty_geometric_membership() {
    let mut scene = base_scene();
    let mut hidden = surface(
        10,
        Rectangle::new(0, 0, 2, 2),
        SurfacePresentation::Ordinary,
    );
    hidden.visible = false;
    scene.surfaces.insert(10, hidden);
    scene
        .surface_outputs
        .insert(10, membership(Vec::new(), RationalScale::default(), 1, 7));
    assert!(scene.validate().is_ok());
}

#[test]
fn enabled_outputs_must_not_overlap_or_exceed_the_total_pixel_limit() {
    let mut overlapping = base_scene();
    overlapping.outputs.insert(2, output(2, 3, 4, 4));
    assert!(overlapping.validate().is_err());

    let mut oversized = Scene {
        primary_output_id: 1,
        configuration_generation: 8,
        ..Scene::default()
    };
    for id in 1..=5 {
        let mut item = output(id, i32::try_from((id - 1) * 1024).unwrap(), 1024, 1024);
        item.physical_width = 4096;
        item.physical_height = 4096;
        item.scale = RationalScale {
            numerator: 4,
            denominator: 1,
        };
        oversized.outputs.insert(id, item);
    }
    assert!(oversized.validate().is_err());
}

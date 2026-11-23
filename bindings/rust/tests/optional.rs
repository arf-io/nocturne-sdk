#[cfg(feature = "graph")]
#[test]
fn graph_can_mount_in_the_safe_tree() {
    use nocturne::{raw, MountOptions, Size, Tree};
    let mut options = unsafe { raw::nt_graph_view_options_default() };
    assert_eq!(options.preferred.columns, 32);
    assert_eq!(options.width.emoji, raw::NT_WIDTH_TWO);
    let graph_node = raw::NtGraphNode {
        id: 1,
        pinned: true,
        position: raw::NtPoint { x: 0.5, y: 0.5 },
    };
    options.data.nodes = &graph_node;
    options.data.node_count = 1;
    let mount = MountOptions {
        enabled: true,
        focusable: false,
        hit_testable: true,
    };
    let mut tree = Tree::new().unwrap();
    let graph = unsafe {
        tree.mount_native(None, |tree, parent, out| {
            raw::nt_graph_view_mount(tree, parent, mount, &options, out)
        })
    }
    .unwrap();
    let size = Size {
        columns: 10,
        rows: 4,
    };
    tree.publish(size).unwrap();
    assert!(tree.geometry(&graph).unwrap().placed);
    options.layout.kind = raw::NT_GRAPH_RADIAL;
    unsafe {
        tree.call_native(&graph, |tree, id| {
            raw::nt_graph_view_set(tree, id, &options)
        })
    }
    .unwrap();
    tree.publish(size).unwrap();
    assert!(tree.geometry(&graph).unwrap().placed);
    tree.remove(&graph).unwrap();
}

#[cfg(feature = "spatial")]
#[test]
fn spatial_vector_links() {
    use nocturne::raw;
    let a = raw::NtVec3 {
        x: 1.,
        y: 2.,
        z: 3.,
    };
    let b = raw::NtVec3 {
        x: 3.,
        y: 2.,
        z: 1.,
    };
    let mut result = 0.;
    assert_eq!(
        unsafe { raw::nt_spatial_vec_dot(a, b, &mut result) },
        raw::NT_RESULT_OK
    );
    assert_eq!(result, 10.);
}

#[cfg(feature = "simulation")]
#[test]
fn simulation_links_with_spatial() {
    use nocturne::raw;
    let mut sample = std::mem::MaybeUninit::uninit();
    let status = unsafe {
        raw::nt_water_wave_sample(
            std::ptr::null(),
            0,
            raw::NtPoint { x: 0., y: 0. },
            0.,
            sample.as_mut_ptr(),
        )
    };
    assert_eq!(status, raw::NT_RESULT_OK);
    let sample = unsafe { sample.assume_init() };
    assert_eq!(sample.height, 0.);
    assert_eq!(sample.normal.y, 1.);
}

#[cfg(feature = "posix-host")]
#[test]
fn posix_host_destroy_links() {
    unsafe { nocturne::raw::nt_posix_host_destroy(std::ptr::null_mut()) };
}

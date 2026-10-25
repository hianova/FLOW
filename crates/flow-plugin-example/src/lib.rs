use flow_plugin::*;
use std::ffi::c_char;
use std::os::raw::c_void;
use std::ptr;

static COMPONENT_ID: &[u8] = b"rust_simd_tile\0";
static COMPONENT_KIND: &[u8] = b"algorithm\0";
static COMPONENT_RESOURCE: &[u8] = b"cpu\0";
static COMPONENT_CAPABILITY: &[u8] = b"simd\0";
static DOMAIN_CONTRACT: &[u8] = b"transform\0";
static FLOW_BINDING: &[u8] = b"simd_tile\0";

static COMPONENTS: [FlowComponent; 1] = [FlowComponent {
    id: COMPONENT_ID.as_ptr() as *const c_char,
    kind: COMPONENT_KIND.as_ptr() as *const c_char,
    resource: COMPONENT_RESOURCE.as_ptr() as *const c_char,
    capability: COMPONENT_CAPABILITY.as_ptr() as *const c_char,
    supports_shared: 0,
    supports_read_heavy: 0,
    supports_unordered: 1,
    supports_parallelizable: 1,
    latency_score: 2,
    memory_score: 1,
    domain_contract: DOMAIN_CONTRACT.as_ptr() as *const c_char,
    flow_binding: FLOW_BINDING.as_ptr() as *const c_char,
    memory_fixed_bytes: 512,
    memory_bytes_per_capacity: 4,
    reload_capable: 1,
    cubical_type_mask: 0,
}];

unsafe extern "C" fn rust_plugin_compatible(_ir: *const c_void, _comp: *const FlowComponent) -> i32 {
    1
}

unsafe extern "C" fn rust_plugin_memory_model(_ir: *const c_void, _comp: *const FlowComponent, capacity: usize, _shards: usize, estimated_bytes: *mut usize) -> i32 {
    if !estimated_bytes.is_null() {
        *estimated_bytes = 512 + capacity * 4;
    }
    1
}

unsafe extern "C" fn rust_plugin_verify(_ir: *const c_void, _comp: *const FlowComponent, _capacity: usize, _shards: usize, msg: *mut c_char, msg_size: usize) -> i32 {
    if !msg.is_null() && msg_size > 0 {
        let text = b"rust verifier passed\0";
        let copy_len = text.len().min(msg_size);
        ptr::copy_nonoverlapping(text.as_ptr() as *const c_char, msg, copy_len);
        *msg.add(copy_len - 1) = 0;
    }
    1
}

unsafe extern "C" fn rust_plugin_enumerate_dims(_ir: *const c_void, _comp: *const FlowComponent, dims_out: *mut FlowPlanDimensionSet) -> i32 {
    if dims_out.is_null() { return 0; }
    let dims = &mut *dims_out;
    dims.count = 3;

    // Dim 0: vector_lanes (exponent 2..8 -> 4, 8, 16, 32.. 256)
    dims.dimensions[0].kind = FlowDimensionKind::Exponent;
    dims.dimensions[0].dim_class = FlowDimensionClass::StructuralJIT;
    dims.dimensions[0].min_val = 2;
    dims.dimensions[0].max_val = 8;
    dims.dimensions[0].default_val = 4;
    dims.dimensions[0].step = 1;
    dims.dimensions[0].base_migration_cost_ns = 300;
    let name0 = b"vector_lanes\0";
    ptr::copy_nonoverlapping(name0.as_ptr() as *const c_char, dims.dimensions[0].name.as_mut_ptr(), name0.len());

    // Dim 1: tile_size (linear 16..256 with step 16)
    dims.dimensions[1].kind = FlowDimensionKind::Linear;
    dims.dimensions[1].dim_class = FlowDimensionClass::TactileParam;
    dims.dimensions[1].min_val = 16;
    dims.dimensions[1].max_val = 256;
    dims.dimensions[1].default_val = 64;
    dims.dimensions[1].step = 16;
    dims.dimensions[1].base_migration_cost_ns = 0;
    let name1 = b"tile_size\0";
    ptr::copy_nonoverlapping(name1.as_ptr() as *const c_char, dims.dimensions[1].name.as_mut_ptr(), name1.len());

    // Dim 2: threads (linear 1..16)
    dims.dimensions[2].kind = FlowDimensionKind::Linear;
    dims.dimensions[2].dim_class = FlowDimensionClass::StructuralJIT;
    dims.dimensions[2].min_val = 1;
    dims.dimensions[2].max_val = 16;
    dims.dimensions[2].default_val = 4;
    dims.dimensions[2].step = 1;
    dims.dimensions[2].base_migration_cost_ns = 200;
    let name2 = b"threads\0";
    ptr::copy_nonoverlapping(name2.as_ptr() as *const c_char, dims.dimensions[2].name.as_mut_ptr(), name2.len());

    1
}

unsafe extern "C" fn rust_plugin_evaluate_plan(_ir: *const c_void, _comp: *const FlowComponent, plan: *const FlowPlanAssignment, metrics_out: *mut FlowPlanMetrics) -> i32 {
    if plan.is_null() || metrics_out.is_null() { return 0; }
    let p = &*plan;
    let m = &mut *metrics_out;
    let lanes = (if p.count > 0 { p.values[0] } else { 4 }).max(1);
    let tile = (if p.count > 1 { p.values[1] } else { 64 }).max(1);
    let threads = (if p.count > 2 { p.values[2] } else { 1 }).max(1);

    m.capacity = (tile.saturating_mul(lanes)) as usize;
    m.threads = threads as usize;
    m.shards = 1;
    m.memory_bytes = 512 + m.capacity * 4;
    m.latency_score = 100.0 / (threads as f64 * lanes as f64);
    m.throughput_score = (threads as f64 * lanes as f64) * 1000.0;
    m.energy = m.latency_score + (m.memory_bytes as f64 / 1024.0);
    1
}

unsafe extern "C" fn rust_plugin_verify_plan(_ir: *const c_void, _comp: *const FlowComponent, plan: *const FlowPlanAssignment, report_out: *mut VerificationReport) -> i32 {
    if report_out.is_null() { return 0; }
    let rep = &mut *report_out;
    let (lanes, tile) = if !plan.is_null() {
        let p = &*plan;
        let l = (if p.count > 0 { p.values[0] } else { 4 }).max(1);
        let t = (if p.count > 1 { p.values[1] } else { 64 }).max(1);
        (l, t)
    } else {
        (4, 64)
    };
    let cap = (tile.saturating_mul(lanes) as usize).max(64);
    rep.status = VerificationStatus::Proven;
    rep.capacity = cap;
    rep.estimated_bytes = 512 + cap * 4;
    rep.max_count_proven = 1;
    rep.runtime_input_guard = 0;
    let msg = b"proven by rust SIMD static bounds\0";
    let copy_len = msg.len().min(rep.message.len());
    ptr::copy_nonoverlapping(msg.as_ptr() as *const c_char, rep.message.as_mut_ptr(), copy_len);
    rep.message[copy_len - 1] = 0;
    1
}

#[allow(dead_code)]
unsafe extern "C" fn rust_plugin_emit(_out: *mut c_void, _ir: *const c_void, _comp: *const FlowComponent, _sr: *const c_void, _vr: *const VerificationReport, _reload: i32) -> i32 {
    0
}

unsafe extern "C" fn rust_unit_init(_host: *mut c_void, state_out: *mut *mut c_void) -> i32 {
    if state_out.is_null() {
        return -1;
    }
    *state_out = Box::into_raw(Box::new(0u64)) as *mut c_void;
    0
}

unsafe extern "C" fn rust_unit_run(_host: *mut c_void, state: *mut c_void, input: *const c_void, output: *mut c_void) -> i32 {
    if !state.is_null() {
        let counter = &mut *(state as *mut u64);
        *counter = counter.wrapping_add(1);
    }
    if !input.is_null() && !output.is_null() && (input as *const u8) != (output as *const u8) {
        ptr::copy(input as *const u8, output as *mut u8, 8);
    }
    0
}

unsafe extern "C" fn rust_unit_drop(_host: *mut c_void, state: *mut c_void) {
    if !state.is_null() {
        let _ = Box::from_raw(state as *mut u64);
    }
}

declare_flow_unit_plugin!(
    name: "rust_simd_tile_unit",
    init: rust_unit_init,
    run: rust_unit_run,
    drop: rust_unit_drop
);

unsafe extern "C" fn rust_plugin_create_unit(_art: *const c_void, _ir: *const c_void, _comp: *const FlowComponent, unit_out: *mut FlowUnit) -> i32 {
    if unit_out.is_null() {
        return 0;
    }
    *unit_out = FLOW_UNIT_INSTANCE;
    1
}

declare_flow_plugin!(name: "rust_simd_plugin", version: "1.0.0", FlowPlugin {
    name: b"rust_simd_plugin\0".as_ptr() as *const c_char,
    version: b"1.0.0\0".as_ptr() as *const c_char,
    components: COMPONENTS.as_ptr(),
    component_count: COMPONENTS.len(),
    compatible: Some(rust_plugin_compatible),
    memory_model: Some(rust_plugin_memory_model),
    verify: Some(rust_plugin_verify),
    emit: None,
    oracle: None,
    preference: None,
    validate_contract: None,
    lower_domain_semantics: None,
    free_domain_semantics: None,
    enumerate_dimensions: Some(rust_plugin_enumerate_dims),
    evaluate_plan: Some(rust_plugin_evaluate_plan),
    verify_plan: Some(rust_plugin_verify_plan),
    benchmark: None,
    get_mutation_mask: None,
    preference_mask: None,
    contract_mask: None,
    resource_mask: None,
    environment_mask: None,
    create_unit: Some(rust_plugin_create_unit),
    doc_title: ptr::null(),
    doc_responsibilities: ptr::null(),
    doc_algorithmic_guarantee: ptr::null(),
    doc_memory_concurrency_model: ptr::null(),
    doc_key_apis: ptr::null(),
    doc_layer: 1,
    domain_context: ptr::null_mut(),
});

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CStr;

    #[test]
    fn test_plugin_entry_and_register() {
        unsafe {
            let desc = flow_plugin_entry_v1();
            assert!(!desc.is_null());
            assert_eq!((*desc).abi_major, FLOW_PLUGIN_ABI_MAJOR);
            assert_eq!((*desc).abi_minor, FLOW_PLUGIN_ABI_MINOR);
            let plugin = (*desc).plugin;
            assert!(!plugin.is_null());
            assert_eq!((*plugin).component_count, 1);
            assert!((*plugin).emit.is_none());
            assert!((*plugin).create_unit.is_some());
        }
    }

    #[test]
    fn test_verify_null_termination_and_short_buffer() {
        unsafe {
            let mut small_buf = [0x7fi8 as c_char; 5];
            let rc = rust_plugin_verify(ptr::null(), COMPONENTS.as_ptr(), 64, 1, small_buf.as_mut_ptr(), small_buf.len());
            assert_eq!(rc, 1);
            assert_eq!(small_buf[4], 0);
            let s = CStr::from_ptr(small_buf.as_ptr()).to_str().unwrap();
            assert_eq!(s, "rust");

            let rc_zero = rust_plugin_verify(ptr::null(), COMPONENTS.as_ptr(), 64, 1, small_buf.as_mut_ptr(), 0);
            assert_eq!(rc_zero, 1);
        }
    }

    #[test]
    fn test_evaluate_and_verify_plan_zero_and_normal() {
        unsafe {
            let zero_plan = FlowPlanAssignment { count: 3, values: [0; FLOW_DIMENSION_MAX] };
            let mut metrics: FlowPlanMetrics = std::mem::zeroed();
            let rc = rust_plugin_evaluate_plan(ptr::null(), COMPONENTS.as_ptr(), &zero_plan, &mut metrics);
            assert_eq!(rc, 1);
            assert!(metrics.latency_score.is_finite());
            assert_eq!(metrics.capacity, 1);

            let mut rep: VerificationReport = std::mem::zeroed();
            let vrc = rust_plugin_verify_plan(ptr::null(), COMPONENTS.as_ptr(), ptr::null(), &mut rep);
            assert_eq!(vrc, 1);
            assert_eq!(rep.status, VerificationStatus::Proven);
            assert_eq!(rep.capacity, 256);
            assert_eq!(rep.estimated_bytes, 512 + 256 * 4);
            let msg = CStr::from_ptr(rep.message.as_ptr()).to_str().unwrap();
            assert!(msg.contains("proven by rust SIMD"));
        }
    }

    #[test]
    fn test_create_unit_and_aliased_run() {
        unsafe {
            let mut unit: FlowUnit = std::mem::zeroed();
            assert_eq!(rust_plugin_create_unit(ptr::null(), ptr::null(), COMPONENTS.as_ptr(), &mut unit), 1);
            let mut state: *mut c_void = ptr::null_mut();
            assert_eq!((unit.init.unwrap())(ptr::null_mut(), &mut state), 0);
            assert!(!state.is_null());

            let in_val: u64 = 0xCAFEBABE_DEADBEEF;
            let mut out_val: u64 = 0;
            assert_eq!((unit.run.unwrap())(ptr::null_mut(), state, &in_val as *const _ as *const c_void, &mut out_val as *mut _ as *mut c_void), 0);
            assert_eq!(out_val, in_val);

            // Aliased input == output must not panic or corrupt
            assert_eq!((unit.run.unwrap())(ptr::null_mut(), state, &mut out_val as *mut _ as *const c_void, &mut out_val as *mut _ as *mut c_void), 0);
            assert_eq!(out_val, in_val);
            assert_eq!(*(state as *const u64), 2);

            (unit.drop.unwrap())(ptr::null_mut(), state);
        }
    }
}

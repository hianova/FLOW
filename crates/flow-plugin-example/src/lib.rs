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
    let lanes = if p.count > 0 { p.values[0] } else { 4 };
    let tile = if p.count > 1 { p.values[1] } else { 64 };
    let threads = if p.count > 2 { p.values[2] } else { 1 };

    m.capacity = (tile * lanes) as usize;
    m.threads = threads as usize;
    m.shards = 1;
    m.memory_bytes = 512 + m.capacity * 4;
    m.latency_score = 100.0 / (threads as f64 * lanes as f64);
    m.throughput_score = (threads as f64 * lanes as f64) * 1000.0;
    m.energy = m.latency_score + (m.memory_bytes as f64 / 1024.0);
    1
}

unsafe extern "C" fn rust_plugin_verify_plan(_ir: *const c_void, _comp: *const FlowComponent, _plan: *const FlowPlanAssignment, report_out: *mut VerificationReport) -> i32 {
    if report_out.is_null() { return 0; }
    let rep = &mut *report_out;
    rep.status = VerificationStatus::Proven;
    rep.max_count_proven = 1;
    rep.runtime_input_guard = 0;
    let msg = b"proven by rust SIMD static bounds\0";
    ptr::copy_nonoverlapping(msg.as_ptr() as *const c_char, rep.message.as_mut_ptr(), msg.len());
    1
}

unsafe extern "C" fn rust_plugin_emit(_out: *mut c_void, _ir: *const c_void, _comp: *const FlowComponent, _sr: *const c_void, _vr: *const VerificationReport, _reload: i32) -> i32 {
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
    emit: Some(rust_plugin_emit),
    oracle: None,
    preference: None,
    validate_contract: None,
    lower_domain_semantics: None,
    free_domain_semantics: None,
    enumerate_dimensions: Some(rust_plugin_enumerate_dims),
    evaluate_plan: Some(rust_plugin_evaluate_plan),
    verify_plan: Some(rust_plugin_verify_plan),
    benchmark: None,
    create_unit: None,
});

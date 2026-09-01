//! FLOW Plugin SDK for Rust
//!
//! Provides safe abstractions and declarative macros for building FLOW dynamic modules (DSOs).

use std::ffi::c_char;
use std::os::raw::c_void;
use std::sync::atomic::AtomicU64;

pub const FLOW_PLUGIN_ABI_MAJOR: u32 = 1;
pub const FLOW_PLUGIN_ABI_MINOR: u32 = 0;
pub const FLOW_DIMENSION_MAX: usize = 16;
pub const FLOW_DIM_NAME_MAX: usize = 32;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub enum FlowDimensionKind {
    Exponent = 0,
    Linear = 1,
    Discrete = 2,
    Boolean = 3,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum FlowDimensionClass {
    TactileParam = 0,    // Real-time parameter (0 migration penalty, no JIT required)
    StructuralJIT = 1,   // Structural gene (requires JIT compilation & state migration)
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct FlowPlanDimension {
    pub name: [c_char; FLOW_DIM_NAME_MAX],
    pub kind: FlowDimensionKind,
    pub dim_class: FlowDimensionClass,
    pub min_val: u64,
    pub max_val: u64,
    pub step: u64,
    pub default_val: u64,
    pub base_migration_cost_ns: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct FlowPlanDimensionSet {
    pub count: usize,
    pub dimensions: [FlowPlanDimension; FLOW_DIMENSION_MAX],
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct FlowPlanAssignment {
    pub count: usize,
    pub values: [u64; FLOW_DIMENSION_MAX],
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct FlowPlanMetrics {
    pub capacity: usize,
    pub threads: usize,
    pub shards: usize,
    pub memory_bytes: usize,
    pub latency_score: f64,
    pub throughput_score: f64,
    pub energy: f64,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum VerificationStatus {
    Proven = 0,
    RuntimeCheck = 1,
    CompileError = 2,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct VerificationReport {
    pub status: VerificationStatus,
    pub capacity: usize,
    pub estimated_bytes: usize,
    pub max_count_proven: i32,
    pub runtime_input_guard: i32,
    pub message: [c_char; 128],
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct FlowComponent {
    pub id: *const c_char,
    pub kind: *const c_char,
    pub resource: *const c_char,
    pub capability: *const c_char,
    pub supports_shared: i32,
    pub supports_read_heavy: i32,
    pub supports_unordered: i32,
    pub supports_parallelizable: i32,
    pub latency_score: i32,
    pub memory_score: i32,
    pub domain_contract: *const c_char,
    pub flow_binding: *const c_char,
    pub memory_fixed_bytes: usize,
    pub memory_bytes_per_capacity: usize,
    pub reload_capable: i32,
}

unsafe impl Sync for FlowComponent {}
unsafe impl Send for FlowComponent {}

#[repr(C)]
pub struct FlowPlugin {
    pub name: *const c_char,
    pub version: *const c_char,
    pub components: *const FlowComponent,
    pub component_count: usize,
    pub compatible: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent) -> i32>,
    pub memory_model: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent, usize, usize, *mut usize) -> i32>,
    pub verify: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent, usize, usize, *mut c_char, usize) -> i32>,
    pub emit: Option<unsafe extern "C" fn(*mut c_void, *const c_void, *const FlowComponent, *const c_void, *const VerificationReport, i32) -> i32>,
    pub oracle: Option<unsafe extern "C" fn(*const c_char, *mut c_char, usize) -> i32>,
    pub preference: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent) -> i32>,
    pub validate_contract: Option<unsafe extern "C" fn(*const c_void, *const FlowPlugin, *mut c_char, usize) -> i32>,
    pub lower_domain_semantics: Option<unsafe extern "C" fn(*const c_void, *mut c_void, *const FlowPlugin)>,
    pub free_domain_semantics: Option<unsafe extern "C" fn(*mut c_void)>,
    pub enumerate_dimensions: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent, *mut FlowPlanDimensionSet) -> i32>,
    pub evaluate_plan: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent, *const FlowPlanAssignment, *mut FlowPlanMetrics) -> i32>,
    pub verify_plan: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent, *const FlowPlanAssignment, *mut VerificationReport) -> i32>,
    pub benchmark: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent, *const FlowPlanAssignment) -> u64>,
    pub create_unit: Option<unsafe extern "C" fn(*const c_void, *const c_void, *const FlowComponent, *mut c_void) -> i32>,
}

unsafe impl Sync for FlowPlugin {}
unsafe impl Send for FlowPlugin {}

#[repr(C)]
pub struct FlowPluginDescriptor {
    pub abi_major: u32,
    pub abi_minor: u32,
    pub descriptor_size: usize,
    pub module_name: *const c_char,
    pub module_version: *const c_char,
    pub module_hash: u64,
    pub plugin: *const FlowPlugin,
    pub dso_handle: *mut c_void,
    pub active_references: AtomicU64,
}

unsafe impl Sync for FlowPluginDescriptor {}
unsafe impl Send for FlowPluginDescriptor {}

#[macro_export]
macro_rules! declare_flow_plugin {
    (name: $name:expr, version: $ver:expr, $plugin_instance:expr) => {
        static PLUGIN_NAME: &[u8] = concat!($name, "\0").as_bytes();
        static PLUGIN_VER: &[u8] = concat!($ver, "\0").as_bytes();
        static PLUGIN_RAW: $crate::FlowPlugin = $plugin_instance;

        #[no_mangle]
        pub static FLOW_PLUGIN_DESC: $crate::FlowPluginDescriptor = $crate::FlowPluginDescriptor {
            abi_major: $crate::FLOW_PLUGIN_ABI_MAJOR,
            abi_minor: $crate::FLOW_PLUGIN_ABI_MINOR,
            descriptor_size: std::mem::size_of::<$crate::FlowPluginDescriptor>(),
            module_name: PLUGIN_NAME.as_ptr() as *const std::ffi::c_char,
            module_version: PLUGIN_VER.as_ptr() as *const std::ffi::c_char,
            module_hash: 0x464c4f575f525553, /* "FLOW_RUS" */
            plugin: &PLUGIN_RAW as *const $crate::FlowPlugin,
            dso_handle: std::ptr::null_mut(),
            active_references: std::sync::atomic::AtomicU64::new(0),
        };

        #[no_mangle]
        pub extern "C" fn flow_plugin_entry_v1() -> *const $crate::FlowPluginDescriptor {
            &FLOW_PLUGIN_DESC as *const $crate::FlowPluginDescriptor
        }
    };
}

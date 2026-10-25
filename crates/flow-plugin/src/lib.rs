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
    pub cubical_type_mask: u64,
}

unsafe impl Sync for FlowComponent {}
unsafe impl Send for FlowComponent {}

#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum FlowLayoutKind {
    Default = 0,
    Aos = 1,
    Soa = 2,
    Columnar = 3,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct FlowUnit {
    pub abi_version: u32,
    pub semantic_schema_hash: u64,
    pub constraint_hash: u64,
    pub capability_hash: u64,
    pub name: *const c_char,
    pub layout: FlowLayoutKind,
    pub supports_snapshot_cow: i32,
    pub init: Option<unsafe extern "C" fn(*mut c_void, *mut *mut c_void) -> i32>,
    pub run: Option<unsafe extern "C" fn(*mut c_void, *mut c_void, *const c_void, *mut c_void) -> i32>,
    pub apply: Option<unsafe extern "C" fn(*mut c_void, *mut c_void, *const c_void) -> i32>,
    pub migrate: Option<unsafe extern "C" fn(*mut c_void, *const c_void, *mut c_void) -> i32>,
    pub drop: Option<unsafe extern "C" fn(*mut c_void, *mut c_void)>,
    pub schema: *const c_void,
}

unsafe impl Sync for FlowUnit {}
unsafe impl Send for FlowUnit {}

pub const FLOW_RELOAD_ABI_VERSION: u32 = 3;

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
    pub get_mutation_mask: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent, *const FlowPlanDimensionSet) -> u64>,
    pub preference_mask: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent, *const FlowPlanDimensionSet) -> u64>,
    pub contract_mask: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent, *const FlowPlanDimensionSet) -> u64>,
    pub resource_mask: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent, *const FlowPlanDimensionSet, usize) -> u64>,
    pub environment_mask: Option<unsafe extern "C" fn(*const c_void, *const FlowComponent, *const FlowPlanDimensionSet, *const c_void) -> u64>,
    pub create_unit: Option<unsafe extern "C" fn(*const c_void, *const c_void, *const FlowComponent, *mut FlowUnit) -> i32>,
    pub doc_title: *const c_char,
    pub doc_responsibilities: *const c_char,
    pub doc_algorithmic_guarantee: *const c_char,
    pub doc_memory_concurrency_model: *const c_char,
    pub doc_key_apis: *const c_char,
    pub doc_layer: u32,
    pub domain_context: *mut c_void,
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
    pub abi_v2: *const c_void,
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
            abi_v2: std::ptr::null(),
            dso_handle: std::ptr::null_mut(),
            active_references: std::sync::atomic::AtomicU64::new(0),
        };

        #[no_mangle]
        pub extern "C" fn flow_plugin_entry_v1() -> *const $crate::FlowPluginDescriptor {
            &FLOW_PLUGIN_DESC as *const $crate::FlowPluginDescriptor
        }
    };
}

#[macro_export]
macro_rules! declare_flow_unit_plugin {
    (
        name: $name:expr,
        init: $init_fn:expr,
        run: $run_fn:expr,
        drop: $drop_fn:expr
    ) => {
        static UNIT_NAME: &[u8] = concat!($name, "\0").as_bytes();
        static FLOW_UNIT_INSTANCE: $crate::FlowUnit = $crate::FlowUnit {
            abi_version: $crate::FLOW_RELOAD_ABI_VERSION,
            semantic_schema_hash: 0,
            constraint_hash: 0,
            capability_hash: 0,
            name: UNIT_NAME.as_ptr() as *const std::ffi::c_char,
            layout: $crate::FlowLayoutKind::Default,
            supports_snapshot_cow: 1,
            init: Some($init_fn),
            run: Some($run_fn),
            apply: None,
            migrate: None,
            drop: Some($drop_fn),
            schema: std::ptr::null(),
        };

        #[no_mangle]
        pub extern "C" fn flow_unit_entry_v1() -> *const $crate::FlowUnit {
            &FLOW_UNIT_INSTANCE as *const $crate::FlowUnit
        }
    };
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::{align_of, size_of};

    #[test]
    fn test_repr_c_abi_layouts_and_versions() {
        assert_eq!(FLOW_PLUGIN_ABI_MAJOR, 1);
        assert_eq!(FLOW_PLUGIN_ABI_MINOR, 0);
        assert_eq!(FLOW_RELOAD_ABI_VERSION, 3);
        assert_eq!(size_of::<FlowPluginDescriptor>(), 72);
        assert_eq!(align_of::<FlowPluginDescriptor>(), 8);
        assert_eq!(size_of::<FlowPlugin>(), 240);
        assert_eq!(align_of::<FlowPlugin>(), 8);
        assert_eq!(size_of::<FlowUnit>(), 96);
        assert_eq!(align_of::<FlowUnit>(), 8);
        assert_eq!(size_of::<FlowComponent>(), 104);
        assert_eq!(align_of::<FlowComponent>(), 8);
        assert_eq!(size_of::<FlowPlanDimension>(), 80);
        assert_eq!(size_of::<FlowPlanMetrics>(), 56);
        assert_eq!(size_of::<VerificationReport>(), 160);
    }
}



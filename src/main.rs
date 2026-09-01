extern "C" {
    fn flowc_main(argc: std::os::raw::c_int, argv: *const *const std::os::raw::c_char) -> std::os::raw::c_int;
}

fn main() {
    let args: Vec<std::ffi::CString> = std::env::args()
        .map(|arg| std::ffi::CString::new(arg).expect("Invalid argument"))
        .collect();
    let c_args: Vec<*const std::os::raw::c_char> = args.iter().map(|arg| arg.as_ptr()).collect();
    let status = unsafe { flowc_main(c_args.len() as std::os::raw::c_int, c_args.as_ptr()) };
    std::process::exit(status);
}

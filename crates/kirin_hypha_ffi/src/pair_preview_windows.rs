//! One active scan per module; the Windows pool releases the DLL only after callback return.
//! No join runs from DLL_PROCESS_DETACH. API contracts:
//! https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw
//! https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-freelibrarywhencallbackreturns
//! https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-trysubmitthreadpoolcallback
use super::Service;
use std::ffi::c_void;
use std::sync::Arc;
use windows_sys::Win32::Foundation::{FreeLibrary, HMODULE};
use windows_sys::Win32::System::LibraryLoader::{
    GetModuleHandleExW, GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
};
use windows_sys::Win32::System::Threading::{
    FreeLibraryWhenCallbackReturns, TrySubmitThreadpoolCallback, PTP_CALLBACK_INSTANCE,
};

struct Work {
    service: Arc<Service>,
    module: HMODULE,
}
pub(super) fn submit(service: Arc<Service>) -> bool {
    let mut module = std::ptr::null_mut();
    // The caller owns a live module. Extend that lifetime before asynchronous submission.
    if unsafe {
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            callback as *const () as *const u16,
            &mut module,
        )
    } == 0
    {
        return false;
    }
    let work = Box::into_raw(Box::new(Work { service, module }));
    if unsafe { TrySubmitThreadpoolCallback(Some(callback), work.cast(), std::ptr::null()) } != 0 {
        return true;
    }
    unsafe {
        drop(Box::from_raw(work));
        FreeLibrary(module);
    }
    false
}
unsafe extern "system" fn callback(instance: PTP_CALLBACK_INSTANCE, context: *mut c_void) {
    let work = unsafe { Box::from_raw(context.cast::<Work>()) };
    unsafe {
        FreeLibraryWhenCallbackReturns(instance, work.module);
    }
    // Rust state is retired while code is still mapped; the system returns from the callback
    // before releasing the last module reference. Never unwind through the Windows ABI.
    if std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| work.service.run())).is_err() {
        work.service
            .quit
            .store(true, std::sync::atomic::Ordering::Release);
    }
}

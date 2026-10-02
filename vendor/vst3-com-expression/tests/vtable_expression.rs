#![deny(warnings)]

use std::ffi::c_void;
use vst3_com::interfaces::iunknown::{IUnknown, IUnknownVTable};
use vst3_com::sys::{GUID, HRESULT};
use vst3_com::{Offset0, ProductionComInterface};

struct Probe;

impl IUnknown for Probe {
    unsafe fn query_interface(&self, _iid: *const GUID, _out: *mut *mut c_void) -> HRESULT {
        vst3_com::sys::E_NOINTERFACE
    }

    unsafe fn add_ref(&self) -> u32 {
        1
    }

    unsafe fn release(&self) -> u32 {
        1
    }
}

#[test]
fn exported_macro_returns_the_identical_typed_vtable_expression() {
    // This external expression position reproduces Rust 1.99's diagnostic.
    let actual: IUnknownVTable = vst3_com::vtable!(Probe: IUnknown, vst3_com::Offset0);
    let expected = <dyn IUnknown as ProductionComInterface<Probe>>::vtable::<Offset0>();
    assert_eq!(
        std::mem::size_of_val(&actual),
        3 * std::mem::size_of::<usize>()
    );
    assert_eq!(
        actual.QueryInterface as *const (),
        expected.QueryInterface as *const ()
    );
    assert_eq!(actual.AddRef as *const (), expected.AddRef as *const ());
    assert_eq!(actual.Release as *const (), expected.Release as *const ());
}

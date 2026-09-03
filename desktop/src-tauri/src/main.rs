fn main() {
    #[cfg(target_os = "linux")]
    configure_linux_webview();
    crashvault_desktop_lib::run()
}

#[cfg(target_os = "linux")]
fn configure_linux_webview() {
    // WebKitGTK can paint a cross-hatch "invalid" texture through transparent
    // layers during hover/repaint; these reduce compositor glitches on WSL/Wayland.
    std::env::set_var("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
    std::env::set_var("WEBKIT_DISABLE_COMPOSITING_MODE", "1");
}

#[cfg(target_os = "windows")]
pub async fn create_shared_buffer(
    webview: tauri::Webview,
    data: &[u8],
    extra_data: &[u8],
    transfer_type: impl Into<String>,
) -> Result<(), String> {
    use std::sync::mpsc::channel;
    use webview2_com::Microsoft::Web::WebView2::Win32::{
        ICoreWebView2Environment12, ICoreWebView2_17, COREWEBVIEW2_SHARED_BUFFER_ACCESS_READ_WRITE,
    };
    use windows_core::Interface;

    let mut payload = Vec::with_capacity(data.len() + extra_data.len());
    payload.extend_from_slice(data);
    payload.extend_from_slice(extra_data);
    let payload_len = payload.len();
    let transfer_type = transfer_type.into();

    let (sender, receiver) = channel::<Result<(), String>>();
    let fallback_sender = sender.clone();
    match webview.with_webview(move |webview| {
        let environment = webview.environment();
        let core_webview = match unsafe { webview.controller().CoreWebView2() } {
            Ok(core_webview) => core_webview,
            Err(error) => {
                let _ = sender.send(Err(format!(
                    "[create_shared_buffer] Failed to get core webview: {error:?}",
                )));
                return;
            }
        };

        let environment_12 = match environment.cast::<ICoreWebView2Environment12>() {
            Ok(environment) => environment,
            Err(error) => {
                let _ = sender.send(Err(format!(
                    "[create_shared_buffer] Failed to cast WebView2 environment: {error:?}",
                )));
                return;
            }
        };

        let shared_buffer = match unsafe { environment_12.CreateSharedBuffer(payload_len as u64) } {
            Ok(shared_buffer) => shared_buffer,
            Err(error) => {
                let _ = sender.send(Err(format!(
                    "[create_shared_buffer] Failed to create shared buffer: {error:?}",
                )));
                return;
            }
        };

        let mut shared_buffer_ptr: *mut u8 = std::ptr::null_mut();
        if let Err(error) = unsafe { shared_buffer.Buffer(&mut shared_buffer_ptr) } {
            let _ = sender.send(Err(format!(
                "[create_shared_buffer] Failed to map shared buffer: {error:?}",
            )));
            return;
        }

        let webview_17 = match core_webview.cast::<ICoreWebView2_17>() {
            Ok(webview) => webview,
            Err(error) => {
                let _ = sender.send(Err(format!(
                    "[create_shared_buffer] Failed to cast core webview: {error:?}",
                )));
                return;
            }
        };

        unsafe {
            std::ptr::copy_nonoverlapping(payload.as_ptr(), shared_buffer_ptr, payload_len);
        }

        let additional_data_string: Vec<u16> = serde_json::json!({
            "transfer_type": transfer_type
        })
        .to_string()
        .encode_utf16()
        .chain(std::iter::once(0))
        .collect();
        let additional_data = windows::core::PCWSTR::from_raw(additional_data_string.as_ptr());

        match unsafe {
            webview_17.PostSharedBufferToScript(
                &shared_buffer,
                COREWEBVIEW2_SHARED_BUFFER_ACCESS_READ_WRITE,
                additional_data,
            )
        } {
            Ok(_) => {
                let _ = sender.send(Ok(()));
            }
            Err(error) => {
                let _ = sender.send(Err(format!(
                    "[create_shared_buffer] Failed to post shared buffer: {error:?}",
                )));
            }
        }
    }) {
        Ok(_) => {}
        Err(error) => {
            let _ = fallback_sender.send(Err(format!(
                "[create_shared_buffer] Failed to access webview: {error:?}",
            )));
        }
    }

    receiver
        .recv()
        .map_err(|_| "[create_shared_buffer] Failed to receive transfer result".to_string())?
}

#[cfg(not(target_os = "windows"))]
pub async fn create_shared_buffer(
    _webview: tauri::Webview,
    _data: &[u8],
    _extra_data: &[u8],
    _transfer_type: impl Into<String>,
) -> Result<(), String> {
    Err("WebView2 SharedBuffer is only available on Windows.".to_string())
}

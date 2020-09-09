#include "pch.h"
#include "App.h"
#include "CaptureSnapshot.h"

namespace winrt
{
    using namespace Windows::Storage;
    using namespace Windows::Storage::Pickers;
    using namespace Windows::System;
    using namespace Windows::Foundation;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
    using namespace Windows::UI::Popups;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
}

namespace util
{
    using namespace desktop;
    using namespace uwp;
}

App::App(winrt::ContainerVisual root, winrt::GraphicsCapturePicker capturePicker, winrt::FileSavePicker savePicker)
{
    m_capturePicker = capturePicker;
    m_savePicker = savePicker;
    m_mainThread = winrt::DispatcherQueue::GetForCurrentThread();
    WINRT_VERIFY(m_mainThread != nullptr);

    m_compositor = root.Compositor();
    m_root = m_compositor.CreateContainerVisual();
    m_content = m_compositor.CreateSpriteVisual();
    m_brush = m_compositor.CreateSurfaceBrush();

    m_root.RelativeSizeAdjustment({ 1, 1 });
    root.Children().InsertAtTop(m_root);

    m_content.AnchorPoint({ 0.5f, 0.5f });
    m_content.RelativeOffsetAdjustment({ 0.5f, 0.5f, 0 });
    m_content.RelativeSizeAdjustment({ 1, 1 });
    m_content.Size({ -80, -80 });
    m_content.Brush(m_brush);
    m_brush.HorizontalAlignmentRatio(0.5f);
    m_brush.VerticalAlignmentRatio(0.5f);
    m_brush.Stretch(winrt::CompositionStretch::Uniform);
    auto shadow = m_compositor.CreateDropShadow();
    shadow.Mask(m_brush);
    m_content.Shadow(shadow);
    m_root.Children().InsertAtTop(m_content);

    auto d3dDevice = util::CreateD3DDevice();
    auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
    m_device = CreateDirect3DDevice(dxgiDevice.get());

    m_encoder = std::make_unique<SimpleImageEncoder>(m_device);
}

winrt::GraphicsCaptureItem App::StartCaptureFromWindowHandle(HWND hwnd)
{
    auto item = util::CreateCaptureItemForWindow(hwnd);
    StartCaptureFromItem(item);
    return item;
}

winrt::GraphicsCaptureItem App::StartCaptureFromMonitorHandle(HMONITOR hmon)
{
    auto item = util::CreateCaptureItemForMonitor(hmon);
    StartCaptureFromItem(item);
    return item;
}

winrt::IAsyncOperation<winrt::GraphicsCaptureItem> App::StartCaptureWithPickerAsync()
{
    auto item = co_await m_capturePicker.PickSingleItemAsync();
    if (item)
    {
        // We might resume on a different thread, so let's resume execution on the
        // main thread. This is important because SimpleCapture uses 
        // Direct3D11CaptureFramePool::Create, which requires the existence of
        // a DispatcherQueue. See CaptureSnapshot for an example that uses 
        // Direct3D11CaptureFramePool::CreateFreeThreaded, which doesn't now have this
        // requirement. See the README if you're unsure of which version of 'Create' to use.
        co_await m_mainThread;
        StartCaptureFromItem(item);
    }

    co_return item;
}

winrt::IAsyncOperation<winrt::StorageFile> App::TakeSnapshotAsync()
{
    // Use what we're currently capturing
    if (m_capture == nullptr)
    {
        co_return nullptr;
    }
    auto item = m_capture->CaptureItem();

    // Ask the user where they want to save the snapshot.
    m_savePicker.SuggestedStartLocation(winrt::PickerLocationId::PicturesLibrary);
    m_savePicker.SuggestedFileName(L"snapshot");
    m_savePicker.DefaultFileExtension(L".png");
    m_savePicker.FileTypeChoices().Clear();
    m_savePicker.FileTypeChoices().Insert(L"PNG image", winrt::single_threaded_vector<winrt::hstring>({ L".png" }));
    m_savePicker.FileTypeChoices().Insert(L"JPG image", winrt::single_threaded_vector<winrt::hstring>({ L".jpg" }));
    m_savePicker.FileTypeChoices().Insert(L"JXR image", winrt::single_threaded_vector<winrt::hstring>({ L".jxr" }));
    auto file = co_await m_savePicker.PickSaveFileAsync();
    if (file == nullptr)
    {
        co_return nullptr;
    }

    // Decide on the pixel format depending on the image type
    auto fileExtension = file.FileType();
    SimpleImageEncoder::SupportedFormats fileFormat;
    winrt::DirectXPixelFormat pixelFormat;
    if (fileExtension == L".png")
    {
        fileFormat = SimpleImageEncoder::SupportedFormats::Png;
        pixelFormat = winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized;
    }
    else if (fileExtension == L".jpg" || fileExtension == L".jpeg")
    {
        fileFormat = SimpleImageEncoder::SupportedFormats::Jpg;
        pixelFormat = winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized;
    }
    else if (fileExtension == L".jxr")
    {
        fileFormat = SimpleImageEncoder::SupportedFormats::Jxr;
        pixelFormat = winrt::DirectXPixelFormat::R16G16B16A16Float;
    }
    else
    {
        // Unsupported
        auto dialog = winrt::MessageDialog(L"Unsupported file format!");

        co_await dialog.ShowAsync();
        co_return nullptr;
    }

    // Get the file stream
    auto stream = co_await file.OpenAsync(winrt::FileAccessMode::ReadWrite);

    // Take the snapshot
    auto frame = co_await CaptureSnapshot::TakeAsync(m_device, item, pixelFormat);
    
    // Encode the image
    m_encoder->EncodeImage(frame, stream, fileFormat);

    co_return file;
}

void App::StartCaptureFromItem(winrt::GraphicsCaptureItem item)
{
    m_capture = std::make_unique<SimpleCapture>(m_device, item, m_pixelFormat);

    auto surface = m_capture->CreateSurface(m_compositor);
    m_brush.Surface(surface);

    m_capture->StartCapture();
}

void App::StopCapture()
{
    if (m_capture)
    {
        m_capture->Close();
        m_capture = nullptr;
        m_brush.Surface(nullptr);
    }
}

bool App::IsCursorEnabled()
{
    if (m_capture != nullptr)
    {
        return m_capture->IsCursorEnabled();
    }
    return false;
}

void App::IsCursorEnabled(bool value)
{
    if (m_capture != nullptr)
    {
        m_capture->IsCursorEnabled(value);
    }
}

bool App::IsBorderRequired()
{
    if (m_capture != nullptr)
    {
        return m_capture->IsBorderRequired();
    }
    return false;
}

void App::IsBorderRequired(bool value)
{
    if (m_capture != nullptr)
    {
        auto ignored = winrt::GraphicsCaptureAccess::RequestAccessAsync(winrt::GraphicsCaptureAccessKind::Borderless).get();
        m_capture->IsBorderRequired(value);
    }
}

void App::PixelFormat(winrt::DirectXPixelFormat pixelFormat)
{
    m_pixelFormat = pixelFormat;
    if (m_capture)
    {
        m_capture->SetPixelFormat(pixelFormat);
    }
}
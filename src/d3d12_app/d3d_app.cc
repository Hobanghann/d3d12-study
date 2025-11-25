
#include <DirectXColors.h>

#include <cassert>

#include "d3d12_app.h"

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam,
                         LPARAM lParam) {
  return D3D12App::GetApp().MsgHandler(hWnd, iMessage, wParam, lParam);
}

void D3D12App::CreateApp(HINSTANCE hInstance) {
  assert(app_ == nullptr);
  app_ = new D3D12App(hInstance);
}

D3D12App& D3D12App::GetApp() {
  assert(app_ != nullptr);
  return *app_;
}

D3D12App::D3D12App(HINSTANCE hInstance)
    : WindowsApp(hInstance),
      is_paused_(false),
      is_minimized_(false),
      is_maximized_(false),
      is_resizing_(false),
      current_back_buffer_index_(0),
      fence_value_(0) {}

void D3D12App::Initialize() {
  // Create Window
  windows_.emplace_back(
      std::make_unique<Window>(hInstance_, L"D3D12 App", 1280, 720, WndProc));
  main_window_ = windows_.begin()->get();

  // Set Debug Layer
#if defined(_DEBUG)
  {
    Microsoft::WRL::ComPtr<ID3D12Debug> debug_controller;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
      debug_controller->EnableDebugLayer();
    }
  }
#endif

  // Create Device
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
                               IID_PPV_ARGS(&device_)))) {
    main_window_->ShowMessageBox(L"Error", L"Create Device failed");
    return;
  }

#if defined(_DEBUG)
  {
    Microsoft::WRL::ComPtr<ID3D12DebugDevice> debug_device;
    device_->QueryInterface(IID_PPV_ARGS(&debug_device));
    debug_device->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL);
  }
#endif

  // Create Fence
  if (FAILED(device_->CreateFence(fence_value_, D3D12_FENCE_FLAG_NONE,
                                  IID_PPV_ARGS(&fence_)))) {
    main_window_->ShowMessageBox(L"Error", L"Create Fence failed");
    return;
  }

  // Create Factory
  if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG,
                                IID_PPV_ARGS(&factory_)))) {
    main_window_->ShowMessageBox(L"Error", L"Create Factory failed");
    return;
  }

  // Cache Descriptors Size
  cbv_srv_uav_size_ = device_->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  rtv_size_ =
      device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  dsv_size_ =
      device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

  // Check MSAA feature support
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms_quality_levels = {};
  ms_quality_levels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  ms_quality_levels.SampleCount = 4;
  ms_quality_levels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
  ms_quality_levels.NumQualityLevels = 0;
  if (FAILED(device_->CheckFeatureSupport(
          D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ms_quality_levels,
          sizeof(D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS)))) {
    main_window_->ShowMessageBox(L"Error",
                                 L"Check Multisample Quality Level fauled");
  }
  msaa4x_quality_level_ = ms_quality_levels.NumQualityLevels;
  assert(msaa4x_quality_level_ > 0 && "Unexpected MSAA quality level.");

  // Create Command Queue
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queue_desc.NodeMask = 0;
  if (FAILED(device_->CreateCommandQueue(&queue_desc,
                                         IID_PPV_ARGS(&comm_queue_)))) {
    main_window_->ShowMessageBox(L"Error", L"Create Command Queue failed");
    return;
  }

  // Create Command Allocator
  if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&comm_allocator_)))) {
    main_window_->ShowMessageBox(L"Error", L"Create Command Allocator failed");
    return;
  }

  // Create Command List
  if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        comm_allocator_.Get(), nullptr,
                                        IID_PPV_ARGS(&comm_list_)))) {
    main_window_->ShowMessageBox(L"Error", L"Create Command List failed");
    return;
  }

  comm_list_->Close();  // initial state of command list should be close

  // Create Swap Chain
  DXGI_SWAP_CHAIN_DESC swap_chain_desc = {};

  swap_chain_desc.BufferDesc.Width = main_window_->client_width();
  swap_chain_desc.BufferDesc.Height = main_window_->client_height();
  swap_chain_desc.BufferDesc.RefreshRate.Numerator = 60;
  swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
  swap_chain_desc.BufferDesc.Format = RENDER_TARGET_FORMAT;
  swap_chain_desc.BufferDesc.ScanlineOrdering =
      DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
  swap_chain_desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

  swap_chain_desc.SampleDesc.Count = 1;
  swap_chain_desc.SampleDesc.Quality = 0;

  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

  swap_chain_desc.BufferCount = RENDER_TARGET_COUNT;

  swap_chain_desc.OutputWindow = (*windows_.begin())->handle();

  swap_chain_desc.Windowed = true;

  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

  swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

  if (FAILED(factory_->CreateSwapChain(comm_queue_.Get(), &swap_chain_desc,
                                       swap_chain_.GetAddressOf()))) {
    main_window_->ShowMessageBox(L"Error", L"Create Swap Chain failed");
    return;
  }

  // Create Descriptor Heap
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};

  UINT cbv_heap_size = 4;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = cbv_heap_size;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  heap_desc.NodeMask = 0;
  if (FAILED(device_->CreateDescriptorHeap(&heap_desc,
                                           IID_PPV_ARGS(&cbv_heap_)))) {
    main_window_->ShowMessageBox(L"Error",
                                 L"Create CBV Descriptor Heap failed");
    return;
  }

  UINT srv_heap_size = 4;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = srv_heap_size;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  heap_desc.NodeMask = 0;
  if (FAILED(device_->CreateDescriptorHeap(&heap_desc,
                                           IID_PPV_ARGS(&srv_heap_)))) {
    main_window_->ShowMessageBox(L"Error",
                                 L"Create SRV Descriptor Heap failed");
    return;
  }

  UINT uav_heap_size = 4;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = uav_heap_size;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  heap_desc.NodeMask = 0;
  if (FAILED(device_->CreateDescriptorHeap(&heap_desc,
                                           IID_PPV_ARGS(&uav_heap_)))) {
    main_window_->ShowMessageBox(L"Error",
                                 L"Create UAV Descriptor Heap failed");
    return;
  }

  UINT rtv_heap_size = 2;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap_desc.NumDescriptors = rtv_heap_size;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  heap_desc.NodeMask = 0;
  if (FAILED(device_->CreateDescriptorHeap(&heap_desc,
                                           IID_PPV_ARGS(&rtv_heap_)))) {
    main_window_->ShowMessageBox(L"Error",
                                 L"Create RTV Descriptor Heap failed");
    return;
  }

  UINT dsv_heap_size = 1;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  heap_desc.NumDescriptors = dsv_heap_size;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  heap_desc.NodeMask = 0;
  if (FAILED(device_->CreateDescriptorHeap(&heap_desc,
                                           IID_PPV_ARGS(&dsv_heap_)))) {
    main_window_->ShowMessageBox(L"Error",
                                 L"Create DSV Descriptor Heap failed");
    return;
  }

  // Create Render Target and Its Descriptor
  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(
      rtv_heap_->GetCPUDescriptorHandleForHeapStart());
  for (UINT i = 0; i < RENDER_TARGET_COUNT; i++) {
    swap_chain_->GetBuffer(i, IID_PPV_ARGS(&render_targets_[i]));
    device_->CreateRenderTargetView(render_targets_[i].Get(), nullptr,
                                    rtv_handle.Offset(i * rtv_size_));
  }

  // Create Depth Stencil Buffer and Its Descriptor
  D3D12_HEAP_PROPERTIES heap_prop = {};
  heap_prop.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_prop.VisibleNodeMask = 0;
  heap_prop.CreationNodeMask = 0;

  D3D12_RESOURCE_DESC resource_desc;
  resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  resource_desc.Width = main_window_->client_width();
  resource_desc.Height = main_window_->client_height();
  resource_desc.DepthOrArraySize = 1;
  resource_desc.MipLevels = 1;
  resource_desc.Format = DEPTH_STENCIL_BUFFER_FORMAT;
  resource_desc.SampleDesc.Count = 1;
  resource_desc.SampleDesc.Quality = 0;
  resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_CLEAR_VALUE clear_value;
  clear_value.Format = DEPTH_STENCIL_BUFFER_FORMAT;
  clear_value.DepthStencil.Depth = 1.f;
  clear_value.DepthStencil.Stencil = 0u;

  device_->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE,
                                   &resource_desc, D3D12_RESOURCE_STATE_COMMON,
                                   &clear_value,
                                   IID_PPV_ARGS(&depth_stencil_buffer_));

  D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle(
      dsv_heap_->GetCPUDescriptorHandleForHeapStart());
  device_->CreateDepthStencilView(depth_stencil_buffer_.Get(), nullptr,
                                  dsv_handle);

  comm_list_->ResourceBarrier(
      1, &CD3DX12_RESOURCE_BARRIER::Transition(
             depth_stencil_buffer_.Get(), D3D12_RESOURCE_STATE_COMMON,
             D3D12_RESOURCE_STATE_DEPTH_WRITE));

  // Set Viewport
  viewport_full_.TopLeftX = 0.f;
  viewport_full_.TopLeftY = 0.f;
  viewport_full_.Width = main_window_->client_width();
  viewport_full_.Height = main_window_->client_height();
  viewport_full_.MinDepth = 0.f;
  viewport_full_.MaxDepth = 1.f;

  // Set Scissor Rectangle
  scissor_rect_full_.top = 0;
  scissor_rect_full_.left = 0;
  scissor_rect_full_.right = main_window_->client_width();
  scissor_rect_full_.bottom = main_window_->client_height();
}

void D3D12App::Run() {
  MSG msg = {0};
  timer_.Reset();

  while (msg.message != WM_QUIT) {
    HandleMessages(msg);

    timer_.Tick();

    if (is_paused_) {
      Sleep(100);
    } else {
      /////////////////////////////////////////////////////////
      // Calculate Frame Stats
      /////////////////////////////////////////////////////////
      static int frame_count;
      static float elapsed_time;

      frame_count++;

      if ((timer_.TotalTime() - elapsed_time) >= 1.f) {
        float fps = (float)frame_count;
        float ms_per_frame = 1000.f / fps;

        std::wstring text = L"FPS: " + std::to_wstring(fps) +
                            L" | per frame: " + std::to_wstring(ms_per_frame) +
                            L"ms";
        main_window_->PrintTextInTitle(text);

        frame_count = 0;
        elapsed_time += 1.f;
      }
      /////////////////////////////////////////////////////////
      // Update
      /////////////////////////////////////////////////////////

      /////////////////////////////////////////////////////////
      // Draw
      /////////////////////////////////////////////////////////

      // reset command allocator
      if (FAILED(comm_allocator_->Reset())) {
        main_window_->ShowMessageBox(L"Error",
                                     L"Reset Command Allocator failed");
        return;
      }

      // reset command list
      if (FAILED(comm_list_->Reset(comm_allocator_.Get(), nullptr))) {
        main_window_->ShowMessageBox(L"Error", L"Reset Command List failed");
        return;
      }

      // transition render target state from present state
      // to render target state
      comm_list_->ResourceBarrier(
          1, &CD3DX12_RESOURCE_BARRIER::Transition(
                 render_targets_[current_back_buffer_index_].Get(),
                 D3D12_RESOURCE_STATE_PRESENT,
                 D3D12_RESOURCE_STATE_RENDER_TARGET));

      // set viewport and scissor rect
      comm_list_->RSSetViewports(1, &viewport_full_);
      comm_list_->RSSetScissorRects(1, &scissor_rect_full_);

      // clear back buffer, depth stencil buffer
      D3D12_CPU_DESCRIPTOR_HANDLE curr_rtv_handle =
          CD3DX12_CPU_DESCRIPTOR_HANDLE(
              rtv_heap_->GetCPUDescriptorHandleForHeapStart(),
              current_back_buffer_index_ * rtv_size_);
      comm_list_->ClearRenderTargetView(curr_rtv_handle,
                                        DirectX::Colors::LightGray, 0, nullptr);

      D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle =
          dsv_heap_->GetCPUDescriptorHandleForHeapStart();
      comm_list_->ClearDepthStencilView(
          dsv_handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f,
          0, 0, nullptr);

      // set render target for rendering current frame
      comm_list_->OMSetRenderTargets(1, &curr_rtv_handle, true, &dsv_handle);

      // transition render target state from render target state
      // to present state
      comm_list_->ResourceBarrier(
          1, &CD3DX12_RESOURCE_BARRIER::Transition(
                 render_targets_[current_back_buffer_index_].Get(),
                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                 D3D12_RESOURCE_STATE_PRESENT));

      // close command list
      if (FAILED(comm_list_->Close())) {
        main_window_->ShowMessageBox(L"Error", L"Close Command List failed");
      }

      // enqueue commands
      ID3D12CommandList* comm_lists[] = {comm_list_.Get()};
      comm_queue_->ExecuteCommandLists(_countof(comm_lists), comm_lists);

      // swap the back and front buffers
      if (FAILED(swap_chain_->Present(0, 0))) {
        main_window_->ShowMessageBox(L"Error", L"Swap buffers failed");
      }
      current_back_buffer_index_ =
          (current_back_buffer_index_ + 1) % RENDER_TARGET_COUNT;

      FlushCommandQueue();
    }
  }
}

void D3D12App::Quit() {}

void D3D12App::FlushCommandQueue() {
  //  Flush the command queue so that the command allocator and command list
  //  can be reset in the next frame.

  fence_value_++;

  if (FAILED(comm_queue_->Signal(fence_.Get(), fence_value_))) {
    main_window_->ShowMessageBox(
        L"Error", L"Add fence signal command for flushing failed");
  }

  // wait for the GPU to execute the fence operation

  // This if statement is needed to prevent a race condition in which the
  // GPU completes the fence signal before the event is set
  if (fence_->GetCompletedValue() < fence_value_) {
    HANDLE event_handle =
        CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
    if (FAILED(fence_->SetEventOnCompletion(fence_value_, event_handle))) {
      main_window_->ShowMessageBox(L"Error", L"Set event to fence failed");
    }

    // make sleep this thread until event is signaled.
    WaitForSingleObject(event_handle, INFINITE);
    // remove created event after flushing
    CloseHandle(event_handle);
  }
}

void D3D12App::OnResize() {
  assert(device_);
  assert(swap_chain_);
  assert(comm_allocator_);

  FlushCommandQueue();

  if (FAILED(comm_list_->Reset(comm_allocator_.Get(), nullptr))) {
    main_window_->ShowMessageBox(L"Error", L"Reset Command List failed");
  }

  for (uint32_t i = 0; i < RENDER_TARGET_COUNT; i++) {
    render_targets_[i].Reset();
  }
  depth_stencil_buffer_.Reset();

  if (FAILED(swap_chain_->ResizeBuffers(
          RENDER_TARGET_COUNT, main_window_->client_width(),
          main_window_->client_height(), RENDER_TARGET_FORMAT,
          DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH))) {
    main_window_->ShowMessageBox(L"Error", L"Resize Swap Chain failed");
  }

  current_back_buffer_index_ = 0;

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(
      rtv_heap_->GetCPUDescriptorHandleForHeapStart());
  for (UINT i = 0; i < RENDER_TARGET_COUNT; i++) {
    swap_chain_->GetBuffer(i, IID_PPV_ARGS(&render_targets_[i]));
    device_->CreateRenderTargetView(render_targets_[i].Get(), nullptr,
                                    rtv_handle.Offset(i * rtv_size_));
  }

  D3D12_HEAP_PROPERTIES heap_prop = {};
  heap_prop.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_prop.VisibleNodeMask = 0;
  heap_prop.CreationNodeMask = 0;

  D3D12_RESOURCE_DESC resource_desc;
  resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  resource_desc.Width = main_window_->client_width();
  resource_desc.Height = main_window_->client_height();
  resource_desc.DepthOrArraySize = 1;
  resource_desc.MipLevels = 1;
  resource_desc.Format = DEPTH_STENCIL_BUFFER_FORMAT;
  resource_desc.SampleDesc.Count = 1;
  resource_desc.SampleDesc.Quality = 0;
  resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_CLEAR_VALUE clear_value;
  clear_value.Format = DEPTH_STENCIL_BUFFER_FORMAT;
  clear_value.DepthStencil.Depth = 1.f;
  clear_value.DepthStencil.Stencil = 0u;

  device_->CreateCommittedResource(
      &heap_prop, D3D12_HEAP_FLAG_NONE, &resource_desc,
      D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear_value,
      IID_PPV_ARGS(&depth_stencil_buffer_));

  D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle(
      dsv_heap_->GetCPUDescriptorHandleForHeapStart());
  device_->CreateDepthStencilView(depth_stencil_buffer_.Get(), nullptr,
                                  dsv_handle);

  comm_list_->ResourceBarrier(
      1, &CD3DX12_RESOURCE_BARRIER::Transition(
             depth_stencil_buffer_.Get(), D3D12_RESOURCE_STATE_COMMON,
             D3D12_RESOURCE_STATE_DEPTH_WRITE));

  if (FAILED(comm_list_->Close())) {
    main_window_->ShowMessageBox(L"Error", L"Close Command LIst failed");
  }
  ID3D12CommandList* comm_lists[] = {comm_list_.Get()};
  comm_queue_->ExecuteCommandLists(_countof(comm_lists), comm_lists);

  FlushCommandQueue();

  // Set Viewport
  viewport_full_.TopLeftX = 0.f;
  viewport_full_.TopLeftY = 0.f;
  viewport_full_.Width = main_window_->client_width();
  viewport_full_.Height = main_window_->client_height();
  viewport_full_.MinDepth = 0.f;
  viewport_full_.MaxDepth = 1.f;

  // Set Scissor Rectangle
  scissor_rect_full_.top = 0;
  scissor_rect_full_.left = 0;
  scissor_rect_full_.right = main_window_->client_width();
  scissor_rect_full_.bottom = main_window_->client_height();
}

void D3D12App::OnKEYDOWN(WPARAM wParam, LPARAM lParam) {}
void D3D12App::OnKEYUP(WPARAM wParam, LPARAM lParam) {}
void D3D12App::OnMOUSEDOWN(WPARAM wParam, LPARAM lParam) {}
void D3D12App::OnMOUSEUP(WPARAM wParam, LPARAM lParam) {}
void D3D12App::OnMOUSEMOVE(WPARAM wParam, LPARAM lParam) {}
void D3D12App::OnMOUSEWHEEL(WPARAM wParam, LPARAM lParam) {}
void D3D12App::OnSETFOCUS(WPARAM wParam, LPARAM lParam) {}
void D3D12App::OnKILLFOCUS(WPARAM wParam, LPARAM lParam) {}

void D3D12App::OnSize(WPARAM wParam, LPARAM lParam) {
  if (!device_ || !main_window_) {
    return;
  }

  main_window_->ResizeClient(LOWORD(lParam), HIWORD(lParam));
  if (device_) {
    if (wParam == SIZE_MINIMIZED) {
      is_paused_ = true;
      is_minimized_ = true;
      is_maximized_ = false;
    } else if (wParam == SIZE_MAXIMIZED) {
      is_paused_ = false;
      is_minimized_ = false;
      is_maximized_ = true;
      OnResize();
    } else if (wParam == SIZE_RESTORED) {
      if (is_minimized_) {
        is_paused_ = false;
        is_minimized_ = false;
        OnResize();
      } else if (is_maximized_) {
        is_paused_ = false;
        is_maximized_ = false;
        OnResize();
      } else if (is_resizing_) {
        // Do nothing
      } else {
        OnResize();
      }
    }
  }
}

void D3D12App::OnENTERSIZEMOVE(WPARAM wParam, LPARAM lParam) {
  is_paused_ = true;
  is_resizing_ = true;
  timer_.Stop();
}

void D3D12App::OnEXITSIZEMOVE(WPARAM wParam, LPARAM lParam) {
  is_paused_ = false;
  is_resizing_ = false;
  timer_.Start();
}

void D3D12App::OnACTIVE(WPARAM wParam, LPARAM lParam) {
  is_paused_ = false;
  timer_.Start();
}

void D3D12App::OnINACTIVE(WPARAM wParam, LPARAM lParam) {
  is_paused_ = true;
  timer_.Stop();
}

D3D12App* D3D12App::app_ = nullptr;
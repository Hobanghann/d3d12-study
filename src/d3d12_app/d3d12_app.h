#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_3.h>
#include <dxgidebug.h>
#include <wrl/client.h>

#include "utils/game_timer.h"
#include "windows_app/windows_app.h"

class D3D12App : public WindowsApp {
 public:
  static void CreateApp(HINSTANCE hInstance);
  static D3D12App& GetApp();
  ~D3D12App() = default;

  void Initialize() override;

  void Run() override;

  void Quit() override;

 private:
  D3D12App() = delete;
  D3D12App(const D3D12App&) = delete;
  D3D12App(HINSTANCE hInstance);
  D3D12App& operator=(const D3D12App&) = delete;

  void FlushCommandQueue();

  void OnResize();

  void OnKEYDOWN(WPARAM wParam, LPARAM lParam) override;
  void OnKEYUP(WPARAM wParam, LPARAM lParam) override;
  void OnMOUSEDOWN(WPARAM wParam, LPARAM lParam) override;
  void OnMOUSEUP(WPARAM wParam, LPARAM lParam) override;
  void OnMOUSEMOVE(WPARAM wParam, LPARAM lParam) override;
  void OnMOUSEWHEEL(WPARAM wParam, LPARAM lParam) override;
  void OnSETFOCUS(WPARAM wParam, LPARAM lParam) override;
  void OnKILLFOCUS(WPARAM wParam, LPARAM lParam) override;
  void OnSize(WPARAM wParam, LPARAM lParam) override;
  void OnENTERSIZEMOVE(WPARAM wParam, LPARAM lParam) override;
  void OnEXITSIZEMOVE(WPARAM wParam, LPARAM lParam) override;
  void OnACTIVE(WPARAM wParam, LPARAM lParam) override;
  void OnINACTIVE(WPARAM wParam, LPARAM lParam) override;

  GameTimer timer_;

  static D3D12App* app_;
  bool is_paused_;
  bool is_minimized_;
  bool is_maximized_;
  bool is_resizing_;

  Window* main_window_;
  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<IDXGIFactory2> factory_;

  Microsoft::WRL::ComPtr<ID3D12CommandQueue> comm_queue_;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> comm_allocator_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> comm_list_;

  constexpr static UINT RENDER_TARGET_COUNT = 2;
  constexpr static DXGI_FORMAT RENDER_TARGET_FORMAT =
      DXGI_FORMAT_R8G8B8A8_UNORM;
  constexpr static DXGI_FORMAT DEPTH_STENCIL_BUFFER_FORMAT =
      DXGI_FORMAT_D24_UNORM_S8_UINT;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D12Resource> render_targets_[RENDER_TARGET_COUNT];
  Microsoft::WRL::ComPtr<ID3D12Resource> depth_stencil_buffer_;
  UINT current_back_buffer_index_;

  D3D12_VIEWPORT viewport_full_;
  D3D12_RECT scissor_rect_full_;

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cbv_heap_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> uav_heap_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
  UINT cbv_srv_uav_size_;
  UINT rtv_size_;
  UINT dsv_size_;
  UINT msaa4x_quality_level_;

  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  UINT64 fence_value_;
};
#include "d3d12_app/d3d12_app.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpszCmdParam, int nCmdShow) {
  D3D12App::CreateApp(hInstance);

  D3D12App::GetApp().Initialize();
  D3D12App::GetApp().Run();
  D3D12App::GetApp().Quit();
}
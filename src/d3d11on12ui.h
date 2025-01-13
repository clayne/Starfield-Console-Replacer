#pragma once

extern void DX11_InitializeOrRender(void *dx12_swapchain, void *dx12_commandqueue);
extern void DX11_ReleaseIfInitialized();

extern void DX11_InstallSwapChainHooks(void** dx12_swapchain_vtable, void* fake_present);
extern void DX11_RemoveSwapChainHooks(void** dx12_swapchain_vtable);

extern HRESULT DX11_CallPresent(void* dx12_swapchain, uint32_t sync_interval, uint32_t present_flags);
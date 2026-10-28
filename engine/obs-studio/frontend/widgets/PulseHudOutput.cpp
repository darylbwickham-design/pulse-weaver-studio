#include "PulseHudOutput.hpp"

#include <QMetaObject>
#include <QScreen>
#include <QWindow>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <obs.hpp>
#include <atomic>
#include <mutex>
#include <cstring>
#ifdef PULSE_HUD_TESTS
#include <QImage>
#endif

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

struct PulseHudOutput::Impl {
	PulseHudOutput *owner;
	std::atomic<bool> running{false}, notified{false}, failed{false};
	bool registered = false;
	std::atomic<uint64_t> generation{0};
	OBSSource source;
	gs_texrender_t *render = nullptr;
	gs_texrender_t *output = nullptr;
	gs_effect_t *premultiply = nullptr;
	uint32_t width = 0, height = 0;
	uint64_t lastFrame = 0;
#ifdef PULSE_HUD_TESTS
	std::atomic<uint64_t> frames{0};
#endif
#ifdef _WIN32
	HWND window = nullptr;
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<IDXGISwapChain1> swap;
	ComPtr<IDCompositionDevice> composition;
	ComPtr<IDCompositionTarget> target;
	ComPtr<IDCompositionVisual> visual;
#ifdef PULSE_HUD_TESTS
	ComPtr<ID3D11Texture2D> lastPresented;
#endif

	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
	{
		if (message == WM_NCHITTEST) return HTTRANSPARENT;
		if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
		if (message == WM_ERASEBKGND) return 1;
		return DefWindowProcW(hwnd, message, wparam, lparam);
	}

	HRESULT InitializeGpu()
	{
		if (gs_get_device_type() != GS_DEVICE_DIRECT3D_11) return E_NOINTERFACE;
		auto *device = static_cast<ID3D11Device *>(gs_get_device_obj());
		if (!device) return E_NOINTERFACE;
		ComPtr<IDXGIDevice> dxgiDevice;
		ComPtr<IDXGIAdapter> adapter;
		ComPtr<IDXGIFactory2> factory;
		HRESULT result = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
		if (SUCCEEDED(result)) result = dxgiDevice->GetAdapter(&adapter);
		if (SUCCEEDED(result)) result = adapter->GetParent(IID_PPV_ARGS(&factory));
		if (FAILED(result)) return result;
		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.Width = width;
		desc.Height = height;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = 2;
		desc.Scaling = DXGI_SCALING_STRETCH;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
		result = factory->CreateSwapChainForComposition(device, &desc, nullptr, &swap);
		if (SUCCEEDED(result)) result = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&composition));
		if (SUCCEEDED(result)) result = composition->CreateTargetForHwnd(window, TRUE, &target);
		if (SUCCEEDED(result)) result = composition->CreateVisual(&visual);
		if (SUCCEEDED(result)) result = visual->SetContent(swap.Get());
		if (SUCCEEDED(result)) result = target->SetRoot(visual.Get());
		if (SUCCEEDED(result)) result = composition->Commit();
		if (SUCCEEDED(result)) device->GetImmediateContext(&context);
		return result;
	}

	void Fail(HRESULT result)
	{
		if (failed.exchange(true)) return;
		const QString message = QString("The HUD output stopped (graphics error 0x%1).")
			.arg(quint32(result), 8, 16, QLatin1Char('0'));
		const uint64_t token = generation.load();
		QMetaObject::invokeMethod(owner, [hud = owner, message, token] {
			if (hud->impl->generation != token) return;
			hud->Stop();
			if (hud->stateChanged) hud->stateChanged(false, message);
		}, Qt::QueuedConnection);
	}

	static void Draw(void *data, uint32_t, uint32_t)
	{
		auto &self = *static_cast<Impl *>(data);
		/* libobs invokes this hook for each output mix; render the desktop HUD
		 * only once per video tick, regardless of how many streams are active. */
		const uint64_t frame = obs_get_video_frame_time();
		if (self.lastFrame == frame) return;
		self.lastFrame = frame;
		if (self.failed || !self.source || obs_source_removed(self.source)) {
			if (!self.failed) self.Fail(E_ABORT);
			return;
		}
		if (!self.swap) {
			const HRESULT result = self.InitializeGpu();
			if (FAILED(result)) { self.Fail(result); return; }
		}
		if (!self.render) self.render = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
		if (!self.output) self.output = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
		if (!self.premultiply) {
			/* OBS composites scene items in linear light. Desktop composition
			 * expects premultiplied sRGB bytes, including around translucent FX.
			 * Convert on the GPU; never read frames back through the CPU. */
			constexpr const char *shader = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
sampler_state sampleImage { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
struct Vert { float4 pos : POSITION; float2 uv : TEXCOORD0; };
Vert VS(Vert v) { Vert r; r.pos = mul(float4(v.pos.xyz, 1.0), ViewProj); r.uv = v.uv; return r; }
float encode(float c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 0.416666667) - 0.055; }
float4 PS(Vert v) : TARGET {
    float4 rgba = image.Sample(sampleImage, v.uv);
    if (rgba.a <= 0.000001) return float4(0, 0, 0, 0);
    float3 straight = max(rgba.rgb / rgba.a, 0.0);
    float3 encoded = saturate(float3(encode(straight.r), encode(straight.g), encode(straight.b)));
    return float4(encoded * rgba.a, rgba.a);
}
technique Draw { pass { vertex_shader = VS(v); pixel_shader = PS(v); } }
)";
			self.premultiply = gs_effect_create(shader, "Pulse Weaver HUD compositor", nullptr);
		}
		if (!self.render || !self.output || !self.premultiply) { self.Fail(E_FAIL); return; }
		gs_texrender_reset(self.render);
		gs_viewport_push();
		gs_projection_push();
		gs_matrix_push();
		gs_blend_state_push();
		const bool previousLinear = gs_set_linear_srgb(true);
		const bool previousFramebuffer = gs_framebuffer_srgb_enabled();
		if (!gs_texrender_begin_with_color_space(self.render, self.width, self.height, GS_CS_SRGB_16F)) {
			gs_set_linear_srgb(previousLinear); gs_enable_framebuffer_srgb(previousFramebuffer);
			gs_blend_state_pop(); gs_matrix_pop(); gs_projection_pop(); gs_viewport_pop();
			self.Fail(E_FAIL); return;
		}
		vec4 transparent{};
		gs_clear(GS_CLEAR_COLOR, &transparent, 0.0f, 0);
		gs_ortho(0.0f, float(self.width), 0.0f, float(self.height), -100.0f, 100.0f);
		gs_matrix_identity();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		obs_source_video_render(self.source);
		gs_texrender_end(self.render);
		gs_texrender_reset(self.output);
		if (!gs_texrender_begin(self.output, self.width, self.height)) {
			gs_set_linear_srgb(previousLinear); gs_enable_framebuffer_srgb(previousFramebuffer);
			gs_blend_state_pop(); gs_matrix_pop(); gs_projection_pop(); gs_viewport_pop();
			self.Fail(E_FAIL); return;
		}
		gs_clear(GS_CLEAR_COLOR, &transparent, 0.0f, 0);
		gs_enable_framebuffer_srgb(false);
		gs_effect_set_texture(gs_effect_get_param_by_name(self.premultiply, "image"), gs_texrender_get_texture(self.render));
		while (gs_effect_loop(self.premultiply, "Draw")) gs_draw_sprite(gs_texrender_get_texture(self.render), 0, self.width, self.height);
		gs_texrender_end(self.output);
		gs_set_linear_srgb(previousLinear); gs_enable_framebuffer_srgb(previousFramebuffer);
		gs_blend_state_pop(); gs_matrix_pop(); gs_projection_pop(); gs_viewport_pop();
		auto *texture = static_cast<ID3D11Texture2D *>(gs_texture_get_obj(gs_texrender_get_texture(self.output)));
		ComPtr<ID3D11Texture2D> back;
		HRESULT result = self.swap->GetBuffer(0, IID_PPV_ARGS(&back));
		if (SUCCEEDED(result) && texture) {
			self.context->CopyResource(back.Get(), texture);
#ifdef PULSE_HUD_TESTS
			self.lastPresented = back;
#endif
			result = self.swap->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
		} else if (SUCCEEDED(result)) {
			result = E_FAIL;
		}
		if (result == DXGI_ERROR_WAS_STILL_DRAWING) return;
		if (FAILED(result)) { self.Fail(result); return; }
#ifdef PULSE_HUD_TESTS
		++self.frames;
#endif
		if (!self.notified.exchange(true)) {
			const uint64_t token = self.generation.load();
			QMetaObject::invokeMethod(self.owner, [hud = self.owner, token] {
				if (hud->impl->generation != token) return;
				if (!hud->impl->registered || hud->impl->failed) return;
				ShowWindow(hud->impl->window, SW_SHOWNOACTIVATE);
				hud->impl->running = true;
				if (hud->stateChanged) hud->stateChanged(true, {});
			}, Qt::QueuedConnection);
		}
	}
#endif
};

PulseHudOutput::PulseHudOutput(QObject *parent) : QObject(parent), impl(std::make_unique<Impl>())
{
	impl->owner = this;
}

PulseHudOutput::~PulseHudOutput() { stateChanged = {}; Stop(); }
bool PulseHudOutput::Running() const { return impl->running.load(); }
bool PulseHudOutput::Active() const { return impl->registered; }

PulseHudScreen PulseHudOutput::Describe(QScreen *screen)
{
	PulseHudScreen result;
	if (!screen) return result;
	result.name = screen->name();
	result.serial = screen->serialNumber();
#ifdef _WIN32
	/* Qt may name a screen using its model rather than \\.\DISPLAYn. A hidden
	 * native probe lets Qt do its own mixed-DPI coordinate mapping, then Win32
	 * supplies that exact monitor's physical pixel rectangle. Never guess by
	 * multiplying virtual-desktop coordinates by a global scale factor. */
	QWindow probe;
	probe.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
	probe.setScreen(screen);
	probe.setGeometry(QRect(screen->geometry().center(), QSize(1, 1)));
	probe.create();
	const HMONITOR monitor = MonitorFromWindow(reinterpret_cast<HWND>(probe.winId()), MONITOR_DEFAULTTONULL);
	MONITORINFO info{};
	info.cbSize = sizeof(info);
	if (monitor && GetMonitorInfoW(monitor, &info))
		result.pixels = QRect(info.rcMonitor.left, info.rcMonitor.top,
			info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top);
#endif
	return result;
}

QString PulseHudOutput::Start(obs_source_t *scene, const PulseHudScreen &screen)
{
	Stop();
#ifdef _WIN32
	if (!screen.valid()) return "Choose a connected display for the HUD.";
	OBSCanvasAutoRelease owner = scene ? obs_source_get_canvas(scene) : nullptr;
	if (!scene || !obs_source_is_scene(scene) || !owner || std::strcmp(obs_canvas_get_name(owner), "Pulse Weaver HUD") != 0)
		return "Choose a HUD scene before turning the HUD on.";
	const uint32_t width = obs_source_get_width(scene), height = obs_source_get_height(scene);
	if (width != uint32_t(screen.pixels.width()) || height != uint32_t(screen.pixels.height()))
		return "The HUD canvas does not match its display.";
	impl->source = scene;
	static std::once_flag registered;
	std::call_once(registered, [] {
		WNDCLASSW wc{};
		wc.lpfnWndProc = Impl::WindowProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"PulseWeaverTransparentHud";
		RegisterClassW(&wc);
	});
	impl->window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
		WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP, L"PulseWeaverTransparentHud", L"Pulse Weaver HUD",
		WS_POPUP, screen.pixels.x(), screen.pixels.y(), screen.pixels.width(), screen.pixels.height(),
		nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!impl->window) { impl->source = nullptr; return "Windows could not create the transparent HUD surface."; }
	if (!SetLayeredWindowAttributes(impl->window, 0, 255, LWA_ALPHA)) {
		Stop(); return "Windows could not enable the click-through HUD surface.";
	}
	impl->width = width;
	impl->height = height;
	impl->failed = false;
	impl->lastFrame = 0;
	impl->notified = false;
	obs_source_inc_showing(impl->source);
	impl->registered = true;
	obs_add_main_render_callback(Impl::Draw, impl.get());
	return {};
#else
	(void)scene; (void)screen;
	return "Transparent HUD output currently requires Windows.";
#endif
}

void PulseHudOutput::Stop()
{
	++impl->generation;
#ifdef _WIN32
	if (impl->registered) {
		obs_remove_main_render_callback(Impl::Draw, impl.get());
		impl->registered = false;
		obs_source_dec_showing(impl->source);
	}
	if (impl->render || impl->output || impl->premultiply || impl->swap || impl->composition) {
		obs_enter_graphics();
		gs_texrender_destroy(impl->render);
		impl->render = nullptr;
		gs_texrender_destroy(impl->output); impl->output = nullptr;
		gs_effect_destroy(impl->premultiply); impl->premultiply = nullptr;
		impl->visual.Reset(); impl->target.Reset(); impl->composition.Reset();
		impl->swap.Reset(); impl->context.Reset();
#ifdef PULSE_HUD_TESTS
		impl->lastPresented.Reset();
#endif
		obs_leave_graphics();
	}
	if (impl->window) { DestroyWindow(impl->window); impl->window = nullptr; }
#endif
	impl->source = nullptr;
	impl->running = false;
	impl->notified = false;
	if (stateChanged) stateChanged(false, {});
}

#ifdef PULSE_HUD_TESTS
QImage PulseHudOutput::ReadbackForTest()
{
	QImage result;
	obs_enter_graphics();
	if (impl->lastPresented && impl->context) {
		D3D11_TEXTURE2D_DESC desc{}; impl->lastPresented->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
		ComPtr<ID3D11Texture2D> stage;
		auto *device = static_cast<ID3D11Device *>(gs_get_device_obj());
		if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &stage))) {
			impl->context->CopyResource(stage.Get(), impl->lastPresented.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (SUCCEEDED(impl->context->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
				result = QImage(int(impl->width), int(impl->height), QImage::Format_ARGB32_Premultiplied);
				for (uint32_t y = 0; y < impl->height; ++y)
					std::memcpy(result.scanLine(int(y)), static_cast<const uint8_t *>(mapped.pData) + y * mapped.RowPitch, impl->width * 4);
				impl->context->Unmap(stage.Get(), 0);
			}
		}
	}
	obs_leave_graphics();
	return result;
}

quintptr PulseHudOutput::WindowForTest() const
{
#ifdef _WIN32
	return reinterpret_cast<quintptr>(impl->window);
#else
	return 0;
#endif
}
uint64_t PulseHudOutput::FramesForTest() const { return impl->frames.load(); }
#endif

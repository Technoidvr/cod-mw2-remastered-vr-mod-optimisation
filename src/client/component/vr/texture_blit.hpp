#pragma once
#include "component/d3d11.hpp"
#include <array>
#include <string>

namespace vr::texture_blit
{
	enum class encoding : unsigned
	{
		linear,
		encoded_premultiplied,
		encoded_opaque
	};
	// Short owner-thread draw, with all immediate-context state restored. Sources
	// remain on the same device; callers own their immutable publication leases.
	class renderer
	{
		struct cached_source
		{
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
			D3D11_TEXTURE2D_DESC description{};
			encoding format{};
		};
		struct cached_command
		{
			ID3D11Texture2D* source{};
			ID3D11RenderTargetView* destination{};
			unsigned width{}, height{};
			encoding format{};
			Microsoft::WRL::ComPtr<ID3D11CommandList> list;
		};
		struct shader_parameters
		{
			unsigned mode{};
			float dim{};
			float sharpness{};
			float padding0{};
			float rcp_source_size[2]{};
			float padding1[2]{};
		};
		static_assert(sizeof(shader_parameters) == 32);
		Microsoft::WRL::ComPtr<ID3D11Device> validated_device_;
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
		Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> parameters_;
		std::array<cached_source, 16> sources_;
		std::array<cached_command, 12> commands_{};
		shader_parameters last_parameters_{};
		bool has_last_parameters_{};
		unsigned cursor_{};
		unsigned command_cursor_{};
		bool ensure(ID3D11Device*, std::string&);

	  public:
		struct draw_request
		{
			const d3d11::device_snapshot& graphics;
			ID3D11Texture2D* source;
			ID3D11RenderTargetView* destination;
			unsigned width, height;
			encoding source_encoding;
			float dim{};
			float sharpness{};
		};
		bool draw(const draw_request&, std::string& error);
		void reset() noexcept;
	};
}

#include <std_include.hpp>
#include "engine_stereo_sun_shadow.hpp"
#include "engine_stereo_execution.hpp"
#include "engine_stereo_output_merger.hpp"
#include "engine_stereo_resource_ops.hpp"
#include "native_conversion_command_list.hpp"
#include "native_render_contract.hpp"
#include "component/console.hpp"
#include "game/assets.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::engine_stereo_sun_shadow
{
	namespace
	{
		constexpr std::uintptr_t site = 0x1407A7E3F, native = 0x1407B8210;
		std::array<std::atomic<ID3D11Resource*>, 2> watched{};
		std::atomic<ID3D11DeviceContext*> owner_context{};
		std::atomic_bool changed{};
		std::atomic_bool binding_uncertain{};
		std::atomic_bool draw_observer_armed{};
		std::atomic<ID3D11DepthStencilView*> last_matching_dsv{};
		std::atomic<ID3D11RenderTargetView*> last_matching_rtv{};
		std::array<std::atomic<ID3D11View*>, 8> non_matching_views{};
		std::atomic_size_t non_matching_cursor{};
		thread_local bool producing_left{};
		std::atomic_uint64_t left_count{}, skipped{}, skipped_auxiliary{}, refused{}, dirty_count{}, saved{};
		std::atomic_uint64_t reject_contract{}, reject_cache{}, reject_delegated{},
			reject_texture{}, reject_post_left{}, reject_right_mismatch{};
		std::atomic_uint32_t last_tile{}, last_configured{}, last_active{},
			last_main_target{}, last_trans_target{}, last_trans_enabled{},
			last_cache_enabled{};
		template<class T> T read(const void* data, std::size_t offset) noexcept
		{
			T result{}; std::memcpy(&result, static_cast<const std::uint8_t*>(data) + offset, sizeof(result)); return result;
		}
		[[nodiscard]] bool has_watched() noexcept
		{
			return watched[0].load(std::memory_order_acquire) != nullptr ||
				watched[1].load(std::memory_order_acquire) != nullptr;
		}
		[[nodiscard]] bool matches_watched_resource(const ID3D11Resource* const resource) noexcept
		{
			if (!resource) return false;
			return resource == watched[0].load(std::memory_order_acquire) ||
				resource == watched[1].load(std::memory_order_acquire);
		}
		[[nodiscard]] bool is_cached_non_matching(ID3D11View* const view) noexcept
		{
			if (!view) return false;
			for (const auto& slot : non_matching_views)
			{
				if (slot.load(std::memory_order_relaxed) == view) return true;
			}
			return false;
		}
		void cache_non_matching(ID3D11View* const view) noexcept
		{
			if (!view || is_cached_non_matching(view)) return;
			const auto slot = non_matching_cursor.fetch_add(1, std::memory_order_relaxed) % non_matching_views.size();
			non_matching_views[slot].store(view, std::memory_order_relaxed);
		}
		void clear_non_matching_views() noexcept
		{
			for (auto& slot : non_matching_views)
			{
				slot.store(nullptr, std::memory_order_relaxed);
			}
			non_matching_cursor.store(0, std::memory_order_relaxed);
		}
		void write(ID3D11Resource* resource) noexcept
		{
			if (producing_left) return;
			if (matches_watched_resource(resource)) changed.store(true, std::memory_order_release);
		}
		bool dsv_matches_watched(ID3D11DepthStencilView* view, ID3D11Resource* target,
			ID3D11Resource* secondary) noexcept
		{
			if (!view || (!target && !secondary)) return false;
			if (view == last_matching_dsv.load(std::memory_order_relaxed)) return true;
			if (is_cached_non_matching(view)) return false;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			view->GetResource(&resource);
			if (resource && (resource.Get() == target || resource.Get() == secondary))
			{
				last_matching_dsv.store(view, std::memory_order_relaxed);
				return true;
			}
			cache_non_matching(view);
			return false;
		}
		void write_view(ID3D11View* view) noexcept
		{
			auto* const primary = watched[0].load(std::memory_order_acquire);
			auto* const secondary = watched[1].load(std::memory_order_acquire);
			if (!view || (!primary && !secondary)) return;
			if (view == last_matching_dsv.load(std::memory_order_relaxed) ||
				view == last_matching_rtv.load(std::memory_order_relaxed))
			{
				if (!producing_left) changed.store(true, std::memory_order_release);
				return;
			}
			if (is_cached_non_matching(view)) return;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			view->GetResource(&resource);
			if (resource && (resource.Get() == primary || resource.Get() == secondary))
			{
				write(resource.Get());
				return;
			}
			if (watched[0].load(std::memory_order_acquire) == primary &&
				watched[1].load(std::memory_order_acquire) == secondary)
			{
				cache_non_matching(view);
			}
		}
		void resource_operation(const engine_stereo_resource_ops::event& event) noexcept
		{
			if (!has_watched()) return;
			write(event.destination); write_view(event.view);
		}
		void clear_color(ID3D11DeviceContext*, ID3D11RenderTargetView* view, std::uintptr_t,
			std::uint32_t, std::uintptr_t, std::uint64_t) noexcept { write_view(view); }
		void clear_depth(ID3D11DeviceContext*, ID3D11DepthStencilView* view, std::uint32_t,
			std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint64_t) noexcept { write_view(view); }
		void invocation(ID3D11DeviceContext* context, engine_stereo_execution::api operation, std::uintptr_t,
			std::uint32_t, std::uintptr_t, std::uint64_t, std::uint8_t, const std::array<std::uint64_t, 6>&) noexcept
		{
			if (!has_watched() || producing_left) return;
			using api = engine_stereo_execution::api;
			if (operation == api::execute_command_list) return;
			if (engine_stereo_execution::in_known_conversion(context)) return;
			if (operation == api::dispatch || operation == api::dispatch_indirect) return; // admitted depth resource has no UAV bind
			if (context != owner_context.load(std::memory_order_acquire)) { changed = true; return; }
			const bool has_secondary = watched[1].load(std::memory_order_acquire) != nullptr;
			if (binding_uncertain.load(std::memory_order_acquire))
			{
				Microsoft::WRL::ComPtr<ID3D11RenderTargetView> color;
				Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
				context->OMGetRenderTargets(has_secondary ? 1u : 0u,
					has_secondary ? color.GetAddressOf() : nullptr, &depth);
				write_view(depth.Get());
				if (has_secondary) write_view(color.Get());
				return;
			}
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			if (!binding.valid) { changed = true; return; }
			write_view(reinterpret_cast<ID3D11DepthStencilView*>(binding.depth_stencil));
			if (has_secondary)
			{
				write_view(reinterpret_cast<ID3D11RenderTargetView*>(binding.render_target_0));
			}
		}
		void set_draw_observer_armed(const bool armed) noexcept
		{
			if (draw_observer_armed.exchange(armed, std::memory_order_acq_rel) == armed) return;
			engine_stereo_execution::set_invocation_observer(
				engine_stereo_execution::invocation_observer_channel::sun_reuse,
				armed ? invocation : nullptr);
		}
		void on_depth_bind(ID3D11DeviceContext* context, ID3D11DepthStencilView* depth) noexcept
		{
			auto* const primary = watched[0].load(std::memory_order_acquire);
			auto* const secondary = watched[1].load(std::memory_order_acquire);
			if (!primary && !secondary) return;
			if (context != owner_context.load(std::memory_order_acquire)) return;
			if (engine_stereo_execution::in_known_conversion(context)) return;
			const bool matches = dsv_matches_watched(depth, primary, secondary) || secondary != nullptr;
			binding_uncertain.store(false, std::memory_order_release);
			set_draw_observer_armed(matches);
		}
		void on_execute_command_list(ID3D11DeviceContext*, ID3D11CommandList* commands, const bool restore_state) noexcept
		{
			if (!has_watched() || producing_left) return;
			if (!restore_state || !native_conversion_command_list::is_marked(commands))
			{
				changed.store(true, std::memory_order_release);
				return;
			}
			// Driver replay may update the binding observer before RestoreContextState restores the DSV.
			binding_uncertain.store(true, std::memory_order_release);
			set_draw_observer_armed(true);
		}
		void invoke(void* data, void* record)
		{
			if (engine_stereo_owner_pass::begin_sun_shadow(data, record)) return;
			bool returned{};
			const auto exit = gsl::finally([&] { if (returned) engine_stereo_owner_pass::end_sun_shadow(data, record); else unwatch(); });
			reinterpret_cast<void(*)(void*,void*)>(native)(data, record);
			returned = true;
		}
		[[nodiscard]] bool valid_tile_size(const std::uint32_t tile) noexcept
		{
			return tile == 512u || tile == 1024u || tile == 2048u || tile == 4096u;
		}
	}
	bool snapshot(void* data, void* record, inputs& output,
		Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
		Microsoft::WRL::ComPtr<ID3D11Texture2D>* const transparent_texture,
		const bool record_diagnostics) noexcept
	{
		output = {};
		texture.Reset();
		if (transparent_texture != nullptr) transparent_texture->Reset();
		if (!data || !record || read<std::uintptr_t>(record, 0x1F68) != reinterpret_cast<std::uintptr_t>(data) ||
			read<unsigned>(record, 0x200) != 1)
		{
			if (record_diagnostics) ++reject_contract;
			return false;
		}
		const auto tile = read<unsigned>(record, 0x1FA0);
		const auto configured = read<unsigned>(record, 0x1FA4);
		const auto main_target = read<unsigned>(record, 0x1FA8);
		const auto trans_enabled = read<unsigned>(record, 0x220);
		const auto trans_target = read<unsigned>(record, 0x1FB0);
		const auto cache_enabled = read<unsigned>(record, 0x2FB8);
		const auto active = read<int>(data, 0x61ADA0);
		if (record_diagnostics)
		{
			last_tile.store(tile, std::memory_order_relaxed);
			last_configured.store(configured, std::memory_order_relaxed);
			last_active.store(active >= 0 ? static_cast<std::uint32_t>(active) : 0u, std::memory_order_relaxed);
			last_main_target.store(main_target, std::memory_order_relaxed);
			last_trans_target.store(trans_target, std::memory_order_relaxed);
			last_trans_enabled.store(trans_enabled, std::memory_order_relaxed);
			last_cache_enabled.store(cache_enabled, std::memory_order_relaxed);
		}
		if (cache_enabled != 0)
		{
			if (record_diagnostics) ++reject_cache;
			return false;
		}
		if (!valid_tile_size(tile) || configured < 2 || configured > inputs::max_partitions ||
			active < 1 || static_cast<unsigned>(active) > configured ||
			(main_target != 26 && main_target != 27) ||
			(trans_enabled != 0 && trans_target != 28 && trans_target != 29))
		{
			if (record_diagnostics) ++reject_contract;
			return false;
		}
		for (unsigned i{}; i < static_cast<unsigned>(active); ++i)
		{
			if (read<unsigned>(data, 0x540FC8 + (26 + i) * 4) != 0 ||
				(trans_enabled != 0 && read<unsigned>(data, 0x540FC8 + (30 + i) * 4) != 0))
			{
				if (record_diagnostics) ++reject_delegated;
				return false; // delegated/native list progression stays original
			}
			std::memcpy(output.lists[i].data(), static_cast<const std::uint8_t*>(record) + 0x2FC8 + (26 + i) * 0x130, 0x130);
			std::memcpy(output.partitions[i].data(), static_cast<const std::uint8_t*>(data) + 0x542650 + i * 0x48270, 0x180);
			if (trans_enabled != 0)
			{
				std::memcpy(output.transparent_lists[i].data(),
					static_cast<const std::uint8_t*>(record) + 0x2FC8 + (30 + i) * 0x130, 0x130);
			}
		}
		std::memcpy(output.sun_visibility.data(), static_cast<const std::uint8_t*>(data) + 0x540D0C, output.sun_visibility.size());
		auto* image = *reinterpret_cast<game::GfxImage**>(
			native_render_contract::target_registry_base + main_target * native_render_contract::target_registry_stride);
		if (!image || !image->texture.shaderView)
		{
			if (record_diagnostics) ++reject_texture;
			return false;
		}
		Microsoft::WRL::ComPtr<ID3D11Resource> resource;
		image->texture.shaderView->GetResource(&resource);
		if (!resource || FAILED(resource.As(&texture)) || texture.Get() != image->texture.map)
		{
			if (record_diagnostics) ++reject_texture;
			return false;
		}
		D3D11_TEXTURE2D_DESC d{};
		texture->GetDesc(&d);
		if (d.Width != tile || d.Height != tile * configured ||
			d.Format != DXGI_FORMAT_R24G8_TYPELESS || d.MipLevels != 1 ||
			d.ArraySize != 1 || d.SampleDesc.Count != 1 || d.SampleDesc.Quality ||
			d.Usage != D3D11_USAGE_DEFAULT ||
			d.BindFlags != (D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE) ||
			d.CPUAccessFlags || d.MiscFlags)
		{
			texture.Reset();
			if (record_diagnostics) ++reject_texture;
			return false;
		}
		if (trans_enabled != 0)
		{
			auto* trans_image = *reinterpret_cast<game::GfxImage**>(
				native_render_contract::target_registry_base + trans_target * native_render_contract::target_registry_stride);
			if (!trans_image || !trans_image->texture.shaderView)
			{
				texture.Reset();
				if (record_diagnostics) ++reject_texture;
				return false;
			}
			Microsoft::WRL::ComPtr<ID3D11Resource> trans_res;
			trans_image->texture.shaderView->GetResource(&trans_res);
			Microsoft::WRL::ComPtr<ID3D11Texture2D> trans_tex;
			if (!trans_res || FAILED(trans_res.As(&trans_tex)) || trans_tex.Get() != trans_image->texture.map)
			{
				texture.Reset();
				if (record_diagnostics) ++reject_texture;
				return false;
			}
			D3D11_TEXTURE2D_DESC td{};
			trans_tex->GetDesc(&td);
			if (td.Width == 0 || td.Height == 0 || td.MipLevels != 1 || td.ArraySize != 1 ||
				td.SampleDesc.Count != 1 || td.SampleDesc.Quality || td.Usage != D3D11_USAGE_DEFAULT ||
				(td.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0 || td.CPUAccessFlags || td.MiscFlags)
			{
				texture.Reset();
				if (record_diagnostics) ++reject_texture;
				return false;
			}
			output.transparent_resource = reinterpret_cast<std::uintptr_t>(trans_res.Get());
			if (transparent_texture != nullptr) *transparent_texture = std::move(trans_tex);
		}
		output.data = reinterpret_cast<std::uintptr_t>(data);
		output.resource = reinterpret_cast<std::uintptr_t>(resource.Get());
		output.tile = tile;
		output.configured_partitions = configured;
		output.active_partitions = static_cast<std::uint32_t>(active);
		output.main_target = main_target;
		output.transparent_target = trans_target;
		output.transparent_enabled = trans_enabled != 0;
		return true;
	}
	void watch(ID3D11Resource* resource, ID3D11DeviceContext* context,
		ID3D11Resource* transparent_resource) noexcept
	{
		unwatch(); changed = false; binding_uncertain = false; owner_context = context; producing_left = true;
		last_matching_dsv.store(nullptr, std::memory_order_relaxed);
		last_matching_rtv.store(nullptr, std::memory_order_relaxed);
		clear_non_matching_views();
		engine_stereo_resource_ops::set_observer(engine_stereo_resource_ops::observer_channel::sun_reuse, resource_operation);
		engine_stereo_output_merger::set_clear_observers(engine_stereo_output_merger::clear_observer_channel::sun_reuse, clear_color, clear_depth);
		engine_stereo_output_merger::set_depth_bind_observer(on_depth_bind);
		engine_stereo_execution::set_execute_command_list_observer(on_execute_command_list);
		watched[1].store(transparent_resource, std::memory_order_release);
		watched[0].store(resource, std::memory_order_release);
		if (context != nullptr)
		{
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			if (binding.valid)
			{
				on_depth_bind(context, reinterpret_cast<ID3D11DepthStencilView*>(binding.depth_stencil));
			}
			else
			{
				Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
				context->OMGetRenderTargets(0, nullptr, &depth);
				on_depth_bind(context, depth.Get());
			}
		}
	}
	void unwatch() noexcept
	{
		producing_left = false;
		last_matching_dsv.store(nullptr, std::memory_order_relaxed);
		last_matching_rtv.store(nullptr, std::memory_order_relaxed);
		clear_non_matching_views();
		const auto had0 = watched[0].exchange(nullptr, std::memory_order_acq_rel) != nullptr;
		const auto had1 = watched[1].exchange(nullptr, std::memory_order_acq_rel) != nullptr;
		if (!had0 && !had1) return;
		set_draw_observer_armed(false);
		engine_stereo_execution::set_execute_command_list_observer(nullptr);
		engine_stereo_output_merger::set_depth_bind_observer(nullptr);
		engine_stereo_resource_ops::set_observer(engine_stereo_resource_ops::observer_channel::sun_reuse, nullptr);
		engine_stereo_output_merger::set_clear_observers(engine_stereo_output_merger::clear_observer_channel::sun_reuse, nullptr, nullptr);
		clear_non_matching_views();
	}
	void finish_left() noexcept { producing_left = false; }
	bool watching(std::uintptr_t resource, ID3D11DeviceContext* context,
		std::uintptr_t transparent_resource) noexcept
	{
		return reinterpret_cast<std::uintptr_t>(watched[0].load(std::memory_order_acquire)) == resource &&
			reinterpret_cast<std::uintptr_t>(watched[1].load(std::memory_order_acquire)) == transparent_resource &&
			owner_context.load() == context;
	}
	bool modified() noexcept { return changed.load(std::memory_order_acquire); }
	void note_left() noexcept { ++left_count; }
	void note_skip(std::uint32_t count, bool auxiliary) noexcept
	{
		++skipped;
		if (auxiliary) ++skipped_auxiliary;
		saved += count;
	}
	void note_refusal() noexcept { ++refused; if (modified()) ++dirty_count; }
	void note_post_left_rejection() noexcept { ++reject_post_left; }
	void note_right_mismatch() noexcept { ++reject_right_mismatch; }
	counters get_counters() noexcept
	{
		return {
			left_count.load(std::memory_order_relaxed),
			skipped.load(std::memory_order_relaxed),
			refused.load(std::memory_order_relaxed),
			dirty_count.load(std::memory_order_relaxed),
			saved.load(std::memory_order_relaxed),
			skipped_auxiliary.load(std::memory_order_relaxed),
			reject_contract.load(std::memory_order_relaxed),
			reject_cache.load(std::memory_order_relaxed),
			reject_delegated.load(std::memory_order_relaxed),
			reject_texture.load(std::memory_order_relaxed),
			reject_post_left.load(std::memory_order_relaxed),
			reject_right_mismatch.load(std::memory_order_relaxed),
			last_tile.load(std::memory_order_relaxed),
			last_configured.load(std::memory_order_relaxed),
			last_active.load(std::memory_order_relaxed),
			last_main_target.load(std::memory_order_relaxed),
			last_trans_target.load(std::memory_order_relaxed),
			last_trans_enabled.load(std::memory_order_relaxed),
			last_cache_enabled.load(std::memory_order_relaxed),
		};
	}
	void install() noexcept
	{
		constexpr std::array<std::uint8_t,5> expected{0xE8,0xCC,0x03,0x01,0x00};
		if (!utils::hook_validation::validate_executable_target(reinterpret_cast<const void*>(site)) ||
			std::memcmp(reinterpret_cast<const void*>(site), expected.data(), expected.size()))
		{ console::error("[VR] Sun reuse unavailable: native callsite mismatch\n"); return; }
		try { utils::hook::call(site, reinterpret_cast<void*>(invoke)); }
		catch (const std::exception& e) { console::error("[VR] Sun reuse unavailable: %s\n", e.what()); }
	}
}

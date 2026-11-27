#pragma once
#include <array>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

namespace vr::engine_stereo_owner_pass
{
	// Native callback contract implemented by the exact stereo transaction owner.
	[[nodiscard]] bool begin_sun_shadow(void* data, void* record) noexcept;
	void end_sun_shadow(void* data, void* record) noexcept;
}

namespace vr::engine_stereo_sun_shadow
{
	// H2's observed synchronous, uncached sun contract across 1..4 cascades
	// (main + optional transparent atlas). Cached modes retain native execution.
	struct inputs
	{
		static constexpr std::size_t max_partitions = 4;

		std::uintptr_t data{}, resource{}, transparent_resource{};
		std::uint32_t tile{}, configured_partitions{}, active_partitions{},
			main_target{}, transparent_target{};
		bool transparent_enabled{};
		std::array<std::uint8_t, 0x104> sun_visibility{};
		std::array<std::array<std::uint8_t, 0x130>, max_partitions> lists{};
		std::array<std::array<std::uint8_t, 0x130>, max_partitions> transparent_lists{};
		std::array<std::array<std::uint8_t, 0x180>, max_partitions> partitions{};
		bool operator==(const inputs& other) const noexcept
		{
			return data == other.data && resource == other.resource &&
				transparent_resource == other.transparent_resource &&
				tile == other.tile &&
				configured_partitions == other.configured_partitions &&
				active_partitions == other.active_partitions &&
				main_target == other.main_target &&
				transparent_target == other.transparent_target &&
				transparent_enabled == other.transparent_enabled &&
				sun_visibility == other.sun_visibility &&
				lists == other.lists &&
				transparent_lists == other.transparent_lists &&
				partitions == other.partitions;
		}
	};
	struct range
	{
		inputs left;
		std::uint32_t begin{}, end{};
		bool open{}, valid{};
		bool accepts(const inputs& right, std::uint32_t cursor, std::uint32_t left_count, bool dirty) const noexcept
		{
			return valid && !open && !dirty && end >= begin && cursor == begin && end <= left_count && left == right;
		}
	};
	struct counters
	{
		std::uint64_t left{}, skipped{}, refused{}, dirty{}, saved_boundaries{};
		std::uint64_t skipped_auxiliary{};
		std::uint64_t reject_contract{}, reject_cache{}, reject_delegated{},
			reject_texture{}, reject_post_left{}, reject_right_mismatch{};
		std::uint32_t last_tile{}, last_configured{}, last_active{},
			last_main_target{}, last_trans_target{}, last_trans_enabled{},
			last_cache_enabled{};
	};
	void install() noexcept;
	bool snapshot(void* data, void* record, inputs& output,
		Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
		Microsoft::WRL::ComPtr<ID3D11Texture2D>* transparent_texture = nullptr,
		bool record_diagnostics = false) noexcept;
	void watch(ID3D11Resource* resource, ID3D11DeviceContext* context,
		ID3D11Resource* transparent_resource = nullptr) noexcept;
	void unwatch() noexcept;
	void finish_left() noexcept;
	bool watching(std::uintptr_t resource, ID3D11DeviceContext* context,
		std::uintptr_t transparent_resource = 0) noexcept;
	bool modified() noexcept;
	void note_left() noexcept;
	void note_skip(std::uint32_t boundaries, bool auxiliary = false) noexcept;
	void note_refusal() noexcept;
	void note_post_left_rejection() noexcept;
	void note_right_mismatch() noexcept;
	counters get_counters() noexcept;
}

#pragma once
#include "weapon_carry_grip.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::weapons::carry
{
	inline result back_slot_candidate(const inventory& owned, hand actor, const holsters& slots,
	                                  hands::vec point, bool allow_storage) noexcept
	{
		if (!valid_hand(actor)) return {};
		constexpr unsigned back_slot_bits = (1u << 2) | (1u << 3);
		if (const auto* held = owned.in_hand(actor))
		{
			const auto target = hit(slots, point, back_slot_bits);
			if (!back_slot(target) || !inventory::accepts(*held, target)) return {};
			// Reuse the release rules without changing ownership or preparing a native transfer.
			auto preview = owned;
			const auto change = preview.release(held->id, 1u << unsigned(actor), target, true,
			                                    [](const instance&) { return false; }, false, allow_storage);
			if (change.action == outcome::stowed || change.action == outcome::exchanged) return change;
			return {};
		}
		const auto slot = draw_contact(owned, slots, point);
		if (!back_slot(slot)) return {};
		const auto* item = owned.next_back(slot);
		return item ? result{outcome::drawn, item->id} : result{};
	}

	class back_slot_feedback
	{
	public:
		void reset() noexcept { *this = {}; }
		// Near includes a small exit margin. An operation consumes this visit too,
		// so the newly drawn/exchanged weapon cannot immediately emit another cue.
		template<class Ready>
		unsigned update(const controller_input::frame& input, unsigned near_hands, unsigned operated, Ready&& ready)
		{
			if (reference_ != input.reference_generation || continuity_ != input.continuity_generation)
				reset();
			reference_ = input.reference_generation;
			continuity_ = input.continuity_generation;
			unsigned pulses{};
			for (unsigned h = 0; h < 2; ++h)
			{
				const auto bit = 1u << h;
				if (!input.focused || input.orientation_settling || !input.sequence ||
				    !input.grip[h].valid || !input.aim[h].valid || !(near_hands & bit))
				{
					latched_ &= ~bit;
					continue;
				}
				if (operated & bit) latched_ |= bit;
				if (!(latched_ & bit) && ready(hand(h)))
				{
					latched_ |= bit;
					pulses |= bit;
				}
			}
			return pulses;
		}
	private:
		std::uint64_t reference_{}, continuity_{};
		unsigned latched_{};
	};

	// Generalized per-zone hover latch: fires a pulse once when entering a distinct
	// zone (e.g., left vs right shoulder, waist holster, chest rig, or ammo pouch)
	// and preserves exit hysteresis without double-firing on jitter.
	class zone_hover_feedback
	{
	public:
		void reset() noexcept { *this = {}; }
		template<class ZoneForHand, class Ready>
		std::array<std::uint8_t, 2> update(const controller_input::frame& input, unsigned valid_hands,
		                                   unsigned operated, ZoneForHand&& zone_for_hand, Ready&& ready)
		{
			if (reference_ != input.reference_generation || continuity_ != input.continuity_generation)
				reset();
			reference_ = input.reference_generation;
			continuity_ = input.continuity_generation;
			std::array<std::uint8_t, 2> pulses{};
			for (unsigned h = 0; h < 2; ++h)
			{
				const auto bit = 1u << h;
				if (!input.focused || input.orientation_settling || !input.sequence || !(valid_hands & bit) ||
				    !input.grip[h].valid || !input.aim[h].valid)
				{
					latched_[h] = 0;
					continue;
				}
				const auto zone = zone_for_hand(hand(h), latched_[h]);
				if (!zone)
				{
					latched_[h] = 0;
					continue;
				}
				if (operated & bit)
				{
					latched_[h] = zone;
					continue;
				}
				if (latched_[h] != zone)
				{
					if (ready(hand(h), zone))
					{
						latched_[h] = zone;
						pulses[h] = zone;
					}
					else
					{
						latched_[h] = 0;
					}
				}
			}
			return pulses;
		}
	private:
		std::uint64_t reference_{}, continuity_{};
		std::array<std::uint8_t, 2> latched_{};
	};
}

#pragma once
#include "core.hpp"
#include "../weapon_scene.hpp"
#include "../body_supply_volume.hpp"
#include "../weapon_holsters.hpp"

namespace vr::gameplay::hand_interaction
{
	inline float waist_distance(const head_pose_bridge::spatial_frame& body, const std::array<hands::anchor,2>& wrists,
		unsigned valid_hands, hand actor, const body_supply_layout& layout, float radius) noexcept
	{
		if(!vr::valid_hand(actor) || !(valid_hands&(1u<<unsigned(actor))) || body.units_per_meter<=0)return INFINITY;
		const auto point=wrists[unsigned(actor)].position;
		float result=INFINITY;
		for(const auto& v:body_supply_volumes(body.head_position,body.head_yaw_axis,body.units_per_meter,layout,radius))
			result=std::min(result,v.distance(point)/body.units_per_meter);
		if(body.body.valid)
		{
			const float units=body.units_per_meter;
			const float bonus=0.04f;
			for(const auto& v:body_supply_volumes(body.body.position,body.body.yaw_axis,units,layout,radius+bonus))
				result=std::min(result,std::max(0.f,v.distance(point)/units-bonus));
			auto front_layout=layout;
			front_layout.half_width*=0.65f;
			front_layout.down=std::max(0.40f,layout.down-0.08f);
			front_layout.height+=0.06f;
			const auto torso_front=hands::add(body.body.position,hands::scale(body.body.yaw_axis[0],0.12f*units));
			for(const auto& v:body_supply_volumes(torso_front,body.body.yaw_axis,units,front_layout,radius+bonus))
				result=std::min(result,std::max(0.f,v.distance(point)/units-bonus));
			const auto head_front=hands::add(body.head_position,hands::scale(body.head_yaw_axis[0],0.12f*units));
			for(const auto& v:body_supply_volumes(head_front,body.head_yaw_axis,units,front_layout,radius+bonus))
				result=std::min(result,std::max(0.f,v.distance(point)/units-bonus));
		}
		return result;
	}
	struct frame
	{
		controller_input::frame input{};
		head_pose_bridge::spatial_frame body{};
		std::array<hands::anchor,2> wrists{};
		std::array<weapons::carry::scene,15> objects{};
		unsigned valid_hands{};
		// A carry release can consume a press before this acquisition batch.
		// Keep physical input intact for settling existing mechanics.
		unsigned blocked_acquisitions{};
		weapons::carry::holsters holsters{};
		const weapons::carry::scene* find(weapons::weapon_identity id)const noexcept
		{for(const auto& s:objects)if(s.owner.id()==id && s.assembly)return &s;return nullptr;}
		float waist(hand actor,const body_supply_layout& layout,float radius)const noexcept
		{return waist_distance(body,wrists,valid_hands,actor,layout,radius);}
	};
	// Binding data has assembly lifetime. It contains no input edge or world
	// pose: a culled viewmodel cannot make otherwise current tracking stale.
	struct hand_binding
	{
		hands::quat wrist{0,0,0,1},mirror{0,0,0,1};
		bool valid{};
		hands::quat paired_wrist{0,0,0,1};bool paired_valid{};
	};
}

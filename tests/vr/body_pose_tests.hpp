#pragma once
#include "component/vr/body_pose.hpp"
#include "component/vr/gameplay/body_equipment.hpp"
#include "component/vr/gameplay/weapon_holsters.hpp"
#include <limits>

namespace body_pose_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr;using namespace gameplay::hands;using namespace std::chrono_literals;
		const body_pose::axes identity{vec{1,0,0},vec{0,1,0},vec{0,0,1}};
		const auto yaw=[](float degree){float a=degree*.01745329252f,c=std::cos(a),s=std::sin(a);return body_pose::axes{vec{c,s,0},vec{-s,c,0},vec{0,0,1}};};
		const auto start=std::chrono::steady_clock::time_point{1s};body_pose::estimator estimator;
		auto neutral=estimator.update({},identity,0,1,start);
		check(neutral.valid && length(neutral.position)<1e-5f,"body anchor initializes at current neutral head-equivalent height");
		const auto looking=yaw(30);const vec eye_delta{(looking[0][0]-1)*.08f,looking[0][1]*.08f,0};
		body_pose::estimate glance;
		for(int i=1;i<=100;++i)glance=estimator.update(eye_delta,looking,30,1,start+i*10ms);
		check(length(glance.position)<1e-5f && length(sub(glance.yaw_axis[0],identity[0]))<1e-5f,"sustained small head turn around the neck does not drag equipment");
		auto same=estimator.update(eye_delta,looking,30,1,start+1000ms);
		check(same.position==glance.position && same.yaw_axis==glance.yaw_axis,"duplicate camera/eye queries cannot advance the body filter");
		estimator.reset();estimator.update({},identity,0,1,start);
		auto lean=estimator.update({.05f,0,0},identity,0,1,start+10ms);
		check(length(lean.position)<1e-5f,"small room-scale head lean stays inside the translation tolerance");
		for(int i=2;i<=100;++i)lean=estimator.update({.4f,0,-.4f},identity,0,1,start+i*10ms);
		check(lean.position[0]>.329f && lean.position[0]<.331f && std::abs(lean.position[2]+.4f)<.001f,"larger room-scale movement and physical crouch follow within bounded horizontal offset");
		estimator.reset();estimator.update({},identity,0,1,start);
		auto first=estimator.update({},yaw(90),90,1,start+10ms);
		check(std::atan2(first.yaw_axis[0][1],first.yaw_axis[0][0])*57.29578f<=1.801f,"large head turn cannot instantly spin the torso");
		for(int i=2;i<=150;++i)first=estimator.update({},yaw(90),90,1,start+i*10ms);
		const float angle=std::atan2(first.yaw_axis[0][1],first.yaw_axis[0][0])*57.29578f;
		check(angle>54.5f && angle<55.1f,"sustained large turn brings the body within the 35 degree neck tolerance");
		const auto moved=body_pose::to_world(neutral,{100,-50,60},yaw(90),40);
		check(moved.position==vec{100,-50,60} && length(sub(moved.yaw_axis[0],vec{0,1,0}))<.00001f,"game locomotion and snap turn transform the body immediately, without smoothing the world base");
		for(int mode=0;mode<4;++mode)
		{
			estimator.reset();estimator.update({},identity,0,1,start);estimator.update({},yaw(90),90,1,start+10ms);
			if(mode==3)estimator.reset();
			const auto reset=estimator.update(mode==2?vec{2,0,0}:vec{},identity,0,mode==0?2:1,start+(mode==1?200ms:20ms));
			check(length(sub(reset.yaw_axis[0],identity[0]))<1e-5f && reset.position==(mode==2?vec{2,0,0}:vec{}),"recenter, tracking gap, jump and explicit reset discard prior body history");
		}
		check(!estimator.update({std::numeric_limits<float>::quiet_NaN(),0,0},identity,0,1,start).valid,"nonfinite tracking cannot publish equipment pose");
		auto reflected=identity;reflected[2]={0,0,-1};check(!estimator.update({},reflected,0,1,start).valid,"reflected tracking axes cannot drive the body estimate");
		estimator.reset();const auto wrap_start=estimator.update({},yaw(170),170,1,start);
		const auto wrap_end=estimator.update({},yaw(-170),-170,1,start+10ms);
		check(length(sub(wrap_start.yaw_axis[0],wrap_end.yaw_axis[0]))<1e-5f,"crossing the yaw wrap does not invent a full torso turn");
		// Turning 180 degrees and stepping forward in roomscale must update torso eye
		// compensation to the 180-degree heading rather than retaining a +16 cm offset.
		estimator.reset();
		estimator.update({.08f,0,0},identity,0,1,start);
		body_pose::estimate turned_180;
		for(int i=1;i<=250;++i)
		{
			const float deg=std::min(215.f,i*2.5f);const auto ax=yaw(deg);
			turned_180=estimator.update({ax[0][0]*.08f,ax[0][1]*.08f,0},ax,deg,1,start+i*10ms);
		}
		for(int i=251;i<=350;++i)
		{
			const float x=-.08f-float(i-250)*.01f;
			turned_180=estimator.update({x,0,0},yaw(180.f),180.f,1,start+i*10ms);
		}
		for(int i=351;i<=450;++i)
			turned_180=estimator.update({-1.08f,0,0},yaw(180.f),180.f,1,start+i*10ms);
		check(length(sub(turned_180.position,vec{-1.08f,0,0}))<=.071f,
			"180-degree roomscale turn tracks turned head instead of keeping frozen 16 cm offset");
		// Pitching head downward to check chest gear must not lift torso zones upward.
		estimator.reset();
		estimator.update({},identity,0,1,start);
		const float down_rad=-70.f*.01745329252f;
		const body_pose::axes pitch_down{vec{std::cos(down_rad),0,std::sin(down_rad)},vec{0,1,0},vec{-std::sin(down_rad),0,std::cos(down_rad)}};
		auto look_down=estimator.update({},pitch_down,0,1,start+50ms);
		check(std::abs(look_down.position[2])<1e-5f,"looking downward keeps torso vertical anchor steady");
		{
			head_pose_bridge::spatial_frame frame;frame.units_per_meter=40;frame.head_position={120,70,90};frame.head_yaw_axis=yaw(30);
			frame.body={true,{100,50,90},identity};
			const auto chest=gameplay::equipment::locate_chest(frame);const auto holsters=gameplay::weapons::carry::locate_holsters(frame);
			check(chest.valid && holsters.valid && length(sub(chest.anchors[1].position,vec{101.8f,50,76.f}))<.0001f,
				"chest is 4.5 centimetres forward and 35 down from shared body anchor, independently of head lean/yaw");
			check(length(sub(holsters.centers[0],vec{100,59.2f,66}))<.0001f && length(sub(holsters.centers[1],vec{100,40.8f,66}))<.0001f,
				"left/right waist contacts use the same body anchor as chest rendering");
			check(gameplay::weapons::carry::hit(holsters,holsters.centers[0])==gameplay::weapons::carry::location::left_waist,
				"body-anchored visual waist center remains a valid grab/stow contact");
			frame.body.valid=false;
			const auto legacy=gameplay::equipment::locate_chest(frame);
			check(length(sub(legacy.anchors[1].position,chest.anchors[1].position))>1,"frames without an estimate retain explicit legacy fallback");
		}
	}
}

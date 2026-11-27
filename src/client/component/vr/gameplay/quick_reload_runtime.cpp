#include <std_include.hpp>
#include "quick_reload_runtime.hpp"
#include "hand_interaction/runtime.hpp"
#include "weapon_feedback.hpp"
#include "weapon_profile.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::weapons::quick_reload
{
	namespace {game::dvar_t* enabled{};}
	bool ready(dwell& timer,const hold& owner,std::uint64_t instance,std::uint64_t assembly,std::string_view feed,
		bool eligible,controller_input::clock::time_point now) noexcept
	{
		const auto* frame=hand_interaction::simulation();
		if(!enabled || !enabled->current.enabled || !eligible || !supported(feed) || !frame ||
			!valid_hand(owner.holding_hand()) || !(frame->valid_hands&(1u<<unsigned(owner.holding_hand()))))
		{timer.reset();return false;}
		const auto* live=frame->find(owner.id());
		if(!live || !assembly || live->assembly!=assembly || live->owner.holding_hand()!=owner.holding_hand() || live->owner.rear_revision!=owner.rear_revision)
		{timer.reset();return false;}
		auto holsters=frame->holsters;
		const bool active_dwell=timer.active(owner,frame->input,now);
		auto slot=carry::hit(holsters,frame->wrists[unsigned(owner.holding_hand())].position,3);
		if(slot==carry::location::absent && active_dwell && holsters.valid && frame->body.units_per_meter>0.f)
		{
			const float margin=.04f*frame->body.units_per_meter;
			for(unsigned i=0;i<2;++i){holsters.radii[i]+=margin;holsters.volumes[i].radius+=margin;}
			slot=carry::hit(holsters,frame->wrists[unsigned(owner.holding_hand())].position,3);
		}
		const int index=slot==carry::location::left_waist?0:slot==carry::location::right_waist?1:-1;
		const bool complete=timer.update(owner,instance,frame->input,index,true,now);
		if(timer.began() && live->authored)
		{
			feedback::event sound; sound.owner=owner;sound.reference=frame->input.reference_generation;sound.at=now;
			sound.position=live->gun.position;sound.mechanical_instance=instance;sound.quick_reload_start=true;
			sound.definition=live->authored->reload;sound.cylinder_definition=live->authored->cylinder;
			sound.break_definition=live->authored->break_open;feedback::publish(sound);
			feedback::carry_readiness(owner.holding_hand(),frame->input);
		}
		if(complete)feedback::carry_confirmation(owner.holding_hand(),frame->input);
		return complete;
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			enabled=dvars::register_bool(settings::quick_reload.name,settings::quick_reload.default_value,
				game::DVAR_FLAG_SAVED,"Fill eligible empty feeds after holding the weapon at a waist holster for one second; preserve the action");
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::quick_reload::component)

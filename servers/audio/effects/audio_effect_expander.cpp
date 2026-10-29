/**************************************************************************/
/*  audio_effect_expander.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "audio_effect_expander.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "servers/audio/audio_server.h"

void AudioEffectExpanderInstance::process(const AudioFrame *p_src_frames, AudioFrame *p_dst_frames, int p_frame_count) {
	float threshold = Math::db_to_linear(base->threshold);
	float sample_rate = AudioServer::get_singleton()->get_mix_rate();

	float ratatcoef = std::exp(-1 / (0.00001f * sample_rate));
	float ratrelcoef = std::exp(-1 / (0.5f * sample_rate));
	float attime = base->attack_us / 1000000.0;
	float reltime = base->release_ms / 1000.0;
	float atcoef = std::exp(-1 / (attime * sample_rate));
	float relcoef = std::exp(-1 / (reltime * sample_rate));
	
	//for downward expander, we 
	float rise_coef = upward ? atcoef : relcoef; //depth increasing
	float fall_coef = upward ? relcoef : atcoef; //depth decreasing

	float makeup = Math::db_to_linear(base->gain);

	float mix = base->mix;
	float gr_meter_decay = std::exp(1 / (1 * sample_rate));

	bool  upward = base->range > 0.0f;
	float grange_db = Math::abs(base->range);
	float cratio = base->ratio;
	float max_depth = grange_db / MAX(cratio - 1.0f, 0.01f);


	const AudioFrame *src = p_src_frames;

	if (base->sidechain != StringName() && current_channel != -1) {
		int bus = AudioServer::get_singleton()->thread_find_bus_index(base->sidechain);
		if (bus >= 0) {
			src = AudioServer::get_singleton()->thread_get_channel_mix_buffer(bus, current_channel);
		}
	}

	for (int i = 0; i < p_frame_count; i++) {
		AudioFrame s = src[i];
		//convert to positive
		s.left = Math::abs(s.left);
		s.right = Math::abs(s.right);

		float peak = MAX(MAX(s.left, s.right), CMP_EPSILON); //avoid log(0) and negative values

		// float overdb = 2.08136898f * Math::linear_to_db(peak / threshold);
		float overdb = Math::linear_to_db(peak / threshold);
		overdb = upward ? MAX(overdb, 0.0f) : MIN(overdb, 0.0f);
		float depth = MIN(Math::abs(overdb), max_depth);

		if (depth - rundb > 5) { // difference is too large
			averatio = 4;
		}


		if (depth > rundb) {
			rundb = depth + rise_coef * (rundb - depth);
			runratio = averatio + ratatcoef * (runratio - averatio);
		} else {
			rundb = depth + fall_coef * (rundb - depth);
			runratio = averatio + ratrelcoef * (runratio - averatio);
		}

		averatio = runratio;

		float gain_db = MIN(rundb * (cratio - 1), grange_db); //gain from ratio and threshold
		float grv = Math::db_to_linear(upward ? gain_db : -gain_db);

		runmax = maxover + relcoef * (runmax - maxover); // highest peak for setting att/rel decays in realtime
		maxover = runmax;

		if ((upward && grv > gr_meter) || (!upward && grv < gr_meter)) {
			gr_meter = grv;
		} else if (upward){
			gr_meter /= gr_meter_decay;
			if (gr_meter < 1.0f) {
				gr_meter = 1.0f;
			}
		} else {
			gr_meter *= gr_meter_decay;
			if (gr_meter > 1.0f) {
				gr_meter = 1.0f;
			}
		}
		

		p_dst_frames[i] = p_src_frames[i] * grv * makeup * mix + p_src_frames[i] * (1.0 - mix);
	}
}

Ref<AudioEffectInstance> AudioEffectExpander::instantiate() {
	Ref<AudioEffectExpanderInstance> ins;
	ins.instantiate();
	ins->base = Ref<AudioEffectExpander>(this);
	ins->rundb = 0;
	ins->runratio = 0;
	ins->averatio = 0;
	ins->runmax = 0;
	ins->maxover = 0;
	ins->gr_meter = 1.0;
	ins->current_channel = -1;
	return ins;
}

void AudioEffectExpander::set_threshold(float p_threshold) {
	threshold = p_threshold;
}

float AudioEffectExpander::get_threshold() const {
	return threshold;
}

void AudioEffectExpander::set_ratio(float p_ratio) {
	ratio = p_ratio;
}

float AudioEffectExpander::get_ratio() const {
	return ratio;
}

void AudioEffectExpander::set_gain(float p_gain) {
	gain = p_gain;
}

float AudioEffectExpander::get_gain() const {
	return gain;
}

void AudioEffectExpander::set_attack_us(float p_attack_us) {
	attack_us = p_attack_us;
}

float AudioEffectExpander::get_attack_us() const {
	return attack_us;
}

void AudioEffectExpander::set_release_ms(float p_release_ms) {
	release_ms = p_release_ms;
}

float AudioEffectExpander::get_release_ms() const {
	return release_ms;
}

void AudioEffectExpander::set_mix(float p_mix) {
	mix = p_mix;
}

float AudioEffectExpander::get_mix() const {
	return mix;
}

void AudioEffectExpander::set_sidechain(const StringName &p_sidechain) {
	AudioServer::get_singleton()->lock();
	sidechain = p_sidechain;
	AudioServer::get_singleton()->unlock();
}

StringName AudioEffectExpander::get_sidechain() const {
	return sidechain;
}

void AudioEffectExpander::set_range(float p_range) {
	range = p_range;
}

float AudioEffectExpander::get_range() const {
	return range;
}

void AudioEffectExpander::_validate_property(PropertyInfo &p_property) const {
	if (!Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (p_property.name == "sidechain") {
		String buses = "";
		for (int i = 0; i < AudioServer::get_singleton()->get_bus_count(); i++) {
			buses += ",";
			buses += AudioServer::get_singleton()->get_bus_name(i);
		}

		p_property.hint_string = buses;
	}
}

void AudioEffectExpander::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_threshold", "threshold"), &AudioEffectExpander::set_threshold);
	ClassDB::bind_method(D_METHOD("get_threshold"), &AudioEffectExpander::get_threshold);

	ClassDB::bind_method(D_METHOD("set_ratio", "ratio"), &AudioEffectExpander::set_ratio);
	ClassDB::bind_method(D_METHOD("get_ratio"), &AudioEffectExpander::get_ratio);

	ClassDB::bind_method(D_METHOD("set_gain", "gain"), &AudioEffectExpander::set_gain);
	ClassDB::bind_method(D_METHOD("get_gain"), &AudioEffectExpander::get_gain);

	ClassDB::bind_method(D_METHOD("set_attack_us", "attack_us"), &AudioEffectExpander::set_attack_us);
	ClassDB::bind_method(D_METHOD("get_attack_us"), &AudioEffectExpander::get_attack_us);

	ClassDB::bind_method(D_METHOD("set_release_ms", "release_ms"), &AudioEffectExpander::set_release_ms);
	ClassDB::bind_method(D_METHOD("get_release_ms"), &AudioEffectExpander::get_release_ms);

	ClassDB::bind_method(D_METHOD("set_mix", "mix"), &AudioEffectExpander::set_mix);
	ClassDB::bind_method(D_METHOD("get_mix"), &AudioEffectExpander::get_mix);

	ClassDB::bind_method(D_METHOD("set_sidechain", "sidechain"), &AudioEffectExpander::set_sidechain);
	ClassDB::bind_method(D_METHOD("get_sidechain"), &AudioEffectExpander::get_sidechain);

	ClassDB::bind_method(D_METHOD("set_range", "range"), &AudioEffectExpander::set_range);
	ClassDB::bind_method(D_METHOD("get_range"), &AudioEffectExpander::get_range);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "threshold", PROPERTY_HINT_RANGE, "-60,0,0.1"), "set_threshold", "get_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ratio", PROPERTY_HINT_RANGE, "1,48,0.1"), "set_ratio", "get_ratio");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gain", PROPERTY_HINT_RANGE, "-20,20,0.1"), "set_gain", "get_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "attack_us", PROPERTY_HINT_RANGE, U"20,2000,1,suffix:\u00B5s"), "set_attack_us", "get_attack_us");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "release_ms", PROPERTY_HINT_RANGE, "20,2000,1,suffix:ms"), "set_release_ms", "get_release_ms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mix", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_mix", "get_mix");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "sidechain", PROPERTY_HINT_ENUM), "set_sidechain", "get_sidechain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "range", PROPERTY_HINT_RANGE, "-30,30,0.1"), "set_range", "get_range");
}

AudioEffectExpander::AudioEffectExpander() {
	threshold = 0;
	ratio = 4;
	gain = 0;
	attack_us = 20;
	release_ms = 250;
	mix = 1;
	range = 6;
}

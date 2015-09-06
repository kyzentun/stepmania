function set_newfield_speed_mod(newfield, constant, speed, read_bpm)
	if not newfield then return end
	if not speed then return end
	local music_rate= GAMESTATE:GetSongOptionsObject("ModsLevel_Current"):MusicRate()
	local mod_input= {}
	local show_unjudgable= true
	local speed_segments_enabled= true
	local scroll_segments_enabled= true
	if constant then
		mod_input= {"ModInputType_DistSecond", (speed / 60) / music_rate}
		show_unjudgable= false
		speed_segments_enabled= false
		scroll_segments_enabled= false
	else
		read_bpm= read_bpm or 1
		mod_input= {"ModInputType_DistBeat", (speed / read_bpm) / music_rate}
	end
	for col in ivalues(newfield:get_columns()) do
		col:get_speed_mod():add_mod{"ModFunctionType_Constant", mod_input}
		col:set_show_unjudgable_notes(show_unjudgable)
		col:set_speed_segments_enabled(speed_segments_enabled)
		col:set_scroll_segments_enabled(scroll_segments_enabled)
	end
end

function set_newfield_tilt(newfield, tilt)
	-- The tilt mod is -30 degrees rotation at 1.0.
	local converted_tilt= (tilt * -30) * (math.pi / 180)
	newfield:get_trans_rot_x():set_value(converted_tilt)
end

function set_newfield_mini(newfield, mini)
	-- mini is zoom 0 at 2.0.
	local converted_zoom= 1 + (mini * -.5)
	for dim in ivalues{"x", "y", "z"} do
		newfield["get_trans_zoom_"..dim](newfield):set_value(converted_zoom)
	end
	if math.abs(converted_zoom) < .01 then return end
	local zoom_recip= 1 / converted_zoom
	-- The rev offset values need to be scaled too so the receptors stay fixed.
	for col in ivalues(newfield:get_columns()) do
		local revoff= col:get_reverse_offset_pixels()
		revoff:set_value(revoff:get_value() * zoom_recip)
		col:set_pixels_visible_after(1024 * zoom_recip)
	end
end

function set_newfield_rev_offset(newfield, revoff)
	if not revoff then return end
	for col in ivalues(newfield:get_columns()) do
		col:get_reverse_offset_pixels():set_value(revoff)
	end
end

function set_newfield_reverse(newfield, rev)
	for col in ivalues(newfield:get_columns()) do
		col:get_reverse_percent():set_value(rev)
	end
end

function find_newfield_in_gameplay(screen_gameplay, pn)
	local pactor= screen_gameplay:GetChild("Player" .. ToEnumShortString(pn))
	if not pactor then
		pactor= screen_gameplay:GetChild("Player")
	end
	if not pactor then
		return nil
	end
	return pactor:GetChild("NewField")
end

function set_newfield_mods(screen_gameplay, pn, revoff)
	local field= find_newfield_in_gameplay(screen_gameplay, pn)
	if not field then return end
	set_newfield_rev_offset(field, revoff)
	local pstate= GAMESTATE:GetPlayerState(pn)
	local poptions= pstate:GetPlayerOptions("ModsLevel_Preferred")
	set_newfield_tilt(field, poptions:Tilt())
	set_newfield_mini(field, poptions:Mini())
	set_newfield_reverse(field, poptions:Reverse())
	local mmod= poptions:MMod()
	local cmod= poptions:CMod()
	local xmod= poptions:XMod()
	if mmod then
		set_newfield_speed_mod(field, false, mmod, pstate:get_read_bpm())
	elseif cmod then
		set_newfield_speed_mod(field, true, cmod)
	else
		set_newfield_speed_mod(field, false, xmod)
	end
end

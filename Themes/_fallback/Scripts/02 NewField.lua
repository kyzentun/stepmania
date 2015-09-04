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

function find_newfield_in_gameplay(screen_gameplay, pn)
	
end

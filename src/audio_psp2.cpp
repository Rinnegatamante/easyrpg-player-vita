/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

#if defined(PSP2) && defined(SUPPORT_AUDIO)
#include "audio_psp2.h"
#include "filefinder.h"
#include "output.h"
#include "player.h"
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <cstdlib>
#ifdef USE_CACHE
#   include "3ds_cache.h"
#endif
#include "psp2_decoder.h"

uint8_t bgm_chn;

// osGetTime implementation
uint64_t osGetTime(void){
	return (sceKernelGetProcessTimeWide() / 1000);
}

// BGM audio streaming thread
volatile bool termStream = false;
DecodedMusic* BGM = NULL;
SceUID BGM_Mutex;
static int streamThread(unsigned int args, void* arg){
	
	for(;;) {
		
		// A pretty bad way to close thread
		if(termStream){
			termStream = false;
			sceKernelExitThread(0);
		}
		
		if (BGM == NULL) continue; // No BGM detected
		else if (BGM->starttick == 0) continue; // BGM not started
		else if (!BGM->isPlaying) continue; // BGM paused
		
		sceKernelWaitSema(BGM_Mutex, 1, NULL);
		
		// Calculating delta in milliseconds
		uint64_t delta = (osGetTime() - BGM->starttick);
		
		// Fade effect feature
		/*if (BGM->fade_val != 0){
		
			float vol;
			
			// Fade In	
			if (BGM->fade_val > 0){
				vol = (delta * BGM->vol) / float(BGM->fade_val);	
				if (vol >= BGM->vol){
					vol = BGM->vol;
					BGM->fade_val = 0;
				}
			}
			
			// Fade Out	
			else{
				vol = (delta * BGM->vol) / float(-BGM->fade_val);	
				if (vol >= BGM->vol){
					vol = 0.0;
					BGM->fade_val = 0;
				}else vol = BGM->vol - vol;
			}
			
			if (!Player::use_dsp){
				if (BGM->isStereo){
					CSND_SetVol(0x1E, CSND_VOL(vol, -1.0), CSND_VOL(vol, -1.0));
					CSND_SetVol(0x1F, CSND_VOL(vol, 1.0), CSND_VOL(vol, 1.0));
				}else CSND_SetVol(0x1F, CSND_VOL(vol, 0.0), CSND_VOL(vol, 0.0));
				CSND_UpdateInfo(true);
			}else{
				float vol_table[12] = {vol,vol,vol,vol};
				ndspChnSetMix(SOUND_CHANNELS, vol_table);
			}
		}*/
		
		// Audio streaming feature
		if (BGM->handle != NULL){
			BGM->updateCallback();
			sceAudioOutOutput(bgm_chn, BGM->cur_audiobuf);
		}
		
		sceKernelSignalSema(BGM_Mutex, 1);
		
	}
}

Psp2Audio::Psp2Audio() :
	bgm_volume(0)
{
	
	BGM_Mutex = sceKernelCreateSema("BGM Mutex", 0, 1, 1, NULL);
	
	SceUID audiothread = sceKernelCreateThread("Audio Thread", &streamThread, 0x10000100, 0x10000, 0, 0, NULL);
	int res = sceKernelStartThread(audiothread, sizeof(audiothread), &audiothread);
	if (res != 0){
		Output::Error("Failed to init audio thread (0x%x)", res);
		return;
	}
	
	#ifdef USE_CACHE
	initCache();
	#endif
	
}

Psp2Audio::~Psp2Audio() {
	
	// Just to be sure to clean up before exiting
	SE_Stop();
	BGM_Stop();
	
	// Closing BGM streaming thread
	termStream = true;
	while (termStream){} // Wait for thread exiting...
	if (BGM != NULL){
		free(BGM->audiobuf);
		BGM->closeCallback();
		free(BGM);
	}
	
	#ifdef USE_CACHE
	freeCache();
	#endif
	sceKernelDeleteSema(BGM_Mutex);
}

void Psp2Audio::BGM_OnPlayedOnce() {
	// Deprecated
}

void Psp2Audio::BGM_Play(std::string const& file, int volume, int /* pitch */, int fadein) {
	
	// If a BGM is currently playing, we kill it
	BGM_Stop();
	if (BGM != NULL){
		sceKernelWaitSema(BGM_Mutex, 1, NULL);
		free(BGM->audiobuf);
		free(BGM->audiobuf2);
		BGM->closeCallback();
		free(BGM);
		BGM = NULL;
		sceKernelSignalSema(BGM_Mutex, 1);
	}
	
	// Searching for the file
	std::string const path = FileFinder::FindMusic(file);
	if (path.empty()) {
		Output::Debug("Music not found: %s", file.c_str());
		return;
	}
	
	// Opening and decoding the file
	DecodedMusic* myFile = (DecodedMusic*)malloc(sizeof(DecodedMusic));
	int res = DecodeMusic(path, myFile);
	if (res < 0){
		free(myFile);
		return;
	}else BGM = myFile;
	BGM->starttick = 0;
	
	// Processing music info
	int samplerate = BGM->samplerate;
	BGM->orig_samplerate = BGM->samplerate;
	
	// Setting music volume
	BGM->vol = volume * 327;
	int vol = BGM->vol;
	BGM->fade_val = fadein;
	if (BGM->fade_val != 0){
		vol = 0;
	}

	#ifndef NO_DEBUG
	Output::Debug("Playing music %s:",file.c_str());
	Output::Debug("Samplerate: %i",samplerate);
	Output::Debug("Buffer Size: %i bytes",BGM->audiobuf_size);
	#endif
	
	// Starting BGM
	bgm_chn = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, BGM_BUFSIZE, BGM->orig_samplerate, SCE_AUDIO_OUT_MODE_STEREO);
	sceAudioOutSetConfig(bgm_chn, -1, -1, -1);
	sceAudioOutSetVolume(bgm_chn, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, &vol);
	BGM->isPlaying = true;
	BGM->starttick = osGetTime();
	
}

void Psp2Audio::BGM_Pause() {
	if (BGM == NULL) return;
	if (BGM->isPlaying){
		BGM->isPlaying = false;
		BGM->starttick = osGetTime()-BGM->starttick; // Save current delta
	}
}

void Psp2Audio::BGM_Resume() {
	if (BGM == NULL) return;
	if (!BGM->isPlaying){
		BGM->isPlaying = true;
		BGM->starttick = osGetTime()-BGM->starttick; // Restore starttick
	}
}

void Psp2Audio::BGM_Stop() {
	if (BGM == NULL) return;
	sceAudioOutReleasePort(bgm_chn);
	BGM->isPlaying = false;
}

bool Psp2Audio::BGM_PlayedOnce() {
	if (BGM == NULL) return false;
	return (BGM->block_idx >= BGM->eof_idx);
}

unsigned Psp2Audio::BGM_GetTicks() {
	return 0;
}

void Psp2Audio::BGM_Volume(int volume) {
	if (BGM == NULL) return;
}

void Psp2Audio::BGM_Pitch(int pitch) {
	if (BGM == NULL) return;	
}

void Psp2Audio::BGM_Fade(int fade) {
	if (BGM == NULL) return;
	BGM->fade_val = -fade;
}

void Psp2Audio::BGS_Play(std::string const& file, int volume, int /* pitch */, int fadein) {
	// Deprecated
}

void Psp2Audio::BGS_Pause() {
	// Deprecated
}

void Psp2Audio::BGS_Resume() {
	// Deprecated
}

void Psp2Audio::BGS_Stop() {
	// Deprecated
}

void Psp2Audio::BGS_Fade(int fade) {
	// Deprecated
}

int Psp2Audio::BGS_GetChannel() const {
	// Deprecated
	return 1;
}

void Psp2Audio::ME_Play(std::string const& file, int volume, int /* pitch */, int fadein) {
	// Deprecated
}

void Psp2Audio::ME_Stop() {
	// Deprecated
}

void Psp2Audio::ME_Fade(int fade) {
	// Deprecated
}

void Psp2Audio::SE_Play(std::string const& file, int volume, int /* pitch */) {
	
	// Init needed vars
	bool isStereo = false;
	int audiobuf_size;
	DecodedSound myFile;
	
	#ifdef USE_CACHE
	// Looking if the sound is in sounds cache
	int cacheIdx = lookCache(file.c_str());
	if (cacheIdx < 0){
	#endif
	
		// Searching for the file
		std::string const path = FileFinder::FindSound(file);
		if (path.empty()) {
			Output::Debug("Sound not found: %s", file.c_str());
			return;
		}
	
		// Opening and decoding the file
		int res = DecodeSound(path, &myFile);
		if (res < 0) return;
		#ifdef USE_CACHE
		else sprintf(soundtable[res],"%s",file.c_str());
		#endif
		
	#ifdef USE_CACHE
	}else myFile = decodedtable[cacheIdx];
	#endif
	
	// Processing sound info
	uint8_t* audiobuf = myFile.audiobuf;
	int samplerate = myFile.samplerate;
	audiobuf_size = myFile.audiobuf_size;
	
	isStereo = myFile.isStereo;
	
	#ifndef NO_DEBUG
	Output::Debug("Playing sound %s:",file.c_str());
	Output::Debug("Samplerate: %i",samplerate);
	Output::Debug("Buffer Size: %i bytes",audiobuf_size);
	#endif
	
	// Playing the sound
	int cur_pos = 0;
	int vol = volume * 327;
	int buf_size = SCE_AUDIO_MAX_LEN;
	if (audiobuf_size < SCE_AUDIO_MAX_LEN) buf_size = audiobuf_size;
	int chn = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_VOICE, buf_size, samplerate, SCE_AUDIO_OUT_MODE_STEREO);
	sceAudioOutSetConfig(chn, -1, -1, -1);
	sceAudioOutSetVolume(chn, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, &vol);
	while (cur_pos < audiobuf_size){
		sceAudioOutOutput(chn, &audiobuf[cur_pos]);
		cur_pos += buf_size;
	}
	sceAudioOutReleasePort(chn);
}

void Psp2Audio::SE_Stop() {
	
}

void Psp2Audio::Update() {	
	
}

#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


// Sound commands for Z80
#define SOUNDCMD_PlayJingleA	0x02
#define SOUNDCMD_StopAll		0x04
#define	RAMTESTCMD				0x10

#define SOUNDCMD_PlayCoinA		0x20
#define SOUNDCMD_PlayLeft  		0x21
#define SOUNDCMD_PlayRight		0x22
#define SOUNDCMD_PlayCenter 	0x23
#define SOUNDCMD_PlaySweep	 	0x24
#define SOUNDCMD_StopADPCMA		0x2f

#define SOUNDCMD_ADPCMB_Left	0x30
#define SOUNDCMD_ADPCMB_Center	0x31
#define SOUNDCMD_ADPCMB_Right	0x32
#define SOUNDCMD_LoopB			0x33
#define SOUNDCMD_NoLoopB		0x34
#define SOUNDCMD_ADPCMB_Sample0 0x35
#define SOUNDCMD_ADPCMB_LdSweep	0x36
#define SOUNDCMD_StopADPCMB		0x3f

#define SOUNDCMD_RateB_0		0x40
#define SOUNDCMD_RateB_1		0x41
#define SOUNDCMD_RateB_2		0x42
#define SOUNDCMD_RateB_3		0x43
#define SOUNDCMD_RateB_4		0x44
#define SOUNDCMD_RateB_5		0x45
#define SOUNDCMD_RateB_6		0x46
#define SOUNDCMD_RateB_7		0x47

#define SOUNDCMD_RateB_0_Play	0x48
#define SOUNDCMD_RateB_1_Play	0x49
#define SOUNDCMD_RateB_2_Play	0x4A
#define SOUNDCMD_RateB_3_Play	0x4B
#define SOUNDCMD_RateB_4_Play	0x4C
#define SOUNDCMD_RateB_5_Play	0x4D
#define SOUNDCMD_RateB_6_Play	0x4E
#define SOUNDCMD_RateB_7_Play	0x4F

#define SOUNDCMD_SSGRampinit	0x50
#define SOUNDCMD_SSGRampcycle	0x51
#define SOUNDCMD_SSGRampStep	0x52
#define SOUNDCMD_SSGPulseStart	0x53
#define SOUNDCMD_SSGPulseStop	0x54
#define SOUNDCMD_SSG1KHZStart	0x55
#define SOUNDCMD_SSG1KHZStop	0x56
#define SOUNDCMD_SSG260HZStart	0x57
#define SOUNDCMD_SSG260HZStop	0x58
#define SOUNDCMD_SSGNoiseStart	0x59
#define SOUNDCMD_SSGNoiseStop	0x5a
#define SOUNDCMD_SSGNoiseRamp	0x5b
#define SOUNDCMD_SSGStop		0x5f

#define SOUNDCMD_FMInitSndTest	0x60
#define SOUNDCMD_FMPlay			0x61
#define SOUNDCMD_FMUseLeft		0x62
#define SOUNDCMD_FMUseCenter	0x63
#define SOUNDCMD_FMUseRight		0x64
#define SOUNDCMD_FMInitMDF		0x65
#define SOUNDCMD_FMNextMDF		0x66
#define SOUNDCMD_FMStopAll		0x6f

#define SOUNDCMD_FMOctave0		0x70
#define SOUNDCMD_FMOctave1		0x71
#define SOUNDCMD_FMOctave2		0x72
#define SOUNDCMD_FMOctave3		0x73
#define SOUNDCMD_FMOctave4		0x74
#define SOUNDCMD_FMOctave5		0x75
#define SOUNDCMD_FMOctave6		0x76
#define SOUNDCMD_FMOctave7		0x77

#define SOUNDCMD_FMNote0		0x80
#define SOUNDCMD_FMNote1		0x81
#define SOUNDCMD_FMNote2		0x82
#define SOUNDCMD_FMNote3		0x83
#define SOUNDCMD_FMNote4		0x84
#define SOUNDCMD_FMNote5		0x85
#define SOUNDCMD_FMNote6		0x86
#define SOUNDCMD_FMNote7		0x87
#define SOUNDCMD_FMNote8		0x88
#define SOUNDCMD_FMNote9		0x89
#define SOUNDCMD_FMNote10		0x8A
#define SOUNDCMD_FMNote11		0x8B
#define SOUNDCMD_FMNote12		0x8C

#define SOUNDCMD_CheckVersion	0xD0

extern void send_sound_code(uint8_t code);
extern bool read_sound_response(uint8_t *code);
extern void wait_vblank();

void sendZ80command(uint8_t cmd)
{
    uint8_t response;
    read_sound_response(NULL); // flush

    send_sound_code(cmd);

    while( !read_sound_response(&response) ) {}
    while( !read_sound_response(&response) ) {}
}

void waitVBlank()
{
    wait_vblank();
}

void executePulseTrain()
{
	int frame = 0;

	for(frame = 0; frame < 10; frame++)
	{
		sendZ80command(SOUNDCMD_SSGPulseStart);
		waitVBlank();
		sendZ80command(SOUNDCMD_SSGPulseStop);
		waitVBlank();
	}
}

void executeSilence()
{
	int frame = 0;

	for(frame = 0; frame < 40; frame++)
		waitVBlank();
}

void waitSound(int frames)
{
	int frame = 0;

	for(frame = 0; frame < frames; frame++)
		waitVBlank();
}

void ExecuteFM(int framelen)
{
	int octave, frame;
	
	// FM Test
	
	for(octave = 0; octave < 8; octave ++)
	{
		int note;
			
		sendZ80command(SOUNDCMD_FMOctave0+octave);
		for(note = 0; note < 12; note++)
		{
			sendZ80command(SOUNDCMD_FMNote0+note);
			sendZ80command(SOUNDCMD_FMNextMDF);
				
			for(frame = 0; frame < framelen; frame++)
			{					
				if(frame == framelen - framelen/5)
					sendZ80command(SOUNDCMD_FMStopAll);
				waitVBlank();
			}
		}
	}
	sendZ80command(SOUNDCMD_FMStopAll);
}

void run_mdfourier()
{
    int frame = 0, close = 0;

    waitVBlank();

    sendZ80command(SOUNDCMD_StopAll);

    sendZ80command(SOUNDCMD_FMInitMDF);
    sendZ80command(SOUNDCMD_SSGRampinit);

    sendZ80command(SOUNDCMD_NoLoopB);
    sendZ80command(SOUNDCMD_ADPCMB_LdSweep);

    waitVBlank();

    executePulseTrain();
    executeSilence();

    ExecuteFM(20);

    // First detailed SSG ramp
    for(frame = 0; frame < 256; frame++)
    {
        sendZ80command(SOUNDCMD_SSGRampcycle);
        waitVBlank();
    }
    // then low SSG tones ramp, at 0x10 steps (they would be 3840 frame otherwise)
    for(frame = 0; frame < 240; frame++)
    {
        sendZ80command(SOUNDCMD_SSGRampStep);
        waitVBlank();
    }
    sendZ80command(SOUNDCMD_SSGStop);
    waitVBlank();								// extra frame

#if 1
    // SSG Noise is too random for a peroid
    for(frame = 0; frame < 32; frame++)
    {
        sendZ80command(SOUNDCMD_SSGNoiseRamp);
        waitSound(5);
    }
#endif

    sendZ80command(SOUNDCMD_SSGStop);
    waitVBlank();								// extra frame

#if 0
    // ADPCM-A takes 236.46 frames at 16.77724
    // 3967.312 mmilliseconds in an AES NTSC System
    sendZ80command(SOUNDCMD_PlaySweep);
    waitSound(240);

#ifndef __cd__
    // ADPCM-B not present in Neo Geo CD
    if(mdf_type == MDF_DEFAULT)
    {
        // Only play full test if debug dip is enabled
        if(bkp_data.debug_dip1&DP_DEBUG1)
        {
            sendZ80command(SOUNDCMD_RateB_0_Play);			// 11025hz
            waitSound(610);

            sendZ80command(SOUNDCMD_RateB_2_Play);			// 22050hz
            waitSound(305);
        }

        // Common playback
        sendZ80command(SOUNDCMD_RateB_6_Play);			// 44100hz
        waitSound(152);

        // Only play full test if debug dip is enabled
        if(bkp_data.debug_dip1&DP_DEBUG1)
        {
            // 2.01968 seconds or 121 frames
            sendZ80command(SOUNDCMD_RateB_7_Play);			// 55125hz
            waitSound(122);
        }
    }
#endif
#endif // 0

    executeSilence();

    executePulseTrain();

    sendZ80command(SOUNDCMD_StopAll);
}


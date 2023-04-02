// 
// GME SPC -> SNESAPU Wrapper
//

#include "stdafx.h"
#include "gme/Music_Emu.h"
#include "gme/Spc_Emu.h"

extern "C" {

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned char b8;
typedef short s16;
typedef int s32;
typedef float f32;

typedef union DSPReg
{
	u8		reg[128];
} DSPReg;

// ----- SSDLabo code [2009/03/11] -----
typedef struct Voice
{
	//Voice -----------08
	u16		vAdsr;								//ADSR parameters when KON was written
	u8		vGain;								//Gain parameters when KON was written
	u8		vRsv;								//Changed ADSR/Gain parameters flag
	s16		*sIdx;								//-> current sample in sBuf
	//Waveform --------06
	void	*bCur;								//-> current block
	u8		bHdr;								//Block Header for current block
	u8		mFlg;								//Mixing flags (see MixF)
	//Envelope --------22
	u8		eMode;								//[3-0] Current mode (see EnvM)
												//[6-4] ADSR mode to switch into from Gain
												//[7]   Envelope is idle
	u8		eRIdx;								//Index in RateTab (0-31)
	u32		eRate;								//Rate of envelope adjustment (16.16)
	u32		eCnt;								//Sample counter (16.16)
	u32		eVal;								//Current envelope value
	s32		eAdj;								//Amount to adjust envelope height
	u32		eDest;								//Envelope Destination
	//Visualization ---08
	s32		vMaxL;								//Maximum absolute sample output
	s32		vMaxR;
	//Samples ---------52
	s16		sP1;								//Last sample decompressed (prev1)
	s16		sP2;								//Second to last sample (prev2)
	s16		sBufP[8];							//Last 8 samples from previous block (needed for inter.)
	s16		sBuf[16];							//32 bytes for decompressed sample blocks
	//Mixing ----------32
	f32		mTgtL;								//Target volume (floating-point routine only)
	f32		mTgtR;								// "  "
	s32		mChnL;								//Channel Volume (-24.7)
	s32		mChnR;								// "  "
	u32		mRate;								//Pitch Rate after modulation (16.16)
	u16		mDec;								//Pitch Decimal (.16) (used as delta for interpolation)
	u8		mSrc;								//Current source number
	u8		mKOn;								//Delay time from writing KON to output
	u32		mOrgP;								//Original pitch rate converted from the DSP (16.16)
	s32		mOut;								//Last sample output before chn vol (used for pitch mod)
} Voice;

__declspec(dllexport) void* __stdcall EmuAPU(void *pBuf, u32 len, u8 type);
__declspec(dllexport) void __stdcall LoadSPCFile(void *pFile);
__declspec(dllexport) void __stdcall GetAPUData(u8 **ppRAM, u8 **ppXRAM, u8 **ppOutPort, u32 **ppT64Cnt, DSPReg **ppDSP, Voice **ppVoice, u32 **ppVMMaxL, u32 **ppVMMaxR);
__declspec(dllexport) void __stdcall SNESAPUInfo(u32 *pVer, u32 *pMin, u32 *pOpt);
__declspec(dllexport) u32 __stdcall SetAPULength(u32 song, u32 fade);
__declspec(dllexport) void __stdcall SetAPUOpt(u32 mix, u32 chn, u32 bits, u32 rate, u32 inter, u32 opts);
__declspec(dllexport) void __stdcall GetSPCRegs(u16 *pPC, u8 *pA, u8 *pY, u8 *pX, u8 *pPSW, u8 *pSP);

// Dummy
__declspec(dllexport) void __stdcall SeekAPU(u32 time, b8 fast);
__declspec(dllexport) void __stdcall SetAPUSmpClk(u32 speed);
__declspec(dllexport) void __stdcall SetDSPAmp(u32 amp);
__declspec(dllexport) void __stdcall SetDSPEFBCT(s32 leak);
__declspec(dllexport) void __stdcall SetDSPStereo(u32 sep);
__declspec(dllexport) void __stdcall SetDSPPitch(u32 base);

}

u8 ram[0x10000];
u8 xram[0x80];
u8 mask = 0;
Voice voice[8];
DSPReg dsp;
u32 smprate = 44100;
gme_info_t* track_info = 0;
Music_Emu *emu = 0;
Spc_Emu *snes = 0;
bool is_setrate = false;
u32 maxL,maxR;
u32 timercnt;

// Get version
__declspec(dllexport) void __stdcall SNESAPUInfo(u32 *pVer, u32 *pMin, u32 *pOpt)
{
	*pVer = 0x00020000;
}

// Get register value
void __stdcall GetSPCRegs(u16 *pPC, u8 *pA, u8 *pY, u8 *pX, u8 *pPSW, u8 *pSP)
{
	if (emu && snes) {
		*pA = snes->apu.m.cpu_regs.a;
		*pPC = snes->apu.m.cpu_regs.pc;
		*pY = snes->apu.m.cpu_regs.y;
		*pX = snes->apu.m.cpu_regs.x;
		*pPSW = snes->apu.m.cpu_regs.psw;
		*pSP = snes->apu.m.cpu_regs.sp;
	}
}

// Load
void __stdcall LoadSPCFile(void *pFile)
{
	if (emu) gme_delete( emu );

	gme_open_data( pFile, 66048, &emu, smprate );
	if (emu) {
//		emu->set_sample_rate( smprate );
		emu->start_track( 0 );
		snes = dynamic_cast<Spc_Emu*>(emu);
	}
	mask = 0;
}

// Render
void* __stdcall EmuAPU(void *pBuf, u32 len, u8 type)
{
	if (type == 1) {
		if (emu) {
			// Mask更新
			int mask = 0;
			for (int i=0; i < 8; i++) {
				mask |= (voice[i].mFlg << i);
			}
			emu->mute_voices(mask);

			// Render
			emu->play(len*2, (short*)pBuf);

			// Info系はここで更新
			memcpy(ram, snes->apu.m.ram.ram, sizeof(ram));
			memcpy(dsp.reg, snes->apu.dsp.m.regs, sizeof(snes->apu.dsp.m.regs));

		}
	}
	return (short*)pBuf + len * 2;
}

// Play time
u32 __stdcall SetAPULength(u32 song, u32 fade)
{
	return 0;
}

void __stdcall GetAPUData(u8 **ppRAM, u8 **ppXRAM, u8 **ppOutPort, u32 **ppT64Cnt,
				DSPReg **ppDSP, Voice **ppVoice, u32 **ppVMMaxL, u32 **ppVMMaxR)
{
	if (ppRAM)
		*ppRAM = ram;
	if (ppXRAM)
		*ppXRAM = xram;
	if (ppOutPort)
		*ppOutPort = ram;
	if (ppDSP)
		*ppDSP = &dsp;
	if (ppVoice)
		*ppVoice = voice;

	if (ppT64Cnt)
	*ppT64Cnt = &timercnt;
	if (ppVMMaxL)
	*ppVMMaxL = &maxL;
	if (ppVMMaxR)
	*ppVMMaxR = &maxR;
}

// Stereo/16bit fix
void __stdcall SetAPUOpt(u32 mix, u32 chn, u32 bits, u32 rate, u32 inter, u32 opts){
	smprate = rate;
	if (emu)
		emu->set_sample_rate( rate );
}

// Dummy
void __stdcall SeekAPU(u32 time, b8 fast) {}
void __stdcall SetAPUSmpClk(u32 speed) {};
void __stdcall SetDSPAmp(u32 amp) {}
void __stdcall SetDSPEFBCT(s32 leak) {}
void __stdcall SetDSPStereo(u32 sep) {}
void __stdcall SetDSPPitch(u32 base) {}


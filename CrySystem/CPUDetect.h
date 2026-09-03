/*=============================================================================
  CPUDetect.cpp : CPU detection.
  Copyright 2001 Crytek Studios. All Rights Reserved.

  Revision history:
    * Created by Honitch Andrey

=============================================================================*/

#ifndef __CPUDETECT_H__
#define __CPUDETECT_H__

//-------------------------------------------------------
/// Cpu class
//-------------------------------------------------------

#define MAX_CPU	32

/// Cpu Features
#define CFI_FPUEMULATION 1
#define CFI_MMX   2
#define CFI_3DNOW 4
#define CFI_SSE   8
#define CFI_SSE2  0x10

/// Type of Cpu Vendor.
enum ECpuVendor
{
  eCVendor_Unknown,
  eCVendor_Intel,
  eCVendor_Cyrix,
  eCVendor_AMD,
  eCVendor_Centaur,
  eCVendor_NexGen,
  eCVendor_UMC,
  eCVendor_M68K
};

/// Type of Cpu Model.
enum ECpuModel
{
  eCpu_Unknown,

  eCpu_8086,
  eCpu_80286,
  eCpu_80386,
  eCpu_80486,
  eCpu_Pentium,
  eCpu_PentiumPro,
  eCpu_Pentium2,
  eCpu_Pentium3,
  eCpu_Pentium4,
  eCpu_Pentium2Xeon,
  eCpu_Pentium3Xeon,
  eCpu_Celeron,
  eCpu_CeleronA,

  eCpu_Am5x86,
  eCpu_AmK5,
  eCpu_AmK6,
  eCpu_AmK6_2,
  eCpu_AmK6_3,
  eCpu_AmK6_3D,
  eCpu_AmAthlon,
  eCpu_AmDuron,

  eCpu_CyrixMediaGX,
  eCpu_Cyrix6x86,
  eCpu_CyrixGXm,
  eCpu_Cyrix6x86MX,

  eCpu_CenWinChip,
  eCpu_CenWinChip2,
};

struct SCpu
{
  ECpuVendor meVendor;
  ECpuModel meModel; 
  unsigned long mFeatures;
  bool mbSerialPresent;
  char  mSerialNumber[30];
  int mFamily;
  int mModel;
  int mStepping;
  char mVendor[64];
  char mCpuType[64];
  char mFpuType[64];
  int mSpeed;
  double m_SecondsPerCycle;
};

class CCpuFeatures
{
private:
  int m_NumSystemProcessors;
  int m_NumAvailProcessors;
  bool m_bOS_ISSE;
  bool m_bOS_ISSE_EXCEPTIONS;
public:

  SCpu m_Cpu[MAX_CPU];

public:
  CCpuFeatures()
  {
    m_NumSystemProcessors = 0;
    m_NumAvailProcessors  = 0;
    m_bOS_ISSE = 0;
    m_bOS_ISSE_EXCEPTIONS = 0;
  }
  
  void Detect(void);
  bool hasSSE() { return (m_Cpu[0].mFeatures & CFI_SSE) != 0; }
  bool hasSSE2() { return (m_Cpu[0].mFeatures & CFI_SSE2) != 0; }
  bool has3DNow() { return (m_Cpu[0].mFeatures & CFI_3DNOW) != 0; }
  bool hasMMX() { return (m_Cpu[0].mFeatures & CFI_MMX) != 0; }
};

#if defined(WIN64) || defined(LINUX64)
inline bool IsAMD64()
{
	// [webport] The original demanded _AMD64_ -- a macro the Win64 SDK
	// defines -- and #error'd otherwise. Nothing defines it on the LINUX64
	// seam, so this header could not be compiled at all (13 TUs).
	//
	// It also cannot simply return true. The web port compiles with the
	// LINUX64 defines because that is the seam the engine provides, but the
	// actual target is wasm32: 32-bit, and emphatically not x86-64. Answering
	// "yes, AMD64" there would be a lie that selects x86 code paths.
	//
	// Ask the compiler instead of the SDK.
#if defined(CRY_WASM)
	return false;                       // wasm32 is neither AMD64 nor x86
#elif defined(_AMD64_) || defined(__x86_64__) || defined(_M_X64)
	return true;
#else
	return false;
#endif
}
#else
inline bool
IsAMD64()
{
#if defined(LINUX32)
	return false;
#else
	_asm
	{
		// make sure cpuid is available
		pushfd					// save EFLAGS
		pop eax					// store EFLAGS in EAX
		mov ebx, eax			// save in EBX for later testing
		xor eax, 00200000h		// toggle bit 21
		push eax				// push to stack
		popfd					// save changed EAX to EFLAGS
		pushfd					// push EFLAGS to TOS
		pop eax					// store EFLAGS in EAX
		cmp eax, ebx			// see if bit 21 has changed
		mov eax, 0				// clear eax to return "false"
		jz QUIT					// if no change, no CPUID

		// perform AMD64 detection
		mov eax, 0x80000001		// load argument "Processor Signature and AMD Features" to call cpuid
		cpuid					// call cpuid
		mov eax, edx			// edx contains "AMD Feature Support" flags
		shr eax, 29				// test bit 29 (support for "Long Mode")
		and eax, 0x00000001		// mask out bit for proper return value
QUIT:
	}
#endif
}
#endif // WIN32

#endif

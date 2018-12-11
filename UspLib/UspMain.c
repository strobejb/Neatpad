#include <windows.h>

#pragma comment(linker, "/opt:nowin98")

#pragma comment(linker, "/export:UspAnalyze=_UspAnalyze@40")
#pragma comment(linker, "/export:UspFree=_UspFree@4")
#pragma comment(linker, "/export:UspAllocate=_UspAllocate@0")
#pragma comment(linker, "/export:UspApplyAttributes=_UspApplyAttributes@8")
#pragma comment(linker, "/export:UspApplySelection=_UspApplySelection@12")
#pragma comment(linker, "/export:UspTextOut=_UspTextOut@28")
#pragma comment(linker, "/export:UspSnapXToOffset=_UspSnapXToOffset@20")
#pragma comment(linker, "/export:UspXToOffset=_UspXToOffset@20")
#pragma comment(linker, "/export:UspOffsetToX=_UspOffsetToX@16")
#pragma comment(linker, "/export:UspSetSelColor=_UspSetSelColor@12")
#pragma comment(linker, "/export:UspInitFont=_UspInitFont@12")
#pragma comment(linker, "/export:UspFreeFont=_UspFreeFont@4")
#pragma comment(linker, "/export:UspGetSize=_UspGetSize@8")
#pragma comment(linker, "/export:UspGetLogAttr=_UspGetLogAttr@4")
#pragma comment(linker, "/export:UspBuildAttr=_UspBuildAttr@32")

BOOL CALLBACK DllMain(HINSTANCE hInst, DWORD dwReason, LPVOID lpReserved)
{
	return TRUE;
}
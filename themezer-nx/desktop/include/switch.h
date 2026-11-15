// A stand-in for the bits of libnx the app uses, so it builds and runs on a computer for looking at the UI (see
// desktop/README.md). The controller is the keyboard, the touch screen the mouse, the fonts the computer's.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef uint32_t Result;
#define R_SUCCEEDED(res) ((res) == 0)
#define R_FAILED(res) ((res) != 0)
#define BIT(n) (1ULL << (n))

enum
{
    HidNpadButton_A = BIT(0),
    HidNpadButton_B = BIT(1),
    HidNpadButton_X = BIT(2),
    HidNpadButton_Y = BIT(3),
    HidNpadButton_StickL = BIT(4),
    HidNpadButton_StickR = BIT(5),
    HidNpadButton_L = BIT(6),
    HidNpadButton_R = BIT(7),
    HidNpadButton_ZL = BIT(8),
    HidNpadButton_ZR = BIT(9),
    HidNpadButton_Plus = BIT(10),
    HidNpadButton_Minus = BIT(11),
    HidNpadButton_Left = BIT(12),
    HidNpadButton_Up = BIT(13),
    HidNpadButton_Right = BIT(14),
    HidNpadButton_Down = BIT(15),
    HidNpadButton_StickLLeft = BIT(16),
    HidNpadButton_StickLUp = BIT(17),
    HidNpadButton_StickLRight = BIT(18),
    HidNpadButton_StickLDown = BIT(19),
    HidNpadButton_StickRLeft = BIT(20),
    HidNpadButton_StickRUp = BIT(21),
    HidNpadButton_StickRRight = BIT(22),
    HidNpadButton_StickRDown = BIT(23),
};
#define HidNpadStyleSet_NpadStandard 0

typedef struct
{
    u64 buttons;
    u64 buttonsOld;
} PadState;
void padConfigureInput(u32 maxPlayers, u32 styleSet);
void padInitializeDefault(PadState* pad);
void padUpdate(PadState* pad);
u64 padGetButtons(const PadState* pad);

typedef struct
{
    u32 x, y;
} HidTouchState;
typedef struct
{
    s32 count;
    HidTouchState touches[16];
} HidTouchScreenState;
void hidInitializeTouchScreen(void);
size_t hidGetTouchScreenStates(HidTouchScreenState* states, size_t count);

typedef struct
{
    void* handle;
} Thread;
typedef void (*ThreadFunc)(void*);
Result threadCreate(Thread* t, ThreadFunc entry, void* arg, void* stackMem, size_t stackSize, int prio, int cpuid);
Result threadStart(Thread* t);
Result threadWaitForExit(Thread* t);
Result threadClose(Thread* t);

typedef struct
{
    void* handle;
} Mutex;
void mutexInit(Mutex* m);
void mutexLock(Mutex* m);
void mutexUnlock(Mutex* m);

typedef struct
{
    void* handle;
} CondVar;
void condvarInit(CondVar* c);
Result condvarWait(CondVar* c, Mutex* m);
Result condvarWakeOne(CondVar* c);
Result condvarWakeAll(CondVar* c);

typedef enum
{
    PlSharedFontType_Standard = 0,
    PlSharedFontType_ChineseSimplified = 1,
    PlSharedFontType_ExtChineseSimplified = 2,
    PlSharedFontType_ChineseTraditional = 3,
    PlSharedFontType_KO = 4,
    PlSharedFontType_NintendoExt = 5,
} PlSharedFontType;
typedef enum
{
    PlServiceType_User = 0
} PlServiceType;
typedef struct
{
    u32 type;
    u32 offset;
    u32 size;
    void* address;
} PlFontData;
Result plInitialize(PlServiceType type);
void plExit(void);
Result plGetSharedFontByType(PlFontData* font, PlSharedFontType type);

typedef struct
{
    int unused;
} SwkbdConfig;
Result swkbdCreate(SwkbdConfig* c, s32 max_dictwords);
void swkbdClose(SwkbdConfig* c);
void swkbdConfigMakePresetDefault(SwkbdConfig* c);
void swkbdConfigSetGuideText(SwkbdConfig* c, const char* str);
void swkbdConfigSetInitialText(SwkbdConfig* c, const char* str);
void swkbdConfigSetStringLenMax(SwkbdConfig* c, u32 len);
Result swkbdShow(SwkbdConfig* c, char* out_string, size_t out_string_size);

typedef enum
{
    PsmChargerType_Unconnected = 0,
    PsmChargerType_EnoughPower = 1,
} PsmChargerType;
Result psmInitialize(void);
void psmExit(void);
Result psmGetBatteryChargePercentage(u32* out);
Result psmGetChargerType(PsmChargerType* out);

typedef enum
{
    NifmServiceType_User = 0
} NifmServiceType;
typedef enum
{
    NifmInternetConnectionStatus_Connected = 4
} NifmInternetConnectionStatus;
typedef enum
{
    NifmInternetConnectionType_WiFi = 1
} NifmInternetConnectionType;
Result nifmInitialize(NifmServiceType type);
void nifmExit(void);
Result nifmGetInternetConnectionStatus(NifmInternetConnectionType* type, u32* wifiStrength,
                                       NifmInternetConnectionStatus* status);

Result romfsInit(void);
Result romfsExit(void);
Result socketInitializeDefault(void);
void socketExit(void);
int nxlinkStdio(void);
bool appletMainLoop(void);
typedef enum
{
    AppletType_None = -2,
    AppletType_Default = -1,
    AppletType_Application = 0,
    AppletType_SystemApplet = 1,
    AppletType_LibraryApplet = 2,
    AppletType_OverlayApplet = 3,
    AppletType_SystemApplication = 4,
} AppletType;
AppletType appletGetAppletType(void);
Result appletGetExpectedMasterVolume(float* mainAppletVolume, float* libraryAppletVolume);
Result appletChangeMainAppletMasterVolume(float volume, u64 unk);
Result envSetNextLoad(const char* path, const char* argv);

#ifdef __cplusplus
}
#endif

struct SetSysFirmwareVersion { char display_version[24] = "21.0.0"; };
inline Result setsysInitialize() { return 0; }
inline Result setsysGetFirmwareVersion(SetSysFirmwareVersion*) { return 0; }
inline void setsysExit() {}
enum SplConfigItem { SplConfigItem_Desktop = 65000 };
inline Result splInitialize() { return 0; }
// Atmosphère 1.10.0
inline Result splGetConfig(SplConfigItem, u64* value)
{
    *value = (u64(1) << 56) | (u64(10) << 48);
    return 0;
}
inline void splExit() {}

bool themezerDesktopOffline();
const char* themezerDesktopReportUrl();

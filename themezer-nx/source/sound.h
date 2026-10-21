#pragma once
#include <JAGL.h>

typedef enum {
    SfxCursor = 0,
    SfxPage,
    SfxTab,
    SfxOpen,
    SfxClose,
    SfxConfirm,
    SfxBack,
    SfxDone,
    SfxError,
    SfxCount
} Sfx_t;

void SoundInit(void);
void SoundExit(void);
void SoundPlay(Sfx_t sfx);
Context_t SoundMakeMenu(ShapeLinker_t *in, func_ptr buttonHandler, func_ptr runEveryFrame);

// Every menu gets the cursor, confirm, back, open and close sounds through this wrapper
#define MakeMenu SoundMakeMenu

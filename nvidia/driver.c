/*
 *       ______             ____  ______     _____  ______ 
 *      |  ____|           |  _ \|  ____|   / / _ \|  ____|
 *      | |__ _ __ ___  ___| |_) | |__     / / |_| | |__ 
 *      |  __| '__/ _ \/ _ \  _ <|  __|   / /|  _  |  __|
 *      | |  | | |  __/  __/ |_) | |____ / / | | | | |
 *      |_|  |_|  \___|\___|____/|______/_/  |_| |_|_|
 *
 *
 *      Accelerated driver for Riva 128 and TNT cards, by Shawn Hargreaves.
 *
 *      The VGA mode setting routines are based on the TGUI driver by
 *      Salvador Eduardo Tropea.
 *
 *      This file provides a VBE/AF interface to the hardware routines
 *      in riva_hw.c, which was lifted directly from XFree86, and is
 *      copyrighted by NVidia under a BSD-style license. See the comment
 *      at the top of riva_hw.c for details. Much kudos to NVidia for
 *      supplying such a useful file.
 *
 *      See freebe.txt for copyright information.
 */


#include <pc.h>

#include "vbeaf.h"
#include "riva_hw.h"
#include "font.h"



/* remove this define for a completely native, register level mode set */
// #define USE_VESA



/* driver function prototypes */
void SetBank32();
void SetBank32End();
int  ExtStub();
long GetVideoModeInfo(AF_DRIVER *af, short mode, AF_MODE_INFO *modeInfo);
long SetVideoMode(AF_DRIVER *af, short mode, long virtualX, long virtualY, long *bytesPerLine, int numBuffers, AF_CRTCInfo *crtc);
void RestoreTextMode(AF_DRIVER *af);
long GetClosestPixelClock(AF_DRIVER *af, short mode, unsigned long pixelClock);
void SaveRestoreState(AF_DRIVER *af, int subfunc, void *saveBuf);
void SetDisplayStart(AF_DRIVER *af, long x, long y, long waitVRT);
void SetActiveBuffer(AF_DRIVER *af, long index);
void SetVisibleBuffer(AF_DRIVER *af, long index, long waitVRT);
int  GetDisplayStartStatus(AF_DRIVER *af);
void SetPaletteData(AF_DRIVER *af, AF_PALETTE *pal, long num, long index, long waitVRT);
void SetBank(AF_DRIVER *af, long bank);
void SetCursor(AF_DRIVER *af, AF_CURSOR *cursor);
void SetCursorPos(AF_DRIVER *af, long x, long y);
void SetCursorColor(AF_DRIVER *af, unsigned char red, unsigned char green, unsigned char blue);
void ShowCursor(AF_DRIVER *af, long visible);
void WaitTillIdle(AF_DRIVER *af);
void SetMix(AF_DRIVER *af, long foreMix, long backMix);
void DrawScan(AF_DRIVER *af, long color, long y, long x1, long x2);
void DrawRect(AF_DRIVER *af, unsigned long color, long left, long top, long width, long height);
void DrawTrap(AF_DRIVER *af, unsigned long color, AF_TRAP *trap);
void PutMonoImage(AF_DRIVER *af, long foreColor, long backColor, long dstX, long dstY, long byteWidth, long srcX, long srcY, long width, long height, unsigned char *image);
void BitBlt(AF_DRIVER *af, long left, long top, long width, long height, long dstLeft, long dstTop, long op);


/* hardware info structure as defined by the NVidia libs */
RIVA_HW_INST riva;

RIVA_HW_STATE state;

RIVA_HW_STATE orig_state;


/* VGA register information */
typedef struct VGA_REGS
{
   unsigned char crt[25];
   unsigned char att[21];
   unsigned char gra[9];
   unsigned char seq[5];
   unsigned char mor;
} VGA_REGS;


VGA_REGS orig_regs;


unsigned short ports_table[] = 
{ 
   0x3B0, 0x3B1, 0x3B2, 0x3B3, 0x3B4, 0x3B5, 0x3B6, 0x3B7, 
   0x3B8, 0x3B9, 0x3BA, 0x3BB, 0x3BC, 0x3BD, 0x3BE, 0x3BF,
   0x3C0, 0x3C1, 0x3C2, 0x3C3, 0x3C4, 0x3C5, 0x3C6, 0x3C7, 
   0x3C8, 0x3C9, 0x3CA, 0x3CB, 0x3CC, 0x3CD, 0x3CE, 0x3CF,
   0x3D0, 0x3D1, 0x3D2, 0x3D3, 0x3D4, 0x3D5, 0x3D6, 0x3D7, 
   0x3D8, 0x3D9, 0x3DA, 0x3DB, 0x3DC, 0x3DD, 0x3DE, 0x3DF,
   0xFFFF 
};


/* list of features, so the install program can disable some of them */
FAF_CONFIG_DATA config_data[] = 
{
   {
      FAF_CFG_FEATURES,

      (fafLinear | fafBanked | fafHWCursor |
       fafDrawScan | fafDrawRect | fafDrawTrap | fafPutMonoImage | fafBitBlt)
   },

   { 0, 0 }
};

#define CFG_FEATURES    config_data[0].value


/* video mode and driver state information */
int af_bpp;
int af_width_bytes;
int af_width_pixels;
int af_height;
int af_visible_page;
int af_active_page;
int af_scroll_x;
int af_scroll_y;
int af_y;
int af_fore_mix;
int af_back_mix;

AF_PALETTE af_palette[256];

AF_CURSOR the_cursor;

int cur_color;

int cursor_fg;
int cursor_bg;

int cur_doublemode;

int riva_mix_mode;
int riva_current_mix;


/* magic numbers (for my driver, not hardware) */
#define RIVA128                 128
#define TNT                     42


/* PCI device identifiers */
#define PCI_VENDOR_NVIDIA_SGS   0x12D2
#define PCI_VENDOR_NVIDIA       0x10DE

#define PCI_CHIP_RIVA128        0x0018
#define PCI_CHIP_RIVA128_2      0x001C

#define PCI_CHIP_TNT            0x0020
#define PCI_CHIP_TNT_2          0x0024


int nvidia_list[] = {
   PCI_VENDOR_NVIDIA_SGS,  PCI_CHIP_RIVA128,    RIVA128,
   PCI_VENDOR_NVIDIA_SGS,  PCI_CHIP_RIVA128_2,  RIVA128,
   PCI_VENDOR_NVIDIA,      PCI_CHIP_TNT,        TNT,
   PCI_VENDOR_NVIDIA,      PCI_CHIP_TNT_2,      TNT,
   0
};


int nvidia_id;


/* list of supported video modes */
typedef struct VIDEO_MODE
{
   int vesa;
   int bpp;
   int w;
   int h;
   int nv_screen, nv_horiz, nv_arb0, nv_arb1, nv_vpll;
   VGA_REGS vga;
} VIDEO_MODE;


#define NUM_MODES 23


VIDEO_MODE mode_list[NUM_MODES] =
{
   { 0x130, 8,  320,  200,  0,  0, 3,   16, 312590, { { 45, 39, 39, 145, 42, 159, 191, 31, 0, 192, 0, 0, 0, 0, 0, 0, 156, 14, 143, 40, 0, 143, 192, 227, 255  }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 99  } },
   { 0x134, 8,  320,  240,  0,  0, 3,   16, 312590, { { 45, 39, 39, 145, 42, 159, 11, 62, 0, 192, 0, 0, 0, 0, 0, 0, 234, 12, 223, 40, 0, 223, 12, 227, 255    }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 227 } },
   { 0x131, 8,  320,  400,  0,  0, 3,   16, 312590, { { 45, 39, 39, 145, 42, 159, 191, 31, 0, 64, 0, 0, 0, 0, 0, 0, 156, 14, 143, 40, 0, 143, 192, 227, 255   }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 99  } },
   { 0x100, 8,  640,  400,  0,  0, 3,   16, 247054, { { 95, 79, 79, 131, 83, 159, 191, 31, 0, 64, 0, 0, 0, 0, 0, 0, 156, 14, 143, 80, 0, 143, 192, 227, 255   }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 99  } },
   { 0x101, 8,  640,  480,  0,  0, 131, 16, 247054, { { 95, 79, 79, 131, 83, 159, 11, 62, 0, 64, 0, 0, 0, 0, 0, 0, 234, 12, 223, 80, 0, 223, 12, 227, 255     }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 235 } },
   { 0x103, 8,  800,  600,  0,  0, 3,   16, 162571, { { 127, 99, 99, 131, 106, 26, 114, 240, 0, 96, 0, 0, 0, 0, 0, 0, 89, 13, 87, 100, 0, 87, 115, 227, 255   }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 43  } },
   { 0x105, 8,  1024, 768,  0,  0, 3,   16, 95757,  { { 163, 127, 127, 135, 132, 149, 36, 245, 0, 96, 0, 0, 0, 0, 0, 0, 3, 9, 255, 128, 0, 255, 37, 227, 255  }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 235 } },
   { 0x107, 8,  1280, 1024, 21, 0, 3,   16, 115981, { { 207, 159, 159, 147, 169, 25, 40, 90, 0, 96, 0, 0, 0, 0, 0, 0, 1, 4, 255, 160, 0, 255, 41, 227, 255    }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 43  } },
   { 0x10E, 16, 320,  200,  0,  0, 3,   16, 312590, { { 45, 39, 39, 145, 42, 159, 191, 31, 0, 192, 0, 0, 0, 0, 0, 0, 156, 14, 143, 80, 0, 143, 192, 227, 255  }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 99  } },
   { 0x135, 16, 320,  240,  0,  0, 3,   16, 312590, { { 45, 39, 39, 145, 42, 159, 11, 62, 0, 192, 0, 0, 0, 0, 0, 0, 234, 12, 223, 80, 0, 223, 12, 227, 255    }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 227 } },
   { 0x132, 16, 320,  400,  0,  0, 3,   16, 312590, { { 45, 39, 39, 145, 42, 159, 191, 31, 0, 64, 0, 0, 0, 0, 0, 0, 156, 14, 143, 80, 0, 143, 192, 227, 255   }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 99  } },
   { 0x13D, 16, 640,  400,  0,  0, 3,   16, 247054, { { 95, 79, 79, 131, 83, 159, 191, 31, 0, 64, 0, 0, 0, 0, 0, 0, 156, 14, 143, 160, 0, 143, 192, 227, 255  }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 99  } },
   { 0x111, 16, 640,  480,  0,  0, 131, 16, 247054, { { 95, 79, 79, 131, 83, 159, 11, 62, 0, 64, 0, 0, 0, 0, 0, 0, 234, 12, 223, 160, 0, 223, 12, 227, 255    }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 235 } },
   { 0x114, 16, 800,  600,  0,  0, 3,   16, 162571, { { 127, 99, 99, 131, 106, 26, 114, 240, 0, 96, 0, 0, 0, 0, 0, 0, 89, 13, 87, 200, 0, 87, 115, 227, 255   }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 43  } },
   { 0x117, 16, 1024, 768,  0,  0, 3,   16, 95757,  { { 163, 127, 127, 135, 132, 149, 36, 245, 0, 96, 0, 0, 0, 0, 0, 0, 3, 9, 255, 0, 0, 255, 37, 227, 255    }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 235 } },
   { 0x11A, 16, 1280, 1024, 21, 0, 3,   16, 115981, { { 207, 159, 159, 147, 169, 25, 40, 90, 0, 96, 0, 0, 0, 0, 0, 0, 1, 4, 255, 64, 0, 255, 41, 227, 255     }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 43  } },
   { 0x10F, 32, 320,  200,  0,  0, 3,   16, 312590, { { 45, 39, 39, 145, 42, 159, 191, 31, 0, 192, 0, 0, 0, 0, 0, 0, 156, 14, 143, 160, 0, 143, 192, 227, 255 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 99  } },
   { 0x136, 32, 320,  240,  0,  0, 3,   16, 312590, { { 45, 39, 39, 145, 42, 159, 11, 62, 0, 192, 0, 0, 0, 0, 0, 0, 234, 12, 223, 160, 0, 223, 12, 227, 255   }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 227 } },
   { 0x133, 32, 320,  400,  0,  0, 3,   16, 312590, { { 45, 39, 39, 145, 42, 159, 191, 31, 0, 64, 0, 0, 0, 0, 0, 0, 156, 14, 143, 160, 0, 143, 192, 227, 255  }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 99  } },
   { 0x13E, 32, 640,  400,  0,  0, 3,   16, 247054, { { 95, 79, 79, 131, 83, 159, 191, 31, 0, 64, 0, 0, 0, 0, 0, 0, 156, 14, 143, 64, 0, 143, 192, 227, 255   }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 99  } },
   { 0x112, 32, 640,  480,  0,  0, 131, 16, 247054, { { 95, 79, 79, 131, 83, 159, 11, 62, 0, 64, 0, 0, 0, 0, 0, 0, 234, 12, 223, 64, 0, 223, 12, 227, 255     }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 235 } },
   { 0x115, 32, 800,  600,  0,  0, 3,   16, 162571, { { 127, 99, 99, 131, 106, 26, 114, 240, 0, 96, 0, 0, 0, 0, 0, 0, 89, 13, 87, 144, 0, 87, 115, 227, 255   }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 43  } },
   { 0x118, 32, 1024, 768,  0,  0, 3,   17, 95757,  { { 163, 127, 127, 135, 132, 149, 36, 245, 0, 96, 0, 0, 0, 0, 0, 0, 3, 9, 255, 0, 0, 255, 37, 227, 255    }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65, 0, 15, 0, 0 }, { 0, 0, 0, 0, 0, 64, 5, 15, 255 }, { 3, 1, 15, 0, 14 }, 235 } }
};


short available_modes[NUM_MODES+1] = 
{ 
   1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
   17, 18, 19, 20, 21, 22, 23, -1
};




/* read_crt:
 *  Reads a VGA register.
 */
int read_crt(int index)
{
   outportb(0x3D4, index);
   return inportb(0x3D5);
}



/* read_gra:
 *  Reads a VGA register.
 */
int read_gra(int index)
{
   outportb(0x3CE, index);
   return inportb(0x3CF);
}



/* read_seq:
 *  Reads a VGA register.
 */
int read_seq(int index)
{
   outportb(0x3C4, index);
   return inportb(0x3C5);
}



/* write_crt:
 *  Writes a VGA register.
 */
void write_crt(int index, int value)
{
   outportb(0x3D4,index);
   outportb(0x3D5, value);
}



/* write_gra:
 *  Writes a VGA register.
 */
void write_gra(int index, int value)
{
   outportb(0x3CE, index);
   outportb(0x3CF, value);
}



/* write_seq:
 *  Writes a VGA register.
 */
void write_seq(int index, int value)
{
   outportb(0x3C4, index);
   outportb(0x3C5, value);
}



/* read_att:
 *  Reads a VGA register.
 */
int read_att(int index)
{
   inportb(0x3DA);
   outportb(0x3C0, index);
   return inportb(0x3C1);
}



/* att_end_reads:
 *  Finishes a VGA register read.
 */
void att_end_reads()
{
   inportb(0x3DA);
   outportb(0x3C0, 0x20);
}



/* write_att:
 *  Writes a VGA register.
 */
void write_att(int index, int val)
{
 outportb(0x3C0, index);
 outportb(0x3C0, val);
}



/* read_mor:
 *  Reads a VGA register.
 */
int read_mor()
{
   return inportb(0x3CC);
}



/* write_mor:
 *  Writes a VGA register.
 */
void write_mor(int val)
{
   outportb(0x3C2, val);
}



/* unload_vga_regs:
 *  Saves a VGA register set into a structure.
 */
void unload_vga_regs(VGA_REGS *regs)
{
   int i, mor_val;

   mor_val = read_mor();
   write_mor(mor_val | 1);
   regs->mor = mor_val;

   for (i=0; i<25; i++)
      regs->crt[i] = read_crt(i);

   for (i=0; i<21; i++)
      regs->att[i] = read_att(i);

   att_end_reads();

   for (i=0; i<9; i++)
      regs->gra[i] = read_gra(i);

   for (i=0; i<5; i++)
      regs->seq[i] = read_seq(i);

   write_mor(mor_val);
}



/* load_vga_regs:
 *  Loads a VGA register set from a structure.
 */
void load_vga_regs(VGA_REGS *regs)
{
   int i, crt11, mor_val;

   mor_val = read_mor();
   write_mor(mor_val | 1);

   do {
   } while (inportb(0x3DA) & 8);

   do {
   } while (!(inportb(0x3DA) & 8));

   write_seq(0x01, regs->seq[1] | 0x20);

   write_seq(0x00, 1);

   for (i=2; i<5; i++)
      write_seq(i, regs->seq[i]);

   write_seq(0x00, regs->seq[0]);

   crt11 = regs->crt[0x11];
   write_crt(0x11, crt11 & 0x7F);

   for (i=0; i<25; i++)
      if (i != 0x11)
	 write_crt(i, regs->crt[i]);

   write_crt(0x11, crt11);

   for (i=0; i<9; i++)
      write_gra(i, regs->gra[i]);

   inportb(0x3DA);

   for (; i<21; i++)
      write_att(i, regs->att[i]);

   att_end_reads();

   write_mor(regs->mor);

   write_seq(0x01, regs->seq[1]);
}



/* SetupDriver:
 *  The first thing ever to be called after our code has been relocated.
 *  This is in charge of filling in the driver header with all the required
 *  information and function pointers. We do not yet have access to the
 *  video memory, so we can't talk directly to the card.
 */
int SetupDriver(AF_DRIVER *af)
{
   unsigned long regBase, frameBase;
   int bus_id;
   char *name;
   int i;

   /* find PCI device */
   nvidia_id = 0;
   bus_id = 0;

   for (i=0; nvidia_list[i]; i+=3) {
      if (FindPCIDevice(nvidia_list[i+1], nvidia_list[i], 0, &bus_id)) {
	 nvidia_id = nvidia_list[i+2];
	 break;
      }
   }

   if (!nvidia_id)
      return -1;

   /* set up driver information */
   af->AvailableModes = available_modes;

   af->Attributes = (afHaveMultiBuffer | 
		     afHaveVirtualScroll | 
		     afHaveAccel2D);

   if (CFG_FEATURES & fafLinear)
      af->Attributes |= afHaveLinearBuffer;

   if (CFG_FEATURES & fafBanked)
      af->Attributes |= afHaveBankedBuffer;

   if (CFG_FEATURES & fafHWCursor)
      af->Attributes |= afHaveHWCursor;

   af->BankSize = 64;
   af->BankedBasePtr = 0xA0000;

   af->IOPortsTable = ports_table;

   /* work out the linear framebuffer and MMIO addresses */
   regBase = PCIReadLong(bus_id, 16) & 0xFF800000;
   frameBase = PCIReadLong(bus_id, 20) & 0xFF800000;

   /* don't know the memory size yet, so guess 16 meg (this is yuck) */
   af->LinearBasePtr = frameBase;
   af->LinearSize = 16384;

   af->IOMemoryBase[0] = regBase;
   af->IOMemoryLen[0] = 16384*1024;

   /* get access to the textmode font memory */
   af->IOMemoryBase[1] = 0xB8000;
   af->IOMemoryLen[1] = 32768;

   /* set up the driver name */
   if (nvidia_id == RIVA128)
      name = "Riva 128";
   else if (nvidia_id == TNT)
      name = "Riva TNT";
   else
      return -1;

   i = 0;
   while (af->OemVendorName[i])
      i++;

   af->OemVendorName[i++] = ',';
   af->OemVendorName[i++] = ' ';

   while (*name)
      af->OemVendorName[i++] = *(name++);

   af->OemVendorName[i] = 0;

   /* set up driver functions */
   af->SetBank32 = SetBank32;
   af->SetBank32Len = (long)SetBank32End - (long)SetBank32;

   af->SupplementalExt = ExtStub;
   af->GetVideoModeInfo = GetVideoModeInfo;
   af->SetVideoMode = SetVideoMode;
   af->RestoreTextMode = RestoreTextMode;
   af->GetClosestPixelClock = GetClosestPixelClock;
   af->SaveRestoreState = SaveRestoreState;
   af->SetDisplayStart = SetDisplayStart;
   af->SetActiveBuffer = SetActiveBuffer;
   af->SetVisibleBuffer = SetVisibleBuffer;
   af->GetDisplayStartStatus = GetDisplayStartStatus;
   af->SetPaletteData = SetPaletteData;
   af->SetBank = SetBank;
   af->SetCursor = SetCursor;
   af->SetCursorPos = SetCursorPos;
   af->SetCursorColor = SetCursorColor;
   af->ShowCursor = ShowCursor;
   af->WaitTillIdle = WaitTillIdle;
   af->SetMix = SetMix;
   af->DrawScan = DrawScan;
   af->DrawRect = DrawRect;
   af->DrawTrap = DrawTrap;
   af->PutMonoImage = PutMonoImage;
   af->BitBlt = BitBlt;

   fixup_feature_list(af, CFG_FEATURES);

   return 0;
}



/* InitDriver:
 *  The second thing to be called during the init process, after the 
 *  application has mapped all the memory and I/O resources we need.
 *  This is in charge of finding the card, returning 0 on success or
 *  -1 to abort.
 */
int InitDriver(AF_DRIVER *af)
{
   int tmp;

   riva.EnableIRQ = 0;

   riva.IO = (inportb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;

   riva.PRAMDAC = (unsigned *)(af->IOMemMaps[0]+0x00680000);
   riva.PFB     = (unsigned *)(af->IOMemMaps[0]+0x00100000);
   riva.PFIFO   = (unsigned *)(af->IOMemMaps[0]+0x00002000);
   riva.PGRAPH  = (unsigned *)(af->IOMemMaps[0]+0x00400000);
   riva.PEXTDEV = (unsigned *)(af->IOMemMaps[0]+0x00101000);
   riva.PTIMER  = (unsigned *)(af->IOMemMaps[0]+0x00009000);
   riva.PMC     = (unsigned *)(af->IOMemMaps[0]+0x00000000);
   riva.FIFO    = (unsigned *)(af->IOMemMaps[0]+0x00800000);

   if (nvidia_id == RIVA128) {
      riva.Architecture = 3;
      riva.PRAMIN = (unsigned *)(af->LinearMem+0x00C00000);
   }
   else if (nvidia_id == TNT) {
      riva.Architecture = 4;
      riva.PRAMIN = (unsigned *)(af->IOMemMaps[0]+0x00710000);
      riva.PCRTC  = (unsigned *)(af->IOMemMaps[0]+0x00600000);
   }
   else
      return -1;

   RivaGetConfig(&riva);

   /* unlock registers */
   outportb(riva.IO+4, 0x11);
   tmp = inportb(riva.IO+5);
   outportb(riva.IO+5, tmp & 0x7F);
   outportb(riva.LockUnlockIO, riva.LockUnlockIndex);
   outportb(riva.LockUnlockIO+1, 0x57);

   unload_vga_regs(&orig_regs);
   riva.UnloadStateExt(&riva, &orig_state);

   af->TotalMemory = MIN(riva.RamAmountKBytes, 16384);

   return 0;
}



/* FreeBEX:
 *  Returns an interface structure for the requested FreeBE/AF extension.
 */
void *FreeBEX(AF_DRIVER *af, unsigned long id)
{
   switch (id) {

      case FAFEXT_CONFIG:
	 /* allow the install program to configure our driver */
	 return config_data;

      default:
	 return NULL;
   }
}



/* ExtStub:
 *  Vendor-specific extension hook: we don't provide any.
 */
int ExtStub()
{
   return 0;
}



/* GetVideoModeInfo:
 *  Retrieves information about this video mode, returning zero on success
 *  or -1 if the mode is invalid.
 */
long GetVideoModeInfo(AF_DRIVER *af, short mode, AF_MODE_INFO *modeInfo)
{
   VIDEO_MODE *info;
   int i;

   if ((mode <= 0) || (mode > NUM_MODES))
      return -1;

   info = &mode_list[mode-1];

   /* clear the structure to zero */
   for (i=0; i<(int)sizeof(AF_MODE_INFO); i++)
      ((char *)modeInfo)[i] = 0;

   /* copy data across from our stored list of mode attributes */
   modeInfo->Attributes = (afHaveMultiBuffer | 
			   afHaveVirtualScroll | 
			   afHaveAccel2D);

   if (CFG_FEATURES & fafLinear)
      modeInfo->Attributes |= afHaveLinearBuffer;

   if (CFG_FEATURES & fafBanked)
      modeInfo->Attributes |= afHaveBankedBuffer;

   if (CFG_FEATURES & fafHWCursor)
      modeInfo->Attributes |= afHaveHWCursor;

   modeInfo->XResolution = info->w;
   modeInfo->YResolution = info->h;
   modeInfo->BitsPerPixel = info->bpp;

   /* available pages of video memory */
   modeInfo->MaxBuffers = (af->TotalMemory * 1024) / 
			  (info->w * info->h * BYTES_PER_PIXEL(info->bpp));

   modeInfo->BnkMaxBuffers = modeInfo->LinMaxBuffers = modeInfo->MaxBuffers;

   /* scanline length info */
   modeInfo->BytesPerScanLine = info->w * BYTES_PER_PIXEL(info->bpp);
   modeInfo->LinBytesPerScanLine = info->w * BYTES_PER_PIXEL(info->bpp);
   modeInfo->MaxBytesPerScanLine = 2048 * BYTES_PER_PIXEL(info->bpp);
   modeInfo->MaxScanLineWidth = 2048;

   /* pixel format */
   switch (info->bpp) {

      case 32:
      case 24:
	 modeInfo->RedMaskSize        = modeInfo->LinRedMaskSize        = 8;
	 modeInfo->RedFieldPosition   = modeInfo->LinRedFieldPosition   = 16;
	 modeInfo->GreenMaskSize      = modeInfo->LinGreenMaskSize      = 8;
	 modeInfo->GreenFieldPosition = modeInfo->LinGreenFieldPosition = 8;
	 modeInfo->BlueMaskSize       = modeInfo->LinBlueMaskSize       = 8;
	 modeInfo->BlueFieldPosition  = modeInfo->LinBlueFieldPosition  = 0;
	 modeInfo->RsvdMaskSize       = modeInfo->LinRsvdMaskSize       = 8;
	 modeInfo->RsvdFieldPosition  = modeInfo->LinRsvdFieldPosition  = 24;
	 break;

      case 16:
	 modeInfo->RedMaskSize        = modeInfo->LinRedMaskSize        = 5;
	 modeInfo->RedFieldPosition   = modeInfo->LinRedFieldPosition   = 11;
	 modeInfo->GreenMaskSize      = modeInfo->LinGreenMaskSize      = 6;
	 modeInfo->GreenFieldPosition = modeInfo->LinGreenFieldPosition = 5;
	 modeInfo->BlueMaskSize       = modeInfo->LinBlueMaskSize       = 5;
	 modeInfo->BlueFieldPosition  = modeInfo->LinBlueFieldPosition  = 0;
	 modeInfo->RsvdMaskSize       = modeInfo->LinRsvdMaskSize       = 0;
	 modeInfo->RsvdFieldPosition  = modeInfo->LinRsvdFieldPosition  = 0;
	 break;

      case 15:
	 modeInfo->RedMaskSize        = modeInfo->LinRedMaskSize        = 5;
	 modeInfo->RedFieldPosition   = modeInfo->LinRedFieldPosition   = 10;
	 modeInfo->GreenMaskSize      = modeInfo->LinGreenMaskSize      = 6;
	 modeInfo->GreenFieldPosition = modeInfo->LinGreenFieldPosition = 5;
	 modeInfo->BlueMaskSize       = modeInfo->LinBlueMaskSize       = 5;
	 modeInfo->BlueFieldPosition  = modeInfo->LinBlueFieldPosition  = 0;
	 modeInfo->RsvdMaskSize       = modeInfo->LinRsvdMaskSize       = 1;
	 modeInfo->RsvdFieldPosition  = modeInfo->LinRsvdFieldPosition  = 15;
	 break;

      default:
	 modeInfo->RedMaskSize        = modeInfo->LinRedMaskSize        = 0;
	 modeInfo->RedFieldPosition   = modeInfo->LinRedFieldPosition   = 0;
	 modeInfo->GreenMaskSize      = modeInfo->LinGreenMaskSize      = 0;
	 modeInfo->GreenFieldPosition = modeInfo->LinGreenFieldPosition = 0;
	 modeInfo->BlueMaskSize       = modeInfo->LinBlueMaskSize       = 0;
	 modeInfo->BlueFieldPosition  = modeInfo->LinBlueFieldPosition  = 0;
	 modeInfo->RsvdMaskSize       = modeInfo->LinRsvdMaskSize       = 0;
	 modeInfo->RsvdFieldPosition  = modeInfo->LinRsvdFieldPosition  = 0;
	 break;
   }

   /* I'm not sure exactly what these should be: Allegro doesn't use them */
   modeInfo->MaxPixelClock = 135000000;
   modeInfo->VideoCapabilities = 0;
   modeInfo->VideoMinXScale = 0;
   modeInfo->VideoMinYScale = 0;
   modeInfo->VideoMaxXScale = 0;
   modeInfo->VideoMaxYScale = 0;

   return 0;
}



/* SetVideoMode:
 *  Sets the specified video mode, returning zero on success.
 *
 *  Possible flag bits that may be or'ed with the mode number:
 *
 *    0x8000 = don't clear video memory
 *    0x4000 = enable linear framebuffer
 *    0x2000 = enable multi buffering
 *    0x1000 = enable virtual scrolling
 *    0x0800 = use refresh rate control
 *    0x0400 = use hardware stereo
 */
long SetVideoMode(AF_DRIVER *af, short mode, long virtualX, long virtualY, long *bytesPerLine, int numBuffers, AF_CRTCInfo *crtc)
{
   int linear = ((mode & 0x4000) != 0);
   int noclear = ((mode & 0x8000) != 0);
   long available_vram;
   long used_vram;
   VIDEO_MODE *info;
   int i;

   #ifdef USE_VESA
      RM_REGS r;
   #endif

   /* reject anything with hardware stereo */
   if (mode & 0x400)
      return -1;

   /* reject linear/banked modes if the install program has disabled them */
   if (linear) {
      if (!(CFG_FEATURES & fafLinear))
	 return -1;
   }
   else {
      if (!(CFG_FEATURES & fafBanked))
	 return -1;
   }

   /* mask off the other flag bits */
   mode &= 0x3FF;

   if ((mode <= 0) || (mode > NUM_MODES))
      return -1;

   info = &mode_list[mode-1];

   /* adjust the virtual width */
   *bytesPerLine = ((MAX(info->w, virtualX) + 15) & ~15) * BYTES_PER_PIXEL(info->bpp);

   /* calculate NVidia register state (we only sort of half-use this
    * data, because I'm too dumb to figure out how to do it properly :-)
    */
   riva.CalcStateExt(&riva, &state, 
		     info->bpp,                                /* bpp */
		     *bytesPerLine/BYTES_PER_PIXEL(info->bpp), /* w */
		     info->w,                                  /* hsize */
		     0,                                        /* hdisplay */
		     0,                                        /* hstart */
		     0,                                        /* hend */
		     0,                                        /* htotal */
		     info->h,                                  /* height */
		     0,                                        /* vdisplay */
		     0,                                        /* vstart */
		     0,                                        /* vend */
		     0,                                        /* vtotal */
		     0                                         /* clock */
		     );

   #ifdef USE_VESA

      /* call VESA to set the mode */
      r.x.ax = 0x4F02;
      r.x.bx = info->vesa;
      if (linear)
	 r.x.bx |= 0x4000;
      if (noclear)
	 r.x.bx |= 0x8000;
      rm_int(0x10, &r);
      if (r.h.ah)
	 return -1;

      /* debugging code: dump register values to stdout */
      /*
      {
	 RIVA_HW_STATE cur_state;
	 VGA_REGS cur_regs;

	 unload_vga_regs(&cur_regs);
	 riva.UnloadStateExt(&riva, &cur_state);

	 trace_printf("%dx%d, %d\n", info->w, info->h, info->bpp);
	 trace_printf("%d\n", cur_state.screen);
	 trace_printf("%d\n", cur_state.horiz);
	 trace_printf("%d\n", cur_state.arbitration0);
	 trace_printf("%d\n", cur_state.arbitration1);
	 trace_printf("%d\n", cur_state.vpll);

	 trace_printf("{ ");
	 for (i=0; i<25; i++) trace_printf("%d, ", cur_regs.crt[i]);
	 trace_printf("}\n");

	 trace_printf("{ ");
	 for (i=0; i<21; i++) trace_printf("%d, ", cur_regs.att[i]);
	 trace_printf("}\n");

	 trace_printf("{ ");
	 for (i=0; i<9; i++) trace_printf("%d, ", cur_regs.gra[i]);
	 trace_printf("}\n");

	 trace_printf("{ ");
	 for (i=0; i<5; i++) trace_printf("%d, ", cur_regs.seq[i]);
	 trace_printf("}\n");

	 trace_printf("%d\n\n", cur_regs.mor);
      }
      */

      riva.LoadStateExt(&riva, &state, FALSE);

   #else

      /* do a true register level mode set */
      state.screen = info->nv_screen;
      state.horiz = info->nv_horiz;
      state.arbitration0 = info->nv_arb0;
      state.arbitration1 = info->nv_arb1;
      state.vpll = info->nv_vpll;

      load_vga_regs(&info->vga);
      riva.LoadStateExt(&riva, &state, TRUE);

      if (info->bpp == 8) {
	 for (i=0; i<256; i++) {
	    outportb(0x3C8, i);
	    outportb(0x3C9, DefaultVGAPalette[i*3]);
	    outportb(0x3C9, DefaultVGAPalette[i*3+1]);
	    outportb(0x3C9, DefaultVGAPalette[i*3+2]);
	 }
      }

   #endif

   /* set the virtual width */
   outportb(0x3D4, 0x13);
   outportb(0x3D5, *bytesPerLine/8);

   /* store info about the current mode */
   af_bpp = info->bpp;
   af_width_bytes = *bytesPerLine;
   af_width_pixels = *bytesPerLine/BYTES_PER_PIXEL(af_bpp);
   af_height = MAX(info->h, virtualY);
   af_visible_page = 0;
   af_active_page = 0;
   af_scroll_x = 0;
   af_scroll_y = 0;
   af_y = 0;

   /* return framebuffer dimensions to the application */
   af->BufferEndX = af_width_pixels-1;
   af->BufferEndY = af_height-1;
   af->OriginOffset = 0;

   used_vram = af_width_bytes * af_height * numBuffers;
   available_vram = af->TotalMemory*1024;

   if (used_vram > available_vram)
      return -1;

   if (available_vram-used_vram >= af_width_bytes) {
      af->OffscreenOffset = used_vram;
      af->OffscreenStartY = af_height*numBuffers;
      af->OffscreenEndY = available_vram/af_width_bytes-1;
   }
   else {
      af->OffscreenOffset = 0;
      af->OffscreenStartY = 0;
      af->OffscreenEndY = 0;
   }

   /* set up the accelerator engine */
   RIVA_FIFO_FREE(riva, Rop, 1);
   riva.Rop->Rop3 = 0xCC;

   RIVA_FIFO_FREE(riva, Clip, 2);
   riva.Clip->TopLeft = 0;
   riva.Clip->WidthHeight = 0x40004000;

   RIVA_FIFO_FREE(riva, Patt, 5);
   riva.Patt->Shape = 0;
   riva.Patt->Color0 = ~0;
   riva.Patt->Color1 = ~0;
   riva.Patt->Monochrome[0] = 0;
   riva.Patt->Monochrome[1] = 0;

   SetMix(af, AF_REPLACE_MIX, AF_FORE_MIX);

   riva_current_mix = 0;

   cur_color = -1;
   cur_doublemode = (info->h < 400);

   #ifndef USE_VESA

      /* clear the framebuffer */
      if (!noclear) {
	 DrawRect(af, 0, 0, 0, af_width_pixels, af_height);
	 WaitTillIdle(af);
      }

   #endif

   return 0;
}



/* RestoreTextMode:
 *  Returns to text mode, shutting down the accelerator hardware.
 */
void RestoreTextMode(AF_DRIVER *af)
{
   unsigned *p = (unsigned *)VGA8x16Font;
   unsigned *font = af->IOMemMaps[1];
   int i, j;

   /* shut down video mode */
   riva.LoadStateExt(&riva, &orig_state, TRUE);
   load_vga_regs(&orig_regs);

   /* reset the accelerator */
   RIVA_FIFO_FREE(riva, Rop, 1);
   riva.Rop->Rop3 = 0xCC;

   RIVA_FIFO_FREE(riva, Clip, 2);
   riva.Clip->TopLeft = 0;
   riva.Clip->WidthHeight = 0x40004000;

   RIVA_FIFO_FREE(riva, Patt, 5);
   riva.Patt->Shape = 0;
   riva.Patt->Color0 = ~0;
   riva.Patt->Color1 = ~0;
   riva.Patt->Monochrome[0] = 0;
   riva.Patt->Monochrome[1] = 0;

   /* restore text mode font */
   write_seq(4, 6);
   write_seq(2, 4);
   write_gra(5, 0);

   for (i=0, j=0; i<1024; i+=4, j+=8) {
      font[j+0] = p[i+0];
      font[j+1] = p[i+1];
      font[j+2] = p[i+2];
      font[j+3] = p[i+3];
      font[j+4] = 0;
      font[j+5] = 0;
      font[j+6] = 0;
      font[j+7] = 0;
   }

   /* clear text mode screen */
   write_seq(4, 2);
   write_seq(2, 3);
   write_gra(5, 0x10);

   for (i=0; i<1000; i++)
      font[i] = 0x07200720;

   /* restore text mode palette */
   for (i=0; i<256; i++) {
      outportb(0x3C8, i);
      outportb(0x3C9, DefaultTXTPalette[i*3]);
      outportb(0x3C9, DefaultTXTPalette[i*3+1]);
      outportb(0x3C9, DefaultTXTPalette[i*3+2]);
   }
}



/* GetClosestPixelClock:
 *  I don't have a clue what this should return: it is used for the
 *  refresh rate control.
 */
long GetClosestPixelClock(AF_DRIVER *af, short mode, unsigned long pixelClock)
{
   /* ??? */
   return 135000000;
}



/* SaveRestoreState:
 *  Stores the current driver status: not presently implemented.
 */
void SaveRestoreState(AF_DRIVER *af, int subfunc, void *saveBuf)
{
   /* not implemented (not used by Allegro) */
}



/* SetDisplayStart:
 *  Hardware scrolling function.
 */
void SetDisplayStart(AF_DRIVER *af, long x, long y, long waitVRT)
{
   long addr;

   if (waitVRT >= 0) {
      addr = (((y + af_visible_page*af_height) * af_width_bytes) + 
	      (x*BYTES_PER_PIXEL(af_bpp)));

      riva.SetStartAddress(&riva, addr);

      if (waitVRT) {
	 do {
	 } while (inportb(0x3DA) & 8);

	 do {
	 } while (!(inportb(0x3DA) & 8));
      }
   }

   af_scroll_x = x;
   af_scroll_y = y;
}



/* SetActiveBuffer:
 *  Sets which buffer is being drawn onto, for use in multi buffering
 *  systems (not used by Allegro).
 */
void SetActiveBuffer(AF_DRIVER *af, long index)
{
   if (af->OffscreenOffset) {
      af->OffscreenStartY += af_active_page*af_height;
      af->OffscreenEndY += af_active_page*af_height;
   }

   af_active_page = index;
   af_y = index*af_height;

   af->OriginOffset = af_width_bytes*af_height*index;

   if (af->OffscreenOffset) {
      af->OffscreenStartY -= af_active_page*af_height;
      af->OffscreenEndY -= af_active_page*af_height;
   }
}



/* SetVisibleBuffer:
 *  Sets which buffer is displayed on the screen, for use in multi buffering
 *  systems (not used by Allegro).
 */
void SetVisibleBuffer(AF_DRIVER *af, long index, long waitVRT)
{
   af_visible_page = index;

   SetDisplayStart(af, af_scroll_x, af_scroll_y, waitVRT);
}



/* GetDisplayStartStatus:
 *  Status poll for triple buffering. Not possible on the majority of
 *  present cards: this function is just a placeholder.
 */
int GetDisplayStartStatus(AF_DRIVER *af)
{
   return 1;
}



/* SetPaletteData:
 *  Palette setting routine.
 */
void SetPaletteData(AF_DRIVER *af, AF_PALETTE *pal, long num, long index, long waitVRT)
{
   int i;

   if (waitVRT) {
      do {
      } while (inportb(0x3DA) & 8);

      do {
      } while (!(inportb(0x3DA) & 8));
   }

   for (i=0; i<num; i++) {
      outportb(0x3C8, index+i);
      outportb(0x3C9, pal[i].red);
      outportb(0x3C9, pal[i].green);
      outportb(0x3C9, pal[i].blue);

      af_palette[index+i] = pal[i];
   }

   if ((af_bpp == 8) && (cur_color >= 0) && 
       (((cur_color >= index) && (cur_color < index+num)) || (index == 0)))
      SetCursorColor(af, cur_color, 0, 0);
}



/* SetBank32:
 *  Relocatable bank switch function. This is called with a bank number in
 *  %edx.
 */

asm ("

   .globl _SetBank32, _SetBank32End

      .align 4
   _SetBank32:
      pushl %eax
      pushl %ebx
      pushl %edx

      movl %edx, %ebx
      shll $1, %ebx

      movw $0x3D4, %dx
      movw $0x571F, %ax
      outw %ax, %dx
      movb %bl, %ah
      movb $0x1D, %al
      outw %ax, %dx
      movb $0x1E, %al
      outw %ax, %dx

      orb %bh, %bh
      jz SkipHighBits

      movb %bh, %ah
      movb $0x29, %al
      orb $3, %ah
      outw %ax, %dx

   SkipHighBits:
      popl %edx
      popl %ebx
      popl %eax
      ret

   _SetBank32End:

");



/* SetBank:
 *  C-callable bank switch function. This version simply chains to the
 *  relocatable SetBank32() above.
 */
void SetBank(AF_DRIVER *af, long bank)
{
   asm (
      " call _SetBank32 "
   :
   : "d" (bank)
   );
}



/* pack RGB values into 5551 format */
#define CURSOR_RGB(r, g, b)   ((((r) >> 3) & 0x1F) << 10) | \
			      ((((g) >> 3) & 0x1F) << 5) | \
			      (((b) >> 3) & 0x1F) | 0x8000



/* LoadCursor:
 *  Worker function for downloading a cursor image to the card.
 */
void LoadCursor()
{
   unsigned short image[32][32];
   int i, j, x, y, save;

   for (y=0; y<32; y++) {
      for (x=0; x<32; x++) {
	 i = 1 << ((x/8)*8 + 7-(x&7));
	 j = (cur_doublemode) ? y/2 : y;

	 if (the_cursor.andMask[j] & i) {
	    if (the_cursor.xorMask[j] & i)
	       image[y][x] = cursor_fg;
	    else
	       image[y][x] = cursor_bg;
	 }
	 else
	    image[y][x] = 0;
      }
   }

   save = riva.ShowHideCursor(&riva, 0);

   for (i=0; i<sizeof(image)/sizeof(int); i++)
      riva.CURSOR[i] = ((int *)image)[i];

   riva.ShowHideCursor(&riva, save);
}



/* SetCursor:
 *  Sets the hardware cursor shape.
 */
void SetCursor(AF_DRIVER *af, AF_CURSOR *cursor)
{
   int i;

   for (i=0; i<sizeof(AF_CURSOR); i++)
      ((char *)&the_cursor)[i] = ((char *)cursor)[i];

   LoadCursor();
}



/* SetCursorPos:
 *  Sets the hardware cursor position.
 */
void SetCursorPos(AF_DRIVER *af, long x, long y)
{
   x -= the_cursor.hotx;
   y -= the_cursor.hoty;

   if (cur_doublemode)
      y *= 2;

   *(riva.CURSORPOS) = (x&0xFFFF) | (y<<16);
}



/* SetCursorColor:
 *  Sets the hardware cursor color.
 */
void SetCursorColor(AF_DRIVER *af, unsigned char red, unsigned char green, unsigned char blue)
{
   int r2, g2, b2;

   if (af_bpp == 8) {
      cur_color = red;

      red = af_palette[cur_color].red;
      green = af_palette[cur_color].green;
      blue = af_palette[cur_color].blue;

      r2 = af_palette[0].red;
      g2 = af_palette[0].green;
      b2 = af_palette[0].blue;
   }
   else {
      cur_color = -1;

      r2 = ~red;
      g2 = ~green;
      b2 = ~blue;
   }

   cursor_fg = CURSOR_RGB(red, green, blue);
   cursor_bg = CURSOR_RGB(r2, g2, b2);

   LoadCursor();
}



/* ShowCursor:
 *  Turns the hardware cursor on or off.
 */
void ShowCursor(AF_DRIVER *af, long visible)
{
   riva.ShowHideCursor(&riva, visible);
}



/* WaitTillIdle:
 *  Delay until the hardware controller has finished drawing.
 */
void WaitTillIdle(AF_DRIVER *af)
{
   while (riva.Busy(&riva))
      ;
}



/* SetMix:
 *  Specifies the pixel mix mode to be used for hardware drawing functions.
 */
void SetMix(AF_DRIVER *af, long foreMix, long backMix)
{
   af_fore_mix = foreMix;
   af_back_mix = backMix;

   switch (af_fore_mix) {

      case AF_REPLACE_MIX: 
	 riva_mix_mode = 0xCC;
	 break;

      case AF_AND_MIX:
	 riva_mix_mode = 0x88;
	 break;

      case AF_OR_MIX:
	 riva_mix_mode = 0xEE;
	 break;

      case AF_XOR_MIX:
	 riva_mix_mode = 0x66;
	 break;

      default: 
	 riva_mix_mode = 0xAA;
	 break;
   }
}



/* UpdateMix:
 *  Helper for syncing the hardware with the current mix setting.
 */
void UpdateMix()
{
   if (riva_mix_mode != riva_current_mix) {
      RIVA_FIFO_FREE(riva, Rop, 1);
      riva.Rop->Rop3 = riva_mix_mode;
      riva_current_mix = riva_mix_mode;
   }
}



/* DrawScan:
 *  Fills a scanline in the current foreground mix mode. Draws up to but
 *  not including the second x coordinate. If the second coord is less
 *  than the first, they are swapped. If they are equal, nothing is drawn.
 */
void DrawScan(AF_DRIVER *af, long color, long y, long x1, long x2)
{
   UpdateMix();

   RIVA_FIFO_FREE(riva, Bitmap, 3);

   riva.Bitmap->Color1A = color;

   if (x1 < x2) {
      riva.Bitmap->UnclippedRectangle[0].TopLeft = (x1<<16) | (y+af_y);
      riva.Bitmap->UnclippedRectangle[0].WidthHeight = ((x2-x1)<<16) | 1;
   }
   else if (x2 < x1) {
      riva.Bitmap->UnclippedRectangle[0].TopLeft = (x2<<16) | (y+af_y);
      riva.Bitmap->UnclippedRectangle[0].WidthHeight = ((x1-x2)<<16) | 1;
   }
}



/* DrawRect:
 *  Draws a hardware accelerated rectangle.
 */
void DrawRect(AF_DRIVER *af, unsigned long color, long left, long top, long width, long height)
{
   UpdateMix();

   RIVA_FIFO_FREE(riva, Bitmap, 3);

   riva.Bitmap->Color1A = color;
   riva.Bitmap->UnclippedRectangle[0].TopLeft = (left<<16) | (top+af_y);
   riva.Bitmap->UnclippedRectangle[0].WidthHeight = (width<<16) | height;
}



/* DrawTrap:
 *  Draws a filled trapezoid, using the current foreground mix mode.
 */
void DrawTrap(AF_DRIVER *af, unsigned long color, AF_TRAP *trap)
{
   int ix1, ix2;

   UpdateMix();

   RIVA_FIFO_FREE(riva, Bitmap, 1);
   riva.Bitmap->Color1A = color;

   while (trap->count--) {
      ix1 = (trap->x1+0x8000) >> 16;
      ix2 = (trap->x2+0x8000) >> 16;

      if (ix1 < ix2) {
	 RIVA_FIFO_FREE(riva, Bitmap, 2);
	 riva.Bitmap->UnclippedRectangle[0].TopLeft = (ix1<<16) | (trap->y+af_y);
	 riva.Bitmap->UnclippedRectangle[0].WidthHeight = ((ix2-ix1)<<16) | 1;
      }
      else if (ix2 < ix1) {
	 RIVA_FIFO_FREE(riva, Bitmap, 2);
	 riva.Bitmap->UnclippedRectangle[0].TopLeft = (ix2<<16) | (trap->y+af_y);
	 riva.Bitmap->UnclippedRectangle[0].WidthHeight = ((ix1-ix2)<<16) | 1;
      }

      trap->x1 += trap->slope1;
      trap->x2 += trap->slope2;
      trap->y++;
   }
}



/* PutMonoImage:
 *  Expands a monochrome bitmap from system memory onto the screen.
 */
void PutMonoImage(AF_DRIVER *af, long foreColor, long backColor, long dstX, long dstY, long byteWidth, long srcX, long srcY, long width, long height, unsigned char *image)
{
   int i, w, h;

   UpdateMix();

   image += srcY*byteWidth + srcX/8;
   srcX &= 7;

   if (af_back_mix == AF_NOP_MIX) {
      /* masked drawing mode */
      RIVA_FIFO_FREE(riva, Bitmap, 3);

      riva.Bitmap->ClipD.TopLeft = ((dstY+af_y) << 16) | (dstX+srcX);
      riva.Bitmap->ClipD.BottomRight = ((dstY+af_y+height) << 16) | (dstX+width);
      riva.Bitmap->Color1D = foreColor;

      RIVA_FIFO_FREE(riva, Bitmap, 3);

      if (width <= 8) {
	 /* for masked characters up to 8 pixels wide */
	 h = (height+3)/4;

	 riva.Bitmap->WidthHeightInD = (h<<18) | 8;
	 riva.Bitmap->WidthHeightOutD = (height<<16) | width;
	 riva.Bitmap->PointD = ((dstY+af_y)<<16) | dstX;

	 while (h--) {
	    RIVA_FIFO_FREE(riva, Bitmap, 1);
	    riva.Bitmap->MonochromeData1D = (image[0]) | 
					    (image[byteWidth] << 8) | 
					    (image[byteWidth*2] << 16) | 
					    (image[byteWidth*3] << 24);
	    image += byteWidth*4;
	 }
      }
      else if (width <= 16) {
	 /* for masked characters up to 16 pixels wide */
	 h = (height+1)/2;

	 riva.Bitmap->WidthHeightInD = (h<<17) | 16;
	 riva.Bitmap->WidthHeightOutD = (height<<16) | width;
	 riva.Bitmap->PointD = ((dstY+af_y)<<16) | dstX;

	 while (h--) {
	    RIVA_FIFO_FREE(riva, Bitmap, 1);
	    riva.Bitmap->MonochromeData1D = (image[0]) | 
					    (image[byteWidth] << 16);
	    image += byteWidth*2;
	 }
      }
      else {
	 /* for masked characters more than 16 pixels wide */
	 w = (width+31)/32;

	 riva.Bitmap->WidthHeightInD = (height<<16) | (w<<5);
	 riva.Bitmap->WidthHeightOutD = (height<<16) | width;
	 riva.Bitmap->PointD = ((dstY+af_y)<<16) | dstX;

	 while (height--) {
	    for (i=0; i<w; i++) {
	       RIVA_FIFO_FREE(riva, Bitmap, 1);
	       riva.Bitmap->MonochromeData1D = image[i];
	    }
	    image += byteWidth;
	 }
      }
   }
   else {
      /* opaque drawing mode */
      RIVA_FIFO_FREE(riva, Bitmap, 4);

      riva.Bitmap->ClipE.TopLeft = ((dstY+af_y) << 16) | (dstX+srcX);
      riva.Bitmap->ClipE.BottomRight = ((dstY+af_y+height) << 16) | (dstX+width);
      riva.Bitmap->Color0E = backColor;
      riva.Bitmap->Color1E = foreColor;

      RIVA_FIFO_FREE(riva, Bitmap, 3);

      if (width <= 8) {
	 /* for opaque characters up to 8 pixels wide */
	 h = (height+3)/4;

	 riva.Bitmap->WidthHeightInE = (h<<18) | 8;
	 riva.Bitmap->WidthHeightOutE = (height<<16) | width;
	 riva.Bitmap->PointE = ((dstY+af_y)<<16) | dstX;

	 while (h--) {
	    RIVA_FIFO_FREE(riva, Bitmap, 1);
	    riva.Bitmap->MonochromeData01E = (image[0]) | 
					     (image[byteWidth] << 8) | 
					     (image[byteWidth*2] << 16) | 
					     (image[byteWidth*3] << 24);
	    image += byteWidth*4;
	 }
      }
      else if (width <= 16) {
	 /* for opaque characters up to 16 pixels wide */
	 h = (height+1)/2;

	 riva.Bitmap->WidthHeightInE = (h<<17) | 16;
	 riva.Bitmap->WidthHeightOutE = (height<<16) | width;
	 riva.Bitmap->PointE = ((dstY+af_y)<<16) | dstX;

	 while (h--) {
	    RIVA_FIFO_FREE(riva, Bitmap, 1);
	    riva.Bitmap->MonochromeData01E = (image[0]) | 
					     (image[byteWidth] << 16);
	    image += byteWidth*2;
	 }
      }
      else {
	 /* for opaque characters more than 16 pixels wide */
	 w = (width+31)/32;

	 riva.Bitmap->WidthHeightInE = (height<<16) | (w<<5);
	 riva.Bitmap->WidthHeightOutE = (height<<16) | width;
	 riva.Bitmap->PointE = ((dstY+af_y)<<16) | dstX;

	 while (height--) {
	    for (i=0; i<w; i++) {
	       RIVA_FIFO_FREE(riva, Bitmap, 1);
	       riva.Bitmap->MonochromeData01E = image[i];
	    }
	    image += byteWidth;
	 }
      }
   }
}



/* BitBlt:
 *  Blits from one part of video memory to another, using the specified
 *  mix operation. This must correctly handle the case where the two
 *  regions overlap.
 */
void BitBlt(AF_DRIVER *af, long left, long top, long width, long height, long dstLeft, long dstTop, long op)
{
   UpdateMix();

   RIVA_FIFO_FREE(riva, Blt, 3);

   riva.Blt->TopLeftSrc  = ((top+af_y)<<16) | left;
   riva.Blt->TopLeftDst  = ((dstTop+af_y)<<16) | dstLeft;
   riva.Blt->WidthHeight = (height<<16) | width;
}


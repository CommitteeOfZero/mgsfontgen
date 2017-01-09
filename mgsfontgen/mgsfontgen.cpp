#pragma comment(lib, "gdiplus.lib")
#include <Windows.h>
#include <gdiplus.h>
#include <cmath>
#include <cstdint>
#include "stdafx.h"

using namespace Gdiplus;

typedef struct {
  wchar_t* characters;
  size_t colOffset;
  size_t row;  // we don't actually really use this yet
  Font* font;
  Font* fontFallback;
} glyph_range_t;

// ===== CONFIGURE THIS ===== //
// also configure

#define TEXTURE_WIDTH 3072
#define CELL_WIDTH 48
#define CELL_HEIGHT 48
#define CELLS_PER_ROW (TEXTURE_WIDTH / CELL_WIDTH)
#define TOTAL_CELL_COUNT 8000
#define FONT_FAMILY L"Droid Sans Fallback"
#define FONT_SIZE 38
#define OUTLINE_MAX 8
#define OUTLINE_MIN 1
#define Y_SAFETY_MARGIN -3
#define OUTLINE_X_SAFETY_MARGIN 4
// The engine build we're looking at stores 1/1.5 of the cell width instead of
// the actual cell width
#define GAME_WIDTH_MULTIPLIER 1.5

// upper bound
uint8_t widths[TOTAL_CELL_COUNT];
uint8_t outlineWidths[TOTAL_CELL_COUNT];

// ===== CONFIGURATION END ===== //

// https://msdn.microsoft.com/en-us/library/ms533843(v=vs.85).aspx
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
  UINT num = 0;   // number of image encoders
  UINT size = 0;  // size of the image encoder array in bytes

  ImageCodecInfo* pImageCodecInfo = NULL;

  GetImageEncodersSize(&num, &size);
  if (size == 0) return -1;  // Failure

  pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
  if (pImageCodecInfo == NULL) return -1;  // Failure

  GetImageEncoders(num, size, pImageCodecInfo);

  for (UINT j = 0; j < num; ++j) {
    if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
      *pClsid = pImageCodecInfo[j].Clsid;
      free(pImageCodecInfo);
      return j;  // Success
    }
  }
  free(pImageCodecInfo);
  return -1;  // Failure
}

void main() {
  GdiplusStartupInput gsi;
  ULONG_PTR gTok;
  GdiplusStartup(&gTok, &gsi, NULL);

  // http://stackoverflow.com/questions/3354136/access-violation-in-image-destructor
  // yeah IDK either
  {
    memset(widths, 0, TOTAL_CELL_COUNT);

    SolidBrush brush(Color(255, 255, 255, 255));
    Matrix identity;
    Bitmap bitmap(
        TEXTURE_WIDTH,
        ceil((double)TOTAL_CELL_COUNT / (double)CELLS_PER_ROW) * CELL_HEIGHT);
    Bitmap outlineBitmap(
        TEXTURE_WIDTH,
        ceil((double)TOTAL_CELL_COUNT / (double)CELLS_PER_ROW) * CELL_HEIGHT);
    Graphics graphics(&bitmap);
    Graphics outlineGraphics(&outlineBitmap);

    // unfortunately we need regular old GDI later
    HDC tempHdc = graphics.GetHDC();
    HDC hdc = CreateCompatibleDC(tempHdc);
    graphics.ReleaseHDC(tempHdc);

    graphics.Clear(Color(0, 0, 0, 0));
    graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    outlineGraphics.Clear(Color(0, 0, 0, 0));
    outlineGraphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    outlineGraphics.SetSmoothingMode(SmoothingModeAntiAlias);
    outlineGraphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);

    FontFamily fontFamily(FONT_FAMILY);
    Font font(&fontFamily, FONT_SIZE, FontStyleRegular, UnitPixel);
    LOGFONTW logFont;
    HFONT hfont;
    font.GetLogFontW(&graphics, &logFont);
    hfont = CreateFontIndirectW(&logFont);
    SelectObject(hdc, hfont);

    FILE* infile = fopen("input.bin", "rb");
    wchar_t overwriteThis[TOTAL_CELL_COUNT];
    fread(overwriteThis, 2, TOTAL_CELL_COUNT, infile);
    fclose(infile);

    wchar_t* override_4_12 = L"¹⁸";
    // wchar_t *override_4_16 = L"‡タ"; - unfortunately this is too wide

    wchar_t* c = overwriteThis;
    int row = 0;
    int col = 0;
    for (size_t i = 0; i < TOTAL_CELL_COUNT; i++) {
      if (*c == L'\0') {
        c++;
        continue;
      }
      int row = i / CELLS_PER_ROW;
      int col = i % CELLS_PER_ROW;

      bool useOverride = false;
      wchar_t* pOverride;

      if (row == 4 && (col == 12)) {
        useOverride = true;
        if (col == 12) pOverride = override_4_12;
        // if (col == 16) pOverride = override_4_16;
      }

      // empirical testing indicates there are some off-by-one errors
      PointF origin(1 + col * CELL_WIDTH, (row + 1) * CELL_HEIGHT);

      ABC abc;
      // A-width is overhang (e.g. -2 for "glyph is drawn 2px left of
      // origin").
      // We can't support this so we need to shift origin right by that
      // amount.
      // Also, we can only get this info from GDI.
      if (useOverride) {
        ABC temp;
        for (int i = 0; i < wcslen(pOverride); i++) {
          uint32_t tc = *(pOverride + i);
          GetCharABCWidthsW(hdc, tc, tc, &temp);
          abc.abcA += temp.abcA;
          abc.abcB += temp.abcB;
          abc.abcC += temp.abcC;
        }
      } else {
        GetCharABCWidthsW(hdc, *(uint32_t*)c, *(uint32_t*)c, &abc);
      }
      origin.Y += Y_SAFETY_MARGIN;

      PointF outlineOrigin(origin);
      outlineOrigin.X += OUTLINE_X_SAFETY_MARGIN;

      {
        GraphicsPath path;
        StringFormat strformat = StringFormat::GenericTypographic();
        strformat.SetLineAlignment(StringAlignmentFar);

        if (useOverride) {
          path.AddString(pOverride, -1, &fontFamily, FontStyleRegular,
                         font.GetSize(), outlineOrigin, &strformat);
        } else {
          path.AddString(c, 1, &fontFamily, FontStyleRegular, font.GetSize(),
                         outlineOrigin, &strformat);
        }

        for (int j = OUTLINE_MIN; j < OUTLINE_MAX; j++) {
          Pen pen(Color(255 / ceil(j / 1.5), 255, 255, 255), j);
          pen.SetLineJoin(LineJoinRound);
          outlineGraphics.DrawPath(&pen, &path);
        }
      }
      GraphicsPath path;
      StringFormat strformat = StringFormat::GenericTypographic();
      strformat.SetLineAlignment(StringAlignmentFar);
      if (useOverride) {
        path.AddString(pOverride, -1, &fontFamily, FontStyleRegular,
                       font.GetSize(), origin, &strformat);
      } else {
        path.AddString(c, 1, &fontFamily, FontStyleRegular, font.GetSize(),
                       origin, &strformat);
      }
      graphics.FillPath(&brush, &path);

      // again, we can't (reliably) get this from GDI+. See docs for what
      // these values are.
      double rawWidth = (abc.abcA > 0 ? abc.abcA : 0) + abc.abcB +
                        (abc.abcC > 0 ? abc.abcC : 0);
      widths[i] = ceil(rawWidth / GAME_WIDTH_MULTIPLIER) + 1;

      // UTF-16 uses 2 *or 4* bytes per codepoint -> we can't just c++
      c = CharNextW(c);
    }

    CLSID myClsId;
    GetEncoderClsid(L"image/png", &myClsId);
    bitmap.Save(L"output.png", &myClsId);
    outlineBitmap.Save(L"outputOutline.png", &myClsId);

    FILE* fp = fopen("widths.bin", "wb");
    fwrite(widths, 1, TOTAL_CELL_COUNT, fp);
    fflush(fp);
    fclose(fp);
  }

  GdiplusShutdown(gTok);
}

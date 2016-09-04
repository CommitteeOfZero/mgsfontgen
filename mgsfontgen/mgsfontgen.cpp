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

#define TEXTURE_WIDTH 3072
#define CELL_WIDTH 48
#define CELL_HEIGHT 48
#define CELLS_PER_ROW (TEXTURE_WIDTH / CELL_WIDTH)
#define USED_ROWS 2
// The engine build we're looking at stores 1/1.5 of the cell width instead of
// the actual cell width
#define GAME_WIDTH_MULTIPLIER 1.5

glyph_range_t glyphRanges[USED_ROWS];
uint8_t widths[USED_ROWS][CELLS_PER_ROW];

void SetRanges() {
  glyphRanges[0] =
    {
        /*   .characters = */ L" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklm"
        L"nopqrstuvwxyz ", // yes, that last space is actually used
        /*    .colOffset = */ 0,
        /*          .row = */ 0,
        /*         .font = */ new Font(new FontFamily(L"Open Sans"), 44,
            FontStyleRegular, UnitPixel),
        /* .fontFallback = */ new Font(new FontFamily(L"Arial Unicode MS"), 44,
            FontStyleRegular, UnitPixel)
    };
  glyphRanges[1] = {
      /*   .characters = */ L"/:-;!?'.@#%~*&`()°^>+<ﾉ･=\"$´,[\\]_{|}",
      /*    .colOffset = */ 0,
      /*          .row = */ 1,
      /*         .font = */ new Font(new FontFamily(L"Open Sans"), 44,
                                     FontStyleRegular, UnitPixel),
      /* .fontFallback = */ new Font(new FontFamily(L"Arial Unicode MS"), 44,
                                     FontStyleRegular, UnitPixel)};
  memset(widths, 0, USED_ROWS * CELLS_PER_ROW);
}

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
    SetRanges();

    SolidBrush brush(Color(255, 255, 255, 255));
    Matrix identity;
    Bitmap bitmap(TEXTURE_WIDTH, CELL_HEIGHT * USED_ROWS);
    Graphics graphics(&bitmap);

    // unfortunately we need regular old GDI later
    HDC tempHdc = graphics.GetHDC();
    HDC hdc = CreateCompatibleDC(tempHdc);
    graphics.ReleaseHDC(tempHdc);

    graphics.Clear(Color(0, 0, 0, 0));
    graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    for (size_t i = 0; i < sizeof(glyphRanges) / sizeof(glyph_range_t); i++) {
      glyph_range_t* range = glyphRanges + i;

      FontFamily fontFamily, fontFamilyFallback;
      LOGFONTW logFont, logFontFallback;
      HFONT hfont, hfontFallback;
      range->font->GetFamily(&fontFamily);
      range->fontFallback->GetFamily(&fontFamilyFallback);
      range->font->GetLogFontW(&graphics, &logFont);
      hfont = CreateFontIndirectW(&logFont);
      range->fontFallback->GetLogFontW(&graphics, &logFontFallback);
      hfontFallback = CreateFontIndirectW(&logFontFallback);

      for (size_t j = 0; j < wcslen(range->characters); j++) {
        size_t col = range->colOffset + j;
        // size_t row = range->row;
        size_t row = i;

        Font* curFont = range->font;
        FontFamily* curFontFamily = &fontFamily;
        SelectObject(hdc, hfont);

        uint16_t glyphIndex;
        GetGlyphIndicesW(hdc, range->characters + j, 1, &glyphIndex,
                         GGI_MARK_NONEXISTING_GLYPHS);
        // 0xFFFF = nonexistant
        if (glyphIndex == 0xFFFF) {
          curFont = range->fontFallback;
          curFontFamily = &fontFamilyFallback;
          SelectObject(hdc, hfontFallback);
        }

        // empirical testing indicates there are some off-by-one errors
        PointF origin(1 + col * CELL_WIDTH, (row + 1) * CELL_HEIGHT);
        // origin.Y is baseline, so we need to shift it up by the font's cell
        // descent
        // (i.e. maximum glyph height *below* baseline) to always fit the
        // bounding box.
        // GetCellDescent()'s return value is in design units, so we need to do
        // this calculation to get pixels
        origin.Y -= curFont->GetSize() *
                    curFontFamily->GetCellDescent(FontStyleRegular) /
                    curFontFamily->GetEmHeight(FontStyleRegular);

        ABC abc;
        // A-width is overhang (e.g. -2 for "glyph is drawn 2px left of
        // origin").
        // We can't support this so we need to shift origin right by that
        // amount.
        // Also, we can only get this info from GDI.
        GetCharABCWidthsW(hdc, range->characters[col], range->characters[col],
                          &abc);
        if (abc.abcA < 0) origin.X -= abc.abcA;

        graphics.DrawDriverString((uint16_t*)(range->characters + col), 1,
                                  curFont, &brush, &origin,
                                  DriverStringOptionsCmapLookup, &identity);

        // again, we can't (reliably) get this from GDI+. See docs for what
        // these values are.
        double rawWidth = (abc.abcA > 0 ? abc.abcA : 0) + abc.abcB +
                          (abc.abcC > 0 ? abc.abcC : 0);

        // empirical testing, see above
        widths[row][col] = ceil(rawWidth / GAME_WIDTH_MULTIPLIER) + 1;
      }
    }

    CLSID myClsId;
    GetEncoderClsid(L"image/png", &myClsId);
    bitmap.Save(L"output.png", &myClsId);

    FILE* fp = fopen("widths.bin", "wb");
    fwrite(widths, 1, USED_ROWS * CELLS_PER_ROW, fp);
    fflush(fp);
    fclose(fp);
  }

  GdiplusShutdown(gTok);
}

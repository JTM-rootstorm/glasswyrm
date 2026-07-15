#pragma once

#include <cstdint>

namespace gw::protocol::x11 {

enum class CoreOpcode : std::uint8_t {
  CreateWindow = 1,
  ChangeWindowAttributes = 2,
  GetWindowAttributes = 3,
  DestroyWindow = 4,
  MapWindow = 8,
  MapSubwindows = 9,
  UnmapWindow = 10,
  UnmapSubwindows = 11,
  ConfigureWindow = 12,
  GetGeometry = 14,
  QueryTree = 15,
  InternAtom = 16,
  GetAtomName = 17,
  ChangeProperty = 18,
  DeleteProperty = 19,
  GetProperty = 20,
  ListProperties = 21,
  SetSelectionOwner = 22,
  GetSelectionOwner = 23,
  ConvertSelection = 24,
  SendEvent = 25,
  QueryPointer = 38,
  TranslateCoordinates = 40,
  AllocColor = 84,
  AllocNamedColor = 85,
  FreeColors = 88,
  QueryColors = 91,
  LookupColor = 92,
  CreateCursor = 93,
  CreateGlyphCursor = 94,
  FreeCursor = 95,
  RecolorCursor = 96,
  QueryBestSize = 97,
  QueryExtension = 98,
  ListExtensions = 99,
  GetKeyboardMapping = 101,
  GetPointerMapping = 117,
  GetModifierMapping = 119,
  GetInputFocus = 43,
  OpenFont = 45,
  CloseFont = 46,
  QueryFont = 47,
  QueryTextExtents = 48,
  ListFonts = 49,
  CreatePixmap = 53,
  FreePixmap = 54,
  CreateGC = 55,
  ChangeGC = 56,
  FreeGC = 60,
  ClearArea = 61,
  CopyArea = 62,
  PolyLine = 65,
  PolySegment = 66,
  FillPoly = 69,
  PolyFillRectangle = 70,
  PolyFillArc = 71,
  PutImage = 72,
  PolyText8 = 74,
  ImageText8 = 76,
  NoOperation = 127,
};

enum class CoreEventType : std::uint8_t {
  KeyPress = 2,
  KeyRelease = 3,
  ButtonPress = 4,
  ButtonRelease = 5,
  MotionNotify = 6,
  EnterNotify = 7,
  LeaveNotify = 8,
  FocusIn = 9,
  FocusOut = 10,
  Expose = 12,
  GraphicsExpose = 13,
  NoExpose = 14,
  DestroyNotify = 17,
  UnmapNotify = 18,
  MapNotify = 19,
  ConfigureNotify = 22,
  PropertyNotify = 28,
  SelectionClear = 29,
  SelectionRequest = 30,
  SelectionNotify = 31,
  ClientMessage = 33,
};

enum class CoreMapState : std::uint8_t {
  Unmapped = 0,
  Unviewable = 1,
  Viewable = 2,
};

enum class CoreStackMode : std::uint8_t {
  Above = 0,
  Below = 1,
  TopIf = 2,
  BottomIf = 3,
  Opposite = 4,
};

enum class CoreErrorCode : std::uint8_t {
  BadRequest = 1,
  BadValue = 2,
  BadWindow = 3,
  BadPixmap = 4,
  BadAtom = 5,
  BadCursor = 6,
  BadFont = 7,
  BadMatch = 8,
  BadDrawable = 9,
  BadAccess = 10,
  BadAlloc = 11,
  BadColor = 12,
  BadColormap = 12,
  BadGContext = 13,
  BadIDChoice = 14,
  BadName = 15,
  BadLength = 16,
  BadImplementation = 17,
};

[[nodiscard]] constexpr std::uint16_t
wire_sequence(const std::uint64_t sequence) noexcept {
  return static_cast<std::uint16_t>(sequence & 0xffffU);
}

} // namespace gw::protocol::x11

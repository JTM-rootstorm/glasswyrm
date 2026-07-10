#include "glasswyrmd/atom_table.hpp"

#include <array>
#include <string_view>

int main() {
  using namespace glasswyrm::server;
  constexpr std::array<std::string_view, kHighestPredefinedAtom + 1> expected{
      "", "PRIMARY", "SECONDARY", "ARC", "ATOM", "BITMAP", "CARDINAL",
      "COLORMAP", "CURSOR", "CUT_BUFFER0", "CUT_BUFFER1", "CUT_BUFFER2",
      "CUT_BUFFER3", "CUT_BUFFER4", "CUT_BUFFER5", "CUT_BUFFER6",
      "CUT_BUFFER7", "DRAWABLE", "FONT", "INTEGER", "PIXMAP", "POINT",
      "RECTANGLE", "RESOURCE_MANAGER", "RGB_COLOR_MAP", "RGB_BEST_MAP",
      "RGB_BLUE_MAP", "RGB_DEFAULT_MAP", "RGB_GRAY_MAP", "RGB_GREEN_MAP",
      "RGB_RED_MAP", "STRING", "VISUALID", "WINDOW", "WM_COMMAND",
      "WM_HINTS", "WM_CLIENT_MACHINE", "WM_ICON_NAME", "WM_ICON_SIZE",
      "WM_NAME", "WM_NORMAL_HINTS", "WM_SIZE_HINTS", "WM_ZOOM_HINTS",
      "MIN_SPACE", "NORM_SPACE", "MAX_SPACE", "END_SPACE", "SUPERSCRIPT_X",
      "SUPERSCRIPT_Y", "SUBSCRIPT_X", "SUBSCRIPT_Y", "UNDERLINE_POSITION",
      "UNDERLINE_THICKNESS", "STRIKEOUT_ASCENT", "STRIKEOUT_DESCENT",
      "ITALIC_ANGLE", "X_HEIGHT", "QUAD_WIDTH", "WEIGHT", "POINT_SIZE",
      "RESOLUTION", "COPYRIGHT", "NOTICE", "FONT_NAME", "FAMILY_NAME",
      "FULL_NAME", "CAP_HEIGHT", "WM_CLASS", "WM_TRANSIENT_FOR"};

  AtomTable atoms;
  if (atoms.valid(0) || !atoms.valid(0, true) || atoms.name(0).has_value() ||
      atoms.size() != kHighestPredefinedAtom) {
    return 1;
  }
  for (std::uint32_t atom = 1; atom <= kHighestPredefinedAtom; ++atom) {
    if (atoms.name(atom) != expected[atom] || atoms.find(expected[atom]) != atom) {
      return 2;
    }
  }
  if (atoms.intern("case-sensitive", true).atom != 0) {
    return 3;
  }
  const auto first = atoms.intern("case-sensitive", false);
  const auto second = atoms.intern("case-sensitive", false);
  const auto different = atoms.intern("CASE-SENSITIVE", false);
  if (first.status != InternAtomStatus::Success || first.atom != 69 ||
      second.atom != first.atom || different.atom != 70 ||
      atoms.name(first.atom) != "case-sensitive") {
    return 4;
  }
  AtomTable limited(69);
  if (limited.intern("last", false).atom != 69 ||
      limited.intern("exhausted", false).status !=
          InternAtomStatus::Exhausted) {
    return 5;
  }
  return 0;
}

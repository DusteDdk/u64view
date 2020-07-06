/*
  keys.h

  Mapping tables from SDL key symbols to C-64 PETSCII codes

  See https://wiki.libsdl.org/SDLKeycodeLookup and Appendix C in
  the Commodore 64 Programmer's Reference Manual.
*/


/* Special codes for actions not mappable to a PETSCII code */
#define CODE_QUIT  -1
#define CODE_RESET -2


/* Key pressed without modifiers */
static int keymap_nomod[] = {
    SDLK_RETURN, 13,
    SDLK_DOWN, 17,
    SDLK_HOME, 19,
    SDLK_BACKSPACE, 20,
    SDLK_RIGHT, 29,
    SDLK_SPACE, 32,
    SDLK_QUOTE, 39,
    SDLK_COMMA, 44,
    SDLK_MINUS, 45,
    SDLK_PERIOD, 46,
    SDLK_SLASH, 47,
    SDLK_SEMICOLON, 59,
    SDLK_EQUALS, 61,
    SDLK_0, 48,
    SDLK_a, 65,
    SDLK_LEFTBRACKET, 91,
    SDLK_RIGHTBRACKET, 93,
    SDLK_BACKQUOTE, 95, // Left arrow
    SDLK_F1, 133,
    SDLK_F2, 137,
    SDLK_F3, 134,
    SDLK_F4, 138,
    SDLK_F5, 135,
    SDLK_F6, 139,
    SDLK_F7, 136,
    SDLK_F8, 140,
    SDLK_UP, 145,
    SDLK_INSERT, 148,
    SDLK_LEFT, 157,
    SDLK_BACKSLASH, 255, // PI symbol,
    SDLK_F12, CODE_RESET,
    SDLK_ESCAPE, CODE_QUIT,
    0 // End marker
};

/* Key pressed with LSHIFT or RSHIFT modifier */
static int keymap_shift[] = {
    SDLK_QUOTE, 34, // "
    SDLK_RETURN, 141,
    SDLK_SPACE, 160,
    SDLK_HOME, 147, // Clear
    SDLK_EQUALS, 43, // +
    SDLK_SEMICOLON, 58, // :
    SDLK_COMMA, 60, // <
    SDLK_PERIOD, 62, // >
    SDLK_SLASH, 63, // ?
    SDLK_MINUS, 92, // £
    SDLK_PAGEUP, 18, // Reverse on
    SDLK_PAGEDOWN, 146, // Reverse off
    SDLK_a, 97,
    SDLK_1, 33, // !
    SDLK_2, 64, // @
    SDLK_3, 35, // #
    SDLK_4, 36, // $
    SDLK_5, 37, // %
    SDLK_6, 94, // ^
    SDLK_7, 38, // &
    SDLK_8, 42, // *
    SDLK_9, 40, // (
    SDLK_0, 41, // )
    0 // End marker
};

/* Key pressed with LCTRL or RCTRL modifier */
static int keymap_ctrl[] = {
    SDLK_c, 3, // Stop
    0 // End marker
};

/* Key pressed with LALT or RALT modifier */
static int keymap_alt[] = {
    SDLK_r, CODE_RESET,
    SDLK_q, CODE_QUIT,
    0 // End marker
};

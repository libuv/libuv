/* Copyright libuv project contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifdef _WIN32

#include "uv.h"
#include "task.h"
#include "../src/win/internal.h"

#include <errno.h>
#include <io.h>
#include <string.h>
#include <windows.h>

#define BUF_SIZE           256
#define CSI                "\033["

#define ENC_EXT            CSI"?1005"
#define ENC_SGR            CSI"?1006"
#define ENC_URXVT          CSI"?1015"
#define MODE_X10           CSI"?9"
#define MODE_NORMAL        CSI"?1000"
#define MODE_BT            CSI"?1002"
#define MODE_ANY           CSI"?1003"

#define SET_ENC_EXT        ENC_EXT"h"
#define SET_ENC_SGR        ENC_SGR"h"
#define SET_ENC_URXVT      ENC_URXVT"h"
#define SET_MODE_X10       MODE_X10"h"
#define SET_MODE_NORMAL    MODE_NORMAL"h"
#define SET_MODE_BT        MODE_BT"h"
#define SET_MODE_ANY       MODE_ANY"h"

#define RESET_ENC_EXT      ENC_EXT"l"
#define RESET_ENC_SGR      ENC_SGR"l"
#define RESET_ENC_URXVT    ENC_URXVT"l"
#define RESET_MODE_X10     MODE_X10"l"
#define RESET_MODE_NORMAL  MODE_NORMAL"l"
#define RESET_MODE_BT      MODE_BT"l"
#define RESET_MODE_ANY     MODE_ANY"l"


typedef enum click {
  PRESS,
  RELEASE,
  BOTH
} click_t;

typedef enum rotate {
  FORWARD,
  BACKWARD
} rotate_t;

typedef enum encording {
  X10,
  EXT,
  URXVT,
  SGR
} encording_t;

static void dump_str(const char* str, ssize_t len) {
  ssize_t i;
  char current_char;
  for (i = 0; i < len; i++) {
    current_char = *(str + i);
    if (current_char > ' ' && current_char <= '~') {
      fprintf(stderr, "%c ", *(str + i));
    } else {
      fprintf(stderr, "%#02x ", (*(str + i)) & 0xff);
    }
  }
}

static void print_err_msg(const char* expect, ssize_t expect_len,
                          const char* found, ssize_t found_len) {
  fprintf(stderr, "expect ");
  dump_str(expect, expect_len);
  fprintf(stderr, ", but found ");
  dump_str(found, found_len);
  fprintf(stderr, "\n");
}

static BOOL assert_same(uv_buf_t *expected, uv_buf_t *actual) {
  if (expected->len != actual->len) {
    fprintf(stderr, "expected nread %ld, but found %ld\n",
        (long)expected->len, (long)actual->len);
    print_err_msg(expected->base, expected->len, actual->base, actual->len);
    return FALSE;
  }
  if (strncmp(expected->base, actual->base, expected->len) != 0) {
    print_err_msg(expected->base, expected->len, actual->base, actual->len);
    return FALSE;
  }
  return TRUE;
}

static void append_expected(uv_buf_t *expected, char cb, short x, short y,
                          char fbyte, encording_t type) {
  int written, utf8_len;
  WCHAR buf[BUF_SIZE];

  switch (type) {
    case X10:
      written = snprintf(expected->base + expected->len,
                         BUF_SIZE - expected->len,
                         "%s%c%c%c%c", CSI, fbyte, cb, (char)x, (char)y);
      ASSERT(written >= 0);
      expected->len += written;
      break;
    case EXT:
      written = _snwprintf(buf, BUF_SIZE - expected->len, L"\x1b[%c%c%c%c",
                         (WCHAR)fbyte, (WCHAR)cb, (WCHAR)x, (WCHAR)y);
      ASSERT(written >= 0);
      utf8_len = WideCharToMultiByte(CP_UTF8, 0, buf, -1,
                                     expected->base + expected->len,
                                     BUF_SIZE - expected->len, NULL, NULL);
      fprintf(stderr, "utf8_len%d\n", utf8_len);
      ASSERT(utf8_len > 0);
      /*
       * It is necessary to subtract one character of the terminating null
       * character.
       */
      expected->len += utf8_len - 1;
      break;
    case URXVT:
      written = snprintf(expected->base + expected->len,
                         BUF_SIZE - expected->len,
                         "%s%d;%d;%d%c", CSI, cb, x, y, fbyte);
      ASSERT(written >= 0);
      expected->len += written;
      break;
    case SGR:
      written = snprintf(expected->base + expected->len,
                         BUF_SIZE - expected->len,
                         "%s<%d;%d;%d%c", CSI, cb, x, y, fbyte);
      ASSERT(written >= 0);
      expected->len += written;
      break;
  }

}

static void tty_alloc(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  buf->base = malloc(size);
  ASSERT(buf->base != NULL);
  buf->len = size;
}

static void tty_read(uv_stream_t* tty_in, ssize_t nread, const uv_buf_t* buf) {
  if (nread > 0) {
    uv_buf_t *actual;

    ASSERT(nread <= BUF_SIZE);
    actual = (uv_buf_t*)uv_handle_get_data((uv_handle_t*)tty_in);
    memcpy(actual->base, buf->base, nread);
    actual->len = nread;
    free(buf->base);
    uv_stop(uv_handle_get_loop((uv_handle_t*)tty_in));
  } else {
    ASSERT(nread == 0);
  }
}

static void make_key_event_records(WORD virt_key, DWORD ctr_key_state,
                                   BOOL is_wsl, INPUT_RECORD* records) {
# define KEV(I) records[(I)].Event.KeyEvent
  BYTE kb_state[256] = {0};
  WCHAR buf[2];
  int ret;

  records[0].EventType = records[1].EventType = KEY_EVENT;
  KEV(0).bKeyDown = TRUE;
  KEV(1).bKeyDown = FALSE;
  KEV(0).wVirtualKeyCode = KEV(1).wVirtualKeyCode = virt_key;
  KEV(0).wRepeatCount = KEV(1).wRepeatCount = 1;
  KEV(0).wVirtualScanCode = KEV(1).wVirtualScanCode =
    MapVirtualKeyW(virt_key, MAPVK_VK_TO_VSC);
  KEV(0).dwControlKeyState = KEV(1).dwControlKeyState = ctr_key_state;
  if (ctr_key_state & LEFT_ALT_PRESSED) {
    kb_state[VK_LMENU] = 0x01;
  }
  if (ctr_key_state & RIGHT_ALT_PRESSED) {
    kb_state[VK_RMENU] = 0x01;
  }
  if (ctr_key_state & LEFT_CTRL_PRESSED) {
    kb_state[VK_LCONTROL] = 0x01;
  }
  if (ctr_key_state & RIGHT_CTRL_PRESSED) {
    kb_state[VK_RCONTROL] = 0x01;
  }
  if (ctr_key_state & SHIFT_PRESSED) {
    kb_state[VK_SHIFT] = 0x01;
  }
  ret = ToUnicode(virt_key, KEV(0).wVirtualScanCode, kb_state, buf, 2, 0);
  if (ret == 1) {
    if(!is_wsl &&
        ((ctr_key_state & LEFT_ALT_PRESSED) ||
         (ctr_key_state & RIGHT_ALT_PRESSED))) {
      /*
       * If ALT key is pressed, the UnicodeChar value of the keyup event is
       * set to 0 on nomal console. Emulate this behavior.
       * See https://github.com/Microsoft/console/issues/320
       */
      KEV(0).uChar.UnicodeChar = buf[0];
      KEV(1).uChar.UnicodeChar = 0;
    } else{
      /*
       * In WSL UnicodeChar is normally set. This behavior cause #2111.
       */
      KEV(0).uChar.UnicodeChar = KEV(1).uChar.UnicodeChar = buf[0];
    }
  } else {
    KEV(0).uChar.UnicodeChar = KEV(1).uChar.UnicodeChar = 0;
  }
# undef KEV
}

static void make_mouse_event_records(uv_tty_t* tty_out, COORD position,
                                     DWORD button_state, DWORD ctr_key_state,
                                     DWORD event_flags, INPUT_RECORD* record) {
#define MEV record->Event.MouseEvent
  CONSOLE_SCREEN_BUFFER_INFO info;
  record->EventType = MOUSE_EVENT;
  ASSERT(GetConsoleScreenBufferInfo(tty_out->handle, &info));
  record->Event.MouseEvent.dwMousePosition.X = position.X - 1;
  record->Event.MouseEvent.dwMousePosition.Y = position.Y + info.srWindow.Top - 1;
  record->Event.MouseEvent.dwButtonState = button_state;
  record->Event.MouseEvent.dwControlKeyState = ctr_key_state;
  record->Event.MouseEvent.dwEventFlags = event_flags;
#undef MEV
}

static void write_console(uv_tty_t *tty_out, char* src) {
  int r;
  uv_buf_t buf;

  buf.base = src;
  buf.len = strlen(buf.base);

  r = uv_try_write((uv_stream_t*) tty_out, &buf, 1);
  ASSERT(r >= 0);
  ASSERT((unsigned int) r == buf.len);
}

static void write_console_mouse_click(uv_tty_t* tty_out, uv_tty_t* tty_in,
                                      COORD pos, DWORD button_state,
                                      DWORD ctr_key_state, DWORD event_flags,
                                      click_t click) {
  DWORD written;
  INPUT_RECORD records[2];

  make_mouse_event_records(tty_out, pos, button_state, ctr_key_state,
                           event_flags, &records[0]);
  make_mouse_event_records(tty_out, pos, 0, ctr_key_state,
                           event_flags, &records[1]);
  if (click == PRESS || click == BOTH) {
    WriteConsoleInputW(tty_in->handle, &records[0], 1, &written);
    ASSERT(written == 1);
  }
  if (click == RELEASE || click == BOTH) {
    WriteConsoleInputW(tty_in->handle, &records[1], 1, &written);
    ASSERT(written == 1);
  }
}

static void write_console_mouse_wheel(uv_tty_t* tty_out, uv_tty_t* tty_in,
                                      COORD pos, rotate_t rotate) {
  DWORD written;
  INPUT_RECORD record;

  /* The value of dwButtonState uses the value confirmed by the debugger. */
  if (rotate == FORWARD) {
    /* 0000 0000 0111 1000 0000 0000 0000 0000(7864320) */
    make_mouse_event_records(tty_out, pos, 7864320, 0,
                             MOUSE_WHEELED, &record);
  } else {
    /* 1111 1111 1000 1000 0000 0000 0000 0000(4287102976) */
    make_mouse_event_records(tty_out, pos, 4287102976u, 0,
                             MOUSE_WHEELED, &record);

  }
  WriteConsoleInputW(tty_in->handle, &record, 1, &written);
  ASSERT(written == 1);
}

static int get_fd(const char *name) {
  int fd;
  HANDLE handle;

  handle = CreateFileA(name,
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
  ASSERT(handle != INVALID_HANDLE_VALUE);
  fd = _open_osfhandle((intptr_t) handle, 0);
  ASSERT(fd >= 0);
  ASSERT(UV_TTY == uv_guess_handle(fd));

  return fd;
}

static void initialize_tty(uv_tty_t *tty_in, uv_tty_t *tty_out) {
  int r, fd;

  /*
   * FIXME Testing may fail due to key input and mouse input from the actual
   * console. If possible, it is necessary to block input from the actual
   * console.
   */
  fd = get_fd("conin$");
  r = uv_tty_init(uv_default_loop(), tty_in, fd, 1);  /* Readable .*/
  ASSERT(r == 0);
  ASSERT(uv_is_readable((uv_stream_t*) tty_in));
  ASSERT(!uv_is_writable((uv_stream_t*) tty_in));

  /* Turn on raw mode. */
  r = uv_tty_set_mode(tty_in, UV_TTY_MODE_RAW);
  ASSERT(r == 0);

  r = uv_read_start((uv_stream_t*)tty_in, tty_alloc, tty_read);
  ASSERT(r == 0);

  fd = get_fd("conout$");
  r = uv_tty_init(uv_default_loop(), tty_out, fd, 1);  /* Writeable .*/
  ASSERT(r == 0);
  ASSERT(!uv_is_readable((uv_stream_t*) tty_out));
  ASSERT(uv_is_writable((uv_stream_t*) tty_out));

  uv_tty_set_vterm_state(UV_TTY_UNSUPPORTED);
}

static void initialize_buf(uv_buf_t **buf) {
  *buf = (uv_buf_t*)malloc(sizeof(uv_buf_t));
  ASSERT(*buf);
  (*buf)->len = 0;
  (*buf)->base = (char*)malloc(BUF_SIZE);
  ASSERT((*buf)->base);
}

static void finalize_buf(uv_buf_t *buf) {
  free(buf->base);
  free(buf);
}

TEST_IMPL(tty_mouse_tracking_button) {
  uv_tty_t tty_in, tty_out;
  uv_loop_t* loop;
  uv_buf_t  *expected, *actual;
  COORD start = {1, 1}, end = {2, 2};
  DWORD number_of_buttons;
  char button;

  loop = uv_default_loop();

  if (!GetNumberOfConsoleMouseButtons(&number_of_buttons)) {
    number_of_buttons = 2;
  }

  initialize_tty(&tty_in, &tty_out);
  initialize_buf(&expected);
  initialize_buf(&actual);

  uv_handle_set_data((uv_handle_t*)&tty_in, (void*)actual);

  /*
   * The button number(Cb) is as follows. Test if this is correct.
   * On button press, sends CSI M CbCxCy(Cb: MB1=0, MB2=1, MB3=2).
   * On button release, sends CSI M CbCxCy(Cb = 3).
   * Mouse wheel event sends CSI M CbCxCy(Cb forward 64, backward 65).
   */
  write_console(&tty_out, SET_MODE_ANY);

  /* The 1st left button(MB=0). */
  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 0 + 32 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, PRESS);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, MOUSE_MOVED,
                            PRESS);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, RELEASE);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* The 2nd left button(MB1=1). */
  expected->len = 0;
  append_expected(expected, 1 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 1 + 32 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_2ND_BUTTON_PRESSED, 0, 0, PRESS);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_2ND_BUTTON_PRESSED, 0, MOUSE_MOVED,
                            PRESS);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_2ND_BUTTON_PRESSED, 0, 0, RELEASE);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* The most right button(MB2=2 or MB1=1). */
  button = number_of_buttons > 2 ? 2 : 1;
  expected->len = 0;
  append_expected(expected, button + ' ', start.X + ' ', start.Y + ' ',
                  'M', X10);
  append_expected(expected, button + 32 + ' ', end.X + ' ', end.Y + ' ',
                  'M', X10);
  append_expected(expected, 3 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            RIGHTMOST_BUTTON_PRESSED, 0, 0, PRESS);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            RIGHTMOST_BUTTON_PRESSED, 0, MOUSE_MOVED,
                            PRESS);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            RIGHTMOST_BUTTON_PRESSED, 0, 0, RELEASE);
  /* The 3rd and 4th left button do not send anything. */
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_3RD_BUTTON_PRESSED, 0, 0, PRESS);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_3RD_BUTTON_PRESSED, 0, MOUSE_MOVED,
                            PRESS);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_3RD_BUTTON_PRESSED, 0, 0, RELEASE);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_4TH_BUTTON_PRESSED, 0, 0, PRESS);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_4TH_BUTTON_PRESSED, 0, MOUSE_MOVED,
                            PRESS);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_4TH_BUTTON_PRESSED, 0, 0, RELEASE);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* Move mouse not pressing button. */
  expected->len = 0;
  append_expected(expected, 3 + 32 + ' ', start.X + ' ', start.Y + ' ',
                  'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start, 0, 0, MOUSE_MOVED,
                            RELEASE);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /*
   * For the mouse wheel, the forward button number(Cb) is 64 and the
   * backward button number(Cb) is 65.
   */
  expected->len = 0;
  append_expected(expected, 64 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 65 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  write_console_mouse_wheel(&tty_out, &tty_in, start, FORWARD);
  write_console_mouse_wheel(&tty_out, &tty_in, start, BACKWARD);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* Test encoding with modifier key. */
  button = 4;  /* 4 = Shift */
  expected->len = 0;
  append_expected(expected, 0 + button + ' ', start.X + ' ', start.Y + ' ',
                  'M', X10);
  append_expected(expected, 3 + button + ' ', start.X + ' ', start.Y + ' ',
                  'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, SHIFT_PRESSED, 0,
                            BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  button = 8;  /* 8 = Meta */
  expected->len = 0;
  append_expected(expected, 0 + button + ' ', start.X + ' ', start.Y + ' ',
                  'M', X10);
  append_expected(expected, 3 + button + ' ', start.X + ' ', start.Y + ' ',
                  'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, LEFT_ALT_PRESSED, 0,
                            BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  button = 16;  /* 16 = Control */
  expected->len = 0;
  append_expected(expected, 0 + button + ' ', start.X + ' ', start.Y + ' ',
                  'M', X10);
  append_expected(expected, 3 + button + ' ', start.X + ' ', start.Y + ' ',
                  'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, RIGHT_CTRL_PRESSED, 0,
                            BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  button = 4 + 8 + 16;
  expected->len = 0;
  append_expected(expected, 0 + button + ' ', start.X + ' ', start.Y + ' ',
                  'M', X10);
  append_expected(expected, 3 + button + ' ', start.X + ' ', start.Y + ' ',
                  'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED,
                            (SHIFT_PRESSED |
                             LEFT_ALT_PRESSED |
                             LEFT_CTRL_PRESSED),
                            0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* Test whether button number when button release is correctly generated. */
  write_console(&tty_out, SET_ENC_SGR);
  expected->len = 0;
  append_expected(expected, 0, start.X, start.Y, 'M', SGR);
  append_expected(expected, 1, start.X, start.Y, 'M', SGR);
  append_expected(expected, number_of_buttons > 2 ? 2 : 1,
                  start.X, start.Y, 'M', SGR);
  append_expected(expected, 1, start.X, start.Y, 'm', SGR);
  append_expected(expected, number_of_buttons > 2 ? 2 : 1,
                  start.X, start.Y, 'm', SGR);
  append_expected(expected, 0, start.X, start.Y, 'm', SGR);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0,
                            PRESS);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED |
                            FROM_LEFT_2ND_BUTTON_PRESSED, 0, 0, PRESS);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED |
                            FROM_LEFT_2ND_BUTTON_PRESSED |
                            RIGHTMOST_BUTTON_PRESSED, 0, 0, PRESS);
  /* Release 2nd button from the left. */
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED |
                            RIGHTMOST_BUTTON_PRESSED, 0, 0, PRESS);
  /* Release rightmost button. */
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, PRESS);
  write_console_mouse_click(&tty_out, &tty_in, start, 0, 0, 0, RELEASE);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));


  finalize_buf(expected);
  finalize_buf(actual);
  MAKE_VALGRIND_HAPPY();
  return 0;
}

TEST_IMPL(tty_mouse_tracking_mode_x10) {
  uv_tty_t tty_in, tty_out;
  uv_loop_t* loop;
  uv_buf_t  *expected, *actual;
  INPUT_RECORD key_records[2];
  COORD start = {1, 1}, end = {4, 4}, current_pos;
  DWORD written;
  int resolution;

  loop = uv_default_loop();

  initialize_tty(&tty_in, &tty_out);
  initialize_buf(&expected);
  initialize_buf(&actual);

  uv_handle_set_data((uv_handle_t*)&tty_in, (void*)actual);

  /*
   * X10 compatbility mode.
   * On button press, sends CSI M CbCxCy.
   * On button release, sends none.
   * Mouse wheel event will not send anything.
   * Mouse move event will not send anything.
   */
  write_console(&tty_out, SET_MODE_X10);

  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 0 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  write_console_mouse_wheel(&tty_out, &tty_in, end, FORWARD);
  write_console_mouse_wheel(&tty_out, &tty_in, end, BACKWARD);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /*
   * Mouse movement test. When writing a sequence, only some events
   * occurred, so uv_run is called for each event.
   */
  expected->len = 0;
  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  /* start drag. */
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, PRESS);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));
  for (current_pos.X = start.X + 1, current_pos.Y = start.Y + 1;
       current_pos.X <= end.X; current_pos.X++, current_pos.Y++) {
    resolution = 3;
    actual->len = 0;
    /* move to x, y. */
    while (resolution) {
      write_console_mouse_click(&tty_out, &tty_in, current_pos,
                                FROM_LEFT_1ST_BUTTON_PRESSED, 0,
                                MOUSE_MOVED, PRESS);
      uv_run(loop, UV_RUN_ONCE);
      ASSERT(actual->len == 0);
      resolution--;
    }
  }
  expected->len = 0;
  append_expected(expected, 0 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  /* end drag. */
  write_console_mouse_click(&tty_out, &tty_in, end, 0, 0, 0, RELEASE);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* Move the mouse with the button not pressed. */
  for (current_pos = start; current_pos.X <= end.X;
       current_pos.X++, current_pos.Y++) {
    resolution = 3;
    actual->len = 0;
    /* move to x, y. */
    while (resolution) {
      write_console_mouse_click(&tty_out, &tty_in, current_pos, 0, 0,
                                MOUSE_MOVED, RELEASE);
      uv_run(loop, UV_RUN_ONCE);
      ASSERT(actual->len == 0);
      resolution--;
    }
  }
  expected->len = 0;
  append_expected(expected, 0 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* No key modifiers events occur in x10 compatibility mode. */
  expected->len = 0;
  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 0 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED,
                            LEFT_ALT_PRESSED, 0, BOTH);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED,
                            RIGHT_ALT_PRESSED, 0, BOTH);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED,
                            LEFT_CTRL_PRESSED, 0, BOTH);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED,
                            RIGHT_CTRL_PRESSED, 0, BOTH);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED,
                            SHIFT_PRESSED, 0, BOTH);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /*
   * Since X10 compatibility mode and extended coordinates are exclusive,
   * no events will occur if the extended coordinates are valid.
   */
  snprintf(expected->base, BUF_SIZE, "a");
  expected->len = strlen(expected->base);
  make_key_event_records('A', 0, FALSE, key_records);

  write_console(&tty_out, SET_ENC_EXT);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  WriteConsoleInputW(tty_in.handle, key_records, ARRAY_SIZE(key_records),
                     &written);
  ASSERT(written == ARRAY_SIZE(key_records));
  uv_run(loop, UV_RUN_DEFAULT);

  write_console(&tty_out, SET_ENC_SGR);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  WriteConsoleInputW(tty_in.handle, key_records, ARRAY_SIZE(key_records),
                     &written);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  write_console(&tty_out, SET_ENC_URXVT);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  WriteConsoleInputW(tty_in.handle, key_records, ARRAY_SIZE(key_records),
                     &written);
  uv_run(loop, UV_RUN_DEFAULT);
  write_console(&tty_out, RESET_ENC_URXVT);

  /* Test mode reset. */
  expected->len = 0;
  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  /* In modes other than the correct mode, reset is not performed. */
  write_console(&tty_out, RESET_MODE_NORMAL RESET_MODE_BT RESET_MODE_ANY);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);

  snprintf(expected->base, BUF_SIZE, "a");
  expected->len = strlen(expected->base);
  write_console(&tty_out, RESET_MODE_X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  make_key_event_records('A', 0, FALSE, key_records);
  WriteConsoleInputW(tty_in.handle, key_records, ARRAY_SIZE(key_records),
                     &written);
  ASSERT(written == ARRAY_SIZE(key_records));
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));


  finalize_buf(expected);
  finalize_buf(actual);
  MAKE_VALGRIND_HAPPY();
  return 0;
}

TEST_IMPL(tty_mouse_tracking_mode_normal) {
  uv_tty_t tty_in, tty_out;
  uv_loop_t* loop;
  uv_buf_t* expected;
  uv_buf_t* actual;
  COORD start = {1, 1}, end = {4, 4}, current_pos;
  INPUT_RECORD key_records[2];
  DWORD written;
  int resolution;

  loop = uv_default_loop();

  initialize_tty(&tty_in, &tty_out);
  initialize_buf(&expected);
  initialize_buf(&actual);

  uv_handle_set_data((uv_handle_t*)&tty_in, (void*)actual);

  /*
   * Normal tracking mode.
   * On button press, sends CSI M CbCxCy.
   * On button release, sends CSI M CbCxCy(Cb = 3).
   * Mouse wheel event sends CSI M CbCxCy(Cb forward 64, backward 65).
   * Mouse move event will not send anything.
   */
  write_console(&tty_out, SET_MODE_NORMAL);

  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 64 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  append_expected(expected, 65 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  write_console_mouse_wheel(&tty_out, &tty_in, end, FORWARD);
  write_console_mouse_wheel(&tty_out, &tty_in, end, BACKWARD);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /*
   * Mouse movement test. When writing a sequence, only some events
   * occurred, so uv_run is called for each event.
   */
  /* start drag. */
  expected->len = 0;
  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, PRESS);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));
  for (current_pos.X = start.X + 1, current_pos.Y = start.Y + 1;
       current_pos.X <= end.X; current_pos.X++, current_pos.Y++) {
    resolution = 3;
    actual->len = 0;
    /* move to x, y. */
    while (resolution) {
      write_console_mouse_click(&tty_out, &tty_in, current_pos,
                                FROM_LEFT_1ST_BUTTON_PRESSED, 0, MOUSE_MOVED,
                                PRESS);
      uv_run(loop, UV_RUN_ONCE);
      ASSERT(actual->len == 0);
      resolution--;
    }
  }
  expected->len = 0;
  append_expected(expected, 3 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  /* end drag. */
  write_console_mouse_click(&tty_out, &tty_in, end, 0, 0, 0, RELEASE);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* Move the mouse with the button not pressed. */
  for (current_pos = start; current_pos.X <= end.X;
       current_pos.X++, current_pos.Y++) {
    resolution = 3;
    actual->len = 0;
    /* move to x, y. */
    while (resolution) {
      write_console_mouse_click(&tty_out, &tty_in, current_pos, 0, 0,
                                MOUSE_MOVED, RELEASE);
      uv_run(loop, UV_RUN_ONCE);
      ASSERT(actual->len == 0);
      resolution--;
    }
  }
  expected->len = 0;
  append_expected(expected, 0 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* Test mode reset. */
  expected->len = 0;
  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  /* In modes other than the correct mode, reset is not performed. */
  write_console(&tty_out, RESET_MODE_X10 RESET_MODE_BT RESET_MODE_ANY);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  snprintf(expected->base, BUF_SIZE, "a");
  expected->len = strlen(expected->base);
  write_console(&tty_out, RESET_MODE_NORMAL);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  make_key_event_records('A', 0, FALSE, key_records);
  WriteConsoleInputW(tty_in.handle, key_records, ARRAY_SIZE(key_records),
                     &written);
  ASSERT(written == ARRAY_SIZE(key_records));
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));


  finalize_buf(expected);
  finalize_buf(actual);
  MAKE_VALGRIND_HAPPY();
  return 0;
}

TEST_IMPL(tty_mouse_tracking_mode_button) {
  uv_tty_t tty_in, tty_out;
  uv_loop_t* loop;
  uv_buf_t* expected;
  uv_buf_t* actual;
  INPUT_RECORD key_records[2];
  COORD start = {1, 1}, end = {4, 4}, current_pos;
  DWORD written;
  int resolution;

  loop = uv_default_loop();

  initialize_tty(&tty_in, &tty_out);
  initialize_buf(&expected);
  initialize_buf(&actual);

  uv_handle_set_data((uv_handle_t*)&tty_in, (void*)actual);

  /*
   * Button-event tracking mode.
   * On button press, sends CSI M CbCxCy.
   * On button release, sends CSI M CbCxCy(Cb = 3).
   * Mouse wheel event sends CSI M CbCxCy(Cb forward 64, backward 65).
   * Mouse move event send CSI M CbCxCy(32 is added to Cb).
   */
  write_console(&tty_out, SET_MODE_BT);

  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 64 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  append_expected(expected, 65 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  append_expected(expected, 0 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  write_console_mouse_wheel(&tty_out, &tty_in, end, FORWARD);
  write_console_mouse_wheel(&tty_out, &tty_in, end, BACKWARD);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /*
   * Mouse movement test. When writing a sequence, only some events
   * occurred, so uv_run is called for each event.
   */
  /* start drag. */
  expected->len = 0;
  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, PRESS);
  uv_run(loop, UV_RUN_DEFAULT);
  for (current_pos.X = start.X + 1, current_pos.Y = start.Y + 1;
       current_pos.X <= end.X; current_pos.X++, current_pos.Y++) {
    resolution = 3;
    /* move to x, y. */
    expected->len = 0;
    append_expected(expected, 32 + ' ', current_pos.X + ' ',
                    current_pos.Y + ' ', 'M', X10);
    write_console_mouse_click(&tty_out, &tty_in, current_pos,
                              FROM_LEFT_1ST_BUTTON_PRESSED, 0, MOUSE_MOVED,
                              PRESS);
    uv_run(loop, UV_RUN_DEFAULT);
    ASSERT(assert_same(expected, actual));
    resolution--;
    actual->len = 0;
    while (resolution) {
      /* In the same cell, no event occurs multiple times. */
      write_console_mouse_click(&tty_out, &tty_in, current_pos,
                                FROM_LEFT_1ST_BUTTON_PRESSED, 0, MOUSE_MOVED,
                                PRESS);
      uv_run(loop, UV_RUN_ONCE);
      ASSERT(actual->len == 0);
      resolution--;
    }
  }
  expected->len = 0;
  append_expected(expected, 3 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  /* end drag. */
  write_console_mouse_click(&tty_out, &tty_in, end, 0, 0, 0, RELEASE);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* Move the mouse with the button not pressed. */
  for (current_pos = start; current_pos.X <= end.X;
       current_pos.X++, current_pos.Y++) {
    resolution = 3;
    actual->len = 0;
    /* move to x, y. */
    while (resolution) {
      write_console_mouse_click(&tty_out, &tty_in, current_pos, 0, 0,
                                MOUSE_MOVED, RELEASE);
      uv_run(loop, UV_RUN_ONCE);
      ASSERT(actual->len == 0);
      resolution--;
    }
  }
  expected->len = 0;
  append_expected(expected, 0 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* Test mode reset. */
  expected->len = 0;
  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  /* In modes other than the correct mode, reset is not performed. */
  write_console(&tty_out, RESET_MODE_X10 RESET_MODE_NORMAL RESET_MODE_ANY);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  snprintf(expected->base, BUF_SIZE, "a");
  expected->len = strlen(expected->base);
  write_console(&tty_out, RESET_MODE_BT);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  make_key_event_records('A', 0, FALSE, key_records);
  WriteConsoleInputW(tty_in.handle, key_records, ARRAY_SIZE(key_records),
                     &written);
  ASSERT(written == ARRAY_SIZE(key_records));
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));


  finalize_buf(expected);
  finalize_buf(actual);
  MAKE_VALGRIND_HAPPY();
  return 0;
}

TEST_IMPL(tty_mouse_tracking_mode_any) {
  uv_tty_t tty_in, tty_out;
  uv_loop_t* loop;
  uv_buf_t *expected, *actual;
  INPUT_RECORD key_records[2];
  COORD start = {1, 1}, end = {4, 4}, current_pos;
  DWORD written;
  int resolution;

  loop = uv_default_loop();

  initialize_tty(&tty_in, &tty_out);
  initialize_buf(&expected);
  initialize_buf(&actual);

  uv_handle_set_data((uv_handle_t*)&tty_in, (void*)actual);

  /*
   * Button-event tracking mode.
   * On button press, sends CSI M CbCxCy.
   * On button release, sends CSI M CbCxCy(Cb = 3).
   * Mouse wheel event sends CSI M CbCxCy(Cb forward 64, backward 65).
   * Mouse move event send CSI M CbCxCy(32 is added to Cb).
   */
  write_console(&tty_out, SET_MODE_ANY);

  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 64 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  append_expected(expected, 65 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  append_expected(expected, 0 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  write_console_mouse_wheel(&tty_out, &tty_in, end, FORWARD);
  write_console_mouse_wheel(&tty_out, &tty_in, end, BACKWARD);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /*
   * Mouse movement test. When writing a sequence, only some events
   * occurred, so uv_run is called for each event.
   */
  /* start drag. */
  expected->len = 0;
  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, PRESS);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));
  for (current_pos.X = start.X + 1, current_pos.Y = start.Y + 1;
      current_pos.X <= end.X; current_pos.X++, current_pos.Y++) {
    resolution = 3;
    /* move to x, y. */
    expected->len = 0;
    append_expected(expected, 32 + ' ', current_pos.X + ' ',
                    current_pos.Y + ' ', 'M', X10);
    write_console_mouse_click(&tty_out, &tty_in, current_pos,
                              FROM_LEFT_1ST_BUTTON_PRESSED, 0, MOUSE_MOVED,
                              PRESS);
    uv_run(loop, UV_RUN_DEFAULT);
    ASSERT(assert_same(expected, actual));
    resolution--;
    actual->len = 0;
    while (resolution) {
      /* In the same cell, no event occurs multiple times. */
      write_console_mouse_click(&tty_out, &tty_in, current_pos,
                                FROM_LEFT_1ST_BUTTON_PRESSED, 0, MOUSE_MOVED,
                                PRESS);
      uv_run(loop, UV_RUN_ONCE);
      ASSERT(actual->len == 0);
      resolution--;
    }
  }
  expected->len = 0;
  append_expected(expected, 3 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  /* end drag. */
  write_console_mouse_click(&tty_out, &tty_in, end, 0, 0, 0, RELEASE);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* Move the mouse with the button not pressed. */
  for (current_pos = start; current_pos.X <= end.X;
       current_pos.X++, current_pos.Y++) {
    resolution = 3;
    /* move to x, y. */
    expected->len = 0;
    append_expected(expected, 35 + ' ', current_pos.X + ' ',
                    current_pos.Y + ' ', 'M', X10);
    write_console_mouse_click(&tty_out, &tty_in, current_pos, 0, 0,
                              MOUSE_MOVED, RELEASE);
    uv_run(loop, UV_RUN_DEFAULT);
    ASSERT(assert_same(expected, actual));
    resolution--;
    actual->len = 0;
    while (resolution) {
      /* In the same cell, no event occurs multiple times. */
      write_console_mouse_click(&tty_out, &tty_in, current_pos, 0, 0,
                                MOUSE_MOVED, RELEASE);
      uv_run(loop, UV_RUN_ONCE);
      ASSERT(actual->len == 0);
      resolution--;
    }
  }
  expected->len = 0;
  append_expected(expected, 0 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', end.X + ' ', end.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, end,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* Test mode reset. */
  expected->len = 0;
  append_expected(expected, 0 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', start.X + ' ', start.Y + ' ', 'M', X10);
  /* In modes other than the correct mode, reset is not performed. */
  write_console(&tty_out, RESET_MODE_X10 RESET_MODE_NORMAL RESET_MODE_BT);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  snprintf(expected->base, BUF_SIZE, "a");
  expected->len = strlen(expected->base);
  write_console(&tty_out, RESET_MODE_ANY);
  write_console_mouse_click(&tty_out, &tty_in, start,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  make_key_event_records('A', 0, FALSE, key_records);
  WriteConsoleInputW(tty_in.handle, key_records, ARRAY_SIZE(key_records),
                     &written);
  ASSERT(written == ARRAY_SIZE(key_records));
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));


  finalize_buf(expected);
  finalize_buf(actual);
  MAKE_VALGRIND_HAPPY();
  return 0;
}

TEST_IMPL(tty_mouse_tracking_enc_x10) {
  uv_tty_t tty_in, tty_out;
  uv_loop_t* loop;
  uv_buf_t *expected, *actual;
  COORD pos = {1, 1};

  loop = uv_default_loop();

  initialize_tty(&tty_in, &tty_out);
  initialize_buf(&expected);
  initialize_buf(&actual);

  uv_handle_set_data((uv_handle_t*)&tty_in, (void*)actual);

  /* Normal coordinates. */
  write_console(&tty_out, SET_MODE_X10);

  append_expected(expected, 0 + ' ', pos.X + ' ', pos.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  pos.X = 223;
  pos.Y = 223;
  expected->len = 0;
  append_expected(expected, 0 + ' ', pos.X + ' ', pos.Y + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /*
   * When the coordinates are 224 or more, overflow occurs and the
   * coordinates become 0.
   * */
  pos.X = 224;
  pos.Y = 224;
  expected->len = 0;
  append_expected(expected, 0 + ' ', 0 + ' ', 0 + ' ', 'M', X10);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));


  finalize_buf(expected);
  finalize_buf(actual);
  MAKE_VALGRIND_HAPPY();
  return 0;
}

TEST_IMPL(tty_mouse_tracking_enc_ext) {
  uv_tty_t tty_in, tty_out;
  uv_loop_t* loop;
  uv_buf_t *expected, *actual;
  COORD pos = {1, 1};

  loop = uv_default_loop();

  initialize_tty(&tty_in, &tty_out);
  initialize_buf(&expected);
  initialize_buf(&actual);

  uv_handle_set_data((uv_handle_t*)&tty_in, (void*)actual);

  /* UTF-8 extended coordinates. */
  write_console(&tty_out, SET_MODE_NORMAL SET_ENC_EXT);

  append_expected(expected, 0 + ' ', pos.X + ' ', pos.Y + ' ', 'M', EXT);
  append_expected(expected, 3 + ' ', pos.X + ' ', pos.Y + ' ', 'M', EXT);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  pos.X = 95;
  pos.Y = 95;
  expected->len = 0;
  append_expected(expected, 0 + ' ', pos.X + ' ', pos.Y + ' ', 'M', EXT);
  append_expected(expected, 3 + ' ', pos.X + ' ', pos.Y + ' ', 'M', EXT);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* If it is 96 or more, it is encoded in UTF-8. */
  pos.X = 96;
  pos.Y = 96;
  /* Encoding 128(96 + 32)(0x80) with UTF-8 will result in 0xC2 0x80. */
  expected->len = 0;
  append_expected(expected, 0 + ' ', pos.X + ' ', pos.Y + ' ', 'M', EXT);
  append_expected(expected, 3 + ' ', pos.X + ' ', pos.Y + ' ', 'M', EXT);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  pos.X = 2015;
  pos.Y = 2015;
  /*
   * Encoding 2047(2015 + 32)(0x7FF) with UTF-8 will result in 0xDF 0xBF.
   */
  expected->len = 0;
  append_expected(expected, 0 + ' ', pos.X + ' ', pos.Y + ' ', 'M', EXT);
  append_expected(expected, 3 + ' ', pos.X + ' ', pos.Y + ' ', 'M', EXT);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /*
   * When the coordinates are 2016 or more, overflow occurs and the coordinates
   * become 0.
   * */
  pos.X = 2016;
  pos.Y = 2016;
  expected->len = 0;
  append_expected(expected, 0 + ' ', 0 + ' ', 0 + ' ', 'M', EXT);
  append_expected(expected, 3 + ' ', 0 + ' ', 0 + ' ', 'M', EXT);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /*
   * Test extended coordinates reset. If 95 or less, there is no difference
   * between UTF-8 and ordinary encoding, so use 96.
   */
  pos.X = 96;
  pos.Y = 96;
  expected->len = 0;
  append_expected(expected, 0 + ' ', pos.X + ' ', pos.Y + ' ', 'M', EXT);
  append_expected(expected, 3 + ' ', pos.X + ' ', pos.Y + ' ', 'M', EXT);
  /*
   * In encoding other than the correct encoding, reset is not performed.
   */
  write_console(&tty_out, RESET_ENC_SGR RESET_ENC_URXVT);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  expected->len = 0;
  append_expected(expected, 0 + ' ', pos.X + ' ', pos.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', pos.X + ' ', pos.Y + ' ', 'M', X10);
  write_console(&tty_out, RESET_ENC_EXT);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));


  finalize_buf(expected);
  finalize_buf(actual);
  MAKE_VALGRIND_HAPPY();
  return 0;
}

TEST_IMPL(tty_mouse_tracking_enc_urxvt) {
  uv_tty_t tty_in, tty_out;
  uv_loop_t* loop;
  uv_buf_t *expected, *actual;
  COORD pos = {1, 1};

  loop = uv_default_loop();

  initialize_tty(&tty_in, &tty_out);
  initialize_buf(&expected);
  initialize_buf(&actual);

  uv_handle_set_data((uv_handle_t*)&tty_in, (void*)actual);

  /* URXVT extended coordinates. */
  write_console(&tty_out, SET_MODE_NORMAL SET_ENC_URXVT);

  append_expected(expected, 0 + ' ', pos.X, pos.Y, 'M', URXVT);
  append_expected(expected, 3 + ' ', pos.X, pos.Y, 'M', URXVT);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  pos.X = SHRT_MAX;
  pos.Y = SHRT_MAX;
  expected->len =  0;
  append_expected(expected, 0 + ' ', pos.X, pos.Y, 'M', URXVT);
  append_expected(expected, 3 + ' ', pos.X, pos.Y, 'M', URXVT);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* Test extended coordinates reset. */
  expected->len =  0;
  append_expected(expected, 0 + ' ', pos.X, pos.Y, 'M', URXVT);
  append_expected(expected, 3 + ' ', pos.X, pos.Y, 'M', URXVT);
  /*
   * In encoding other than the correct encoding, reset is not performed.
   */
  write_console(&tty_out, RESET_ENC_EXT RESET_ENC_SGR);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  expected->len = 0;
  append_expected(expected, 0 + ' ', pos.X + ' ', pos.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', pos.X + ' ', pos.Y + ' ', 'M', X10);
  write_console(&tty_out, RESET_ENC_URXVT);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));


  finalize_buf(expected);
  finalize_buf(actual);
  MAKE_VALGRIND_HAPPY();
  return 0;
}

TEST_IMPL(tty_mouse_tracking_enc_sgr) {
  uv_tty_t tty_in, tty_out;
  uv_loop_t* loop;
  uv_buf_t *expected, *actual;
  COORD pos = {1, 1};

  loop = uv_default_loop();

  initialize_tty(&tty_in, &tty_out);
  initialize_buf(&expected);
  initialize_buf(&actual);

  uv_handle_set_data((uv_handle_t*)&tty_in, (void*)actual);

  /*
   * SGR extended mode.
   * On button press, sends CSI < Cb ; Cx ; Cy M.
   * On button release, sends CSI < Cb ; Cx ; Cy m.
   */
  write_console(&tty_out, SET_MODE_NORMAL SET_ENC_SGR);

  append_expected(expected, 0, pos.X, pos.Y, 'M', SGR);
  append_expected(expected, 0, pos.X, pos.Y, 'm', SGR);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  pos.X = SHRT_MAX;
  pos.Y = SHRT_MAX;
  expected->len = 0;
  append_expected(expected, 0, pos.X, pos.Y, 'M', SGR);
  append_expected(expected, 0, pos.X, pos.Y, 'm', SGR);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  /* Test extended coordinates reset. */
  expected->len = 0;
  append_expected(expected, 0, pos.X, pos.Y, 'M', SGR);
  append_expected(expected, 0, pos.X, pos.Y, 'm', SGR);
  /*
   * In encoding other than the correct encoding, reset is not performed.
   */
  write_console(&tty_out, RESET_ENC_EXT RESET_ENC_URXVT);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));

  expected->len = 0;
  append_expected(expected, 0 + ' ', pos.X + ' ', pos.Y + ' ', 'M', X10);
  append_expected(expected, 3 + ' ', pos.X + ' ', pos.Y + ' ', 'M', X10);
  write_console(&tty_out, RESET_ENC_SGR);
  write_console_mouse_click(&tty_out, &tty_in, pos,
                            FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0, BOTH);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT(assert_same(expected, actual));


  finalize_buf(expected);
  finalize_buf(actual);
  MAKE_VALGRIND_HAPPY();
  return 0;
}
#else

typedef int file_has_no_tests;  /* ISO C forbids an empty translation unit. */

#endif  /* ifndef _WIN32 */

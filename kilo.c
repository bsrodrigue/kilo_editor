#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define APPEND_BUF_INIT                                                        \
  { NULL, 0 }

enum EditorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

struct editor_row {
  int size;
  char *chars;
};

struct append_buf {
  char *buffer;
  int len;
};

struct editor_state {
  struct termios original_termios;
  int screen_rows;
  int screen_cols;
  int numrows;
  struct editor_row *rows;
  int cx, cy;
};

struct editor_state state;

void append_buf_append(struct append_buf *ab, const char *s, int len) {
  char *new = realloc(ab->buffer, ab->len + len);

  if (new == NULL)
    return;

  memcpy(&new[ab->len], s, len);
  ab->buffer = new;
  ab->len += len;
}

void append_buf_free(struct append_buf *ab) { free(ab->buffer); }

void clear_screen() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
}

void die(const char *s) {
  clear_screen();

  perror(s);
  exit(1);
}

void editor_move_cursor(int key) {
  switch (key) {
  case ARROW_LEFT:
    if (state.cx != 0) {
      state.cx--;
    }
    break;
  case ARROW_RIGHT:
    if (state.cx != state.screen_cols - 1) {
      state.cx++;
    }
    break;
  case ARROW_UP:
    if (state.cy != 0) {
      state.cy--;
    }
    break;
  case ARROW_DOWN:
    if (state.cy != state.screen_rows - 1) {
      state.cy++;
    }
    break;
  }
}

void disable_raw_mode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &state.original_termios) == -1)
    die("tcsetattr");
}

void enable_raw_mode() {
  if (tcgetattr(STDIN_FILENO, &state.original_termios) == -1)
    die("tcgetattr");

  struct termios raw_termios = state.original_termios;

  // Input Flags
  raw_termios.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);

  // Output Flags
  raw_termios.c_oflag &= ~(OPOST);

  // Local Flags
  raw_termios.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

  // Control Flags
  raw_termios.c_cflag |= (CS8);

  raw_termios.c_cc[VMIN] = 0;
  raw_termios.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_termios) == -1)
    die("tcsetattr");
}

int editor_read_key() {
  int nread;
  char c;

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= 9) {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';

        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      }

      else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    }

    else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }

    return '\x1b';
  }

  return c;
}

int get_cursor_position(int *rows, int *cols) {
  char buffer[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buffer) - 1) {
    if (read(STDIN_FILENO, &buffer[i], 1) != 1)
      break;
    if (buffer[i] == 'R')
      break;
    i++;
  };

  buffer[i] = '\0';

  if (buffer[0] != '\x1b' || buffer[1] != '[')
    return -1;

  if (sscanf(&buffer[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

int get_win_size(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDIN_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;

    return get_cursor_position(rows, cols);
  }

  *cols = ws.ws_col;
  *rows = ws.ws_row;

  return 0;
}

void editor_draw_rows(struct append_buf *ab) {
  int y;

  for (y = 0; y < state.screen_rows; y++) {
    if (y >= state.numrows) {
      append_buf_append(ab, "~", 1);
    }

    else {
      int len = state.rows[y].size;
      if (len > state.screen_cols)
        len = state.screen_cols;
      append_buf_append(ab, state.rows[y].chars, len);
    }

    append_buf_append(ab, "\x1b[k", 3);
    if (y < state.screen_rows - 1) {
      append_buf_append(ab, "\r\n", 2);
    }
  }
}

// \x1b[ --- Escape Sequence

void editor_refresh_screen() {
  struct append_buf ab = APPEND_BUF_INIT;

  append_buf_append(&ab, "\x1b[?25l", 6);
  append_buf_append(&ab, "\x1b[H", 3);

  editor_draw_rows(&ab);

  char buf[32];
  // Storing formatted string
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", state.cy + 1, state.cx + 1);
  append_buf_append(&ab, buf, strlen(buf));

  append_buf_append(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.buffer, ab.len);

  // This seems to be terrible from a performance perspective.
  // Freeing memory at each refresh?
  append_buf_free(&ab);
}

void editor_proccess_keypress() {
  int c = editor_read_key();

  switch (c) {
  case CTRL_KEY('q'):
    clear_screen();
    exit(0);
    break;

  case HOME_KEY:
    state.cx = 0;
    break;

  case END_KEY:
    state.cx = state.screen_cols - 1;
    break;

  case PAGE_UP:
  case PAGE_DOWN: {
    int times = state.screen_rows;
    while (times--)
      editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);

  } break;

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editor_move_cursor(c);
    break;
  default:
    printf("%c", c);
    fflush(stdout);
    break;
  }
}

void init_editor() {
  state.cx = 0;
  state.cy = 0;
  state.numrows = 0;
  state.rows = NULL;

  if (get_win_size(&state.screen_rows, &state.screen_cols) == -1)
    die("get_win_size");

  enable_raw_mode();
}

void editor_append_row(char *s, size_t len) {
  state.rows =
      realloc(state.rows, sizeof(struct editor_row) * (state.numrows + 1));

  int at = state.numrows;

  state.rows[at].size = len;
  state.rows[at].chars = malloc(len + 1);
  memcpy(state.rows[at].chars, s, len);
  state.rows[at].chars[len] = '\0';
  state.numrows++;
}

void editor_open(char *filename) {
  FILE *fp = fopen(filename, "r");

  if (fp == NULL)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap, fp) != -1)) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    editor_append_row(line, linelen);
  };

  free(line);
  fclose(fp);
}

int main(int argc, char *argv[]) {
  // Simple Terminal Based Text Editor (Just for fun and learning)
  atexit(disable_raw_mode);
  init_editor();

  if (argc >= 2) {
    editor_open(argv[1]);
  }

  while (1) {
    editor_refresh_screen();
    editor_proccess_keypress();
  };

  return 0;
}

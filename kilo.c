#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct editor_config {
  struct termios original_termios;
  int rows;
  int cols;
};

struct editor_config config;

void clear_screen() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
}

void die(const char *s) {
  clear_screen();

  perror(s);
  exit(1);
}

void disable_raw_mode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.original_termios) == -1)
    die("tcsetattr");
}

void enable_raw_mode() {
  if (tcgetattr(STDIN_FILENO, &config.original_termios) == -1)
    die("tcgetattr");

  struct termios raw_termios = config.original_termios;

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

char editor_read_key() {
  int nread;
  char c;

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
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

void editor_draw_rows() {
  int y;

  for (y = 0; y < config.rows; y++) {
    write(STDOUT_FILENO, "~\r\n", 3);
  }
}

void editor_refresh_screen() {
  clear_screen();

  editor_draw_rows();

  write(STDOUT_FILENO, "\x1b[H", 3);
}

void editor_proccess_keypress() {
  char c = editor_read_key();

  switch (c) {
  case CTRL_KEY('q'):
    clear_screen();
    exit(0);
    break;
  case '\r':
    printf("\r\n");
    break;
  default:
    printf("%c", c);
    fflush(stdout);
    break;
  }
}

void init_editor() {
  if (get_win_size(&config.rows, &config.cols) == -1)
    die("get_win_size");

  enable_raw_mode();
}

int main(void) {
  atexit(disable_raw_mode);
  init_editor();

  while (1) {
    editor_refresh_screen();
    editor_proccess_keypress();
  };

  return 0;
}

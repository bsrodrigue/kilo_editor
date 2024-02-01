#include <asm-generic/errno-base.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct termios original_termios;

void die(const char *s) {
  perror(s);
  exit(1);
}

void disable_raw_mode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios) == -1)
    die("tcsetattr");
}

void enable_raw_mode() {
  if (tcgetattr(STDIN_FILENO, &original_termios) == -1)
    die("tcgetattr");

  struct termios raw_termios = original_termios;

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

void editor_proccess_keypress() {
  char c = editor_read_key();

  switch (c) {
  case CTRL_KEY('q'):
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

int main(void) {
  atexit(disable_raw_mode);
  enable_raw_mode();

  printf("\r\n");
  printf("===[KILO TEXT EDITOR]===\r\n");

  while (1) {
    editor_proccess_keypress();
  };

  return 0;
}

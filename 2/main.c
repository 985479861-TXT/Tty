#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <pty.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <sys/ioctl.h>

#define MAX_COLS 256
#define MAX_ROWS 128
#define FONT_NAME "Monospace:pixelsize=12:antialias=false"

typedef enum {
  STATE_NORMAL,
  STATE_ESC,
  STATE_CSI
} ParserState;

typedef struct {
  char c;
  unsigned int fg_idx;
} Cell;

Cell grid[MAX_ROWS][MAX_COLS];
int COLS = 80;
int ROWS = 24;
int cursor_col = 0;
int cursor_row = 0;
unsigned int current_fg = 0;

XftColor xft_colors[8];
XRenderColor render_colors[8] = {
  {0xaaaa, 0xaaaa, 0xaaaa, 0xffff},
  {0xffff, 0x0000, 0x0000, 0xffff},
  {0x0000, 0xffff, 0x0000, 0xffff},
  {0xffff, 0xffff, 0x0000, 0xffff},
  {0x0000, 0x0000, 0xffff, 0xffff},
  {0xffff, 0x0000, 0xffff, 0xffff},
  {0x0000, 0xffff, 0xffff, 0xffff},
  {0x0000, 0x0000, 0x0000, 0x0000}
};

void clear_grid() {
  for (int r =0; r< MAX_ROWS; r++) {
    for (int c = 0; c < MAX_COLS; c++) {
      grid[r][c].c = ' ';
      grid[r][c].fg_idx = 0;
    }
  }
}

int main(){
  int pty_master;
  struct winsize ws = { .ws_row = 24, .ws_col = 80 };
  pid_t pid = forkpty(&pty_master, NULL, NULL, &ws);
  if (pid == 0) {
    setenv("TERM", "linux", 1);
    execlp("bash", "bash", (char *)NULL);
    exit(1);
  }
  Display *dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fprintf(stderr, "Cannot open X display\n");
    return 1;
  }

  int screen = DefaultScreen(dpy);
  Window root = RootWindow(dpy, screen);
  
  XftFont *font = XftFontOpenName(dpy, screen, FONT_NAME);
  if (!font) {
    fprintf(stderr, "Cannot open font %s\n", FONT_NAME);
    return 1;
  }

  int char_width = font->max_advance_width;
  int char_height = font->height;

  for (int i = 0; i < 8; i++) {
    XftColorAllocValue(dpy, DefaultVisual(dpy, screen),
                       DefaultColormap(dpy, screen), &render_colors[i], &xft_colors[i]);
  }
  

  Window win = XCreateSimpleWindow(dpy, root, 0, 0, COLS * char_width, 
               ROWS * char_height, 0, BlackPixel(dpy, screen),
               BlackPixel(dpy, screen));

  XClassHint *class_hint = XAllocClassHint();
  if (class_hint) {
    class_hint->res_name = "Tty";
    class_hint->res_class = "tsh";
    XSetClassHint(dpy, win, class_hint);
    XFree(class_hint);
  }

  XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask);
  XMapWindow(dpy, win);

  XftDraw *draw = XftDrawCreate(dpy, win, DefaultVisual(dpy, screen),
                                DefaultColormap(dpy, screen));
  
  clear_grid();

  ParserState state = STATE_NORMAL;
  char csi_buf[32];
  int csi_idx = 0;
  int x11_fd = ConnectionNumber(dpy);

  while (1) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(pty_master, &fds);
    FD_SET(x11_fd, &fds);

    int max_fd = (pty_master > x11_fd) ? pty_master : x11_fd;
    
    if (!XPending(dpy)) {
      if (select(max_fd + 1, &fds, NULL, NULL, NULL) < 0) {
        break;
      }
    }
    
    if (FD_ISSET(pty_master, &fds)) {
      char buf[1024];
      ssize_t n = read(pty_master,buf, sizeof(buf) -1);
      if (n <=0) {
        break;
      }
      
      for (ssize_t i = 0; i < n; i++) {
      char ch = buf[i];

      switch (state) {
        case STATE_NORMAL:
          if (ch == '\033') {
            state = STATE_ESC;
          }
          else if (ch == '\r') {
            cursor_col = 0;
          }
          else if (ch == '\n') {
            cursor_row++;
            if (cursor_row >= ROWS) {
              memmove(grid[0], grid[1], COLS * (ROWS -1));
              memset(grid[ROWS -1], ' ', COLS);
              cursor_row = ROWS -1;
            }
          }
          else if (ch == '\b') {
            if (cursor_col > 0) cursor_col--;
          }
          else if ((unsigned char)ch >= 32) {
            if (cursor_col < COLS && cursor_row < ROWS) {
              grid[cursor_row][cursor_col].c = ch;
              grid[cursor_row][cursor_col].fg_idx = current_fg;
              cursor_col++;
            }
          }
          break;
        case STATE_ESC:
          if (ch == '[') {
            state = STATE_CSI;
            csi_idx = 0;
            memset(csi_buf, 0, sizeof(csi_buf));
          }
          else {
            state = STATE_NORMAL;
          }
          break;

        case STATE_CSI:
          if ((ch >= '0' && ch <= '9') || ch == ';' || ch == '?') {
            if (csi_idx < (int)sizeof(csi_buf) -1) {
              csi_buf[csi_idx++] = ch;
            }
          }
          else {
            if (ch == 'm') {
              int code = atoi(csi_buf);
              if (code == 0) current_fg = 0;
              else if (code >= 30 && code <= 37) {
                int ansi_map[] = {7, 1, 2, 3, 4, 5, 6, 0};
                current_fg = ansi_map[code - 30];
              }
            }
            else if (ch == 'J' && atoi(csi_buf) == 2) {
              clear_grid();
              cursor_col = 0;
              cursor_row = 0;
            }
            else if (ch == 'H' || ch == 'f') {
              if (csi_idx == 0) {
                cursor_row = 0;
                cursor_col = 0;
                clear_grid();
              }
              else {
                int r_val = 0;
                int c_val = 0;
                if (sscanf(csi_buf, "%d;%d", &r_val, &c_val) == 2) {
                    cursor_row = (r_val -1 >= 0) ? r_val -1 : 0;
                    cursor_col = (c_val -1 >= 0) ? c_val -1 : 0;
                }
                else if (sscanf(csi_buf, "%d", &r_val) == 1) {
                  cursor_row = (r_val -1 >= 0) ? r_val -1 : 0;
                  cursor_col = 0;
                }
                if (cursor_row >= ROWS) cursor_row = ROWS -1;
                if (cursor_col >= COLS) cursor_col = COLS -1;
              }
            }
            state = STATE_NORMAL;
          }
          break;
      } }
      XClearWindow(dpy, win);
      XEvent exp;
      exp.type = Expose;
      XSendEvent(dpy, win, False, ExposureMask, &exp);
    }
    
    while (XPending(dpy)) {
      XEvent ev;
      XNextEvent(dpy, &ev);
      
      if (ev.type == Expose) {
        for (int r = 0; r < ROWS; r++) {
          for (int c = 0; c < COLS; c++) {
            if (grid[r][c].c != ' ') {
              XftDrawString8(draw, &xft_colors[grid[r][c].fg_idx], font,
                             c * char_width,
                             r * char_height + font->ascent,
                             (XftChar8 *)&grid[r][c].c, 1);
            }
          }
        }
        
        XSetForeground(dpy, DefaultGC(dpy, screen), WhitePixel(dpy, screen));
        XFillRectangle(dpy, win, DefaultGC(dpy, screen),
                       cursor_col * char_width, cursor_row * char_height,
                       char_width, char_height);
      }
      else if (ev.type == ConfigureNotify) {
        int new_cols = ev.xconfigure.width / char_width;
        int new_rows = ev.xconfigure.height / char_height;

        if (new_cols > 0 && new_rows) {
          COLS = (new_cols < MAX_COLS) ? new_cols : MAX_COLS;
          ROWS = (new_rows < MAX_ROWS) ? new_rows : MAX_ROWS;
          
          struct winsize new_ws = {
            .ws_row = ROWS,
            .ws_col = COLS,
            .ws_xpixel = ev.xconfigure.width,
            .ws_ypixel = ev.xconfigure.height
          };
          ioctl(pty_master, TIOCSWINSZ, &new_ws);
        }
      }
      else if (ev.type == KeyPress) {
        char kbuf[32];
        KeySym ksym;
        int len = XLookupString(&ev.xkey, kbuf, sizeof(kbuf), &ksym, NULL);
        if (ksym == XK_Up) {
          write(pty_master, "\033[A", 3);
        }
        else if (ksym == XK_Down) {
          write(pty_master, "\033[B", 3);
        } else if (ksym == XK_Right) {
          write(pty_master, "\033[C", 3);
        } 
        else if (ksym == XK_Left) {
          write(pty_master, "\033[D", 3);
        }
        else if (len > 0) {
          write(pty_master, kbuf, len);
        }
      }
    }
  }

  for (int i = 0; i<8; i++) {
    XftColorFree(dpy, DefaultVisual(dpy, screen),
    DefaultColormap(dpy, screen), &xft_colors[i]);
  }

  XftDrawDestroy(draw);
  XftFontClose(dpy, font);
  XDestroyWindow(dpy, win);
  XCloseDisplay(dpy);
  return 0;
}

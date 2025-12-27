/* This demo visualizes the gap buffer when compiled with -DGAP_DEBUG, otherwise
 * acts as a simple readline-like. The original visualization idea is taken from this
 * youtube video: https://youtu.be/NH7PapZINtc?si=iZomXg5TAUPiP1IA
 * however the author of the video did not share the implementation,
 * so I tried to do something similar and share it!
 *
 *  Run with -h or --help for the motions*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "gbf.h"

#define CTRL_A  0x1
#define CTRL_B  0x2
#define CTRL_D  0x4
#define CTRL_E  0x5
#define CTRL_F  0x6
#define CTRL_K  0xb
#define CTRL_L  0xc
#define ENTER   0xa
#define CTRL_U  0x15
#define CTRL_W  0x17
#define ESC     0x1b
#define BACKSPACE 0x7f

typedef struct {
    int size;
    char *data;
    int size_allocated;
} CString;

typedef struct {
    const char *prompt;
    char *dst;
    size_t dst_sz;

    Buffer gbf;
} State;

/* globals */
static State state;
static struct termios term;

/* ------------------------------------------------------------------------- */
/* CString handling */
void cstr_realloc(CString *cstr, int new_size)
{
    int size;

    size = cstr->size_allocated;
    if (size < 8)
        size = 8; /* no need to allocate a too small first string */
    while (size < new_size)
        size = size * 2;
    cstr->data = realloc(cstr->data, size);
    cstr->size_allocated = size;
}

/* add a byte */
void cstr_ccat(CString *cstr, int ch)
{
    int size;
    size = cstr->size + 1;
    if (size > cstr->size_allocated)
        cstr_realloc(cstr, size);
    cstr->data[size - 1] = ch;
    cstr->size = size;
}

/* add string of 'len', or of its len/len+1 when 'len' == -1/0 */
void cstr_cat(CString *cstr, const char *str, int len)
{
    int size;
    if (len <= 0)
        len = strlen(str) + 1 + len;
    size = cstr->size + len;
    if (size > cstr->size_allocated)
        cstr_realloc(cstr, size);
    memmove(cstr->data + cstr->size, str, len);
    cstr->size = size;
}

void cstr_new(CString *cstr)
{
    memset(cstr, 0, sizeof(CString));
}

void cstr_free(CString *cstr)
{
    free(cstr->data);
}
/* ------------------------------------------------------------------------- */
/* terminal handling */
static void rawmode_end(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &term);
}

int rawmode_start(void)
{
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &term) < 0)
        return -1;

    raw = term;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    atexit(rawmode_end);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
        return -1;
    return 1;
}

/* clear the screen and move to top left */
int clear_screen(void)
{
    return write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
}

/* TODO: handle resizing and when length is larger than terminal columns.*/
int redraw(void)
{
    size_t plen;
    char seq[32];
    CString cstr;
    Buffer *gbf;

    gbf = &state.gbf;
    plen = strlen(state.prompt);
    cstr_new(&cstr);
    /* gap visualization is avaiable only when compiled with this flag
     * otherwise acts as readline */
#ifdef GAP_DEBUG
    uint8_t *p = buf_flatten(gbf);
    if (p) {
        cstr_cat(&cstr,
                "\x1b[2J\x1b[H" /* move to top the left and clear the entire screen */
                "\x1b[7m",      /* reverse video and go to the start */
                -1);  /* -1 discards '\0' */

        cstr_cat(&cstr, state.prompt, plen);
        cstr_cat(&cstr, (const char *)p, -1);
        /* revert video back and go down one line*/
        cstr_cat(&cstr, "\x1b[m\n", -1);
        free(p);
    }
#endif

    /* add prompt */
    cstr_ccat(&cstr, '\r');
    cstr_cat(&cstr, state.prompt, plen);

    /* add buffer content */
    buf_slice bs[2];
    if (buf_view(gbf, 0, buf_len(gbf), bs)) {
        cstr_cat(&cstr, (const char *)bs->ptr, bs->len);
        if (bs[1].len)
            cstr_cat(&cstr, (const char *)bs[1].ptr, bs[1].len);
    }
    /* clear anythig after the cursor */
    cstr_cat(&cstr, "\x1b[0K", -1);
    /* move cursor to it's position */
    if (snprintf(seq, sizeof(seq) ,"\r\x1b[%zuC", plen + buf_cursor(gbf)) < 0)
        goto error;
    cstr_cat(&cstr, seq, strlen(seq));

    if (write(STDOUT_FILENO, cstr.data, cstr.size) < 0)
        goto error;

    cstr_free(&cstr);
    return 1;
error:
    cstr_free(&cstr);
    return 0;
}

int store(void)
{
    int r;
    Buffer *gbf;
    buf_slice bs[2];

    gbf = &state.gbf;
    r = 0;
    if (!buf_view(gbf, 0, buf_len(gbf), bs))
        return 0;
    if (bs[1].len)
        r = snprintf(state.dst, state.dst_sz, SLICE_FMT SLICE_FMT,
                SLICE_ARG(bs[0]), SLICE_ARG(bs[1]));
    else
        r = snprintf(state.dst, state.dst_sz, SLICE_FMT,
                SLICE_ARG(bs[0]));
    return r;
}

int edit(void)
{
    char c, seq[8];
    Buffer *gbf;

    gbf = &state.gbf;
    while (1) {
        if (read(STDIN_FILENO, &c, 1) < 0)
            return -1;
        switch(c) {
#define CASE(x, fn) case x: fn; break;
            CASE(CTRL_F, buf_forward_char(gbf));
            CASE(CTRL_B, buf_backward_char(gbf));
            CASE(CTRL_A, buf_home(gbf));
            CASE(CTRL_E, buf_end(gbf));
            CASE(CTRL_K, buf_kill_line(gbf));
            CASE(CTRL_U, buf_line_discard(gbf));
            CASE(CTRL_W, buf_word_rubout(gbf));
            CASE(BACKSPACE, buf_delete(gbf, -1));
            CASE(CTRL_L, clear_screen());
            case CTRL_D:
                if (buf_len(gbf)) {
                    buf_delete(gbf, 1);
                    break;
                }
                return -1;
            case ENTER:
                goto end;
            case ESC:
            if (read(STDIN_FILENO, seq, 1) < 0)
                return -1;
            if (*seq >= 'a' && *seq <= 'z') {
                switch (*seq) {
                    CASE('f', buf_forward_word(gbf));  /* M-f */
                    CASE('b', buf_backward_word(gbf)); /* M-b */
                    CASE('d', buf_kill_word(gbf));     /* M-d */
                }
            } else if (*seq == '[') {
                if (read(0, seq+1, 1) <= 0)
                    return -1;
                if (seq[1] >= '0' && seq[1] <= '9') {
                    if (read(0, seq+2, 1) <= 0)
                        return -1;
                    if (seq[2] == ';') {
                        if (read(0, seq+3, 2) <= 0)
                            return -1;
                        if (seq[3] == '5') {
                            switch (seq[4]) {
                                CASE('C', buf_forward_word(gbf));  /* ctlr-right */
                                CASE('D', buf_backward_word(gbf)); /* ctlr-left */
                            }
                        }
                    } else if (seq[2] == '~') {
                        switch (seq[1] ) {
                            CASE('1', buf_home(gbf));
                            CASE('3', buf_delete(gbf, 1));
                            CASE('4', buf_end(gbf));
                        }
                    }
                } else {
                    switch(seq[1]) {
                        CASE('C', buf_forward_char(gbf));
                        CASE('D', buf_backward_char(gbf));

                        CASE('P', buf_delete(gbf, 1));
                        CASE('H', buf_home(gbf));
                        CASE('F', buf_end(gbf));
                    }
                }
            }
            break;
            default:
            if (isprint(c))
                buf_ccat(gbf, c);
            break;
        }
        if (!redraw())
            return -1;
    }
end:
    return store();
}

int repl_read(const char *prompt, char *dst, const size_t dst_sz)
{
    int r;
    if (!prompt || !dst || !dst_sz)
        return -1;

    if (rawmode_start() < 0)
        return -1;
    if (write(STDOUT_FILENO, prompt, strlen(prompt)) < 0)
        return -1;

    state.prompt = prompt;
    state.dst = dst;
    state.dst_sz = dst_sz;
    buf_new(&state.gbf);

    r = edit();

    rawmode_end();
    putchar('\n');
    buf_free(&state.gbf);
    return r;
}

void repl(void)
{
    int n;
    char buffer[1024];

    while (1) {
        if ((n = repl_read("> ", buffer, sizeof(buffer))) < 0)
            break;
        if (n)
            printf("got: \"%s\"\n", buffer);
    }
}

void usage(void)
{
    fprintf(stderr,
        "Editing motions:\n\n"
        "Cursor movement:\n"
        "  Ctrl-A         beginning of line\n"
        "  Ctrl-E         end of line\n"
        "  Ctrl-B         backward character\n"
        "  Ctrl-F         forward character\n"
        "  Left / Right   backward / forward character\n"
        "  Home / End     beginning / end of line\n\n"
        "Word movement:\n"
        "  Meta-B         backward word\n"
        "  Meta-F         forward word\n"
        "  Ctrl-Left      backward word\n"
        "  Ctrl-Right     forward word\n\n"
        "Deletion:\n"
        "  Backspace      delete character before cursor\n"
        "  Del            delete character at cursor\n"
        "  Ctrl-D         delete character at cursor\n"
        "  Meta-D         delete word forward\n"
        "  Ctrl-W         delete word backward\n"
        "  Ctrl-K         delete to end of line\n"
        "  Ctrl-U         delete to start of line\n\n"
        "Other:\n"
        "  Ctrl-L         clear screen\n");
}

int main(int argc, char **argv)
{
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "run this demo in the terminal!\n");
        return 1;
    }

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage();
        return 0;
    }

    repl();
    return 0;
}

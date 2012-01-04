/*  jsdev.c
    Douglas Crockford
    2012-01-05

    Public Domain

    JSPrep is a simple JavaScript preprocessor. It implements a basic macro
    language that is written in the form of comments. These comments are
    normally ignored, and will be removed by JSMin. But JSPrep will activate
    these comments, replacing them with executable forms that can be used to do
    debugging, testing, logging, or tracing. JSPrep scans a source looking for
    and replacing patterns. A pattern is a slashstar comment containing a
    command and some stuff, and optionally a condition wrapped in parens.
    There must be no space between the slashstar and the <cmd>.
*/
        /*<cmd> <stuff>*/
        /*<cmd>(<condition>) <stuff>*/
/*
    The command line will contain a list of <cmd>s, each of which can
    optionally be followed by a colon and <command>. There must not be
    any spaces around the colon.

    A <cmd> may contain any short sequence of ASCII letters, digits,
    underbar (_), dollar ($), and period(.). The active <cmd> strings are
    declared in the command line. All <cmd>s that are not declared are ignored.

    The <stuff> may not include a regular expression literal or a comment or
    a string or regexp containing slashstar.

    If a <cmd> does not have a :<command>, then it will expand into

        {<stuff>}

    Effectively, the outer part of the comment is replaced with braces, turning
    an inert comment into an executable block. If a <condition> was included,
    it will expand into

        if (<condition>) {<stuff>}

    Note that there can be no space between the <cmd> and the paren that
    encloses the <condition>. If there is a space, then everything is <stuff>.

    If <cmd> was declared with :<command>, then it will expand into

        {<command>(<stuff>);}

    A function call is constructed, replacing the <cmd> with <command>, and
    using the <stuff> as the arguments. If a condition was included, it will
    expand into

        if (<condition>) {<command>(<stuff>);}

    Also, a command line can contain a comment.

        -comment <comment>

            A string that will be prepended to the file as a comment.

    Sample command line:

        jsprep debug log:console.log alarm:alert -comment "Devel Edition"

    That will enable
*/
        /*debug <stuff>*/
/*
    comments that expand into

        {<stuff>;}

    as well as
*/
        /*log <stuff>*/
/*
    comments that expand into

        {console.log(<stuff>);}

    and
*/
        /*alarm(<condition>) <stuff>*/
/*
    comments that expand into

        if (<condition>) {alert(<stuff>);}

    It will also insert the comment

        // Devel Edition

    at the top of the output file.

    A program is read from stdin, and a modified program is written to stdout.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define false          0
#define true           1
#define MAX_CMD_LENGTH 80
#define MAX_NR_CMDS    100

static char cmd                  [MAX_CMD_LENGTH + 1];
static char cmds    [MAX_NR_CMDS][MAX_CMD_LENGTH + 1];
static char commands[MAX_NR_CMDS][MAX_CMD_LENGTH + 1];
static int  cr;
static int  line_nr;
static int  nr_cmds;
static int  preview = 0;

static void
error(char* message)
{
    fputs("JSDev: ", stderr);
    if (line_nr) {
        fprintf(stderr, "%d. ", line_nr);
    } else {
        fputs("bad command line ", stderr);
    }
    fputs(message, stderr);
    fputs("\r\n", stderr);
    exit(1);
}


static int
is_alphanum(char c)
{
/*
    Return true if the character is a letter, digit, underscore,
    dollar sign, or period.
*/
    return ((c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') ||
             c == '_' || c == '$' || c == '.');
}


static int
emit(int c)
{
/*
    Send a character to stdout.
*/
    if (c > 0 && fputc(c, stdout) == EOF) {
        error("write error.");
    }
    return c;
}


static void
emits(char* s)
{
/*
    Send a string to stdout.
*/
    if (fputs(s, stdout) == EOF) {
        error("write error.");
    }
}


static int
peek()
{
    return preview = preview ? preview : fgetc(stdin);
}


static int
get(int echo)
{
/*
    Return the next character from the input. If the echo argument is
    true, then the character will also be emitted.
*/
    int c;
    if (preview) {
        c = preview;
        preview = 0;
    } else {
        c = fgetc(stdin);
    }
    if (c <= 0) {
        return EOF;
    } else if (c == '\r') {
        cr = true;
        line_nr += 1;
    } else {
        if (c == '\n' && !cr) {
            line_nr += 1;
        } 
        cr = false;
    }
    if (echo) {
        emit(c);
    }
    return c;
}


static void
unget(int c)
{
    preview = c;
}


static void
string(int quote, int in_comment)
{
    int c, was = line_nr;
    for (;;) {
        c = get(true);
        if (c == quote) {
            return;
        }
        if (c == '\\') {
            c = get(true);
        }
        if (in_comment && c == '*' && peek() == '/') {
            error("unexpected close comment in string.");
        }
        if (c == EOF) {
            line_nr = was;
            error("unterminated string literal.");
        }
    }
}


static int
pre_regexp(int left)
{
    return (left == '(' || left == ',' || left == '=' ||
            left == ':' || left == '[' || left == '!' ||
            left == '&' || left == '|' || left == '?' ||
            left == '{' || left == '}' || left == ';');
}


static void
regexp(int in_comment)
{
    int c, was = line_nr;
    for (;;) {
        c = get(true);
        if (c == '[') {
            for (;;) {
                c = get(true);
                if (c == ']') {
                    break;
                }
                if (c == '\\') {
                    c = get(true);
                }
                if (in_comment && c == '*' && peek() == '/') {
                    error("unexpected close comment in regexp.");
                }
                if (c == EOF) {
                    error("unterminated set in Regular Expression literal.");
                }
            }
        } else if (c == '/') {
            if (in_comment && (peek() == '/' || peek() == '*')) {
                error("unexpected comment.");
            }
            return;
        } else if (c =='\\') {
            c = get(true);
        }
        if (in_comment && c == '*' && peek() == '/') {
            error("unexpected comment.");
        }
        if (c == EOF) {
            line_nr = was;
            error("unterminated regexp literal.");
        }
    }
}


static void
condition()
{
    int c, left, paren = 0;
    for (;;) {
        c = get(true);
        if (c == '(' || c == '{' || c == '[') {
            paren += 1;
        } else if (c == ')' || c == '}' || c == ']') {
            paren -= 1;
            if (paren == 0) {
                return;
            }
        } else if (c == EOF) {
            error("Unterminated condition.");
        } else if (c == '\'' || c == '"' || c == '`') {
            string(c, true);
        } else if (c == '/') {
            if (peek() == '/' || peek() == '*') {
                error("unexpected comment.");
            }
            if (pre_regexp(left)) {
                regexp(true);
            }
        } else if (c == '*' && peek() == '/') {
            error("unclosed condition.");
        }
        if (c > ' ') {
            left = c;
        }
    }
}


static void
stuff()
{
    int c, left = '{', paren = 0;
    for (;;) {
        while (peek() == '*') {
            get(false);
            if (peek() == '/') {
                get(false);
                return;
            }
            emit('*');
        }
        c = get(true);
        if (c == EOF) {
            error("Unterminated stuff.");
        } else if (c == '\'' || c == '"' || c == '`') {
            string(c, true);
        } else if (c == '/') {
            if (peek() == '/' || peek() == '*') {
                error("unexpected comment.");
            }
            if (pre_regexp(left)) {
                regexp(true);
            }
        }
        if (c > ' ') {
            left = c;
        }
    }
}


static void
expand(int cmd_nr)
{
    int c;
    int cond = false;

    c = peek();
    if (c == '(') {
        emits("if ");
        condition();
    }
    emit('{');
    if (commands[cmd_nr][0]) {
        emits(commands[cmd_nr]);
        emit('(');
        stuff();
        emit(')');
    } else {
        stuff();
    }
    emits(";}");
}


static int
match()
{
    int cmd_nr;

    for (cmd_nr = 0; cmd_nr < nr_cmds; cmd_nr += 1) {
        if (strcmp(cmd, cmds[cmd_nr]) == 0) {
            return cmd_nr;
        }
    }
    return EOF;
}


static void
process()
{
/*
    Loop through the program text, looking for patterns.
*/
    int c, i, left = 0;
    line_nr = 1;
    c = get(false);
    for (;;) {
        if (c == EOF) {
            break;
        } else if (c == '\'' || c == '"' || c == '`') {
            emit(c);
            string(c, false);
            c = 0;
/*
    The most complicated case is the slash. It can mean division or a regexp
    literal or a line comment or a block comment. A block comment can also be
    a pattern to be expanded.
*/
        } else if (c == '/') {
/*
    A slash slash comment skips to the end of the file.
*/
            if (peek() == '/') {
                emit('/');
                for (;;) {
                    c = get(true);
                    if (c == '\n' || c == '\r' || c == EOF) {
                        break;
                    }
                }
                c = get(false);
/*
    The first component of a slash star comment might be the cmd.
*/
            } else {
                if (peek() == '*') {
                    get(false);
                    for (i = 0; i < MAX_CMD_LENGTH; i += 1) {
                        c = get(false);
                        if (!is_alphanum(c)) {
                            break;
                        }
                        cmd[i] = c;
                    }
                    cmd[i] = 0;
                    unget(c);
/*
    Did the cmd matches something?.
*/
                    i = i == 0 ? -1 : match();
                    if (i >= 0) {
                        expand(i);
                        c = get(false);
                    } else {
/*
    If the cmd didn't match, then echo the comment.
*/
                        emits("/*");
                        emits(cmd);
                        for (;;) {
                            if (c == EOF) {
                                error("unterminated comment.");
                            }
                            if (c == '/') {
                                c = get(true);
                                if (c == '*') {
                                    error("nested comment.");
                                }
                            } else if (c == '*') {
                                c = get(true);
                                if (c == '/') {
                                    break;
                                }
                            } else {
                                c = get(true);
                            }
                        }
                        c = get(false);
                    }
                } else {
                    emit('/');
                    if (pre_regexp(left)) {
/*
    We are looking at a single slash. Is it a division operator, or is it the
    start of a regexp literal? If is not possible to tell for sure without doing
    a complete parse of the program, and we are not going to do that. Instead,
    we are adopting the convention that a regexp literal must have one of a
    small set of characters to its left.
*/
                        regexp(false);
                    } else {
/*
    Or maybe the slash was a division operator.
*/
                    }
                    left = '/';
                    c = get(false);
                }
            }
        } else {
/*
    The character was nothing special, to just echo it.
    If it wasn't whitespace, remember it as the character to the left of the
    next character.
*/
            emit(c);
            if (c > ' ') {
                left = c;
            }
            c = get(false);
        }
    }
}


extern int
main(int argc, char* argv[])
{
    int c, comment = false, i, j, k;
    cr = false;
    line_nr = 0;
    nr_cmds = 0;
    for (i = 1; i < argc; i += 1) {
        if (comment) {
            comment = false;
            emits("// ");
            emits(argv[i]);
            emit('\n');
        } else if (strcmp(argv[i], "-comment") == 0) {
            comment = true;
        } else {
            for (j = 0; j < MAX_CMD_LENGTH; j += 1) {
                c = argv[i][j];
                if (!is_alphanum(c)) {
                    break;
                }
                cmds[nr_cmds][j] = c;
            }
            if (j == 0) {
                error(argv[i]);
            }
            cmds[nr_cmds][j] = 0;
            if (c == 0) {
                commands[nr_cmds][0] = 0;
            } else if (c == ':') {
                j += 1;
                for (k = 0; k < MAX_CMD_LENGTH; k += 1) {
                    c = argv[i][j + k];
                    if (!is_alphanum(c)) {
                        break;
                    }
                    commands[nr_cmds][k] = c;
                }
                commands[nr_cmds][k] = 0;
                if (k == 0 || c != 0) {
                    error(argv[i]);
                }
            } else {
                error(argv[i]);
            }
            nr_cmds += 1;
        }
    }
    process();
    return 0;
}

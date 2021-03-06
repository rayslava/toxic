/*
 * Toxic -- Tox Curses Client
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "toxic_windows.h"
#include "friendlist.h"
#include "commands.h"
#include "misc_tools.h"

extern char *DATA_FILE;
extern int store_data(Tox *m, char *path);

static void chat_onMessage(ToxWindow *self, Tox *m, int num, uint8_t *msg, uint16_t len)
{
    if (self->num != num)
        return;

    ChatContext *ctx = (ChatContext *) self->chatwin;

    uint8_t nick[TOX_MAX_NAME_LENGTH] = {'\0'};
    tox_getname(m, num, nick);

    print_time(ctx->history);
    wattron(ctx->history, COLOR_PAIR(4));
    wprintw(ctx->history, "%s: ", nick);
    wattroff(ctx->history, COLOR_PAIR(4));

    if (msg[0] == '>') {
        wattron(ctx->history, COLOR_PAIR(GREEN));
        wprintw(ctx->history, "%s\n", msg);
        wattroff(ctx->history, COLOR_PAIR(GREEN));
    } else
        wprintw(ctx->history, "%s\n", msg);

    self->blink = true;
    beep();
}

static void chat_onConnectionChange(ToxWindow *self, Tox *m, int num, uint8_t status)
{
    if (self->num != num)
        return;

    StatusBar *statusbar = (StatusBar *) self->stb;
    statusbar->is_online = status == 1 ? true : false;
}

static void chat_onAction(ToxWindow *self, Tox *m, int num, uint8_t *action, uint16_t len)
{
    if (self->num != num)
        return;

    ChatContext *ctx = (ChatContext *) self->chatwin;

    uint8_t nick[TOX_MAX_NAME_LENGTH] = {'\0'};
    tox_getname(m, num, nick);

    print_time(ctx->history);
    wattron(ctx->history, COLOR_PAIR(YELLOW));
    wprintw(ctx->history, "* %s %s\n", nick, action);
    wattroff(ctx->history, COLOR_PAIR(YELLOW));

    self->blink = true;
    beep();
}

static void chat_onNickChange(ToxWindow *self, int num, uint8_t *nick, uint16_t len)
{
    if (self->num != num)
        return;

    memcpy(self->name, nick, len);
}

static void chat_onStatusChange(ToxWindow *self, Tox *m, int num, TOX_USERSTATUS status)
{
    if (self->num != num)
        return;

    StatusBar *statusbar = (StatusBar *) self->stb;
    statusbar->status = status;
}

static void chat_onStatusMessageChange(ToxWindow *self, int num, uint8_t *status, uint16_t len)
{
    if (self->num != num)
        return;

    StatusBar *statusbar = (StatusBar *) self->stb;
    statusbar->statusmsg_len = len;
    memcpy(statusbar->statusmsg, status, len);
}

static void chat_onFileSendRequest(ToxWindow *self, Tox *m, int num, uint8_t filenum, 
                                   uint64_t filesize, uint8_t *pathname, uint16_t path_len)
{
    if (self-> num != num)
        return;

    ChatContext *ctx = (ChatContext *) self->chatwin;

    wprintw(ctx->history, "File transfer request for '%s' of size %llu.\n", pathname, 
            (long long unsigned int)filesize);

    if (filenum > MAX_FILENUMBER) {
        wprintw(ctx->history, "Too many pending file requests; discarding.\n");
        return;
    }

    wprintw(ctx->history, "Type '/savefile %d' to accept the file transfer.\n", filenum);

    pending_file_transfers[filenum] = num;

    self->blink = true;
    beep();

}

static void chat_onFileControl(ToxWindow *self, Tox *m, int num, uint8_t receive_send, 
                               uint8_t filenum, uint8_t control_type, uint8_t *data, uint16_t length)
{
    if (self->num != num)
        return;

    ChatContext *ctx = (ChatContext *) self->chatwin;

    switch(control_type) {
    case 0:
        wprintw(ctx->history, "File transfer accepted.\n");
        break;
    case 3:
        wprintw(ctx->history, "File successfully recieved.\n");
        break;
    default:
        wprintw(ctx->history, "Control %u receieved.\n", control_type);
        break;
    }

    self->blink = true;
    beep();
}

static void chat_onFileData(ToxWindow *self, Tox *m, int num, uint8_t filenum, uint8_t *data,
                            uint16_t length)
{
    if (self->num != num)
        return;

    ChatContext *ctx = (ChatContext *) self->chatwin;

    char pathname[MAX_STR_SIZE];
    snprintf(pathname, sizeof(pathname), "%d.%u.bin", num, filenum);

    FILE *file_to_save = fopen(pathname, "a");

    if (fwrite(data, length, 1, file_to_save) != 1) {
        wattron(ctx->history, COLOR_PAIR(RED));
        wprintw(ctx->history, "* Error writing to file.\n");
        wattroff(ctx->history, COLOR_PAIR(RED));
    }

    fclose(file_to_save);
}

static void chat_groupinvite(ToxWindow *self, ChatContext *ctx, Tox *m, uint8_t *line)
{
    int groupnum = atoi(line);

    if (groupnum == 0 && strcmp(line, "0")) {    /* atoi returns 0 value on invalid input */
        wprintw(ctx->history, "Invalid syntax.\n");
        return;
    }

    if (tox_invite_friend(m, self->num, groupnum) == -1) {
        wprintw(ctx->history, "Failed to invite friend.\n");
        return;
    }

    wprintw(ctx->history, "Invited friend to group chat %d.\n", groupnum);
}

static void chat_sendfile(ToxWindow *self, ChatContext *ctx, Tox *m, uint8_t *path)
{
    int path_len = strlen(path);

    if (path_len > MAX_STR_SIZE) {
        wprintw(ctx->history, "File path exceeds character limit.\n");
        return;
    }

    FILE *file_to_send = fopen(path, "r");

    if (file_to_send == NULL) {
        wprintw(ctx->history, "File '%s' not found.\n", path);
        return;
    }

    fseek(file_to_send, 0, SEEK_END);
    uint64_t filesize = ftell(file_to_send);
    fseek(file_to_send, 0, SEEK_SET);

    int friendnum = self->num;
    uint8_t friendname[TOX_MAX_NAME_LENGTH] = {'\0'};
    tox_getname(m, friendnum, friendname);

    int filenum = tox_new_filesender(m, friendnum, filesize, path, path_len + 1);

    if (filenum == -1) {
        wprintw(ctx->history, "Error sending file.\n");
        return;
    }

    memcpy(file_senders[num_file_senders].pathname, path, path_len + 1);
    memcpy(file_senders[num_file_senders].friendname, friendname, strlen(friendname) + 1);
    file_senders[num_file_senders].file = file_to_send;
    file_senders[num_file_senders].filenum = filenum;
    file_senders[num_file_senders].friendnum = friendnum;
    file_senders[num_file_senders].piecelen = fread(file_senders[num_file_senders].nextpiece, 1,
                                                    tox_filedata_size(m, friendnum), file_to_send);


    wprintw(ctx->history, "Sending file '%s'...\n", path);
    ++num_file_senders;
}

static void print_chat_help(ChatContext *ctx)
{
    wattron(ctx->history, COLOR_PAIR(CYAN) | A_BOLD);
    wprintw(ctx->history, "Chat commands:\n");
    wattroff(ctx->history, A_BOLD);

    wprintw(ctx->history, "      /status <type> <message>   : Set your status with optional note\n");
    wprintw(ctx->history, "      /note <message>            : Set a personal note\n");
    wprintw(ctx->history, "      /nick <nickname>           : Set your nickname\n");
    wprintw(ctx->history, "      /invite <n>                : Invite friend to a groupchat\n");
    wprintw(ctx->history, "      /me <action>               : Do an action\n");
    wprintw(ctx->history, "      /myid                      : Print your ID\n");
    wprintw(ctx->history, "      /clear                     : Clear the screen\n");
    wprintw(ctx->history, "      /close                     : Close the current chat window\n");
    wprintw(ctx->history, "      /sendfile <filepath>       : Send a file\n");
    wprintw(ctx->history, "      /savefile <n>              : Receive a file\n");
    wprintw(ctx->history, "      /quit or /exit             : Exit Toxic\n");
    wprintw(ctx->history, "      /help                      : Print this message again\n");
    
    wattron(ctx->history, A_BOLD);
    wprintw(ctx->history, "\n * Argument messages must be enclosed in quotation marks.\n");
    wattroff(ctx->history, A_BOLD);
    
    wattroff(ctx->history, COLOR_PAIR(CYAN));
}

static void send_action(ToxWindow *self, ChatContext *ctx, Tox *m, uint8_t *action) {
    if (action == NULL) {
        wprintw(ctx->history, "Invalid syntax.\n");
        return;
    }

    uint8_t selfname[TOX_MAX_NAME_LENGTH];
    tox_getselfname(m, selfname, TOX_MAX_NAME_LENGTH);

    print_time(ctx->history);
    wattron(ctx->history, COLOR_PAIR(YELLOW));
    wprintw(ctx->history, "* %s %s\n", selfname, action);
    wattroff(ctx->history, COLOR_PAIR(YELLOW));

    if (tox_sendaction(m, self->num, action, strlen(action) + 1) == 0) {
        wattron(ctx->history, COLOR_PAIR(RED));
        wprintw(ctx->history, " * Failed to send action\n");
        wattroff(ctx->history, COLOR_PAIR(RED));
    }
}

static void chat_onKey(ToxWindow *self, Tox *m, wint_t key)
{
    ChatContext *ctx = (ChatContext *) self->chatwin;
    StatusBar *statusbar = (StatusBar *) self->stb;

    int x, y, y2, x2;
    getyx(self->window, y, x);
    getmaxyx(self->window, y2, x2);
    /* BACKSPACE key: Remove one character from line */
    if (key == 0x107 || key == 0x8 || key == 0x7f) {
        if (ctx->pos > 0) {
            ctx->line[--ctx->pos] = L'\0';

            if (x == 0)
                mvwdelch(self->window, y - 1, x2 - 1);
            else
                mvwdelch(self->window, y, x - 1);
        }
    } else
    /* Add printable chars to buffer and print on input space */
#if HAVE_WIDECHAR
    if (iswprint(key)) {
#else
    if (isprint(key)) {
#endif
        if (ctx->pos < (MAX_STR_SIZE-1)) {
            mvwaddstr(self->window, y, x, wc_to_char(key));
            ctx->line[ctx->pos++] = key;
            ctx->line[ctx->pos] = L'\0';
        }
    }
    /* RETURN key: Execute command or print line */
    else if (key == '\n') {
        uint8_t *line = wcs_to_char(ctx->line);
        line[ctx->pos+1] = '\0';
        wclear(ctx->linewin);
        wmove(self->window, y2 - CURS_Y_OFFSET, 0);
        wclrtobot(self->window);
        bool close_win = false;
        if (line[0] == '/') {
            if (close_win = !strncmp(line, "/close", strlen("/close"))) {
                int f_num = self->num;
                delwin(ctx->linewin);
                delwin(statusbar->topline);
                del_window(self);
                disable_chatwin(f_num);
            } else if (!strncmp(line, "/me ", strlen("/me ")))
                send_action(self, ctx, m, line + strlen("/me "));
              else if (!strncmp(line, "/help", strlen("/help")))
                print_chat_help(ctx);
              else if (!strncmp(line, "/invite", strlen("/invite")))
                chat_groupinvite(self, ctx, m, line + strlen("/invite "));
              else if(!strncmp(line, "/sendfile ", strlen("/sendfile ")))
                chat_sendfile(self, ctx, m, line + strlen("/sendfile "));
              else
                execute(ctx->history, self->prompt, m, line, ctx->pos);
        } else {
            /* make sure the string has at least non-space character */
            if (!string_is_empty(line)) {
                uint8_t selfname[TOX_MAX_NAME_LENGTH];
                tox_getselfname(m, selfname, TOX_MAX_NAME_LENGTH);

                print_time(ctx->history);
                wattron(ctx->history, COLOR_PAIR(GREEN));
                wprintw(ctx->history, "%s: ", selfname);
                wattroff(ctx->history, COLOR_PAIR(GREEN));

                if (line[0] == '>') {
                    wattron(ctx->history, COLOR_PAIR(GREEN));
                    wprintw(ctx->history, "%s\n", line);
                    wattroff(ctx->history, COLOR_PAIR(GREEN));
                } else
                    wprintw(ctx->history, "%s\n", line);

                if (!statusbar->is_online
                        || tox_sendmessage(m, self->num, line, strlen(line) + 1) == 0) {
                    wattron(ctx->history, COLOR_PAIR(RED));
                    wprintw(ctx->history, " * Failed to send message.\n");
                    wattroff(ctx->history, COLOR_PAIR(RED));
                }
            }
        }

        if (close_win) {
            free(ctx);
            free(statusbar);
        } else {
            ctx->line[0] = L'\0';
            ctx->pos = 0;
        }

        free(line);
    }
}

static void chat_onDraw(ToxWindow *self, Tox *m)
{
    curs_set(1);
    int x, y;
    getmaxyx(self->window, y, x);

    ChatContext *ctx = (ChatContext *) self->chatwin;

    /* Draw status bar */
    StatusBar *statusbar = (StatusBar *) self->stb;
    mvwhline(statusbar->topline, 1, 0, '-', x);
    wmove(statusbar->topline, 0, 0);

    /* Draw name, status and note in statusbar */
    if (statusbar->is_online) {
        char *status_text = "Unknown";
        int colour = WHITE;

        TOX_USERSTATUS status = statusbar->status;

        switch(status) {
        case TOX_USERSTATUS_NONE:
            status_text = "Online";
            colour = GREEN;
            break;
        case TOX_USERSTATUS_AWAY:
            status_text = "Away";
            colour = YELLOW;
            break;
        case TOX_USERSTATUS_BUSY:
            status_text = "Busy";
            colour = RED;
            break;
        }

        wattron(statusbar->topline, A_BOLD);
        wprintw(statusbar->topline, " %s ", self->name);
        wattroff(statusbar->topline, A_BOLD);
        wattron(statusbar->topline, COLOR_PAIR(colour) | A_BOLD);
        wprintw(statusbar->topline, "[%s]", status_text);
        wattroff(statusbar->topline, COLOR_PAIR(colour) | A_BOLD);
    } else {
        wattron(statusbar->topline, A_BOLD);
        wprintw(statusbar->topline, " %s ", self->name);
        wattroff(statusbar->topline, A_BOLD);
        wprintw(statusbar->topline, "[Offline]");
    }

    /* Reset statusbar->statusmsg on window resize */
    if (x != self->x) {
        uint8_t statusmsg[TOX_MAX_STATUSMESSAGE_LENGTH] = {'\0'};
        tox_copy_statusmessage(m, self->num, statusmsg, TOX_MAX_STATUSMESSAGE_LENGTH);
        snprintf(statusbar->statusmsg, sizeof(statusbar->statusmsg), "%s", statusmsg);
        statusbar->statusmsg_len = tox_get_statusmessage_size(m, self->num);
    }

    self->x = x;

    /* Truncate note if it doesn't fit in statusbar */
    uint16_t maxlen = x - getcurx(statusbar->topline) - 6;
    if (statusbar->statusmsg_len > maxlen) {
        statusbar->statusmsg[maxlen] = '\0';
        statusbar->statusmsg_len = maxlen;
    }

    if (statusbar->statusmsg[0]) {
        wattron(statusbar->topline, A_BOLD);
        wprintw(statusbar->topline, " | %s | ", statusbar->statusmsg);
        wattroff(statusbar->topline, A_BOLD);
    }

    wprintw(statusbar->topline, "\n");
    mvwhline(ctx->linewin, 0, 0, '_', x);
    wrefresh(self->window);
}

static void chat_onInit(ToxWindow *self, Tox *m)
{
    int x, y;
    getmaxyx(self->window, y, x);
    self->x = x;

    /* Init statusbar info */
    StatusBar *statusbar = (StatusBar *) self->stb;
    statusbar->status = tox_get_userstatus(m, self->num);
    statusbar->is_online = tox_get_friend_connectionstatus(m, self->num) == 1;

    uint8_t statusmsg[TOX_MAX_STATUSMESSAGE_LENGTH] = {'\0'};
    tox_copy_statusmessage(m, self->num, statusmsg, TOX_MAX_STATUSMESSAGE_LENGTH);
    snprintf(statusbar->statusmsg, sizeof(statusbar->statusmsg), "%s", statusmsg);
    statusbar->statusmsg_len = tox_get_statusmessage_size(m, self->num);

    /* Init subwindows */
    ChatContext *ctx = (ChatContext *) self->chatwin;
    statusbar->topline = subwin(self->window, 2, x, 0, 0);
    ctx->history = subwin(self->window, y-3, x, 0, 0);
    scrollok(ctx->history, 1);
    ctx->linewin = subwin(self->window, 0, x, y-4, 0);
    wprintw(ctx->history, "\n\n");
    print_chat_help(ctx);
    wmove(self->window, y - CURS_Y_OFFSET, 0);
}

ToxWindow new_chat(Tox *m, ToxWindow *prompt, int friendnum)
{
    ToxWindow ret;
    memset(&ret, 0, sizeof(ret));

    ret.onKey = &chat_onKey;
    ret.onDraw = &chat_onDraw;
    ret.onInit = &chat_onInit;
    ret.onMessage = &chat_onMessage;
    ret.onConnectionChange = &chat_onConnectionChange;
    ret.onNickChange = &chat_onNickChange;
    ret.onStatusChange = &chat_onStatusChange;
    ret.onStatusMessageChange = &chat_onStatusMessageChange;
    ret.onAction = &chat_onAction;
    ret.onFileSendRequest = &chat_onFileSendRequest;
    ret.onFileControl = &chat_onFileControl;
    ret.onFileData = &chat_onFileData;

    uint8_t name[TOX_MAX_NAME_LENGTH] = {'\0'};
    tox_getname(m, friendnum, name);
    snprintf(ret.name, sizeof(ret.name), "%s", name);

    ChatContext *chatwin = calloc(1, sizeof(ChatContext));
    StatusBar *stb = calloc(1, sizeof(StatusBar));

    if (stb != NULL && chatwin != NULL) {
        ret.chatwin = chatwin;
        ret.stb = stb;
    } else {
        endwin();
        fprintf(stderr, "calloc() failed. Aborting...\n");
        exit(EXIT_FAILURE);
    }

    ret.prompt = prompt;
    ret.num = friendnum;

    return ret;
}

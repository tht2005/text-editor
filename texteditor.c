#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

/*** DEFINE ***/

#define 	NAME		"Text Editor"
#define		VERSION		"0.0.1"

#define 	MAX_CHAR	1000006
#define		MAX_LINE	1003

#define 	IS			STDIN_FILENO
#define 	OS			STDOUT_FILENO

#define 	ESC			('\x1b')
#define 	CTRL_(k)	((k) & 0x1f)

#define		WIDTH		(E.w - 1)
#define 	HEIGHT		(E.h - 1)
#define 	BLANK		12

enum {
	BACKSPACE = 127,
	ARROW_UP = 1000,
	ARROW_DOWN,
	ARROW_LEFT,
	ARROW_RIGHT,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

/*** DEBUG ***/

int CC(int code, const char* msg) {
	if(code < 0) {
		write(OS, "\x1b[H\x1b[2J", 7);
		perror(msg);
		exit(1);
	}
	return code;
}
void *CP(void *ptr, const char *msg) {
	if(!ptr) {
		write(OS, "\x1b[H\x1b[2J", 7);
		perror(msg);
		exit(1);
	}
	return ptr;
}

/*** GLOBAL ***/

FILE *textFile = NULL;
char fileContent[MAX_CHAR], *fileName=NULL;

struct terminal_properties {
	int top_row, left_col;
	int cx, cy;
	int w, h;
	int notSave;
} E;

struct buffer {
	char *b;
	int len;
	int max_len;
};

#define BUF_INIT { NULL, 0, 1 }

void append(struct buffer *bf, const char *s, int len) {
	int new_max_len = bf->max_len;	
	while(new_max_len < bf->len + len)
		new_max_len <<= 1;
	
	char *new = CP((void*)realloc(bf->b, new_max_len), "buffer::append memory alloc");
	memcpy(new + bf->len, s, len);
	bf->b = new;
	bf->len += len;
	bf->max_len = new_max_len;
}

void insert(struct buffer *bf, int pos, int c) {
	if(pos < 0 || pos > bf->len) {
		pos = E.cx = bf->len;
	}	
	append(bf, " ", 1);
	memmove(&bf->b[pos + 1], &bf->b[pos], bf->len - pos - 1);
	bf->b[pos] = c;
}

int nLine = 0;
struct buffer lineContent[MAX_LINE];
void freeBuffer() {
	for(int i = 0; i < MAX_LINE; ++i) {
		free(lineContent[i].b);
	}
	free(fileName);
}

void dataInit() {
	for(int i = 0; i < MAX_LINE; ++i) {
		lineContent[i].b = (char*)malloc(sizeof(char) * 5);
		lineContent[i].len = 0;
		lineContent[i].max_len = 5;
	}
	atexit(freeBuffer);
}

struct termios orig;


/*** FILE ***/

void editorPrintFile(struct buffer*);

void fileInit() {
	fseek(textFile, 0L, SEEK_END);
	int sz = ftell(textFile);
	fseek(textFile, 0L, SEEK_SET);	

	fread(fileContent, 1, sz, textFile);
	fclose(textFile);
	textFile = 1;

	if(strlen(fileContent) != sz) {
		CC(-1, "assert failed");
	}

	int last = 0;
	for(int i = 0; i < sz; ++i) {
		if(fileContent[i] == '\n') {
			append(&lineContent[nLine], &fileContent[last], i - last);		
			last = i + 1;
			++nLine;
		}
	}	
	if(last != sz) {
		append(&lineContent[nLine], &fileContent[last], sz - last);
		++nLine;
	}

	struct buffer bf = BUF_INIT;
	editorPrintFile(&bf);
	free(bf.b);
}

void fileSave() {
	if(fileName == NULL)
		return;
	int ptr = 0;
	for(int i = 0; i < nLine; ++i) {
		memcpy(&fileContent[ptr], lineContent[i].b, lineContent[i].len);
		ptr += lineContent[i].len;
		if(i < nLine - 1) {
			fileContent[ptr++] = '\n';
		}
	}
	fileContent[ptr] = 0;
	FILE* f = CP(fopen(fileName, "w"), "can't open file to save");
	fprintf(f, "%s", fileContent);

	E.notSave = 0;
}

/*** TERMINAL ***/

void disableRawMode() {
	CC(tcsetattr(IS, TCSAFLUSH, &orig), "tcsetattr");
}
void enableRawMode() {
	CC(tcgetattr(IS, &orig), "tcgetattr");	
	atexit(disableRawMode);

	struct termios raw;
	tcgetattr(IS, &raw);
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	CC(tcsetattr(IS, TCSAFLUSH, &raw), "tcsetattr");

}

void appendLine(struct buffer *bf, const char* msg) {
	int padding = (WIDTH - strlen(msg)) / 2;
	while(padding--) {
		append(bf, " ", 1);
	}
	append(bf, msg, strlen(msg));
}

void editorPrintScreenDemo(struct buffer *bf) {
	for(int i = 0; i < HEIGHT; ++i) {
		if(i == HEIGHT / 3) {
			appendLine(bf, NAME" --- version "VERSION);
		}
		else if(i == HEIGHT / 3 + 1) {
			appendLine(bf, "Use command 'texteditor text.txt' to edit file.");
		}
		else if(i == HEIGHT / 3 + 2) {
			appendLine(bf, "Ctrl + Q to quit.");
		}
		else {
			append(bf, "~", 1);
		}
		append(bf, "\x1b[K", 3);
		append(bf, "\r\n", 2);
	}
}

void editorPrintFile(struct buffer *bf) {
	// print the rectangle (E.top_row, E.left_col) - (E.top_row + HEIGHT - 1, E.left_col + WIDTH - 1) 
	for(int line = E.top_row; line < E.top_row + HEIGHT; ++line) {
		// temporary print full line
		if(line < nLine) {
			append(bf, lineContent[line].b, lineContent[line].len);
		}
		else {
			append(bf, "~", 1);
		}
		append(bf, "\x1b[K", 3);
		append(bf, "\r\n", 2);
	}	
}

void editorInsertRow(int pos, const char *s) {
	if(pos < nLine) {
		char *ptr = lineContent[nLine].b;
		memmove(&lineContent[pos + 1], &lineContent[pos], (nLine - pos) * sizeof(struct buffer));
		lineContent[pos].b = ptr;
	}

	lineContent[pos].len = 0;
	append(&lineContent[pos], s, strlen(s));
	++nLine;
}
void editorDeleteRow(int row) {
	char *ptr = lineContent[row].b;
	memmove(&lineContent[row], &lineContent[row + 1], (nLine - 1 - row) * sizeof(struct buffer));
	--nLine;
	lineContent[nLine].b = ptr;
}

void editorInsertChar(int c) {
	if(E.top_row + E.cy >= nLine) {
		E.cy = nLine - E.top_row;
		E.cx = 0;
		editorInsertRow(nLine, "");
	}
	insert(&lineContent[E.top_row + E.cy], E.left_col + E.cx, c);
	++E.cx;
}
void editorDeleteChar() {
	int cur_row = E.top_row + E.cy;
	if(E.cx >= lineContent[cur_row].len)
		E.cx = lineContent[cur_row].len;
	if(E.cx < lineContent[cur_row].len)
		memmove(&lineContent[cur_row].b[E.cx - 1], &lineContent[cur_row].b[E.cx], lineContent[cur_row].len - 1 - E.cx);	
	--E.cx;
	--lineContent[cur_row].len;
}

void editorCursorMove(int);

void editorBackspace() {
	int cur_row = E.top_row + E.cy;
	if(cur_row < 0 || cur_row >= nLine)
		return;

	if(E.cx == 0 || lineContent[cur_row].len == 0) {
		if(cur_row > 0) {
			editorCursorMove(ARROW_UP);	
			E.cx = lineContent[cur_row - 1].len;
			append(&lineContent[cur_row - 1], lineContent[cur_row].b, lineContent[cur_row].len);
			editorDeleteRow(cur_row);
		}	
	}	
	else {
		editorDeleteChar();
	}
}
void editorDelete() {
	int cur_row = E.top_row + E.cy;
	if(cur_row < 0 || cur_row >= nLine)
		return;
	if(E.cx >= lineContent[cur_row].len)
		return;
	editorCursorMove(ARROW_RIGHT);
	editorBackspace();
}

void editorEnter() {
	int cur_row = E.top_row + E.cy;
	if(cur_row < 0 || cur_row >= nLine)
		return;
	if(E.cx < 0 || E.cx >= lineContent[cur_row].len)
		E.cx = lineContent[cur_row].len;

	lineContent[cur_row].b[lineContent[cur_row].len] = 0;
	editorInsertRow(cur_row + 1, &lineContent[cur_row].b[E.cx]);	
	lineContent[cur_row].len = E.cx;
	editorCursorMove(ARROW_DOWN);
	E.cx = 0;
}

void editorPrintScreen(struct buffer *bf) {
	if(textFile) {
		editorPrintFile(bf);		
	}
	else {
		editorPrintScreenDemo(bf);
	}

	// last line decoration
	append(bf, "-- INSERT --", 12);
	if(E.notSave) {
		append(bf, " (NOT SAVED)", 12);
	}
	append(bf, "\x1b[K", 3);
}
void editorRefresh() {
	struct buffer bf = BUF_INIT;
	append(&bf, "\x1b[?25l", 6);
	append(&bf, "\x1b[H", 3);
	editorPrintScreen(&bf);	

	static char buf[50];
	sprintf(buf, "\x1b[%d;%dH", E.cy + 1, E.cx + 1);	
	append(&bf, buf, strlen(buf));

	append(&bf, "\x1b[?25h", 6);
	write(OS, bf.b, bf.len);
	free(bf.b);
}

void editorCursorMove(int key) {
	switch(key) {
		case ARROW_UP:
			if(E.cy != 0) {
				--E.cy;
			}
			else if(E.top_row != 0) {
				--E.top_row;
			}
			break;
		case ARROW_DOWN:
			if(E.top_row + E.cy < nLine + BLANK) {
				if(E.cy == HEIGHT - 1) {
					++E.top_row;
				}
				else {
					++E.cy;
				}
			}
			break;
		case ARROW_LEFT:
			if(E.cx != 0) {
				--E.cx;
			}
			break;
		case ARROW_RIGHT:
			if(E.cx != WIDTH - 1) {
				++E.cx;
			}
			break;
	}
}

int keyInputHandler() {			// Handle complex input and convert to int
	char c;
	while(CC(read(IS, &c, 1), "read") != 1);	

	if(c == ESC) {
		static char buf[10];	
		if(read(IS, &buf[0], 1) != 1) return ESC;
		if(read(IS, &buf[1], 1) != 1) return ESC;	
		if(buf[0] == '[') {
			if('0' <= buf[1] && buf[1] <= '9') {
				if (read(IS, &buf[2], 1) != 1) return ESC;
				if (buf[2] == '~') {
					switch (buf[1]) {
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			}	
			else {
				switch(buf[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		}
		else if(buf[0] == 'O') {
			switch (buf[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}
		return ESC;
	}
	return c;
}
void editorGetKey() {
	int key = keyInputHandler();
	switch(key) {
		case '\r':
			E.notSave = 1;
			editorEnter();
			break;

		case CTRL_('Q'):
			write(OS, "\x1b[H\x1b[2J", 7);
			exit(0);
			break;

		case CTRL_('S'):
			fileSave();	
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
				int times = HEIGHT;
				while (times--)
					editorCursorMove(key == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;

		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			E.cx = WIDTH - 1;
			break;

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorCursorMove(key);					
			break;
		
		case CTRL_('H'):
			break;
		case BACKSPACE:
			E.notSave = 1;
			editorBackspace();
			break;
		case DEL_KEY:
			E.notSave = 1;
			editorDelete();			
			break;

		case ESC:
		case CTRL_('L'):
			break;

		default:
			E.notSave = 1;
			editorInsertChar(key);
			break;
	}
}

int getEditorSize(int *cols, int *rows) {
	write(OS, "\x1b[999B\x1b[999C", 12);			// move cursor to the bottom right
	write(OS, "\x1b[6n", 4);						// request cursor position
	static char buf[50];
	int i = 0;
	for(;; ++i)
		if(read(IS, &buf[i], 1) != 1 || buf[i] == 'R') {
			break;
		}
	buf[i] = 0;
	if(buf[0] != ESC || buf[1] != '[') return -1;
	if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
	return 0;
}
void editorInit() {
	E.top_row = E.left_col = 0;
	E.cx = E.cy = 0;
	E.notSave = 0;
	CC(getEditorSize(&E.w, &E.h), "getEditorSize");	
}

int main(int argc, char **argv) {
	if(argc > 2) {
		printf("Invalid number of arguments\n");
		return 1;
	}
	else if(argc == 2) {
		textFile = CP((void*)fopen(argv[1], "r"), "File not exists");
		fileName = (char*)malloc(sizeof(argv[1]));
		memcpy(fileName, argv[1], sizeof(argv[1]));
	}

	enableRawMode();
	editorInit();
	dataInit();

	if(textFile) {
		fileInit(); //must be placed after editorInit() and enableRawMode() and dataInit()
	}

	while(1) {
		editorRefresh();
		editorGetKey();
	}
	return 0;
}
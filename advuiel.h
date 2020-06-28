#ifndef _ADVUIEL_H_
#define _ADVUIEL_H_

#include <ncurses.h>
#include <string.h>

#define OUTPUT_BUFFER_SIZE 1024
#define LINE_BUFFER_SIZE 1024
#define MAX_LIST_ITEMS 256
#define LIST_ITEM_SIZE 64

/* utility */
void removeCharAt(char *str, int *length, int pos);
void insertCharAt(char *str, int *length, int pos, char c);

/* ui - general */
WINDOW *createNewWindow(int height, int width, int y, int x, bool borders);
void getPadDisplayDimensions(WINDOW *window, WINDOW *pad, int *padPosY, int *padPosX, int *padSizeY, int *padSizeX);

/* ui - scrollable output */
typedef struct {
	WINDOW *window;
	WINDOW *pad;
	int scrollPosition;
	int previousPadSize;
} outputField;

void createOutputField(outputField *field, int height, int width, int y, int x);
void refreshOutputField(outputField *field);
void triggerOutputFieldEvent(outputField *field, int c);
void deleteOutputField(outputField *field);

/* ui - scrollable input */
typedef struct {
	int position;
	int length;
	char buffer[LINE_BUFFER_SIZE];
} lineBuffer;

typedef struct {
	WINDOW *window;
	WINDOW *pad;
	lineBuffer lineBuffer;
} inputField;

void createInputField(inputField *field, int width, int y, int x);
void refreshInputField(inputField *field);
void triggerInputFieldEvent(inputField *field, int c);
void deleteInputField(inputField *field);

/* ui - scrollable list */
typedef struct {
	int position;
	int length;
	char items[MAX_LIST_ITEMS][LIST_ITEM_SIZE];
} listBuffer;

typedef struct {
	WINDOW *window;
	WINDOW *pad;
	listBuffer listBuffer;
	int scrollPosition;
} listField;

void createListField(listField *field, int height, int width, int y, int x);
void refreshListField(listField *field);
void addListFieldItem(listField *field, char *item);
void removeListFieldItem(listField *field, char *item);
void replaceListFieldItem(listField *field, char *itemOld, char *itemNew);
void focusListField(listField *field);
void unfocusListField(listField *field);
void triggerListFieldEvent(listField *field, int c);
void deleteListField(listField *field);

/* ui - button */
typedef struct {
	WINDOW *window;
} button;

void createButton(button *btn, char *labelText, int y, int x);
void focusButton(button *btn);
void unfocusButton(button *btn);
void deleteButton(button *btn);

/* ui - label */
typedef struct {
	WINDOW *window;
} label;

void createLabel(label *lbl, char *labelText, int y, int x);
void updateLabel(label *lbl, char *newLabelText);
void deleteLabel(label *lbl);

#endif

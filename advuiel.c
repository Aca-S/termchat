#include <ncurses.h>
#include <string.h>
#include "advuiel.h"

void removeCharAt(char *str, int *length, int pos) {
	for(int i = pos; i < *length - 1; i++)
		str[i] = str[i + 1];
	*length = *length - 1;
}

void insertCharAt(char *str, int *length, int pos, char c) {
	for(int i = *length; i > pos; i--)
		str[i] = str[i - 1];
	str[pos] = c;
	*length = *length + 1;
}

WINDOW *createNewWindow(int height, int width, int y, int x, bool borders) {
	WINDOW *newWindow = newwin(height, width, y, x);
	if(borders)
		box(newWindow, 0, 0);
	wrefresh(newWindow);
	return newWindow;
}

void getPadDisplayDimensions(WINDOW *window, WINDOW *pad, int *padPosY, int *padPosX, int *padSizeY, int *padSizeX) {
	int windowPosY, windowPosX;
	getbegyx(window, windowPosY, windowPosX);
	int windowSizeY, windowSizeX;
	getmaxyx(window, windowSizeY, windowSizeX);
	*padPosY = windowPosY + 1;
	*padPosX = windowPosX + 1;
	*padSizeY = windowSizeY - 2;
	*padSizeX = windowSizeX - 2;
}

void createOutputField(outputField *field, int height, int width, int y, int x) {
	field->window = createNewWindow(height, width, y, x, TRUE);
	field->pad = newpad(OUTPUT_BUFFER_SIZE, width - 2);
	keypad(field->pad, TRUE);
	field->scrollPosition = 0;
	field->previousPadSize = 0;
}

void refreshOutputField(outputField *field) {
	int padPosY, padPosX, padSizeY, padSizeX;
	getPadDisplayDimensions(field->window, field->pad, &padPosY, &padPosX, &padSizeY, &padSizeX);
	int padRows, padColumns;
	getyx(field->pad, padRows, padColumns);
	prefresh(field->pad, padRows - padSizeY - field->scrollPosition, 0, padPosY, padPosX, padPosY + padSizeY - 1, padPosX + padSizeX - 1);
}

void triggerOutputFieldEvent(outputField *field, int c) {
	int padPosY, padPosX, padSizeY, padSizeX;
	getPadDisplayDimensions(field->window, field->pad, &padPosY, &padPosX, &padSizeY, &padSizeX);
	int padRows, padColumns;
	getyx(field->pad, padRows, padColumns);
	if(field->scrollPosition != 0)
		(field->scrollPosition) += padRows - field->previousPadSize;
	switch(c)
	{
		case KEY_UP:
			if(padRows - field->scrollPosition > padSizeY)
				field->scrollPosition++;
			break;
		case KEY_DOWN:
			if(field->scrollPosition > 0)
				field->scrollPosition--;
			break;
	}
	field->previousPadSize = padRows;
	refreshOutputField(field);
}

void deleteOutputField(outputField *field) {
	delwin(field->window);
	delwin(field->pad);
}

void createInputField(inputField *field, int width, int y, int x) {
	field->window = createNewWindow(3, width, y, x, TRUE);
	field->pad = newpad(1, LINE_BUFFER_SIZE);
	keypad(field->pad, TRUE);
	field->lineBuffer.position = field->lineBuffer.length = 0;
}

void refreshInputField(inputField *field) {
	int padPosY, padPosX, padSizeY, padSizeX;
	getPadDisplayDimensions(field->window, field->pad, &padPosY, &padPosX, &padSizeY, &padSizeX);
	prefresh(field->pad, 0, field->lineBuffer.position - padSizeX + 1, padPosY, padPosX, padPosY, padPosX + padSizeX - 1);
}

void triggerInputFieldEvent(inputField *field, int c) {
	int padPosY, padPosX, padSizeY, padSizeX;
	getPadDisplayDimensions(field->window, field->pad, &padPosY, &padPosX, &padSizeY, &padSizeX);
	switch(c)
	{
		case '\n': case KEY_ENTER:
			field->lineBuffer.buffer[field->lineBuffer.length] = '\0';
			werase(field->pad);
			field->lineBuffer.position = 0;
			field->lineBuffer.length = 0;
			break;
		case 8: case 127: case KEY_BACKSPACE:
			if(field->lineBuffer.position != 0 && field->lineBuffer.length != 0)
			{
				field->lineBuffer.position--;
				removeCharAt(field->lineBuffer.buffer, &(field->lineBuffer.length), field->lineBuffer.position);
				field->lineBuffer.buffer[field->lineBuffer.length] = '\0';
			}
			wmove(field->pad, 0, field->lineBuffer.position);
			wclrtoeol(field->pad);
			waddstr(field->pad, field->lineBuffer.buffer + field->lineBuffer.position);
			wmove(field->pad, 0, field->lineBuffer.position);
			break;
		case KEY_LEFT:
			if(field->lineBuffer.position != 0)
			{
				field->lineBuffer.position--;
				wmove(field->pad, 0, field->lineBuffer.position);
			}
			break;
		case KEY_RIGHT:
			if(field->lineBuffer.position < field->lineBuffer.length)
			{
				field->lineBuffer.position++;
				wmove(field->pad, 0, field->lineBuffer.position);
			}
			break;
		default:
			if(field->lineBuffer.length < LINE_BUFFER_SIZE - 1 && c >= 32 && c <= 127)
			{
				insertCharAt(field->lineBuffer.buffer, &(field->lineBuffer.length), field->lineBuffer.position, c);
				field->lineBuffer.buffer[field->lineBuffer.length] = '\0';
				wclrtoeol(field->pad);
				waddstr(field->pad, field->lineBuffer.buffer + field->lineBuffer.position);
				field->lineBuffer.position++;
				wmove(field->pad, 0, field->lineBuffer.position);
			}
			break;
	}
	refreshInputField(field);
}

void deleteInputField(inputField *field) {
	delwin(field->window);
	delwin(field->pad);
}

void createListField(listField *field, int height, int width, int y, int x) {
	field->window = createNewWindow(height, width, y, x, TRUE);
	int maxPadRows = LIST_ITEM_SIZE / (width - 2) * MAX_LIST_ITEMS;
	field->pad = newpad(maxPadRows, width - 2);
	keypad(field->pad, TRUE);
	field->listBuffer.position = field->listBuffer.length = 0;
	field->scrollPosition = 0;
}

void refreshListField(listField *field) {
	int padPosY, padPosX, padSizeY, padSizeX;
	getPadDisplayDimensions(field->window, field->pad, &padPosY, &padPosX, &padSizeY, &padSizeX);
	int padRows, padColumns;
	getyx(field->pad, padRows, padColumns);
	prefresh(field->pad, padRows - padSizeY + 1, 0, padPosY, padPosX, padPosY + padSizeY - 1, padPosX + padSizeX - 1);
}

void addListFieldItem(listField *field, char *item) {
	int padPosY, padPosX, padSizeY, padSizeX;
	getPadDisplayDimensions(field->window, field->pad, &padPosY, &padPosX, &padSizeY, &padSizeX);
	int padRows, padColumns;
	getyx(field->pad, padRows, padColumns);
	strcpy(field->listBuffer.items[field->listBuffer.length], item);
	field->listBuffer.length++;
	int itemRowPosition = 0;
	for(int i = 0; i < field->listBuffer.length - 1; i++)
	{
		int itemStrLen = strlen(field->listBuffer.items[i]);
		itemRowPosition += itemStrLen / padSizeX + 1;
	}
	wmove(field->pad, itemRowPosition, 0);
	wprintw(field->pad, "%s\n", item);
	wmove(field->pad, padRows, 0);
	refreshListField(field);
}

void removeListFieldItem(listField *field, char *item) {
	int padPosY, padPosX, padSizeY, padSizeX;
	getPadDisplayDimensions(field->window, field->pad, &padPosY, &padPosX, &padSizeY, &padSizeX);
	int padRows, padColumns;
	getyx(field->pad, padRows, padColumns);
	int searchedStrLen = strlen(item);
	int itemRowPosition = 0;
	for(int i = 0; i < field->listBuffer.length; i++)
	{
		int itemStrLen = strlen(field->listBuffer.items[i]);
		if(itemStrLen == searchedStrLen && strcmp(item, field->listBuffer.items[i]) == 0)
		{
			for(int j = i; j < field->listBuffer.length - 1; j++)
				strcpy(field->listBuffer.items[j], field->listBuffer.items[j + 1]);
			field->listBuffer.length--;
			wmove(field->pad, itemRowPosition, 0);
			wclrtobot(field->pad);
			for(int j = i; j < field->listBuffer.length; j++)
				wprintw(field->pad, "%s\n", field->listBuffer.items[j]);
			break;
		}
		else
			itemRowPosition += itemStrLen / padSizeX + 1;
	}
	wmove(field->pad, padRows, 0);
	refreshListField(field);
}

void replaceListFieldItem(listField *field, char *itemOld, char *itemNew) {
	int padPosY, padPosX, padSizeY, padSizeX;
	getPadDisplayDimensions(field->window, field->pad, &padPosY, &padPosX, &padSizeY, &padSizeX);
	int padRows, padColumns;
	getyx(field->pad, padRows, padColumns);
	int searchedStrLen = strlen(itemOld);
	int itemRowPosition = 0;
	for(int i = 0; i < field->listBuffer.length; i++)
	{
		int itemStrLen = strlen(field->listBuffer.items[i]);
		if(itemStrLen == searchedStrLen && strcmp(itemOld, field->listBuffer.items[i]) == 0)
		{
			strcpy(field->listBuffer.items[i], itemNew);
			wmove(field->pad, itemRowPosition, 0);
			wclrtobot(field->pad);
			for(int j = i; j < field->listBuffer.length; j++)
				wprintw(field->pad, "%s\n", field->listBuffer.items[j]);
			break;
		}
		else
			itemRowPosition += itemStrLen / padSizeX + 1;
	}
	wmove(field->pad, padRows, 0);
	refreshListField(field);
}

void focusListField(listField *field) {
	int padPosY, padPosX, padSizeY, padSizeX;
	getPadDisplayDimensions(field->window, field->pad, &padPosY, &padPosX, &padSizeY, &padSizeX);
	int itemRowLength = strlen(field->listBuffer.items[field->listBuffer.position]) / padSizeX + 1;
	for(int i = 0; i < itemRowLength; i++)
		mvwchgat(field->pad, field->scrollPosition + i, 0, -1, A_REVERSE, 0, NULL);
	refreshListField(field);
}

void unfocusListField(listField *field) {
	int padPosY, padPosX, padSizeY, padSizeX;
	getPadDisplayDimensions(field->window, field->pad, &padPosY, &padPosX, &padSizeY, &padSizeX);
	int itemRowLength = strlen(field->listBuffer.items[field->listBuffer.position]) / padSizeX + 1;
	for(int i = 0; i < itemRowLength; i++)
		mvwchgat(field->pad, field->scrollPosition + i, 0, -1, 0, 0, NULL);
	refreshListField(field);
}

void triggerListFieldEvent(listField *field, int c) {
	int padPosY, padPosX, padSizeY, padSizeX;
	getPadDisplayDimensions(field->window, field->pad, &padPosY, &padPosX, &padSizeY, &padSizeX);
	int itemRowLength;
	switch(c)
	{
		case KEY_UP:
			if(field->listBuffer.position > 0)
			{
				unfocusListField(field);
				itemRowLength = strlen(field->listBuffer.items[field->listBuffer.position - 1]) / padSizeX + 1;
				field->scrollPosition = field->scrollPosition - itemRowLength;
				field->listBuffer.position--;
				focusListField(field);
			}
			break;
		case KEY_DOWN:
			if(field->listBuffer.position < field->listBuffer.length - 1)
			{
				unfocusListField(field);
				itemRowLength = strlen(field->listBuffer.items[field->listBuffer.position]) / padSizeX + 1;
				field->scrollPosition = field->scrollPosition + itemRowLength;
				field->listBuffer.position++;
				focusListField(field);
			}
			break;
	}
}

void deleteListField(listField *field) {
	delwin(field->window);
	delwin(field->pad);
}

void createButton(button *btn, char *labelText, int y, int x) {
	btn->window = createNewWindow(3, strlen(labelText) + 2, y, x, TRUE);
	keypad(btn->window, TRUE);
	mvwaddstr(btn->window, 1, 1, labelText);
	wrefresh(btn->window);
}

void focusButton(button *btn) {
	int windowSizeY, windowSizeX;
	getmaxyx(btn->window, windowSizeY, windowSizeX);
	curs_set(0);
	mvwchgat(btn->window, 1, 1, windowSizeX - 2, A_REVERSE, 0, NULL);
	wrefresh(btn->window);
}

void unfocusButton(button *btn) {
	int windowSizeY, windowSizeX;
	getmaxyx(btn->window, windowSizeY, windowSizeX);
	curs_set(1);
	mvwchgat(btn->window, 1, 1, windowSizeX - 2, 0, 0, NULL);
	wrefresh(btn->window);
}

void deleteButton(button *btn) {
	delwin(btn->window);
}

void createLabel(label *lbl, char *labelText, int y, int x) {
	lbl->window = createNewWindow(1, strlen(labelText), y, x, FALSE);
	if(labelText != NULL)
		waddstr(lbl->window, labelText);
	wrefresh(lbl->window);
}

void updateLabel(label *lbl, char *newLabelText) {
	werase(lbl->window);
	wrefresh(lbl->window);
	wresize(lbl->window, 1, strlen(newLabelText));
	waddstr(lbl->window, newLabelText);
	wrefresh(lbl->window);
}

void deleteLabel(label *lbl) {
	delwin(lbl->window);
}

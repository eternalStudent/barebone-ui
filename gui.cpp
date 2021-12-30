#define FLIPY(y)        (ui.windowElement->height-(y)-1)         

#define UI_MOVABLE          1
#define UI_RESIZABLE        2
#define UI_CLICKABLE        4
#define UI_HIDDEN			8
#define UI_CENTER           16

#include "font.cpp"

struct UIText {
	union {
		Point2i pos;
		struct {int32 x, y;};
	};

	Font* font;
	String string;
};

struct UIStyle {
	float32 radius;
	uint32 background;
	float32 borderWidth;
	uint32 borderColor;
};

union UIBox {
	struct {int32 x, y, width, height;};
	struct {Point2i pos; Dimensions2i dim;};
};

struct UIImage {
	union {
		struct {int32 x, y, width, height;};
		struct {Point2i pos; Dimensions2i dim;};
		UIBox box;
	};

	TextureId atlas;
	Box2 crop;
};

struct UIElement {
	union {
		struct {int32 x, y, width, height;};
		struct {Point2i pos; Dimensions2i dim;};
		UIBox box;
	};

	union {
		struct {
			float32 radius;
			uint32 background;
			float32 borderWidth;
			uint32 borderColor;
		};
		UIStyle style;
	};

	String name; // NOTE: for debugging

	uint32 flags;

	void (*onClick)(UIElement*);
	void (*onHover)(UIElement*);
	void (*onResize)(UIElement*);
	void (*onMove)(UIElement*);

	UIElement* parent;
	UIElement* first;
	UIElement* last;
	UIElement* next;
	UIElement* prev;

	UIText* text;
	UIImage* image;
	void* context;
};

struct {
	union {
		UIElement* elements;
		UIElement* windowElement;
	};
	int32 capacity;
	int32 elementCount;

	UIStyle originalStyle;

	Window window;

	UIElement* active;
	Point2i originalPos;
	Point2i grabPos;

	bool isGrabbing;
	bool isResizing;
	bool isPressed;
	bool isBottomRight;

	Arena* arena;
} ui;

Box2i GetAbsolutePosition(UIElement* element) {
	if (element->parent == 0)
		return Box2i{element->x, element->y, element->x+element->width, element->y+element->height};

	Point2i parent = GetAbsolutePosition(element->parent).p0;
	Box2i result;
	result.p0 = MOVE2(element->pos, parent);
	result.p1 = MOVE2(result.p0, element->dim);
	return result;
}

bool IsInBottomRight(UIElement* element, Point2i pos) {
	Box2i box = GetAbsolutePosition(element);
	return box.x1-4 <= pos.x && box.y1-4 <= pos.y;
}

void SetPosition(UIElement* element, int32 x, int32 y) {
	if (!element->parent) {
		element->pos = {x,y};
	}
	else {
		UIElement* parent = element->parent;

		if (x < 0) element->x = 0;
		else if (x+element->width > parent->width) element->x = parent->width-element->width;
		else element->x = x;

		if (y < 0) element->y = 0;
		else if (y+element->height > parent->height) element->y = parent->height-element->height;
		else element->y = y;
	}
}

UIElement* GetElementByPosition(Point2i p) {
	UIElement* element = ui.windowElement;
	while (element->last) element = element->last;

	while (element) {
		if (!(element->flags & UI_HIDDEN)) {
			Box2i b = GetAbsolutePosition(element);
			if (INSIDE2(b, p)) break;
		}

		if (element->prev) {
			element = element->prev;
			while (element->last) element = element->last;
		}
		else element = element->parent;
	}

	if (element == ui.windowElement) return NULL;
	return element;
}

Point2i GetRelativePosition(Point2i cursorPos, UIElement* element) {
	Box2i absolute = GetAbsolutePosition(element);
	return {cursorPos.x - absolute.x0, cursorPos.y - absolute.y0};
}

void MoveToFront(UIElement* element) {
	if (element->parent->last == element) return;

	UIElement* parent = element->parent;
	UIElement* prev = element->prev;
	UIElement* next = element->next;

	if (parent->first == element) parent->first = next;

	if (prev) prev->next = next;
	next->prev = prev;
	element->prev = parent->last;
	element->next = NULL;
	parent->last->next = element;
	parent->last = element;
}

void HandleCursorPosition(Point2i cursorPos){
	UIElement* prev = ui.active;
	ui.isBottomRight = false;
	if (!ui.isResizing && !ui.isGrabbing) { // NOTE: this is the reason I keep ui.active
		ui.active = GetElementByPosition(cursorPos);
	}
	UIElement* element = ui.active;
	if (prev != element) {
		if (prev)       prev->style = ui.originalStyle;
		if (element)    ui.originalStyle = element->style;
	}
	OsSetCursorIcon(CUR_ARROW);
	if (element) {
		if(IsInBottomRight(element, cursorPos) && (element->flags & UI_RESIZABLE)){
			OsSetCursorIcon(CUR_RESIZE);
			ui.isBottomRight = true;
		}
		else {
			if (element->onHover) element->onHover(element);
			else if (element->flags & UI_CLICKABLE) OsSetCursorIcon(CUR_HAND);
			else if (element->flags & UI_MOVABLE) OsSetCursorIcon(CUR_MOVE);        
		}
	}
}

void HandleMouseEvent(Event mouseEvent, Point2i cursorPos) {
	UIElement* element = ui.active;
	if (mouseEvent == MOUSE_LDN && element){
		MoveToFront(element);
		if (element->flags & UI_CLICKABLE){
			ui.isPressed = true;
			if (element->onClick)
				element->onClick(element);
		}
		else {
			if (ui.isBottomRight && (element->flags & UI_RESIZABLE)) ui.isResizing = true;
			else if (element->flags & UI_MOVABLE) ui.isGrabbing = true;
			ui.grabPos = cursorPos;
			ui.originalPos = element->pos;
		}
	}
	else if (mouseEvent == MOUSE_LUP) {
		ui.isGrabbing = false;
		ui.isResizing = false;
		ui.isPressed = false;
	}
}

void UpdateActiveElement(Point2i cursorPos) {
	UIElement* element = ui.active;
	if (!element) return;

	// Handle grabbing
	if (ui.isGrabbing && 
			(cursorPos.x != ui.grabPos.x || cursorPos.y != ui.grabPos.y)){
		int32 newx  = ui.originalPos.x + cursorPos.x - ui.grabPos.x;
		int32 newy = ui.originalPos.y + cursorPos.y - ui.grabPos.y;
		SetPosition(element, newx, newy);
		if (element->onMove) element->onMove(element);
	}

	// Handle resizing
	if (ui.isResizing) {
		Point2i relativeCursorPos = GetRelativePosition(cursorPos, element->parent);
		int32 x0 = MIN(ui.originalPos.x, relativeCursorPos.x);
		int32 y0 = MIN(ui.originalPos.y, relativeCursorPos.y);
		int32 x1 = MAX(ui.originalPos.x, relativeCursorPos.x);
		int32 y1 = MAX(ui.originalPos.y, relativeCursorPos.y);

		element->x = MAX(x0, 0);
		element->y = MAX(y0, 0);
		element->width = MIN(x1 - element->x, element->parent->width - element->x);
		element->height = MIN(y1 - element->y, element->parent->height - element->y);

		if (element->onResize) element->onResize(element);
	}
}

void RenderText(UIText* text, Point2i parentPos) {
	if (!text) return;

	float32 x = (float32)(text->pos.x + parentPos.x);
	float32 y = FLIPY((float32)(text->pos.y + parentPos.y)) - text->font->height;
	DrawText(text->font, x, y, text->string);
}

void RenderImage(UIImage* image, Point2i parentPos) {
	if (!image) return;

	Point2i p0 = MOVE2(image->pos, parentPos);
	Box2 renderBox = Box2{(float32)p0.x, (float32)FLIPY(p0.y+image->height), (float32)(p0.x+image->width), (float32)FLIPY(p0.y)};
	DrawImage(image->atlas, image->crop, renderBox);
}

int32 __center(UIElement* element) {
	return (element->parent->width - element->width)/2;
}

void RenderElement(UIElement* element) {
	if (element->flags & UI_CENTER) element->x = __center(element);
	Box2i box = GetAbsolutePosition(element);
	Box2 renderBox = Box2{(float32)box.x0, (float32)FLIPY(box.y1), (float32)box.x1, (float32)FLIPY(box.y0)};
	if (element->radius) {
		DrawBox2Rounded(element->background, renderBox, element->radius);
		if (element->borderWidth && element->borderColor) 
			DrawBox2RoundedLines(element->borderColor, element->borderWidth, renderBox, element->radius);
	}
	else {
		DrawBox2(element->background, renderBox);
		if (element->borderWidth && element->borderColor) 
			DrawBox2Lines(element->borderColor, element->borderWidth, renderBox);
	}
	UIElement* child = element->first;
	while (child) {
		RenderElement(child);
		child = child->next;
	}
	RenderText(element->text, box.p0);
	RenderImage(element->image, box.p0);
}

// API
//-----------

Event UIHandleWindowEvents() {
	return OsHandleWindowEvents(ui.window);
}

Point2i UIGetCursorPosition() {
	return OsGetCursorPosition(ui.window);
}

bool UIEnterFullScreen() {
	return OsEnterFullScreen(ui.window);
}

void UIExitFullScreen() {
	OsExitFullScreen(ui.window);
}

void UIInit(Arena* persist, Arena* scratch) {
	int32 capacity = 100;
	ArenaAlign(persist, 8);
	void* buffer = ArenaAlloc(persist, capacity*sizeof(UIElement));
	memset(buffer, 0, capacity*sizeof(UIElement));
	ui.elements = (UIElement*) buffer;
	ui.capacity = capacity;
	ui.arena = scratch;
}

void UIRenderElements() {
	UIElement* child = ui.windowElement->first;
	while (child) {
		RenderElement(child);
		child = child->next;
	}
	RenderText(ui.windowElement->text, {0, 0});
	RenderImage(ui.windowElement->image, {0, 0});
}

void UISetWindowElement(Window window, uint32 background) {
	ui.elementCount = 1;
	ui.window = window;
	ui.windowElement->pos = {0, 0};
	ui.windowElement->background = background;
}

void UICreateWindow(Arena* arena, const char* title, Dimensions2i dimensions, uint32 background) {
	Window window = OsCreateWindow(title, dimensions.width, dimensions.height);
	GraphicsInit(arena, window);
	UISetWindowElement(window, background);
}

void UICreateWindowFullScreen(Arena* arena, const char* title, uint32 background) {
	int32 width, height;
	Window window = OsCreateWindowFullScreen(title, &width, &height);
	GraphicsInit(arena, window);
	UISetWindowElement(window, background);
}

UIElement* UICreateElement(UIElement* parent) {
	if (ui.elementCount == ui.capacity) return NULL;

	UIElement* element = &(ui.elements[ui.elementCount++]);
	if (parent == NULL) parent = ui.windowElement;

	if (parent->last) parent->last->next = element;
	else parent->first = element;
	element->prev = parent->last;
	parent->last = element;
	element->parent = parent;
	return element;
}

UIText* UICreateText(UIElement* parent) {
	if (ui.elementCount == ui.capacity) return NULL;
	UIText* text = (UIText*)&(ui.elements[ui.elementCount++]);
	if (parent) parent->text = text;
	else ui.windowElement->text = text;
	return text;
}

UIImage* UICreateImage(UIElement* parent) {
	if (ui.elementCount == ui.capacity) return NULL;
	UIImage* image = (UIImage*)&(ui.elements[ui.elementCount++]);
	if (parent) {
		parent->image = image;
		image->dim = parent->dim;
	}
	else ui.windowElement->image = image;
	return image;
}

void UIUpdateDimensions(Dimensions2i dimensions) {
	ui.windowElement->dim = dimensions;
}

bool UIUpdateElements(Event event, Point2i cursorPos){
	HandleCursorPosition(cursorPos);
	HandleMouseEvent(event, cursorPos);
	UpdateActiveElement(cursorPos);
	return ui.active != NULL;
}

// specific elements
//-------------------

UIBox __pad(UIElement* parent, int32 l, int32 r, int32 t, int32 b) {
	return UIBox{l, t, parent->width - (l+r), parent->height - (t+b)};
}

UIBox __pad(UIElement* parent, int32 pad) {
	return __pad(parent, pad, pad, pad, pad);
}

void __toggle(UIElement* e) {
	byte* context = (byte*)e->context;
	*context = !(*context);
	e->background = *context ? RGBA_DARKGREY : 0;
	ui.originalStyle = e->style;
}

UIElement* UICreateCheckbox(UIElement* parent, Font* font, String str, byte* context) {
	UIElement* wrapper = UICreateElement(parent);
	wrapper->width = 37+(int32)GetTextWidth(font, str);
	wrapper->height = 24;
	wrapper->name = STR("wrapper");

	UIElement* checkbox = UICreateElement(wrapper);
	checkbox->dim = {24, 24};
	checkbox->background = RGBA_LIGHTGREY;
	checkbox->radius = 6;
	checkbox->name = STR("checkbox");

	UIElement* check = UICreateElement(checkbox);
	check->box = __pad(checkbox, 4);
	check->flags = UI_CLICKABLE;
	check->radius = 3;
	check->onClick = __toggle;
	check->context = context;
	check->name = STR("check");

	UIText* text = UICreateText(wrapper);
	text->pos = {37, -4};
	text->font = font;
	text->string = str;

	return wrapper;
}

void __move_sideway(UIElement* e) {
	OsSetCursorIcon(CUR_MOVESIDE);
}

uint32 __get_background_color(UIElement* e) {
	uint32 result;
	do {
		result = e->parent->background;
		e = e->parent;
	} while(!(result & 0xff000000) && e);
	return result;
}

UIElement* UICreateSlider(UIElement* parent, int32 width) {
	UIElement* slider = UICreateElement(parent);
	slider->dim = {width, 16};
	slider->background = RGBA_LIGHTGREY;
	slider->borderWidth = 4;
	slider->borderColor = __get_background_color(slider);
	slider->name = STR("slider");

	UIElement* sled = UICreateElement(slider);
	sled->pos = {(width - 8)/2, 0};
	sled->dim = {8, 16};
	sled->radius = 4;
	sled->background = RGBA_WHITE;
	sled->flags = UI_MOVABLE;
	sled->onHover = __move_sideway;
	sled->name = STR("sled");

	return slider;
}

void __hover(UIElement* e) {
	e->background = RGBA_GREY;
	OsSetCursorIcon(CUR_HAND);
}

UIElement* UICreateButton(UIElement* parent, Dimensions2i dim) {
	UIElement* button = UICreateElement(parent);
	button->dim = dim;
	button->background = RGBA_LIGHTGREY;
	button->radius = 12.0f;
	button->borderWidth = 1;
	button->borderColor = RGBA_WHITE;
	button->onHover = __hover;
	button->flags = UI_CLICKABLE;
	button->name = STR("button");
	return button;
}

void __choose(UIElement* e) {
	UIElement* parent = e->parent;
	for (UIElement* child = parent->first; child != NULL; child = child->next) {
		child->background = RGBA_LIGHTGREY;
		child->onHover = __hover;
	}
	e->background = RGBA_DARKGREY;
	e->onHover = NULL;
	ui.originalStyle = e->style;
}

void __fit(UIElement* e) {
	for (UIElement* child = e->first; child != NULL; child = child->next) {
		Dimensions2i dim = child->dim;
		UIElement* body = child->first;
		body->height = e->height - (dim.height+1);
    	body->width = e->width-2;
	}
}

UIElement* UICreateTabControl(UIElement* parent, Dimensions2i dim) {
	UIElement* control = UICreateElement(parent);
	control->dim = dim;
	control->background = RGBA_LIGHTGREY;
	control->flags = UI_MOVABLE | UI_RESIZABLE;
	control->borderColor = RGBA_WHITE;
	control->borderWidth = 1;
	control->onResize = __fit;
	control->name = STR("tab control");

	return control;
}

UIElement* UICreateTab(UIElement* parent, Dimensions2i dim, String title, Font* font) {
	UIElement* last = parent->last;

	UIElement* header = UICreateElement(parent);
	header->x = last ? last->x+last->width+1 : 24;
	header->dim = dim;
	header->radius = 3;
	header->flags = UI_CLICKABLE;
	header->onHover = __hover;
	header->onClick = __choose;
	header->background = RGBA_LIGHTGREY;
	header->name = STR("tab header");

	UIText* text = UICreateText(header);
    text->string = title;
    text->x = 3;
    text->font = font;

    UIElement* body = UICreateElement(header);
    body->x = -header->x;
    body->y = dim.height;
    body->height = parent->height - (dim.height+1);
    body->width = parent->width-2;
    body->background = RGBA_DARKGREY;
    body->name = STR("tab body");

    return body;
}

void UISetActiveTab(UIElement* active) {
	MoveToFront(active->parent);
	__choose(active->parent);
}
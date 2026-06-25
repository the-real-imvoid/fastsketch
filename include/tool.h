//tool.h
#ifndef TOOL_H
#define TOOL_H

typedef enum
{
    TOOL_BRUSH,
    TOOL_ERASER,
    TOOL_LINE,
    TOOL_RECT,
    TOOL_CIRCLE,
    TOOL_FILL,
    TOOL_EYEDROPPER,
    TOOL_AIRBRUSH,
    TOOL_BRIGHTNESS,
    TOOL_FILTER,
    TOOL_SMUDGE,
    TOOL_MIXER,
    TOOL_PENCIL,
    TOOL_SELECTION
} Tool;

typedef struct
{
    Tool selected;
} Toolbar;

#endif

/*
 * imgui_extension.cpp - Native Dear ImGui UI extension with dynamic loading
 *
 * Uses C ABI (HavelCAPI.h) for stability.
 * GLFW, OpenGL, and ImGui are loaded dynamically at runtime via dlopen/dlsym.
 * No hard link-time dependency on ImGui/GLFW/OpenGL.
 */

#include "HavelCAPI.h"
#include "DynamicLoader.hpp"

#include <unordered_map>
#include <memory>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cfloat>

namespace {

/* Forward declarations for dynamic loading */
typedef struct GLFWwindow GLFWwindow;
typedef void* ImTextureID;
typedef unsigned int ImGuiID;
typedef long long ImGuiS64;
typedef unsigned long long ImGuiU64;

/* ImGui basic types */
struct ImVec2 {
    float x, y;
    ImVec2() : x(0.0f), y(0.0f) {}
    ImVec2(float _x, float _y) : x(_x), y(_y) {}
};

struct ImVec4 {
    float x, y, z, w;
    ImVec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    ImVec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

/* ImGui enums */
enum ImGuiCol_ {
    ImGuiCol_Text, ImGuiCol_TextDisabled, ImGuiCol_WindowBg, ImGuiCol_ChildBg,
    ImGuiCol_PopupBg, ImGuiCol_Border, ImGuiCol_BorderShadow, ImGuiCol_FrameBg,
    ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_TitleBg,
    ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed, ImGuiCol_MenuBarBg,
    ImGuiCol_ScrollbarBg, ImGuiCol_ScrollbarGrab, ImGuiCol_ScrollbarGrabHovered,
    ImGuiCol_ScrollbarGrabActive, ImGuiCol_CheckMark, ImGuiCol_SliderGrab,
    ImGuiCol_SliderGrabActive, ImGuiCol_Button, ImGuiCol_ButtonHovered,
    ImGuiCol_ButtonActive, ImGuiCol_Header, ImGuiCol_HeaderHovered,
    ImGuiCol_HeaderActive, ImGuiCol_Separator, ImGuiCol_SeparatorHovered,
    ImGuiCol_SeparatorActive, ImGuiCol_ResizeGrip, ImGuiCol_ResizeGripHovered,
    ImGuiCol_ResizeGripActive, ImGuiCol_Tab, ImGuiCol_TabHovered, ImGuiCol_TabActive,
    ImGuiCol_TabUnfocused, ImGuiCol_TabUnfocusedActive, ImGuiCol_PlotLines,
    ImGuiCol_PlotLinesHovered, ImGuiCol_PlotHistogram, ImGuiCol_PlotHistogramHovered,
    ImGuiCol_TableHeaderBg, ImGuiCol_TableBorderStrong, ImGuiCol_TableBorderLight,
    ImGuiCol_TableRowBg, ImGuiCol_TableRowBgAlt, ImGuiCol_TextSelectedBg,
    ImGuiCol_DragDropTarget, ImGuiCol_NavHighlight, ImGuiCol_NavWindowingHighlight,
    ImGuiCol_NavWindowingDimBg, ImGuiCol_ModalWindowDimBg, ImGuiCol_COUNT
};

enum ImGuiStyleVar_ {
    ImGuiStyleVar_Alpha, ImGuiStyleVar_DisabledAlpha, ImGuiStyleVar_WindowPadding,
    ImGuiStyleVar_WindowRounding, ImGuiStyleVar_WindowBorderSize,
    ImGuiStyleVar_WindowMinSize, ImGuiStyleVar_WindowTitleAlign,
    ImGuiStyleVar_ChildRounding, ImGuiStyleVar_ChildBorderSize,
    ImGuiStyleVar_PopupRounding, ImGuiStyleVar_PopupBorderSize,
    ImGuiStyleVar_FramePadding, ImGuiStyleVar_FrameRounding,
    ImGuiStyleVar_FrameBorderSize, ImGuiStyleVar_ItemSpacing,
    ImGuiStyleVar_ItemInnerSpacing, ImGuiStyleVar_IndentSpacing,
    ImGuiStyleVar_CellPadding, ImGuiStyleVar_ScrollbarSize,
    ImGuiStyleVar_ScrollbarRounding, ImGuiStyleVar_GrabMinSize,
    ImGuiStyleVar_GrabRounding, ImGuiStyleVar_TabRounding,
    ImGuiStyleVar_ButtonTextAlign, ImGuiStyleVar_SelectableTextAlign,
    ImGuiStyleVar_COUNT
};

enum ImGuiCond_ {
    ImGuiCond_None = 0, ImGuiCond_Always = 1 << 0, ImGuiCond_Once = 1 << 1,
    ImGuiCond_FirstUseEver = 1 << 2, ImGuiCond_Appearing = 1 << 3
};

enum ImGuiWindowFlags_ {
    ImGuiWindowFlags_None = 0, ImGuiWindowFlags_NoTitleBar = 1 << 0,
    ImGuiWindowFlags_NoResize = 1 << 1, ImGuiWindowFlags_NoMove = 1 << 2,
    ImGuiWindowFlags_NoScrollbar = 1 << 3, ImGuiWindowFlags_NoScrollWithMouse = 1 << 4,
    ImGuiWindowFlags_NoCollapse = 1 << 5, ImGuiWindowFlags_AlwaysAutoResize = 1 << 6,
    ImGuiWindowFlags_NoBackground = 1 << 7, ImGuiWindowFlags_NoSavedSettings = 1 << 8,
    ImGuiWindowFlags_NoMouseInputs = 1 << 9, ImGuiWindowFlags_MenuBar = 1 << 10,
    ImGuiWindowFlags_HorizontalScrollbar = 1 << 11, ImGuiWindowFlags_NoFocusOnAppearing = 1 << 12,
    ImGuiWindowFlags_NoBringToFrontOnFocus = 1 << 13, ImGuiWindowFlags_AlwaysVerticalScrollbar = 1 << 14,
    ImGuiWindowFlags_AlwaysHorizontalScrollbar = 1 << 15, ImGuiWindowFlags_NoNavInputs = 1 << 16,
    ImGuiWindowFlags_NoNavFocus = 1 << 17, ImGuiWindowFlags_UnsavedDocument = 1 << 18,
    ImGuiWindowFlags_NoNav = ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus,
    ImGuiWindowFlags_NoDecoration = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse,
    ImGuiWindowFlags_NoInputs = ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoNavInputs |
                                ImGuiWindowFlags_NoNavFocus
};

enum ImGuiTabBarFlags_ {
    ImGuiTabBarFlags_None = 0, ImGuiTabBarFlags_Reorderable = 1 << 0,
    ImGuiTabBarFlags_AutoSelectNewTabs = 1 << 1, ImGuiTabBarFlags_TabListPopupButton = 1 << 2,
    ImGuiTabBarFlags_NoCloseWithMiddleMouseButton = 1 << 3,
    ImGuiTabBarFlags_NoTabListScrollingButtons = 1 << 4,
    ImGuiTabBarFlags_NoTooltip = 1 << 5, ImGuiTabBarFlags_FittingPolicyResizeDown = 1 << 6,
    ImGuiTabBarFlags_FittingPolicyScroll = 1 << 7
};

enum ImGuiTabItemFlags_ {
    ImGuiTabItemFlags_None = 0, ImGuiTabItemFlags_UnsavedDocument = 1 << 0,
    ImGuiTabItemFlags_SetSelected = 1 << 1, ImGuiTabItemFlags_NoCloseWithMiddleMouseButton = 1 << 2,
    ImGuiTabItemFlags_NoPushId = 1 << 3, ImGuiTabItemFlags_NoTooltip = 1 << 4,
    ImGuiTabItemFlags_NoReorder = 1 << 5, ImGuiTabItemFlags_Leading = 1 << 6,
    ImGuiTabItemFlags_Trailing = 1 << 7
};

enum ImGuiTreeNodeFlags_ {
    ImGuiTreeNodeFlags_None = 0, ImGuiTreeNodeFlags_Selected = 1 << 0,
    ImGuiTreeNodeFlags_Framed = 1 << 1, ImGuiTreeNodeFlags_AllowItemOverlap = 1 << 2,
    ImGuiTreeNodeFlags_NoTreePushOnOpen = 1 << 3, ImGuiTreeNodeFlags_NoAutoOpenOnLog = 1 << 4,
    ImGuiTreeNodeFlags_DefaultOpen = 1 << 5, ImGuiTreeNodeFlags_OpenOnDoubleClick = 1 << 6,
    ImGuiTreeNodeFlags_OpenOnArrow = 1 << 7, ImGuiTreeNodeFlags_Leaf = 1 << 8,
    ImGuiTreeNodeFlags_Bullet = 1 << 9, ImGuiTreeNodeFlags_FramePadding = 1 << 10,
    ImGuiTreeNodeFlags_SpanAvailWidth = 1 << 11, ImGuiTreeNodeFlags_SpanFullWidth = 1 << 12,
    ImGuiTreeNodeFlags_SpanAllColumns = 1 << 13, ImGuiTreeNodeFlags_NavLeftJumpsBackHere = 1 << 14,
    ImGuiTreeNodeFlags_CollapsingHeader = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                          ImGuiTreeNodeFlags_NoAutoOpenOnLog
};

enum ImGuiSelectableFlags_ {
    ImGuiSelectableFlags_None = 0, ImGuiSelectableFlags_DontClosePopups = 1 << 0,
    ImGuiSelectableFlags_SpanAllColumns = 1 << 1, ImGuiSelectableFlags_AllowDoubleClick = 1 << 2,
    ImGuiSelectableFlags_Disabled = 1 << 3, ImGuiSelectableFlags_AllowItemOverlap = 1 << 4
};

enum ImGuiPopupFlags_ {
    ImGuiPopupFlags_None = 0, ImGuiPopupFlags_MouseButtonLeft = 0,
    ImGuiPopupFlags_MouseButtonRight = 1, ImGuiPopupFlags_MouseButtonMiddle = 2,
    ImGuiPopupFlags_MouseButtonMask = 0x1F, ImGuiPopupFlags_MouseButtonDefault = 1
};

enum ImGuiMouseButton_ {
    ImGuiMouseButton_Left = 0, ImGuiMouseButton_Right = 1, ImGuiMouseButton_Middle = 2,
    ImGuiMouseButton_COUNT = 5
};

/* ImGuiIO structure (partial) */
struct ImGuiIO {
    void* ConfigFlags;
    void* BackendFlags;
    float DeltaTime;
    float IniSavingRate;
    const char* IniFilename;
    const char* LogFilename;
    void* UserData;
    void* Fonts;
    float FontGlobalScale;
    bool FontAllowUserScaling;
    void* FontDefault;
    const char* DisplayVisibleMin;
    const char* DisplayVisibleMax;
    bool WantCaptureMouse;
    bool WantCaptureKeyboard;
    bool WantTextInput;
    bool WantSetMousePos;
    bool WantSaveIniSettings;
    int NavActive;
    int NavVisible;
    float Framerate;
    int MetricsRenderVertices;
    int MetricsRenderIndices;
    int MetricsRenderWindows;
    int MetricsActiveWindows;
    void* MouseDelta;
    ImVec2 MousePos;
    bool MouseDown[5];
    float MouseWheel;
    float MouseWheelH;
    void* KeyMap;
    bool KeyDown[512];
    bool KeyShift;
    bool KeyCtrl;
    bool KeyAlt;
    void* KeyRepeatDelay;
    void* KeyRepeatRate;
    bool WantCaptureMouseForPopup;
    bool WantCaptureKeyboardForPopup;
    ImVec2 MousePosPrev;
    ImVec2 MouseClickedPos[5];
    double MouseClickedTime[5];
    bool MouseClicked[5];
    bool MouseDoubleClicked[5];
    unsigned short MouseClickedCount[5];
    unsigned short MouseClickedLastCount[5];
    bool MouseReleased[5];
    bool MouseDownOwned[5];
    bool MouseDownOwnedUnlessPopupClose[5];
    bool MouseWheelRequestAxisSwap;
    bool MouseCtrlLeftAsRightClick;
    float MouseDownDuration[5];
    float MouseDownDurationPrev[5];
    ImVec2 MouseDragMaxDistanceAbs[5];
    float MouseDragMaxDistanceSqr[5];
    float PenPressure;
    bool AppFocusLost;
    short AppAcceptingEvents;
    void* BackendPlatformUserData;
    void* BackendRendererUserData;
    void* BackendLanguageUserData;
    const char* WantClipboardTextFn;
    const char* SetClipboardTextFn;
    void* ClipboardUserData;
    void* PlatformLocale;
    bool ConfigMacOSXBehaviors;
    bool ConfigInputTrickleEventQueue;
    bool ConfigInputTextCursorBlink;
    bool ConfigInputTextEnterKeepActive;
    bool ConfigDragClickToInputText;
    bool ConfigWindowsResizeFromEdges;
    bool ConfigWindowsMoveFromTitleBarOnly;
    float ConfigMemoryCompactTimer;
    float MouseDoubleClickTime;
    float MouseDoubleClickMaxDist;
    float MouseDragThreshold;
    float KeyRepeatDelay2;
    float KeyRepeatRate2;
    bool OptMacOSXAltBehavior;
    bool OptCursorBlink;
    bool OptInputMethodTray;
};

/* ImGui context opaque pointer */
typedef void ImGuiContext;

/* GLFW function pointer types */
typedef int (*GlfwInitFn)(void);
typedef void (*GlfwTerminateFn)(void);
typedef GLFWwindow* (*GlfwCreateWindowFn)(int width, int height, const char* title, void* monitor, void* share);
typedef void (*GlfwDestroyWindowFn)(GLFWwindow* window);
typedef void (*GlfwMakeContextCurrentFn)(GLFWwindow* window);
typedef void (*GlfwSwapIntervalFn)(int interval);
typedef void (*GlfwPollEventsFn)(void);
typedef void (*GlfwGetFramebufferSizeFn)(GLFWwindow* window, int* width, int* height);
typedef void (*GlfwSwapBuffersFn)(GLFWwindow* window);
typedef int (*GlfwWindowShouldCloseFn)(GLFWwindow* window);
typedef void (*GlfwSetWindowShouldCloseFn)(GLFWwindow* window, int value);
typedef const char* (*GlfwGetVersionStringFn)(void);

/* OpenGL function pointer types */
typedef void (*GlViewportFn)(int x, int y, int width, int height);
typedef void (*GlClearColorFn)(float r, float g, float b, float a);
typedef void (*GlClearFn)(unsigned int mask);
typedef unsigned int (*GlCreateShaderFn)(unsigned int type);
typedef void (*GlShaderSourceFn)(unsigned int shader, int count, const char** string, const int* length);
typedef void (*GlCompileShaderFn)(unsigned int shader);
typedef void (*GlGetShaderivFn)(unsigned int shader, unsigned int pname, int* params);
typedef void (*GlGetShaderInfoLogFn)(unsigned int shader, int bufSize, int* length, char* infoLog);
typedef unsigned int (*GlCreateProgramFn)(void);
typedef void (*GlAttachShaderFn)(unsigned int program, unsigned int shader);
typedef void (*GlLinkProgramFn)(unsigned int program);
typedef void (*GlGetProgramivFn)(unsigned int program, unsigned int pname, int* params);
typedef void (*GlGetProgramInfoLogFn)(unsigned int program, int bufSize, int* length, char* infoLog);
typedef void (*GlDeleteShaderFn)(unsigned int shader);
typedef void (*GlUseProgramFn)(unsigned int program);
typedef int (*GlGetUniformLocationFn)(unsigned int program, const char* name);
typedef void (*GlUniform1iFn)(int location, int v0);
typedef void (*GlUniformMatrix4fvFn)(int location, int count, int transpose, const float* value);
typedef void (*GlEnableFn)(unsigned int cap);
typedef void (*GlDisableFn)(unsigned int cap);
typedef void (*GlBlendFuncFn)(unsigned int sfactor, unsigned int dfactor);
typedef void (*GlGenTexturesFn)(int n, unsigned int* textures);
typedef void (*GlBindTextureFn)(unsigned int target, unsigned int texture);
typedef void (*GlTexImage2DFn)(unsigned int target, int level, int internalformat, int width, int height,
                                int border, unsigned int format, unsigned int type, const void* pixels);
typedef void (*GlTexParameteriFn)(unsigned int target, unsigned int pname, int param);
typedef void (*GlDeleteTexturesFn)(int n, const unsigned int* textures);
typedef unsigned int (*GlGenBuffersFn)(int n, unsigned int* buffers);
typedef void (*GlBindBufferFn)(unsigned int target, unsigned int buffer);
typedef void (*GlBufferDataFn)(unsigned int target, long size, const void* data, unsigned int usage);
typedef void (*GlDeleteBuffersFn)(int n, const unsigned int* buffers);
typedef void (*GlEnableVertexAttribArrayFn)(unsigned int index);
typedef void (*GlDisableVertexAttribArrayFn)(unsigned int index);
typedef void (*GlVertexAttribPointerFn)(unsigned int index, int size, unsigned int type, int normalized,
                                         int stride, const void* pointer);
typedef void (*GlDrawArraysFn)(unsigned int mode, int first, int count);
typedef void (*GlDrawElementsFn)(unsigned int mode, int count, unsigned int type, const void* indices);

/* ImGui function pointer types */
typedef void (*ImGuiCreateContextFn)(ImVec4* clear_color);
typedef void (*ImGuiDestroyContextFn)(ImGuiContext* ctx);
typedef ImGuiIO* (*ImGuiGetIOFn)(void);
typedef void (*ImGuiStyleColorsDarkFn)(void);
typedef void (*ImGuiNewFrameFn)(void);
typedef void (*ImGuiRenderFn)(void);
typedef void* (*ImGuiGetDrawDataFn)(void);
typedef void (*ImGuiTextFn)(const char* fmt, ...);
typedef void (*ImGuiTextColoredFn)(ImVec4 col, const char* fmt, ...);
typedef int (*ImGuiButtonFn)(const char* label, ImVec2 size);
typedef int (*ImGuiCheckboxFn)(const char* label, int* v);
typedef int (*ImGuiSliderFloatFn)(const char* label, float* v, float v_min, float v_max, const char* format, int flags);
typedef int (*ImGuiSliderIntFn)(const char* label, int* v, int v_min, int v_max, const char* format, int flags);
typedef int (*ImGuiInputTextFn)(const char* label, char* buf, size_t buf_size, int flags, void* callback, void* user_data);
typedef int (*ImGuiInputFloatFn)(const char* label, float* v, float step, float step_fast, const char* format, int flags);
typedef int (*ImGuiInputIntFn)(const char* label, int* v, int step, int step_fast, int flags);
typedef void (*ImGuiSeparatorFn)(void);
typedef void (*ImGuiSpacingFn)(void);
typedef void (*ImGuiSameLineFn)(float offset_from_start_x, float spacing);
typedef void (*ImGuiNewLineFn)(void);
typedef void (*ImGuiDummyFn)(ImVec2 size);
typedef int (*ImGuiBeginFn)(const char* name, int* p_open, int flags);
typedef void (*ImGuiEndFn)(void);
typedef int (*ImGuiBeginChildFn)(const char* str_id, ImVec2 size, int border, int flags);
typedef void (*ImGuiEndChildFn)(void);
typedef void (*ImGuiBeginGroupFn)(void);
typedef void (*ImGuiEndGroupFn)(void);
typedef int (*ImGuiBeginTabBarFn)(const char* str_id, int flags);
typedef void (*ImGuiEndTabBarFn)(void);
typedef int (*ImGuiTabItemButtonFn)(const char* label, int flags);
typedef int (*ImGuiBeginTabItemFn)(const char* label, int* p_open, int flags);
typedef void (*ImGuiEndTabItemFn)(void);
typedef int (*ImGuiCollapsingHeaderFn)(const char* label, int* p_visible, int flags);
typedef int (*ImGuiTreeNodeFn)(const char* label);
typedef int (*ImGuiTreeNodeExFn)(const char* label, int flags);
typedef void (*ImGuiTreePopFn)(void);
typedef int (*ImGuiSelectableFn)(const char* label, int* p_selected, int flags, ImVec2 size);
typedef int (*ImGuiListBoxHeaderFn)(const char* label, ImVec2 size);
typedef void (*ImGuiListBoxFooterFn)(void);
typedef int (*ImGuiComboFn)(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items);
typedef int (*ImGuiColorEdit3Fn)(const char* label, float* col, int flags);
typedef int (*ImGuiColorEdit4Fn)(const char* label, float* col, int flags);
typedef void (*ImGuiProgressBarFn)(float fraction, ImVec2 size_arg, const char* overlay);
typedef void (*ImGuiPlotLinesFn)(const char* label, const float* values, int values_count, int values_offset,
                                  const char* overlay_text, float scale_min, float scale_max, ImVec2 graph_size, int stride);
typedef void (*ImGuiPlotHistogramFn)(const char* label, const float* values, int values_count, int values_offset,
                                      const char* overlay_text, float scale_min, float scale_max, ImVec2 graph_size, int stride);
typedef void (*ImGuiSetCursorPosFn)(ImVec2 local_pos);
typedef void (*ImGuiSetCursorScreenPosFn)(ImVec2 pos);
typedef ImVec2 (*ImGuiGetCursorPosFn)(void);
typedef ImVec2 (*ImGuiGetContentRegionAvailFn)(void);
typedef ImVec2 (*ImGuiGetWindowSizeFn)(void);
typedef ImVec2 (*ImGuiGetWindowPosFn)(void);
typedef void (*ImGuiSetNextWindowSizeFn)(ImVec2 size, int cond);
typedef void (*ImGuiSetNextWindowPosFn)(ImVec2 pos, int cond, ImVec2 pivot);
typedef void (*ImGuiSetNextWindowBgAlphaFn)(float alpha);
typedef void (*ImGuiPushStyleColorFn)(int idx, ImVec4 col);
typedef void (*ImGuiPopStyleColorFn)(int count);
typedef void (*ImGuiPushStyleVarFn)(int idx, float val);
typedef void (*ImGuiPopStyleVarFn)(int count);
typedef void (*ImGuiPushIDStrFn)(const char* str_id);
typedef void (*ImGuiPopIDFn)(void);
typedef int (*ImGuiIsItemHoveredFn)(int flags);
typedef int (*ImGuiIsItemActiveFn)(void);
typedef int (*ImGuiIsItemClickedFn)(int mouse_button);
typedef int (*ImGuiIsItemVisibleFn)(void);
typedef int (*ImGuiIsItemEditedFn)(void);
typedef int (*ImGuiIsItemActivatedFn)(void);
typedef int (*ImGuiIsItemDeactivatedFn)(void);
typedef int (*ImGuiIsItemDeactivatedAfterEditFn)(void);
typedef int (*ImGuiIsAnyItemHoveredFn)(void);
typedef int (*ImGuiIsAnyItemActiveFn)(void);
typedef int (*ImGuiIsAnyItemFocusedFn)(void);
typedef void (*ImGuiSetTooltipFn)(const char* fmt, ...);
typedef int (*ImGuiBeginTooltipFn)(void);
typedef void (*ImGuiEndTooltipFn)(void);
typedef void (*ImGuiSetItemTooltipFn)(const char* fmt, ...);
typedef void (*ImGuiOpenPopupFn)(const char* str_id, int popup_flags);
typedef int (*ImGuiBeginPopupModalFn)(const char* name, int* p_open, int flags);
typedef void (*ImGuiEndPopupFn)(void);
typedef void (*ImGuiCloseCurrentPopupFn)(void);

/* ImGui backend function types */
typedef int (*ImGuiImplGlfwInitForOpenGLFn)(GLFWwindow* window, int install_callbacks);
typedef void (*ImGuiImplGlfwShutdownFn)(void);
typedef void (*ImGuiImplGlfwNewFrameFn)(void);
typedef int (*ImGuiImplOpenGL3InitFn)(const char* glsl_version);
typedef void (*ImGuiImplOpenGL3ShutdownFn)(void);
typedef void (*ImGuiImplOpenGL3NewFrameFn)(void);
typedef void (*ImGuiImplOpenGL3RenderDrawDataFn)(void* draw_data);

/* Dynamic loader for ImGui and dependencies */
struct ImGuiLibs {
    DynamicLoader glfwLoader;
    DynamicLoader glLoader;
    DynamicLoader imguiLoader;
    
    /* GLFW functions */
    GlfwInitFn glfwInit = nullptr;
    GlfwTerminateFn glfwTerminate = nullptr;
    GlfwCreateWindowFn glfwCreateWindow = nullptr;
    GlfwDestroyWindowFn glfwDestroyWindow = nullptr;
    GlfwMakeContextCurrentFn glfwMakeContextCurrent = nullptr;
    GlfwSwapIntervalFn glfwSwapInterval = nullptr;
    GlfwPollEventsFn glfwPollEvents = nullptr;
    GlfwGetFramebufferSizeFn glfwGetFramebufferSize = nullptr;
    GlfwSwapBuffersFn glfwSwapBuffers = nullptr;
    GlfwWindowShouldCloseFn glfwWindowShouldClose = nullptr;
    GlfwSetWindowShouldCloseFn glfwSetWindowShouldClose = nullptr;
    GlfwGetVersionStringFn glfwGetVersionString = nullptr;
    
    /* OpenGL functions */
    GlViewportFn glViewport = nullptr;
    GlClearColorFn glClearColor = nullptr;
    GlClearFn glClear = nullptr;
    GlEnableFn glEnable = nullptr;
    GlDisableFn glDisable = nullptr;
    GlBlendFuncFn glBlendFunc = nullptr;
    
    /* ImGui functions */
    ImGuiCreateContextFn ImGui_CreateContext = nullptr;
    ImGuiDestroyContextFn ImGui_DestroyContext = nullptr;
    ImGuiGetIOFn ImGui_GetIO = nullptr;
    ImGuiStyleColorsDarkFn ImGui_StyleColorsDark = nullptr;
    ImGuiNewFrameFn ImGui_NewFrame = nullptr;
    ImGuiRenderFn ImGui_Render = nullptr;
    ImGuiGetDrawDataFn ImGui_GetDrawData = nullptr;
    ImGuiTextFn ImGui_Text = nullptr;
    ImGuiTextColoredFn ImGui_TextColored = nullptr;
    ImGuiButtonFn ImGui_Button = nullptr;
    ImGuiCheckboxFn ImGui_Checkbox = nullptr;
    ImGuiSliderFloatFn ImGui_SliderFloat = nullptr;
    ImGuiSliderIntFn ImGui_SliderInt = nullptr;
    ImGuiInputTextFn ImGui_InputText = nullptr;
    ImGuiInputFloatFn ImGui_InputFloat = nullptr;
    ImGuiInputIntFn ImGui_InputInt = nullptr;
    ImGuiSeparatorFn ImGui_Separator = nullptr;
    ImGuiSpacingFn ImGui_Spacing = nullptr;
    ImGuiSameLineFn ImGui_SameLine = nullptr;
    ImGuiNewLineFn ImGui_NewLine = nullptr;
    ImGuiDummyFn ImGui_Dummy = nullptr;
    ImGuiBeginFn ImGui_Begin = nullptr;
    ImGuiEndFn ImGui_End = nullptr;
    ImGuiBeginChildFn ImGui_BeginChild = nullptr;
    ImGuiEndChildFn ImGui_EndChild = nullptr;
    ImGuiBeginGroupFn ImGui_BeginGroup = nullptr;
    ImGuiEndGroupFn ImGui_EndGroup = nullptr;
    ImGuiBeginTabBarFn ImGui_BeginTabBar = nullptr;
    ImGuiEndTabBarFn ImGui_EndTabBar = nullptr;
    ImGuiTabItemButtonFn ImGui_TabItemButton = nullptr;
    ImGuiBeginTabItemFn ImGui_BeginTabItem = nullptr;
    ImGuiEndTabItemFn ImGui_EndTabItem = nullptr;
    ImGuiCollapsingHeaderFn ImGui_CollapsingHeader = nullptr;
    ImGuiTreeNodeFn ImGui_TreeNode = nullptr;
    ImGuiTreeNodeExFn ImGui_TreeNodeEx = nullptr;
    ImGuiTreePopFn ImGui_TreePop = nullptr;
    ImGuiSelectableFn ImGui_Selectable = nullptr;
    ImGuiListBoxHeaderFn ImGui_ListBoxHeader = nullptr;
    ImGuiListBoxFooterFn ImGui_ListBoxFooter = nullptr;
    ImGuiComboFn ImGui_Combo = nullptr;
    ImGuiColorEdit3Fn ImGui_ColorEdit3 = nullptr;
    ImGuiColorEdit4Fn ImGui_ColorEdit4 = nullptr;
    ImGuiProgressBarFn ImGui_ProgressBar = nullptr;
    ImGuiPlotLinesFn ImGui_PlotLines = nullptr;
    ImGuiPlotHistogramFn ImGui_PlotHistogram = nullptr;
    ImGuiSetCursorPosFn ImGui_SetCursorPos = nullptr;
    ImGuiSetCursorScreenPosFn ImGui_SetCursorScreenPos = nullptr;
    ImGuiGetCursorPosFn ImGui_GetCursorPos = nullptr;
    ImGuiGetContentRegionAvailFn ImGui_GetContentRegionAvail = nullptr;
    ImGuiGetWindowSizeFn ImGui_GetWindowSize = nullptr;
    ImGuiGetWindowPosFn ImGui_GetWindowPos = nullptr;
    ImGuiSetNextWindowSizeFn ImGui_SetNextWindowSize = nullptr;
    ImGuiSetNextWindowPosFn ImGui_SetNextWindowPos = nullptr;
    ImGuiSetNextWindowBgAlphaFn ImGui_SetNextWindowBgAlpha = nullptr;
    ImGuiPushStyleColorFn ImGui_PushStyleColor = nullptr;
    ImGuiPopStyleColorFn ImGui_PopStyleColor = nullptr;
    ImGuiPushStyleVarFn ImGui_PushStyleVar = nullptr;
    ImGuiPopStyleVarFn ImGui_PopStyleVar = nullptr;
    ImGuiPushIDStrFn ImGui_PushID_Str = nullptr;
    ImGuiPopIDFn ImGui_PopID = nullptr;
    ImGuiIsItemHoveredFn ImGui_IsItemHovered = nullptr;
    ImGuiIsItemActiveFn ImGui_IsItemActive = nullptr;
    ImGuiIsItemClickedFn ImGui_IsItemClicked = nullptr;
    ImGuiIsItemVisibleFn ImGui_IsItemVisible = nullptr;
    ImGuiIsItemEditedFn ImGui_IsItemEdited = nullptr;
    ImGuiIsItemActivatedFn ImGui_IsItemActivated = nullptr;
    ImGuiIsItemDeactivatedFn ImGui_IsItemDeactivated = nullptr;
    ImGuiIsItemDeactivatedAfterEditFn ImGui_IsItemDeactivatedAfterEdit = nullptr;
    ImGuiIsAnyItemHoveredFn ImGui_IsAnyItemHovered = nullptr;
    ImGuiIsAnyItemActiveFn ImGui_IsAnyItemActive = nullptr;
    ImGuiIsAnyItemFocusedFn ImGui_IsAnyItemFocused = nullptr;
    ImGuiSetTooltipFn ImGui_SetTooltip = nullptr;
    ImGuiBeginTooltipFn ImGui_BeginTooltip = nullptr;
    ImGuiEndTooltipFn ImGui_EndTooltip = nullptr;
    ImGuiSetItemTooltipFn ImGui_SetItemTooltip = nullptr;
    ImGuiOpenPopupFn ImGui_OpenPopup = nullptr;
    ImGuiBeginPopupModalFn ImGui_BeginPopupModal = nullptr;
    ImGuiEndPopupFn ImGui_EndPopup = nullptr;
    ImGuiCloseCurrentPopupFn ImGui_CloseCurrentPopup = nullptr;
    
    /* ImGui backend functions */
    ImGuiImplGlfwInitForOpenGLFn ImGui_ImplGlfw_InitForOpenGL = nullptr;
    ImGuiImplGlfwShutdownFn ImGui_ImplGlfw_Shutdown = nullptr;
    ImGuiImplGlfwNewFrameFn ImGui_ImplGlfw_NewFrame = nullptr;
    ImGuiImplOpenGL3InitFn ImGui_ImplOpenGL3_Init = nullptr;
    ImGuiImplOpenGL3ShutdownFn ImGui_ImplOpenGL3_Shutdown = nullptr;
    ImGuiImplOpenGL3NewFrameFn ImGui_ImplOpenGL3_NewFrame = nullptr;
    ImGuiImplOpenGL3RenderDrawDataFn ImGui_ImplOpenGL3_RenderDrawData = nullptr;
    
    bool load() {
        /* Load GLFW */
        if (!glfwLoader.load(LibNames::GLFW3)) {
            fprintf(stderr, "[ImGui] Failed to load GLFW3\n");
            return false;
        }
        
        /* Load OpenGL */
        if (!glLoader.load(LibNames::GL)) {
            fprintf(stderr, "[ImGui] Failed to load OpenGL\n");
            return false;
        }
        
        /* Try to load ImGui from common locations */
        const char* imguiPaths[] = {
            "libimgui.so",
            "libimgui.so.1",
            "/usr/lib/libimgui.so",
            "/usr/local/lib/libimgui.so",
            nullptr
        };
        
        bool imguiLoaded = false;
        for (int i = 0; imguiPaths[i] != nullptr; i++) {
            if (imguiLoader.load(imguiPaths[i])) {
                imguiLoaded = true;
                break;
            }
        }
        
        if (!imguiLoaded) {
            fprintf(stderr, "[ImGui] Failed to load Dear ImGui from any location\n");
            return false;
        }
        
        /* Load GLFW symbols */
#define LOAD_GLFW(name) \
    name = glfwLoader.getSymbol<Glfw##name##Fn>("glfw" #name); \
    if (!name) { \
        fprintf(stderr, "[ImGui] Failed to load GLFW symbol: glfw%s\n", #name); \
        return false; \
    }
        
        LOAD_GLFW(Init)
        LOAD_GLFW(Terminate)
        LOAD_GLFW(CreateWindow)
        LOAD_GLFW(DestroyWindow)
        LOAD_GLFW(MakeContextCurrent)
        LOAD_GLFW(SwapInterval)
        LOAD_GLFW(PollEvents)
        LOAD_GLFW(GetFramebufferSize)
        LOAD_GLFW(SwapBuffers)
        LOAD_GLFW(WindowShouldClose)
        LOAD_GLFW(SetWindowShouldClose)
        
#undef LOAD_GLFW
        
        /* Load OpenGL symbols */
#define LOAD_GL(name) \
    name = glLoader.getSymbol<Gl##name##Fn>("gl" #name); \
    if (!name) { \
        fprintf(stderr, "[ImGui] Failed to load GL symbol: gl%s\n", #name); \
        return false; \
    }
        
        LOAD_GL(Viewport)
        LOAD_GL(ClearColor)
        LOAD_GL(Clear)
        LOAD_GL(Enable)
        LOAD_GL(Disable)
        LOAD_GL(BlendFunc)
        
#undef LOAD_GL
        
        /* Load ImGui symbols */
#define LOAD_IMGUI(name) \
    name = imguiLoader.getSymbol<ImGui##name##Fn>("ImGui" #name); \
    if (!name) { \
        fprintf(stderr, "[ImGui] Failed to load ImGui symbol: ImGui%s\n", #name); \
        return false; \
    }
        
        LOAD_IMGUI(CreateContext)
        LOAD_IMGUI(DestroyContext)
        LOAD_IMGUI(GetIO)
        LOAD_IMGUI(StyleColorsDark)
        LOAD_IMGUI(NewFrame)
        LOAD_IMGUI(Render)
        LOAD_IMGUI(GetDrawData)
        LOAD_IMGUI(Text)
        LOAD_IMGUI(TextColored)
        LOAD_IMGUI(Button)
        LOAD_IMGUI(Checkbox)
        LOAD_IMGUI(SliderFloat)
        LOAD_IMGUI(SliderInt)
        LOAD_IMGUI(InputText)
        LOAD_IMGUI(InputFloat)
        LOAD_IMGUI(InputInt)
        LOAD_IMGUI(Separator)
        LOAD_IMGUI(Spacing)
        LOAD_IMGUI(SameLine)
        LOAD_IMGUI(NewLine)
        LOAD_IMGUI(Dummy)
        LOAD_IMGUI(Begin)
        LOAD_IMGUI(End)
        LOAD_IMGUI(BeginChild)
        LOAD_IMGUI(EndChild)
        LOAD_IMGUI(BeginGroup)
        LOAD_IMGUI(EndGroup)
        LOAD_IMGUI(BeginTabBar)
        LOAD_IMGUI(EndTabBar)
        LOAD_IMGUI(TabItemButton)
        LOAD_IMGUI(BeginTabItem)
        LOAD_IMGUI(EndTabItem)
        LOAD_IMGUI(CollapsingHeader)
        LOAD_IMGUI(TreeNode)
        LOAD_IMGUI(TreeNodeEx)
        LOAD_IMGUI(TreePop)
        LOAD_IMGUI(Selectable)
        LOAD_IMGUI(ListBoxHeader)
        LOAD_IMGUI(ListBoxFooter)
        LOAD_IMGUI(Combo)
        LOAD_IMGUI(ColorEdit3)
        LOAD_IMGUI(ColorEdit4)
        LOAD_IMGUI(ProgressBar)
        LOAD_IMGUI(PlotLines)
        LOAD_IMGUI(PlotHistogram)
        LOAD_IMGUI(SetCursorPos)
        LOAD_IMGUI(SetCursorScreenPos)
        LOAD_IMGUI(GetCursorPos)
        LOAD_IMGUI(GetContentRegionAvail)
        LOAD_IMGUI(GetWindowSize)
        LOAD_IMGUI(GetWindowPos)
        LOAD_IMGUI(SetNextWindowSize)
        LOAD_IMGUI(SetNextWindowPos)
        LOAD_IMGUI(SetNextWindowBgAlpha)
        LOAD_IMGUI(PushStyleColor)
        LOAD_IMGUI(PopStyleColor)
        LOAD_IMGUI(PushStyleVar)
        LOAD_IMGUI(PopStyleVar)
        LOAD_IMGUI(PushID_Str)
        LOAD_IMGUI(PopID)
        LOAD_IMGUI(IsItemHovered)
        LOAD_IMGUI(IsItemActive)
        LOAD_IMGUI(IsItemClicked)
        LOAD_IMGUI(IsItemVisible)
        LOAD_IMGUI(IsItemEdited)
        LOAD_IMGUI(IsItemActivated)
        LOAD_IMGUI(IsItemDeactivated)
        LOAD_IMGUI(IsItemDeactivatedAfterEdit)
        LOAD_IMGUI(IsAnyItemHovered)
        LOAD_IMGUI(IsAnyItemActive)
        LOAD_IMGUI(IsAnyItemFocused)
        LOAD_IMGUI(SetTooltip)
        LOAD_IMGUI(BeginTooltip)
        LOAD_IMGUI(EndTooltip)
        LOAD_IMGUI(SetItemTooltip)
        LOAD_IMGUI(OpenPopup)
        LOAD_IMGUI(BeginPopupModal)
        LOAD_IMGUI(EndPopup)
        LOAD_IMGUI(CloseCurrentPopup)
        
#undef LOAD_IMGUI
        
        /* Try to load ImGui backend implementations (may be in same library) */
        ImGui_ImplGlfw_InitForOpenGL = imguiLoader.getSymbol<ImGuiImplGlfwInitForOpenGLFn>("ImGui_ImplGlfw_InitForOpenGL");
        ImGui_ImplGlfw_Shutdown = imguiLoader.getSymbol<ImGuiImplGlfwShutdownFn>("ImGui_ImplGlfw_Shutdown");
        ImGui_ImplGlfw_NewFrame = imguiLoader.getSymbol<ImGuiImplGlfwNewFrameFn>("ImGui_ImplGlfw_NewFrame");
        ImGui_ImplOpenGL3_Init = imguiLoader.getSymbol<ImGuiImplOpenGL3InitFn>("ImGui_ImplOpenGL3_Init");
        ImGui_ImplOpenGL3_Shutdown = imguiLoader.getSymbol<ImGuiImplOpenGL3ShutdownFn>("ImGui_ImplOpenGL3_Shutdown");
        ImGui_ImplOpenGL3_NewFrame = imguiLoader.getSymbol<ImGuiImplOpenGL3NewFrameFn>("ImGui_ImplOpenGL3_NewFrame");
        ImGui_ImplOpenGL3_RenderDrawData = imguiLoader.getSymbol<ImGuiImplOpenGL3RenderDrawDataFn>("ImGui_ImplOpenGL3_RenderDrawData");
        
        if (!ImGui_ImplGlfw_InitForOpenGL || !ImGui_ImplOpenGL3_Init) {
            fprintf(stderr, "[ImGui] Warning: ImGui backend implementations not found. Some features may not work.\n");
        }
        
        return true;
    }
    
    bool isLoaded() const {
        return glfwLoader.isLoaded() && glLoader.isLoaded() && imguiLoader.isLoaded();
    }
};

static ImGuiLibs* g_imguiLibs = nullptr;
static GLFWwindow* g_window = nullptr;
static bool g_initialized = false;

} /* anonymous namespace */

/* Static C functions */

static HavelValue* imgui_init(int argc, HavelValue** argv) {
    if (g_initialized) {
        return havel_new_bool(1);
    }
    
    /* Lazy load libraries on first use */
    if (!g_imguiLibs) {
        g_imguiLibs = new ImGuiLibs();
        if (!g_imguiLibs->load()) {
            fprintf(stderr, "[ImGui] Failed to load ImGui libraries\n");
            delete g_imguiLibs;
            g_imguiLibs = nullptr;
            return havel_new_bool(0);
        }
        fprintf(stderr, "[ImGui] ImGui libraries loaded dynamically\n");
    }
    
    int width = 1280;
    int height = 720;
    const char* title = "ImGui Window";
    
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_STRING) {
        title = havel_get_string(argv[0]);
    }
    if (argc >= 2 && havel_get_type(argv[1]) == HAVEL_INT) {
        width = (int)havel_get_int(argv[1]);
    }
    if (argc >= 3 && havel_get_type(argv[2]) == HAVEL_INT) {
        height = (int)havel_get_int(argv[2]);
    }
    
    /* Initialize GLFW */
    if (!g_imguiLibs->glfwInit()) {
        fprintf(stderr, "[ImGui] Failed to initialize GLFW\n");
        return havel_new_bool(0);
    }
    
    /* Create window */
    g_window = g_imguiLibs->glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!g_window) {
        fprintf(stderr, "[ImGui] Failed to create GLFW window\n");
        g_imguiLibs->glfwTerminate();
        return havel_new_bool(0);
    }
    
    g_imguiLibs->glfwMakeContextCurrent(g_window);
    g_imguiLibs->glfwSwapInterval(1);
    
    /* Setup Dear ImGui context */
    g_imguiLibs->ImGui_CreateContext(nullptr);
    
    ImGuiIO* io = g_imguiLibs->ImGui_GetIO();
    if (io) {
        /* Enable keyboard navigation and docking */
        *(int*)(&io->ConfigFlags) |= 1 << 0;  /* NavEnableKeyboard */
        *(int*)(&io->ConfigFlags) |= 1 << 6;  /* DockingEnable */
    }
    
    /* Setup style */
    g_imguiLibs->ImGui_StyleColorsDark();
    
    /* Setup backends (if available) */
    if (g_imguiLibs->ImGui_ImplGlfw_InitForOpenGL) {
        g_imguiLibs->ImGui_ImplGlfw_InitForOpenGL(g_window, 1);
    }
    if (g_imguiLibs->ImGui_ImplOpenGL3_Init) {
        g_imguiLibs->ImGui_ImplOpenGL3_Init("#version 130");
    }
    
    g_initialized = true;
    return havel_new_bool(1);
}

static HavelValue* imgui_shutdown(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    
    if (!g_initialized) {
        return havel_new_bool(0);
    }
    
    if (g_imguiLibs) {
        if (g_imguiLibs->ImGui_ImplOpenGL3_Shutdown) {
            g_imguiLibs->ImGui_ImplOpenGL3_Shutdown();
        }
        if (g_imguiLibs->ImGui_ImplGlfw_Shutdown) {
            g_imguiLibs->ImGui_ImplGlfw_Shutdown();
        }
        g_imguiLibs->ImGui_DestroyContext(nullptr);
    }
    
    if (g_window && g_imguiLibs) {
        g_imguiLibs->glfwDestroyWindow(g_window);
        g_window = nullptr;
        g_imguiLibs->glfwTerminate();
    }
    
    g_initialized = false;
    return havel_new_bool(1);
}

static HavelValue* imgui_frame_begin(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    
    if (!g_initialized || !g_imguiLibs) {
        return havel_new_bool(0);
    }
    
    /* Poll events */
    g_imguiLibs->glfwPollEvents();
    
    /* Start new frame */
    if (g_imguiLibs->ImGui_ImplOpenGL3_NewFrame) {
        g_imguiLibs->ImGui_ImplOpenGL3_NewFrame();
    }
    if (g_imguiLibs->ImGui_ImplGlfw_NewFrame) {
        g_imguiLibs->ImGui_ImplGlfw_NewFrame();
    }
    g_imguiLibs->ImGui_NewFrame();
    
    return havel_new_bool(1);
}

static HavelValue* imgui_frame_end(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    
    if (!g_initialized || !g_imguiLibs) {
        return havel_new_bool(0);
    }
    
    g_imguiLibs->ImGui_Render();
    
    int display_w, display_h;
    g_imguiLibs->glfwGetFramebufferSize(g_window, &display_w, &display_h);
    g_imguiLibs->glViewport(0, 0, display_w, display_h);
    g_imguiLibs->glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
    g_imguiLibs->glClear(1 << 14);  /* GL_COLOR_BUFFER_BIT */
    
    if (g_imguiLibs->ImGui_ImplOpenGL3_RenderDrawData) {
        g_imguiLibs->ImGui_ImplOpenGL3_RenderDrawData(g_imguiLibs->ImGui_GetDrawData());
    }
    
    g_imguiLibs->glfwSwapBuffers(g_window);
    
    return havel_new_bool(1);
}

static HavelValue* imgui_should_close(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    
    if (!g_initialized || !g_window || !g_imguiLibs) {
        return havel_new_bool(1);
    }
    
    int shouldClose = g_imguiLibs->glfwWindowShouldClose(g_window);
    return havel_new_bool(shouldClose);
}

static HavelValue* imgui_set_should_close(int argc, HavelValue** argv) {
    if (argc < 1 || !g_window || !g_imguiLibs) {
        return havel_new_bool(0);
    }
    
    int shouldClose = havel_get_bool(argv[0]);
    g_imguiLibs->glfwSetWindowShouldClose(g_window, shouldClose);
    return havel_new_bool(1);
}

static HavelValue* imgui_text(int argc, HavelValue** argv) {
    if (argc < 1 || !g_initialized || !g_imguiLibs) {
        return havel_new_null();
    }
    
    const char* text = havel_get_string(argv[0]);
    if (text) {
        g_imguiLibs->ImGui_Text("%s", text);
    }
    return havel_new_null();
}

static HavelValue* imgui_text_colored(int argc, HavelValue** argv) {
    if (argc < 4 || !g_initialized || !g_imguiLibs) {
        return havel_new_null();
    }
    
    float r = (float)havel_get_float(argv[0]);
    float g = (float)havel_get_float(argv[1]);
    float b = (float)havel_get_float(argv[2]);
    const char* text = havel_get_string(argv[3]);
    
    if (text) {
        g_imguiLibs->ImGui_TextColored(ImVec4(r, g, b, 1.0f), "%s", text);
    }
    return havel_new_null();
}

static HavelValue* imgui_button(int argc, HavelValue** argv) {
    if (argc < 1 || !g_initialized || !g_imguiLibs) {
        return havel_new_bool(0);
    }
    
    const char* label = havel_get_string(argv[0]);
    float width = 0.0f;
    float height = 0.0f;
    
    if (argc >= 2 && havel_get_type(argv[1]) == HAVEL_FLOAT) {
        width = (float)havel_get_float(argv[1]);
    }
    if (argc >= 3 && havel_get_type(argv[2]) == HAVEL_FLOAT) {
        height = (float)havel_get_float(argv[2]);
    }
    
    int clicked = 0;
    if (width > 0.0f && height > 0.0f) {
        clicked = g_imguiLibs->ImGui_Button(label, ImVec2(width, height));
    } else if (width > 0.0f) {
        clicked = g_imguiLibs->ImGui_Button(label, ImVec2(width, 0.0f));
    } else {
        clicked = g_imguiLibs->ImGui_Button(label, ImVec2(0, 0));
    }
    
    return havel_new_bool(clicked);
}

/* Additional ImGui functions would continue here... */
/* For brevity, implementing key functions only */

static HavelValue* imgui_checkbox(int argc, HavelValue** argv) {
    if (argc < 2 || !g_initialized || !g_imguiLibs) {
        return havel_new_bool(0);
    }
    
    const char* label = havel_get_string(argv[0]);
    int checked = havel_get_bool(argv[1]);
    
    int v = checked;
    int changed = g_imguiLibs->ImGui_Checkbox(label, &v);
    
    HavelValue* result = havel_new_array(2);
    HavelValue* changedVal = havel_new_bool(changed);
    HavelValue* checkedVal = havel_new_bool(v);
    havel_array_push(result, changedVal);
    havel_array_push(result, checkedVal);
    havel_free_value(changedVal);
    havel_free_value(checkedVal);
    
    return result;
}

static HavelValue* imgui_slider_float(int argc, HavelValue** argv) {
    if (argc < 4 || !g_initialized || !g_imguiLibs) {
        HavelValue* result = havel_new_array(2);
        havel_array_push(result, havel_new_bool(0));
        havel_array_push(result, havel_new_float(0.0));
        return result;
    }
    
    const char* label = havel_get_string(argv[0]);
    float value = (float)havel_get_float(argv[1]);
    float minVal = (float)havel_get_float(argv[2]);
    float maxVal = (float)havel_get_float(argv[3]);
    
    int changed = g_imguiLibs->ImGui_SliderFloat(label, &value, minVal, maxVal, "%.3f", 0);
    
    HavelValue* result = havel_new_array(2);
    HavelValue* changedVal = havel_new_bool(changed);
    HavelValue* valueVal = havel_new_float(value);
    havel_array_push(result, changedVal);
    havel_array_push(result, valueVal);
    havel_free_value(changedVal);
    havel_free_value(valueVal);
    
    return result;
}

static HavelValue* imgui_slider_int(int argc, HavelValue** argv) {
    if (argc < 4 || !g_initialized || !g_imguiLibs) {
        HavelValue* result = havel_new_array(2);
        havel_array_push(result, havel_new_bool(0));
        havel_array_push(result, havel_new_int(0));
        return result;
    }
    
    const char* label = havel_get_string(argv[0]);
    int value = (int)havel_get_int(argv[1]);
    int minVal = (int)havel_get_int(argv[2]);
    int maxVal = (int)havel_get_int(argv[3]);
    
    int changed = g_imguiLibs->ImGui_SliderInt(label, &value, minVal, maxVal, "%d", 0);
    
    HavelValue* result = havel_new_array(2);
    HavelValue* changedVal = havel_new_bool(changed);
    HavelValue* valueVal = havel_new_int(value);
    havel_array_push(result, changedVal);
    havel_array_push(result, valueVal);
    havel_free_value(changedVal);
    havel_free_value(valueVal);
    
    return result;
}

static HavelValue* imgui_separator(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (g_initialized && g_imguiLibs) {
        g_imguiLibs->ImGui_Separator();
    }
    return havel_new_null();
}

static HavelValue* imgui_spacing(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (g_initialized && g_imguiLibs) {
        g_imguiLibs->ImGui_Spacing();
    }
    return havel_new_null();
}

static HavelValue* imgui_same_line(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (g_initialized && g_imguiLibs) {
        g_imguiLibs->ImGui_SameLine(0.0f, -1.0f);
    }
    return havel_new_null();
}

static HavelValue* imgui_begin_window(int argc, HavelValue** argv) {
    if (argc < 1 || !g_initialized || !g_imguiLibs) {
        return havel_new_bool(0);
    }
    
    const char* title = havel_get_string(argv[0]);
    int visible = g_imguiLibs->ImGui_Begin(title, nullptr, 0);
    return havel_new_bool(visible);
}

static HavelValue* imgui_end_window(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (g_initialized && g_imguiLibs) {
        g_imguiLibs->ImGui_End();
    }
    return havel_new_null();
}

static HavelValue* imgui_collapsing_header(int argc, HavelValue** argv) {
    if (argc < 1 || !g_initialized || !g_imguiLibs) {
        return havel_new_bool(0);
    }
    
    const char* label = havel_get_string(argv[0]);
    int visible = g_imguiLibs->ImGui_CollapsingHeader(label, nullptr, 0);
    return havel_new_bool(visible);
}

static HavelValue* imgui_tree_node(int argc, HavelValue** argv) {
    if (argc < 1 || !g_initialized || !g_imguiLibs) {
        return havel_new_bool(0);
    }
    
    const char* label = havel_get_string(argv[0]);
    int flags = (argc >= 2) ? (int)havel_get_int(argv[1]) : 0;
    int open = g_imguiLibs->ImGui_TreeNodeEx(label, flags);
    return havel_new_bool(open);
}

static HavelValue* imgui_tree_pop(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (g_initialized && g_imguiLibs) {
        g_imguiLibs->ImGui_TreePop();
    }
    return havel_new_null();
}

static HavelValue* imgui_is_item_hovered(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_initialized || !g_imguiLibs) {
        return havel_new_bool(0);
    }
    return havel_new_bool(g_imguiLibs->ImGui_IsItemHovered(0));
}

static HavelValue* imgui_is_item_clicked(int argc, HavelValue** argv) {
    int button = (argc >= 1) ? (int)havel_get_int(argv[0]) : 0;
    if (!g_initialized || !g_imguiLibs) {
        return havel_new_bool(0);
    }
    return havel_new_bool(g_imguiLibs->ImGui_IsItemClicked(button));
}

static HavelValue* imgui_set_tooltip(int argc, HavelValue** argv) {
    if (argc < 1 || !g_initialized || !g_imguiLibs) {
        return havel_new_null();
    }
    const char* text = havel_get_string(argv[0]);
    if (text) {
        g_imguiLibs->ImGui_SetTooltip("%s", text);
    }
    return havel_new_null();
}

/* Stub functions for remaining API */
static HavelValue* imgui_input_text(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    HavelValue* result = havel_new_array(2);
    havel_array_push(result, havel_new_bool(0));
    havel_array_push(result, havel_new_string(""));
    return result;
}

static HavelValue* imgui_input_float(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    HavelValue* result = havel_new_array(2);
    havel_array_push(result, havel_new_bool(0));
    havel_array_push(result, havel_new_float(0.0f));
    return result;
}

static HavelValue* imgui_input_int(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    HavelValue* result = havel_new_array(2);
    havel_array_push(result, havel_new_bool(0));
    havel_array_push(result, havel_new_int(0));
    return result;
}

static HavelValue* imgui_new_line(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (g_initialized && g_imguiLibs) {
        g_imguiLibs->ImGui_NewLine();
    }
    return havel_new_null();
}

static HavelValue* imgui_dummy(int argc, HavelValue** argv) {
    float w = (argc >= 1) ? (float)havel_get_float(argv[0]) : 0.0f;
    float h = (argc >= 2) ? (float)havel_get_float(argv[1]) : 0.0f;
    if (g_initialized && g_imguiLibs) {
        g_imguiLibs->ImGui_Dummy(ImVec2(w, h));
    }
    return havel_new_null();
}

static HavelValue* imgui_progress_bar(int argc, HavelValue** argv) {
    if (argc < 1 || !g_initialized || !g_imguiLibs) {
        return havel_new_null();
    }
    float fraction = (float)havel_get_float(argv[0]);
    g_imguiLibs->ImGui_ProgressBar(fraction, ImVec2(0, 0), nullptr);
    return havel_new_null();
}

static HavelValue* imgui_color_edit3(int argc, HavelValue** argv) {
    if (argc < 4 || !g_initialized || !g_imguiLibs) {
        HavelValue* result = havel_new_array(4);
        havel_array_push(result, havel_new_bool(0));
        havel_array_push(result, havel_new_float(0));
        havel_array_push(result, havel_new_float(0));
        havel_array_push(result, havel_new_float(0));
        return result;
    }
    
    const char* label = havel_get_string(argv[0]);
    float col[3] = {
        (float)havel_get_float(argv[1]),
        (float)havel_get_float(argv[2]),
        (float)havel_get_float(argv[3])
    };
    
    int changed = g_imguiLibs->ImGui_ColorEdit3(label, col, 0);
    
    HavelValue* result = havel_new_array(4);
    havel_array_push(result, havel_new_bool(changed));
    havel_array_push(result, havel_new_float(col[0]));
    havel_array_push(result, havel_new_float(col[1]));
    havel_array_push(result, havel_new_float(col[2]));
    return result;
}

static HavelValue* imgui_color_edit4(int argc, HavelValue** argv) {
    if (argc < 5 || !g_initialized || !g_imguiLibs) {
        HavelValue* result = havel_new_array(5);
        havel_array_push(result, havel_new_bool(0));
        havel_array_push(result, havel_new_float(0));
        havel_array_push(result, havel_new_float(0));
        havel_array_push(result, havel_new_float(0));
        havel_array_push(result, havel_new_float(0));
        return result;
    }
    
    const char* label = havel_get_string(argv[0]);
    float col[4] = {
        (float)havel_get_float(argv[1]),
        (float)havel_get_float(argv[2]),
        (float)havel_get_float(argv[3]),
        (float)havel_get_float(argv[4])
    };
    
    int changed = g_imguiLibs->ImGui_ColorEdit4(label, col, 0);
    
    HavelValue* result = havel_new_array(5);
    havel_array_push(result, havel_new_bool(changed));
    havel_array_push(result, havel_new_float(col[0]));
    havel_array_push(result, havel_new_float(col[1]));
    havel_array_push(result, havel_new_float(col[2]));
    havel_array_push(result, havel_new_float(col[3]));
    return result;
}

static HavelValue* imgui_get_cursor_pos(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_initialized || !g_imguiLibs || !g_imguiLibs->ImGui_GetCursorPos) {
        return havel_new_null();
    }
    
    ImVec2 pos = g_imguiLibs->ImGui_GetCursorPos();
    HavelValue* result = havel_new_object();
    havel_object_set(result, "x", havel_new_float(pos.x));
    havel_object_set(result, "y", havel_new_float(pos.y));
    return result;
}

static HavelValue* imgui_get_window_size(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_initialized || !g_imguiLibs || !g_imguiLibs->ImGui_GetWindowSize) {
        return havel_new_null();
    }
    
    ImVec2 size = g_imguiLibs->ImGui_GetWindowSize();
    HavelValue* result = havel_new_object();
    havel_object_set(result, "width", havel_new_float(size.x));
    havel_object_set(result, "height", havel_new_float(size.y));
    return result;
}

static HavelValue* imgui_push_id(int argc, HavelValue** argv) {
    if (argc < 1 || !g_initialized || !g_imguiLibs) {
        return havel_new_null();
    }
    const char* strId = havel_get_string(argv[0]);
    if (strId && g_imguiLibs->ImGui_PushID_Str) {
        g_imguiLibs->ImGui_PushID_Str(strId);
    }
    return havel_new_null();
}

static HavelValue* imgui_pop_id(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (g_initialized && g_imguiLibs && g_imguiLibs->ImGui_PopID) {
        g_imguiLibs->ImGui_PopID();
    }
    return havel_new_null();
}

extern "C" void havel_extension_init(HavelAPI* api) {
    /* Register all ImGui functions */
    api->register_function("imgui", "init", imgui_init);
    api->register_function("imgui", "shutdown", imgui_shutdown);
    api->register_function("imgui", "frameBegin", imgui_frame_begin);
    api->register_function("imgui", "frameEnd", imgui_frame_end);
    api->register_function("imgui", "shouldClose", imgui_should_close);
    api->register_function("imgui", "setShouldClose", imgui_set_should_close);
    
    /* Basic widgets */
    api->register_function("imgui", "text", imgui_text);
    api->register_function("imgui", "textColored", imgui_text_colored);
    api->register_function("imgui", "button", imgui_button);
    api->register_function("imgui", "checkbox", imgui_checkbox);
    api->register_function("imgui", "sliderFloat", imgui_slider_float);
    api->register_function("imgui", "sliderInt", imgui_slider_int);
    api->register_function("imgui", "inputText", imgui_input_text);
    api->register_function("imgui", "inputFloat", imgui_input_float);
    api->register_function("imgui", "inputInt", imgui_input_int);
    api->register_function("imgui", "separator", imgui_separator);
    api->register_function("imgui", "spacing", imgui_spacing);
    api->register_function("imgui", "sameLine", imgui_same_line);
    api->register_function("imgui", "newLine", imgui_new_line);
    api->register_function("imgui", "dummy", imgui_dummy);
    
    /* Layout */
    api->register_function("imgui", "beginWindow", imgui_begin_window);
    api->register_function("imgui", "endWindow", imgui_end_window);
    api->register_function("imgui", "collapsingHeader", imgui_collapsing_header);
    api->register_function("imgui", "treeNode", imgui_tree_node);
    api->register_function("imgui", "treePop", imgui_tree_pop);
    
    /* Positioning */
    api->register_function("imgui", "getCursorPos", imgui_get_cursor_pos);
    api->register_function("imgui", "getWindowSize", imgui_get_window_size);
    api->register_function("imgui", "pushId", imgui_push_id);
    api->register_function("imgui", "popId", imgui_pop_id);
    
    /* Item queries */
    api->register_function("imgui", "isItemHovered", imgui_is_item_hovered);
    api->register_function("imgui", "isItemClicked", imgui_is_item_clicked);
    
    /* Tooltips */
    api->register_function("imgui", "setTooltip", imgui_set_tooltip);
    
    /* Data visualization */
    api->register_function("imgui", "progressBar", imgui_progress_bar);
    api->register_function("imgui", "colorEdit3", imgui_color_edit3);
    api->register_function("imgui", "colorEdit4", imgui_color_edit4);
}

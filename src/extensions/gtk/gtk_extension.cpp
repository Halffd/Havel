/*
 * gtk_extension.cpp - Native GTK4 UI extension with dynamic loading
 *
 * Uses C ABI (HavelCAPI.h) for stability.
 * GTK4 libraries are loaded dynamically at runtime via dlopen/dlsym.
 * No hard link-time dependency on GTK4.
 */

#include "HavelCAPI.h"
#include "DynamicLoader.hpp"

#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstring>

namespace {

/* GTK4 type definitions - forward declarations for dynamic loading */
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkWindow GtkWindow;
typedef struct _GtkApplication GtkApplication;
typedef struct _GtkTextBuffer GtkTextBuffer;
typedef struct _GtkTextView GtkTextView;
typedef struct _GtkDialog GtkDialog;
typedef struct _GtkAdjustment GtkAdjustment;
typedef struct _GtkPixbuf GtkPixbuf;
typedef struct _GdkPixbuf GdkPixbuf;
typedef struct _GtkStringList GtkStringList;
typedef struct _GtkSingleSelection GtkSingleSelection;

/* GTK4 function pointer types - Core */
typedef void (*GtkInitFn)(int* argc, char*** argv);
typedef void (*GtkMainFn)(void);
typedef void (*GtkQuitFn)(void);

/* GTK4 function pointer type aliases - Core (for macro compatibility) */
typedef GtkInitFn gtk_initFn;
typedef GtkMainFn gtk_mainFn;
typedef GtkQuitFn gtk_quitFn;

/* GTK4 function pointer types - Window */
typedef GtkWidget* (*GtkWindowNewFn)(void);
typedef void (*GtkWindowSetTitleFn)(GtkWindow* win, const char* title);
typedef void (*GtkWindowSetDefaultSizeFn)(GtkWindow* win, int width, int height);
typedef void (*GtkWindowMoveFn)(GtkWindow* win, int x, int y);
typedef void (*GtkWindowSetPositionFn)(GtkWindow* win, int pos);
typedef void (*GtkWindowShowFn)(GtkWidget* win);
typedef void (*GtkWindowSetChildFn)(GtkWindow* win, GtkWidget* child);
typedef void (*GtkWindowDestroyFn)(GtkWindow* win);
typedef void (*GtkWindowCloseFn)(GtkWindow* win);
typedef int (*GtkWindowGetWidthFn)(GtkWindow* win);
typedef int (*GtkWindowGetHeightFn)(GtkWindow* win);
typedef void (*GtkWindowSetResizableFn)(GtkWindow* win, int resizable);
typedef int (*GtkWindowGetResizableFn)(GtkWindow* win);
typedef void (*GtkWindowSetDecoratedFn)(GtkWindow* win, int decorated);
typedef void (*GtkWindowSetModalFn)(GtkWindow* win, int modal);
typedef int (*GtkWindowIsModalFn)(GtkWindow* win);
typedef void (*GtkWindowMaximizeFn)(GtkWindow* win);
typedef void (*GtkWindowUnmaximizeFn)(GtkWindow* win);
typedef int (*GtkWindowIsMaximizedFn)(GtkWindow* win);
typedef void (*GtkWindowMinimizeFn)(GtkWindow* win);
typedef void (*GtkWindowPresentFn)(GtkWindow* win);

/* Typedef aliases for macro naming convention */
typedef GtkWindowNewFn gtk_window_newFn;
typedef GtkWindowSetTitleFn gtk_window_set_titleFn;
typedef GtkWindowSetDefaultSizeFn gtk_window_set_default_sizeFn;
typedef GtkWindowMoveFn gtk_window_moveFn;
typedef GtkWindowSetPositionFn gtk_window_set_positionFn;
typedef GtkWindowShowFn gtk_window_showFn;
typedef GtkWindowSetChildFn gtk_window_set_childFn;
typedef GtkWindowDestroyFn gtk_window_destroyFn;
typedef GtkWindowCloseFn gtk_window_closeFn;
typedef GtkWindowGetWidthFn gtk_window_get_widthFn;
typedef GtkWindowGetHeightFn gtk_window_get_heightFn;
typedef GtkWindowSetResizableFn gtk_window_set_resizableFn;
typedef GtkWindowGetResizableFn gtk_window_get_resizableFn;
typedef GtkWindowSetDecoratedFn gtk_window_set_decoratedFn;
typedef GtkWindowSetModalFn gtk_window_set_modalFn;
typedef GtkWindowIsModalFn gtk_window_is_modalFn;
typedef GtkWindowMaximizeFn gtk_window_maximizeFn;
typedef GtkWindowUnmaximizeFn gtk_window_unmaximizeFn;
typedef GtkWindowIsMaximizedFn gtk_window_is_maximizedFn;
typedef GtkWindowMinimizeFn gtk_window_minimizeFn;
typedef GtkWindowPresentFn gtk_window_presentFn;

/* GTK4 function pointer types - Label */
typedef GtkWidget* (*GtkLabelNewFn)(const char* str);
typedef void (*GtkLabelSetTextFn)(GtkWidget* label, const char* text);
typedef const char* (*GtkLabelGetTextFn)(GtkWidget* label);
typedef void (*GtkLabelSetMarkupFn)(GtkWidget* label, const char* markup);
typedef void (*GtkLabelSetJustifyFn)(GtkWidget* label, int justification);
typedef void (*GtkLabelSetXalignFn)(GtkWidget* label, float xalign);
typedef void (*GtkLabelSetYalignFn)(GtkWidget* label, float yalign);
typedef void (*GtkLabelSetSelectableFn)(GtkWidget* label, int selectable);
typedef int (*GtkLabelGetSelectableFn)(GtkWidget* label);
typedef void (*GtkLabelSetLineWrapFn)(GtkWidget* label, int wrap);
typedef void (*GtkLabelSetEllipsizeFn)(GtkWidget* label, int mode);

/* Typedef aliases for label functions */
typedef GtkLabelNewFn gtk_label_newFn;
typedef GtkLabelSetTextFn gtk_label_set_textFn;
typedef GtkLabelGetTextFn gtk_label_get_textFn;
typedef GtkLabelSetMarkupFn gtk_label_set_markupFn;
typedef GtkLabelSetJustifyFn gtk_label_set_justifyFn;
typedef GtkLabelSetXalignFn gtk_label_set_xalignFn;
typedef GtkLabelSetYalignFn gtk_label_set_yalignFn;
typedef GtkLabelSetSelectableFn gtk_label_set_selectableFn;
typedef GtkLabelGetSelectableFn gtk_label_get_selectableFn;
typedef GtkLabelSetLineWrapFn gtk_label_set_line_wrapFn;
typedef GtkLabelSetEllipsizeFn gtk_label_set_ellipsizeFn;

/* GTK4 function pointer types - Button */
typedef GtkWidget* (*GtkButtonNewFn)(void);
typedef GtkWidget* (*GtkButtonNewWithLabelFn)(const char* label);
typedef void (*GtkButtonSetLabelFn)(GtkWidget* button, const char* label);
typedef const char* (*GtkButtonGetLabelFn)(GtkWidget* button);
typedef void (*GtkButtonSetIconNameFn)(GtkWidget* button, const char* name);
typedef void (*GtkButtonSetChildFn)(GtkWidget* button, GtkWidget* child);
typedef void (*GtkButtonClickedFn)(GtkWidget* button);
typedef int (*GtkButtonGetHasFrameFn)(GtkWidget* button);
typedef void (*GtkButtonSetHasFrameFn)(GtkWidget* button, int has_frame);

/* Typedef aliases for Button functions */
typedef GtkButtonNewFn gtk_button_newFn;
typedef GtkButtonNewWithLabelFn gtk_button_new_with_labelFn;
typedef GtkButtonSetLabelFn gtk_button_set_labelFn;
typedef GtkButtonGetLabelFn gtk_button_get_labelFn;
typedef GtkButtonSetIconNameFn gtk_button_set_icon_nameFn;
typedef GtkButtonSetChildFn gtk_button_set_childFn;
typedef GtkButtonClickedFn gtk_button_clickedFn;
typedef GtkButtonGetHasFrameFn gtk_button_get_has_frameFn;
typedef GtkButtonSetHasFrameFn gtk_button_set_has_frameFn;

/* GTK4 function pointer types - ToggleButton/CheckButton */
typedef GtkWidget* (*GtkToggleButtonNewFn)(void);
typedef GtkWidget* (*GtkToggleButtonNewWithLabelFn)(const char* label);
typedef void (*GtkToggleButtonSetActiveFn)(GtkWidget* button, int is_active);
typedef int (*GtkToggleButtonGetActiveFn)(GtkWidget* button);
typedef void (*GtkToggleButtonToggledFn)(GtkWidget* button);
typedef GtkWidget* (*GtkCheckButtonNewFn)(void);
typedef GtkWidget* (*GtkCheckButtonNewWithLabelFn)(const char* label);
typedef void (*GtkCheckButtonSetGroupFn)(GtkWidget* button, GtkWidget* group);
typedef GtkWidget* (*GtkCheckButtonGetGroupFn)(GtkWidget* button);

/* Typedef aliases for ToggleButton/CheckButton functions */
typedef GtkToggleButtonNewFn gtk_toggle_button_newFn;
typedef GtkToggleButtonNewWithLabelFn gtk_toggle_button_new_with_labelFn;
typedef GtkToggleButtonSetActiveFn gtk_toggle_button_set_activeFn;
typedef GtkToggleButtonGetActiveFn gtk_toggle_button_get_activeFn;
typedef GtkToggleButtonToggledFn gtk_toggle_button_toggledFn;
typedef GtkCheckButtonNewFn gtk_check_button_newFn;
typedef GtkCheckButtonNewWithLabelFn gtk_check_button_new_with_labelFn;
typedef GtkCheckButtonSetGroupFn gtk_check_button_set_groupFn;
typedef GtkCheckButtonGetGroupFn gtk_check_button_get_groupFn;

/* GTK4 function pointer types - Switch */
typedef GtkWidget* (*GtkSwitchNewFn)(void);
typedef void (*GtkSwitchSetActiveFn)(GtkWidget* sw, int is_active);
typedef int (*GtkSwitchGetActiveFn)(GtkWidget* sw);
typedef void (*GtkSwitchSetStateFn)(GtkWidget* sw, int state);
typedef int (*GtkSwitchGetStateFn)(GtkWidget* sw);

/* Typedef aliases for Switch functions */
typedef GtkSwitchNewFn gtk_switch_newFn;
typedef GtkSwitchSetActiveFn gtk_switch_set_activeFn;
typedef GtkSwitchGetActiveFn gtk_switch_get_activeFn;
typedef GtkSwitchSetStateFn gtk_switch_set_stateFn;
typedef GtkSwitchGetStateFn gtk_switch_get_stateFn;

/* GTK4 function pointer types - Entry */
typedef GtkWidget* (*GtkEntryNewFn)(void);
typedef const char* (*GtkEntryGetTextFn)(GtkWidget* entry);
typedef void (*GtkEntrySetTextFn)(GtkWidget* entry, const char* text);
typedef void (*GtkEntryAppendTextFn)(GtkWidget* entry, const char* text);
typedef int (*GtkEntryGetTextLengthFn)(GtkWidget* entry);
typedef void (*GtkEntrySetPlaceholderTextFn)(GtkWidget* entry, const char* text);
typedef const char* (*GtkEntryGetPlaceholderTextFn)(GtkWidget* entry);
typedef void (*GtkEntrySetEditableFn)(GtkWidget* entry, int editable);
typedef int (*GtkEntryGetEditableFn)(GtkWidget* entry);
typedef void (*GtkEntrySetVisibilityFn)(GtkWidget* entry, int visible);
typedef int (*GtkEntryGetVisibilityFn)(GtkWidget* entry);
typedef void (*GtkEntrySetMaxLengthFn)(GtkWidget* entry, int length);
typedef int (*GtkEntryGetMaxLengthFn)(GtkWidget* entry);
typedef void (*GtkEntrySetActivatesDefaultFn)(GtkWidget* entry, int activates);
typedef void (*GtkEntrySetWidthCharsFn)(GtkWidget* entry, int n_chars);
typedef void (*GtkEntrySetXalignFn)(GtkWidget* entry, float xalign);
typedef void (*GtkEntrySetInputPurposeFn)(GtkWidget* entry, int purpose);

/* Typedef aliases for Entry functions */
typedef GtkEntryNewFn gtk_entry_newFn;
typedef GtkEntryGetTextFn gtk_entry_get_textFn;
typedef GtkEntrySetTextFn gtk_entry_set_textFn;
typedef GtkEntryAppendTextFn gtk_entry_append_textFn;
typedef GtkEntryGetTextLengthFn gtk_entry_get_text_lengthFn;
typedef GtkEntrySetPlaceholderTextFn gtk_entry_set_placeholder_textFn;
typedef GtkEntryGetPlaceholderTextFn gtk_entry_get_placeholder_textFn;
typedef GtkEntrySetEditableFn gtk_entry_set_editableFn;
typedef GtkEntryGetEditableFn gtk_entry_get_editableFn;
typedef GtkEntrySetVisibilityFn gtk_entry_set_visibilityFn;
typedef GtkEntryGetVisibilityFn gtk_entry_get_visibilityFn;
typedef GtkEntrySetMaxLengthFn gtk_entry_set_max_lengthFn;
typedef GtkEntryGetMaxLengthFn gtk_entry_get_max_lengthFn;
typedef GtkEntrySetActivatesDefaultFn gtk_entry_set_activates_defaultFn;
typedef GtkEntrySetWidthCharsFn gtk_entry_set_width_charsFn;
typedef GtkEntrySetXalignFn gtk_entry_set_xalignFn;
typedef GtkEntrySetInputPurposeFn gtk_entry_set_input_purposeFn;

/* GTK4 function pointer types - Box */
typedef GtkWidget* (*GtkBoxNewFn)(int orientation, int spacing);
typedef void (*GtkBoxAppendFn)(GtkWidget* box, GtkWidget* child);
typedef void (*GtkBoxPrependFn)(GtkWidget* box, GtkWidget* child);
typedef void (*GtkBoxInsertChildAfterFn)(GtkWidget* box, GtkWidget* child, GtkWidget* sibling);
typedef void (*GtkBoxInsertChildBeforeFn)(GtkWidget* box, GtkWidget* child, GtkWidget* sibling);
typedef void (*GtkBoxRemoveFn)(GtkWidget* box, GtkWidget* child);
typedef void (*GtkBoxReorderChildFn)(GtkWidget* box, GtkWidget* child, int position);
typedef void (*GtkBoxSetSpacingFn)(GtkWidget* box, int spacing);
typedef int (*GtkBoxGetSpacingFn)(GtkWidget* box);
typedef void (*GtkBoxSetHomogeneousFn)(GtkWidget* box, int homogeneous);
typedef int (*GtkBoxGetHomogeneousFn)(GtkWidget* box);

/* Typedef aliases for Box functions */
typedef GtkBoxNewFn gtk_box_newFn;
typedef GtkBoxAppendFn gtk_box_appendFn;
typedef GtkBoxPrependFn gtk_box_prependFn;
typedef GtkBoxInsertChildAfterFn gtk_box_insert_child_afterFn;
typedef GtkBoxInsertChildBeforeFn gtk_box_insert_child_beforeFn;
typedef GtkBoxRemoveFn gtk_box_removeFn;
typedef GtkBoxReorderChildFn gtk_box_reorder_childFn;
typedef GtkBoxSetSpacingFn gtk_box_set_spacingFn;
typedef GtkBoxGetSpacingFn gtk_box_get_spacingFn;
typedef GtkBoxSetHomogeneousFn gtk_box_set_homogeneousFn;
typedef GtkBoxGetHomogeneousFn gtk_box_get_homogeneousFn;

/* GTK4 function pointer types - Grid */
typedef GtkWidget* (*GtkGridNewFn)(void);
typedef void (*GtkGridAttachFn)(GtkWidget* grid, GtkWidget* child, int left, int top, int width, int height);
typedef void (*GtkGridRemoveFn)(GtkWidget* grid, GtkWidget* child);
typedef void (*GtkGridInsertRowFn)(GtkWidget* grid, int position);
typedef void (*GtkGridInsertColumnFn)(GtkWidget* grid, int position);
typedef void (*GtkGridSetRowSpacingFn)(GtkWidget* grid, int spacing);
typedef int (*GtkGridGetRowSpacingFn)(GtkWidget* grid);
typedef void (*GtkGridSetColumnSpacingFn)(GtkWidget* grid, int spacing);
typedef int (*GtkGridGetColumnSpacingFn)(GtkWidget* grid);
typedef void (*GtkGridSetRowHomogeneousFn)(GtkWidget* grid, int homogeneous);
typedef int (*GtkGridGetRowHomogeneousFn)(GtkWidget* grid);
typedef void (*GtkGridSetColumnHomogeneousFn)(GtkWidget* grid, int homogeneous);
typedef int (*GtkGridGetColumnHomogeneousFn)(GtkWidget* grid);

/* Typedef aliases for Grid functions */
typedef GtkGridNewFn gtk_grid_newFn;
typedef GtkGridAttachFn gtk_grid_attachFn;
typedef GtkGridRemoveFn gtk_grid_removeFn;
typedef GtkGridInsertRowFn gtk_grid_insert_rowFn;
typedef GtkGridInsertColumnFn gtk_grid_insert_columnFn;
typedef GtkGridSetRowSpacingFn gtk_grid_set_row_spacingFn;
typedef GtkGridGetRowSpacingFn gtk_grid_get_row_spacingFn;
typedef GtkGridSetColumnSpacingFn gtk_grid_set_column_spacingFn;
typedef GtkGridGetColumnSpacingFn gtk_grid_get_column_spacingFn;
typedef GtkGridSetRowHomogeneousFn gtk_grid_set_row_homogeneousFn;
typedef GtkGridGetRowHomogeneousFn gtk_grid_get_row_homogeneousFn;
typedef GtkGridSetColumnHomogeneousFn gtk_grid_set_column_homogeneousFn;
typedef GtkGridGetColumnHomogeneousFn gtk_grid_get_column_homogeneousFn;

/* GTK4 function pointer types - Fixed */
typedef GtkWidget* (*GtkFixedNewFn)(void);
typedef void (*GtkFixedPutFn)(GtkWidget* fixed, GtkWidget* widget, int x, int y);
typedef void (*GtkFixedMoveFn)(GtkWidget* fixed, GtkWidget* widget, int x, int y);

/* Typedef aliases for Fixed functions */
typedef GtkFixedNewFn gtk_fixed_newFn;
typedef GtkFixedPutFn gtk_fixed_putFn;
typedef GtkFixedMoveFn gtk_fixed_moveFn;

/* GTK4 function pointer types - Frame */
typedef GtkWidget* (*GtkFrameNewFn)(const char* label);
typedef void (*GtkFrameSetLabelFn)(GtkWidget* frame, const char* label);
typedef const char* (*GtkFrameGetLabelFn)(GtkWidget* frame);
typedef void (*GtkFrameSetLabelAlignFn)(GtkWidget* frame, float xalign, float yalign);
typedef void (*GtkFrameSetChildFn)(GtkWidget* frame, GtkWidget* child);

/* Typedef aliases for Frame functions */
typedef GtkFrameNewFn gtk_frame_newFn;
typedef GtkFrameSetLabelFn gtk_frame_set_labelFn;
typedef GtkFrameGetLabelFn gtk_frame_get_labelFn;
typedef GtkFrameSetLabelAlignFn gtk_frame_set_label_alignFn;
typedef GtkFrameSetChildFn gtk_frame_set_childFn;

/* GTK4 function pointer types - Separator */
typedef GtkWidget* (*GtkSeparatorNewFn)(int orientation);

/* Typedef alias for Separator function */
typedef GtkSeparatorNewFn gtk_separator_newFn;

/* GTK4 function pointer types - Paned */
typedef GtkWidget* (*GtkPanedNewFn)(int orientation);
typedef void (*GtkPanedPack1Fn)(GtkWidget* paned, GtkWidget* child, int resize, int shrink);
typedef void (*GtkPanedPack2Fn)(GtkWidget* paned, GtkWidget* child, int resize, int shrink);
typedef void (*GtkPanedSetPositionFn)(GtkWidget* paned, int position);
typedef int (*GtkPanedGetPositionFn)(GtkWidget* paned);
typedef void (*GtkPanedSetWideHandleFn)(GtkWidget* paned, int wide);

/* Typedef aliases for Paned functions */
typedef GtkPanedNewFn gtk_paned_newFn;
typedef GtkPanedPack1Fn gtk_paned_pack1Fn;
typedef GtkPanedPack2Fn gtk_paned_pack2Fn;
typedef GtkPanedSetPositionFn gtk_paned_set_positionFn;
typedef GtkPanedGetPositionFn gtk_paned_get_positionFn;
typedef GtkPanedSetWideHandleFn gtk_paned_set_wide_handleFn;

/* GTK4 function pointer types - Notebook */
typedef GtkWidget* (*GtkNotebookNewFn)(void);
typedef int (*GtkNotebookAppendPageFn)(GtkWidget* notebook, GtkWidget* child, GtkWidget* tab_label);
typedef int (*GtkNotebookPrependPageFn)(GtkWidget* notebook, GtkWidget* child, GtkWidget* tab_label);
typedef int (*GtkNotebookInsertPageFn)(GtkWidget* notebook, GtkWidget* child, GtkWidget* tab_label, int position);
typedef void (*GtkNotebookRemovePageFn)(GtkWidget* notebook, int page_num);
typedef int (*GtkNotebookGetCurrentPageFn)(GtkWidget* notebook);
typedef void (*GtkNotebookSetCurrentPageFn)(GtkWidget* notebook, int page_num);
typedef int (*GtkNotebookGetNPagesFn)(GtkWidget* notebook);
typedef void (*GtkNotebookSetTabLabelTextFn)(GtkWidget* notebook, GtkWidget* child, const char* text);
typedef void (*GtkNotebookSetShowTabsFn)(GtkWidget* notebook, int show_tabs);
typedef void (*GtkNotebookSetScrollableFn)(GtkWidget* notebook, int scrollable);

/* Typedef aliases for Notebook functions */
typedef GtkNotebookNewFn gtk_notebook_newFn;
typedef GtkNotebookAppendPageFn gtk_notebook_append_pageFn;
typedef GtkNotebookPrependPageFn gtk_notebook_prepend_pageFn;
typedef GtkNotebookInsertPageFn gtk_notebook_insert_pageFn;
typedef GtkNotebookRemovePageFn gtk_notebook_remove_pageFn;
typedef GtkNotebookGetCurrentPageFn gtk_notebook_get_current_pageFn;
typedef GtkNotebookSetCurrentPageFn gtk_notebook_set_current_pageFn;
typedef GtkNotebookGetNPagesFn gtk_notebook_get_n_pagesFn;
typedef GtkNotebookSetTabLabelTextFn gtk_notebook_set_tab_label_textFn;
typedef GtkNotebookSetShowTabsFn gtk_notebook_set_show_tabsFn;
typedef GtkNotebookSetScrollableFn gtk_notebook_set_scrollableFn;

/* GTK4 function pointer types - Stack */
typedef GtkWidget* (*GtkStackNewFn)(void);
typedef void (*GtkStackAddNamedFn)(GtkWidget* stack, GtkWidget* widget, const char* name);
typedef void (*GtkStackAddTitledFn)(GtkWidget* stack, GtkWidget* widget, const char* name, const char* title);
typedef void (*GtkStackSetVisibleChildFn)(GtkWidget* stack, GtkWidget* child);
typedef void (*GtkStackSetVisibleChildByNameFn)(GtkWidget* stack, const char* name);
typedef const char* (*GtkStackGetVisibleChildNameFn)(GtkWidget* stack);
typedef void (*GtkStackSetTransitionTypeFn)(GtkWidget* stack, int transition);
typedef void (*GtkStackSetTransitionDurationFn)(GtkWidget* stack, unsigned int duration);

/* Typedef aliases for Stack functions */
typedef GtkStackNewFn gtk_stack_newFn;
typedef GtkStackAddNamedFn gtk_stack_add_namedFn;
typedef GtkStackAddTitledFn gtk_stack_add_titledFn;
typedef GtkStackSetVisibleChildFn gtk_stack_set_visible_childFn;
typedef GtkStackSetVisibleChildByNameFn gtk_stack_set_visible_child_by_nameFn;
typedef GtkStackGetVisibleChildNameFn gtk_stack_get_visible_child_nameFn;
typedef GtkStackSetTransitionTypeFn gtk_stack_set_transition_typeFn;
typedef GtkStackSetTransitionDurationFn gtk_stack_set_transition_durationFn;

/* GTK4 function pointer types - Revealer */
typedef GtkWidget* (*GtkRevealerNewFn)(void);
typedef void (*GtkRevealerSetChildFn)(GtkWidget* revealer, GtkWidget* child);
typedef void (*GtkRevealerSetRevealChildFn)(GtkWidget* revealer, int reveal);
typedef int (*GtkRevealerGetRevealChildFn)(GtkWidget* revealer);
typedef void (*GtkRevealerSetTransitionTypeFn)(GtkWidget* revealer, int transition);
typedef void (*GtkRevealerSetTransitionDurationFn)(GtkWidget* revealer, unsigned int duration);

/* Typedef aliases for Revealer functions */
typedef GtkRevealerNewFn gtk_revealer_newFn;
typedef GtkRevealerSetChildFn gtk_revealer_set_childFn;
typedef GtkRevealerSetRevealChildFn gtk_revealer_set_reveal_childFn;
typedef GtkRevealerGetRevealChildFn gtk_revealer_get_reveal_childFn;
typedef GtkRevealerSetTransitionTypeFn gtk_revealer_set_transition_typeFn;
typedef GtkRevealerSetTransitionDurationFn gtk_revealer_set_transition_durationFn;

/* GTK4 function pointer types - Expander */
typedef GtkWidget* (*GtkExpanderNewFn)(const char* label);
typedef void (*GtkExpanderSetExpandedFn)(GtkWidget* expander, int expanded);
typedef int (*GtkExpanderGetExpandedFn)(GtkWidget* expander);
typedef void (*GtkExpanderSetLabelFn)(GtkWidget* expander, const char* label);

/* Typedef aliases for Expander functions */
typedef GtkExpanderNewFn gtk_expander_newFn;
typedef GtkExpanderSetExpandedFn gtk_expander_set_expandedFn;
typedef GtkExpanderGetExpandedFn gtk_expander_get_expandedFn;
typedef GtkExpanderSetLabelFn gtk_expander_set_labelFn;

/* GTK4 function pointer types - ScrolledWindow */
typedef GtkWidget* (*GtkScrolledWindowNewFn)(void);
typedef void (*GtkScrolledWindowSetChildFn)(GtkWidget* scrolled, GtkWidget* child);
typedef void (*GtkScrolledWindowSetPolicyFn)(GtkWidget* scrolled, int hscrollbar, int vscrollbar);
typedef void (*GtkScrolledWindowSetKineticScrollingFn)(GtkWidget* scrolled, int kinetic);

/* Typedef aliases for ScrolledWindow functions */
typedef GtkScrolledWindowNewFn gtk_scrolled_window_newFn;
typedef GtkScrolledWindowSetChildFn gtk_scrolled_window_set_childFn;
typedef GtkScrolledWindowSetPolicyFn gtk_scrolled_window_set_policyFn;
typedef GtkScrolledWindowSetKineticScrollingFn gtk_scrolled_window_set_kinetic_scrollingFn;

/* GTK4 function pointer types - Viewport */
typedef GtkWidget* (*GtkViewportNewFn)(void);
typedef void (*GtkViewportSetChildFn)(GtkWidget* viewport, GtkWidget* child);

/* Typedef aliases for Viewport functions */
typedef GtkViewportNewFn gtk_viewport_newFn;
typedef GtkViewportSetChildFn gtk_viewport_set_childFn;

/* GTK4 function pointer types - TextView */
typedef GtkWidget* (*GtkTextViewNewFn)(void);
typedef void* (*GtkTextViewGetBufferFn)(GtkWidget* text_view);
typedef void (*GtkTextViewSetBufferFn)(GtkWidget* text_view, void* buffer);
typedef void (*GtkTextViewSetEditableFn)(GtkWidget* text_view, int editable);
typedef void (*GtkTextViewSetWrapModeFn)(GtkWidget* text_view, int wrap_mode);
typedef void (*GtkTextViewSetLeftMarginFn)(GtkWidget* text_view, int margin);
typedef void (*GtkTextViewSetRightMarginFn)(GtkWidget* text_view, int margin);
typedef void (*GtkTextViewSetTopMarginFn)(GtkWidget* text_view, int margin);
typedef void (*GtkTextViewSetBottomMarginFn)(GtkWidget* text_view, int margin);
typedef void (*GtkTextViewSetMonospaceFn)(GtkWidget* text_view, int monospace);

/* Typedef aliases for TextView functions */
typedef GtkTextViewNewFn gtk_text_view_newFn;
typedef GtkTextViewGetBufferFn gtk_text_view_get_bufferFn;
typedef GtkTextViewSetBufferFn gtk_text_view_set_bufferFn;
typedef GtkTextViewSetEditableFn gtk_text_view_set_editableFn;
typedef int (*GtkTextViewGetEditableFn)(GtkWidget* text_view);
typedef GtkTextViewSetWrapModeFn gtk_text_view_set_wrap_modeFn;
typedef GtkTextViewSetLeftMarginFn gtk_text_view_set_left_marginFn;
typedef GtkTextViewSetRightMarginFn gtk_text_view_set_right_marginFn;
typedef GtkTextViewSetTopMarginFn gtk_text_view_set_top_marginFn;
typedef GtkTextViewSetBottomMarginFn gtk_text_view_set_bottom_marginFn;
typedef GtkTextViewSetMonospaceFn gtk_text_view_set_monospaceFn;

/* GTK4 function pointer types - TextBuffer */
typedef void* (*GtkTextBufferNewFn)(void);
typedef char* (*GtkTextBufferGetTextFn)(void* buffer, void* start, void* end, int include_hidden_chars);
typedef void (*GtkTextBufferSetTextFn)(void* buffer, const char* text, int len);
typedef int (*GtkTextBufferGetCharCountFn)(void* buffer);
typedef void (*GtkTextBufferInsertFn)(void* buffer, void* iter, const char* text, int len);
typedef void (*GtkTextBufferInsertAtCursorFn)(void* buffer, const char* text, int len);

/* Typedef aliases for TextBuffer functions */
typedef GtkTextBufferNewFn gtk_text_buffer_newFn;
typedef GtkTextBufferGetTextFn gtk_text_buffer_get_textFn;
typedef GtkTextBufferSetTextFn gtk_text_buffer_set_textFn;
typedef GtkTextBufferGetCharCountFn gtk_text_buffer_get_char_countFn;
typedef GtkTextBufferInsertFn gtk_text_buffer_insertFn;
typedef GtkTextBufferInsertAtCursorFn gtk_text_buffer_insert_at_cursorFn;

/* GTK4 function pointer types - Image */
typedef GtkWidget* (*GtkImageNewFn)(void);
typedef GtkWidget* (*GtkImageNewFromIconNameFn)(const char* icon_name);
typedef void (*GtkImageSetFromIconNameFn)(GtkWidget* image, const char* icon_name);
typedef void (*GtkImageSetFromPixbufFn)(GtkWidget* image, GdkPixbuf* pixbuf);
typedef void (*GtkImageClearFn)(GtkWidget* image);

/* Typedef aliases for Image functions */
typedef GtkImageNewFn gtk_image_newFn;
typedef GtkImageNewFromIconNameFn gtk_image_new_from_icon_nameFn;
typedef GtkImageSetFromIconNameFn gtk_image_set_from_icon_nameFn;
typedef GtkImageSetFromPixbufFn gtk_image_set_from_pixbufFn;
typedef GtkImageClearFn gtk_image_clearFn;

/* GTK4 function pointer types - ProgressBar */
typedef GtkWidget* (*GtkProgressBarNewFn)(void);
typedef void (*GtkProgressBarSetFractionFn)(GtkWidget* progress, double fraction);
typedef double (*GtkProgressBarGetFractionFn)(GtkWidget* progress);
typedef void (*GtkProgressBarSetPulseFn)(GtkWidget* progress, double fraction);
typedef void (*GtkProgressBarPulseFn)(GtkWidget* progress);
typedef void (*GtkProgressBarSetTextFn)(GtkWidget* progress, const char* text);
typedef const char* (*GtkProgressBarGetTextFn)(GtkWidget* progress);
typedef void (*GtkProgressBarSetShowTextFn)(GtkWidget* progress, int show_text);
typedef void (*GtkProgressBarSetEllipsizeFn)(GtkWidget* progress, int mode);

/* Typedef aliases for ProgressBar functions */
typedef GtkProgressBarNewFn gtk_progress_bar_newFn;
typedef GtkProgressBarSetFractionFn gtk_progress_bar_set_fractionFn;
typedef GtkProgressBarGetFractionFn gtk_progress_bar_get_fractionFn;
typedef GtkProgressBarSetPulseFn gtk_progress_bar_set_pulseFn;
typedef GtkProgressBarPulseFn gtk_progress_bar_pulseFn;
typedef GtkProgressBarSetTextFn gtk_progress_bar_set_textFn;
typedef GtkProgressBarGetTextFn gtk_progress_bar_get_textFn;
typedef GtkProgressBarSetShowTextFn gtk_progress_bar_set_show_textFn;
typedef GtkProgressBarSetEllipsizeFn gtk_progress_bar_set_ellipsizeFn;

/* GTK4 function pointer types - LevelBar */
typedef GtkWidget* (*GtkLevelBarNewFn)(void);
typedef GtkWidget* (*GtkLevelBarNewForIntervalFn)(double min_value, double max_value);
typedef void (*GtkLevelBarSetValueFn)(GtkWidget* levelbar, double value);
typedef double (*GtkLevelBarGetValueFn)(GtkWidget* levelbar);
typedef void (*GtkLevelBarSetMinValueFn)(GtkWidget* levelbar, double value);
typedef double (*GtkLevelBarGetMinValueFn)(GtkWidget* levelbar);
typedef void (*GtkLevelBarSetMaxValueFn)(GtkWidget* levelbar, double value);
typedef double (*GtkLevelBarGetMaxValueFn)(GtkWidget* levelbar);
typedef void (*GtkLevelBarSetBarModeFn)(GtkWidget* levelbar, int mode);
typedef int (*GtkLevelBarGetBarModeFn)(GtkWidget* levelbar);

/* Typedef aliases for LevelBar functions */
typedef GtkLevelBarNewFn gtk_level_bar_newFn;
typedef GtkLevelBarNewForIntervalFn gtk_level_bar_new_for_intervalFn;
typedef GtkLevelBarSetValueFn gtk_level_bar_set_valueFn;
typedef GtkLevelBarGetValueFn gtk_level_bar_get_valueFn;
typedef GtkLevelBarSetMinValueFn gtk_level_bar_set_min_valueFn;
typedef GtkLevelBarGetMinValueFn gtk_level_bar_get_min_valueFn;
typedef GtkLevelBarSetMaxValueFn gtk_level_bar_set_max_valueFn;
typedef GtkLevelBarGetMaxValueFn gtk_level_bar_get_max_valueFn;
typedef GtkLevelBarSetBarModeFn gtk_level_bar_set_bar_modeFn;
typedef GtkLevelBarGetBarModeFn gtk_level_bar_get_bar_modeFn;

/* GTK4 function pointer types - Scale */
typedef GtkWidget* (*GtkScaleNewFn)(int orientation, GtkAdjustment* adjustment);
typedef GtkWidget* (*GtkScaleNewWithRangeFn)(int orientation, double min, double max, double step);
typedef void (*GtkScaleSetDigitsFn)(GtkWidget* scale, int digits);
typedef int (*GtkScaleGetDigitsFn)(GtkWidget* scale);
typedef void (*GtkScaleSetDrawValueFn)(GtkWidget* scale, int draw_value);
typedef int (*GtkScaleGetDrawValueFn)(GtkWidget* scale);
typedef void (*GtkScaleSetValueFn)(GtkWidget* scale, double value);
typedef double (*GtkScaleGetValueFn)(GtkWidget* scale);
typedef void (*GtkScaleSetHasOriginFn)(GtkWidget* scale, int has_origin);
typedef void (*GtkScaleSetFillLevelFn)(GtkWidget* scale, double fill_level);

/* Typedef aliases for Scale functions */
typedef GtkScaleNewFn gtk_scale_newFn;
typedef GtkScaleNewWithRangeFn gtk_scale_new_with_rangeFn;
typedef GtkScaleSetDigitsFn gtk_scale_set_digitsFn;
typedef GtkScaleGetDigitsFn gtk_scale_get_digitsFn;
typedef GtkScaleSetDrawValueFn gtk_scale_set_draw_valueFn;
typedef GtkScaleGetDrawValueFn gtk_scale_get_draw_valueFn;
typedef GtkScaleSetValueFn gtk_scale_set_valueFn;
typedef GtkScaleGetValueFn gtk_scale_get_valueFn;
typedef GtkScaleSetHasOriginFn gtk_scale_set_has_originFn;
typedef GtkScaleSetFillLevelFn gtk_scale_set_fill_levelFn;

/* GTK4 function pointer types - SpinButton */
typedef GtkWidget* (*GtkSpinButtonNewFn)(GtkAdjustment* adjustment, double climb_rate, unsigned int digits);
typedef GtkWidget* (*GtkSpinButtonNewWithRangeFn)(double min, double max, double step);
typedef void (*GtkSpinButtonSetValueFn)(GtkWidget* spin, double value);
typedef double (*GtkSpinButtonGetValueFn)(GtkWidget* spin);
typedef int (*GtkSpinButtonGetValueAsIntFn)(GtkWidget* spin);
typedef void (*GtkSpinButtonSetRangeFn)(GtkWidget* spin, double min, double max);
typedef void (*GtkSpinButtonSetIncrementsFn)(GtkWidget* spin, double step, double page);
typedef void (*GtkSpinButtonSetDigitsFn)(GtkWidget* spin, unsigned int digits);
typedef void (*GtkSpinButtonSpinFn)(GtkWidget* spin, int direction, double increment);

/* Typedef aliases for SpinButton functions */
typedef GtkSpinButtonNewFn gtk_spin_button_newFn;
typedef GtkSpinButtonNewWithRangeFn gtk_spin_button_new_with_rangeFn;
typedef GtkSpinButtonSetValueFn gtk_spin_button_set_valueFn;
typedef GtkSpinButtonGetValueFn gtk_spin_button_get_valueFn;
typedef GtkSpinButtonGetValueAsIntFn gtk_spin_button_get_value_as_intFn;
typedef GtkSpinButtonSetRangeFn gtk_spin_button_set_rangeFn;
typedef GtkSpinButtonSetIncrementsFn gtk_spin_button_set_incrementsFn;
typedef GtkSpinButtonSetDigitsFn gtk_spin_button_set_digitsFn;
typedef GtkSpinButtonSpinFn gtk_spin_button_spinFn;

/* GTK4 function pointer types - ComboBox/ComboBoxText */
typedef GtkWidget* (*GtkComboBoxTextNewFn)(void);
typedef void (*GtkComboBoxTextAppendFn)(GtkWidget* combo, const char* id, const char* text);
typedef void (*GtkComboBoxTextPrependFn)(GtkWidget* combo, const char* id, const char* text);
typedef void (*GtkComboBoxTextInsertFn)(GtkWidget* combo, int position, const char* id, const char* text);
typedef void (*GtkComboBoxTextRemoveFn)(GtkWidget* combo, int position);
typedef void (*GtkComboBoxTextRemoveAllFn)(GtkWidget* combo);
typedef const char* (*GtkComboBoxTextGetActiveIdFn)(GtkWidget* combo);
typedef void (*GtkComboBoxTextSetActiveIdFn)(GtkWidget* combo, const char* id);
typedef void (*GtkComboBoxTextSetActiveFn)(GtkWidget* combo, int index_);
typedef int (*GtkComboBoxTextGetActiveFn)(GtkWidget* combo);
typedef const char* (*GtkComboBoxTextGetActiveTextFn)(GtkWidget* combo);

/* Typedef aliases for ComboBoxText functions */
typedef GtkComboBoxTextNewFn gtk_combo_box_text_newFn;
typedef GtkComboBoxTextAppendFn gtk_combo_box_text_appendFn;
typedef GtkComboBoxTextPrependFn gtk_combo_box_text_prependFn;
typedef GtkComboBoxTextInsertFn gtk_combo_box_text_insertFn;
typedef GtkComboBoxTextRemoveFn gtk_combo_box_text_removeFn;
typedef GtkComboBoxTextRemoveAllFn gtk_combo_box_text_remove_allFn;
typedef GtkComboBoxTextGetActiveIdFn gtk_combo_box_text_get_active_idFn;
typedef GtkComboBoxTextSetActiveIdFn gtk_combo_box_text_set_active_idFn;
typedef GtkComboBoxTextSetActiveFn gtk_combo_box_text_set_activeFn;
typedef GtkComboBoxTextGetActiveFn gtk_combo_box_text_get_activeFn;
typedef GtkComboBoxTextGetActiveTextFn gtk_combo_box_text_get_active_textFn;

/* GTK4 function pointer types - ListBox */
typedef GtkWidget* (*GtkListBoxNewFn)(void);
typedef void (*GtkListBoxAppendFn)(GtkWidget* listbox, GtkWidget* child);
typedef void (*GtkListBoxPrependFn)(GtkWidget* listbox, GtkWidget* child);
typedef void (*GtkListBoxInsertFn)(GtkWidget* listbox, GtkWidget* child, int position);
typedef void (*GtkListBoxRemoveFn)(GtkWidget* listbox, GtkWidget* child);
typedef void (*GtkListBoxSelectRowFn)(GtkWidget* listbox, void* row);
typedef void (*GtkListBoxUnselectRowFn)(GtkWidget* listbox, void* row);
typedef void (*GtkListBoxUnselectAllFn)(GtkWidget* listbox);
typedef void (*GtkListBoxSetSelectionModeFn)(GtkWidget* listbox, int mode);
typedef int (*GtkListBoxGetSelectionModeFn)(GtkWidget* listbox);

/* Typedef aliases for ListBox functions */
typedef GtkListBoxNewFn gtk_list_box_newFn;
typedef GtkListBoxAppendFn gtk_list_box_appendFn;
typedef GtkListBoxPrependFn gtk_list_box_prependFn;
typedef GtkListBoxInsertFn gtk_list_box_insertFn;
typedef GtkListBoxRemoveFn gtk_list_box_removeFn;
typedef GtkListBoxSelectRowFn gtk_list_box_select_rowFn;
typedef GtkListBoxUnselectRowFn gtk_list_box_unselect_rowFn;
typedef GtkListBoxUnselectAllFn gtk_list_box_unselect_allFn;
typedef GtkListBoxSetSelectionModeFn gtk_list_box_set_selection_modeFn;
typedef GtkListBoxGetSelectionModeFn gtk_list_box_get_selection_modeFn;

/* GTK4 function pointer types - FlowBox */
typedef GtkWidget* (*GtkFlowBoxNewFn)(void);
typedef void (*GtkFlowBoxAppendFn)(GtkWidget* flowbox, GtkWidget* child);
typedef void (*GtkFlowBoxInsertFn)(GtkWidget* flowbox, GtkWidget* child, int position);
typedef void (*GtkFlowBoxSelectChildFn)(GtkWidget* flowbox, void* child);
typedef void (*GtkFlowBoxUnselectAllFn)(GtkWidget* flowbox);
typedef void (*GtkFlowBoxSetSelectionModeFn)(GtkWidget* flowbox, int mode);
typedef void (*GtkFlowBoxSetMinChildrenPerLineFn)(GtkWidget* flowbox, int n_children);
typedef void (*GtkFlowBoxSetMaxChildrenPerLineFn)(GtkWidget* flowbox, int n_children);
typedef void (*GtkFlowBoxSetColumnSpacingFn)(GtkWidget* flowbox, int spacing);
typedef void (*GtkFlowBoxSetRowSpacingFn)(GtkWidget* flowbox, int spacing);

/* Typedef aliases for FlowBox functions */
typedef GtkFlowBoxNewFn gtk_flow_box_newFn;
typedef GtkFlowBoxAppendFn gtk_flow_box_appendFn;
typedef GtkFlowBoxInsertFn gtk_flow_box_insertFn;
typedef GtkFlowBoxSelectChildFn gtk_flow_box_select_childFn;
typedef GtkFlowBoxUnselectAllFn gtk_flow_box_unselect_allFn;
typedef GtkFlowBoxSetSelectionModeFn gtk_flow_box_set_selection_modeFn;
typedef GtkFlowBoxSetMinChildrenPerLineFn gtk_flow_box_set_min_children_per_lineFn;
typedef GtkFlowBoxSetMaxChildrenPerLineFn gtk_flow_box_set_max_children_per_lineFn;
typedef GtkFlowBoxSetColumnSpacingFn gtk_flow_box_set_column_spacingFn;
typedef GtkFlowBoxSetRowSpacingFn gtk_flow_box_set_row_spacingFn;

/* GTK4 function pointer types - Menu */
typedef GtkWidget* (*GtkMenuBarNewFn)(void);
typedef void (*GtkMenuBarAppendFn)(GtkWidget* menubar, GtkWidget* item);
typedef GtkWidget* (*GtkMenuNewFn)(void);
typedef void (*GtkMenuAppendFn)(GtkWidget* menu, GtkWidget* child);
typedef void (*GtkMenuPrependFn)(GtkWidget* menu, GtkWidget* child);
typedef void (*GtkMenuInsertFn)(GtkWidget* menu, GtkWidget* child, int position);
typedef void (*GtkMenuRemoveFn)(GtkWidget* menu, GtkWidget* child);
typedef void (*GtkMenuPopupAtPointerFn)(GtkWidget* menu, void* trigger_event);
typedef GtkWidget* (*GtkMenuItemNewFn)(void);
typedef GtkWidget* (*GtkMenuItemNewWithLabelFn)(const char* label);
typedef void (*GtkMenuItemSetLabelFn)(GtkWidget* menuitem, const char* label);
typedef void (*GtkMenuItemSetSubmenuFn)(GtkWidget* menuitem, GtkWidget* submenu);
typedef void (*GtkMenuItemActivateFn)(GtkWidget* menuitem);

/* Typedef aliases for Menu functions */
typedef GtkMenuBarNewFn gtk_menu_bar_newFn;
typedef GtkMenuBarAppendFn gtk_menu_bar_appendFn;
typedef GtkMenuNewFn gtk_menu_newFn;
typedef GtkMenuAppendFn gtk_menu_appendFn;
typedef GtkMenuPrependFn gtk_menu_prependFn;
typedef GtkMenuInsertFn gtk_menu_insertFn;
typedef GtkMenuRemoveFn gtk_menu_removeFn;
typedef GtkMenuPopupAtPointerFn gtk_menu_popup_at_pointerFn;
typedef GtkMenuItemNewFn gtk_menu_item_newFn;
typedef GtkMenuItemNewWithLabelFn gtk_menu_item_new_with_labelFn;
typedef GtkMenuItemSetLabelFn gtk_menu_item_set_labelFn;
typedef GtkMenuItemSetSubmenuFn gtk_menu_item_set_submenuFn;
typedef GtkMenuItemActivateFn gtk_menu_item_activateFn;

/* GTK4 function pointer types - Popover */
typedef GtkWidget* (*GtkPopoverNewFn)(void);
typedef void (*GtkPopoverSetChildFn)(GtkWidget* popover, GtkWidget* child);
typedef void (*GtkPopoverPopupFn)(GtkWidget* popover);
typedef void (*GtkPopoverPopdownFn)(GtkWidget* popover);

/* Typedef aliases for Popover functions */
typedef GtkPopoverNewFn gtk_popover_newFn;
typedef GtkPopoverSetChildFn gtk_popover_set_childFn;
typedef GtkPopoverPopupFn gtk_popover_popupFn;
typedef GtkPopoverPopdownFn gtk_popover_popdownFn;

/* GTK4 function pointer types - Dialog */
typedef GtkWidget* (*GtkDialogNewFn)(void);
typedef void (*GtkDialogAddButtonFn)(GtkWidget* dialog, const char* button_text, int response_id);
typedef void (*GtkDialogAddActionWidgetFn)(GtkWidget* dialog, GtkWidget* child, int response_id);
typedef int (*GtkDialogRunFn)(GtkWidget* dialog);
typedef void (*GtkDialogResponseFn)(GtkWidget* dialog, int response_id);
typedef void (*GtkDialogSetDefaultResponseFn)(GtkWidget* dialog, int response_id);
typedef void (*GtkDialogSetUseHeaderBarFn)(GtkWidget* dialog, int setting);
typedef GtkWidget* (*GtkDialogGetContentAreaFn)(GtkWidget* dialog);
typedef GtkWidget* (*GtkDialogGetHeaderBarFn)(GtkWidget* dialog);

/* Typedef aliases for Dialog functions */
typedef GtkDialogNewFn gtk_dialog_newFn;
typedef GtkDialogAddButtonFn gtk_dialog_add_buttonFn;
typedef GtkDialogAddActionWidgetFn gtk_dialog_add_action_widgetFn;
typedef GtkDialogRunFn gtk_dialog_runFn;
typedef GtkDialogResponseFn gtk_dialog_responseFn;
typedef GtkDialogSetDefaultResponseFn gtk_dialog_set_default_responseFn;
typedef GtkDialogSetUseHeaderBarFn gtk_dialog_set_use_header_barFn;
typedef GtkDialogGetContentAreaFn gtk_dialog_get_content_areaFn;
typedef GtkDialogGetHeaderBarFn gtk_dialog_get_header_barFn;

/* GTK4 function pointer types - AboutDialog */
typedef GtkWidget* (*GtkAboutDialogNewFn)(void);
typedef void (*GtkAboutDialogSetProgramNameFn)(GtkWidget* dialog, const char* name);
typedef void (*GtkAboutDialogSetVersionFn)(GtkWidget* dialog, const char* version);
typedef void (*GtkAboutDialogSetCommentsFn)(GtkWidget* dialog, const char* comments);
typedef void (*GtkAboutDialogSetWebsiteFn)(GtkWidget* dialog, const char* website);
typedef void (*GtkAboutDialogSetAuthorsFn)(GtkWidget* dialog, void** authors);
typedef void (*GtkAboutDialogSetLicenseFn)(GtkWidget* dialog, const char* license);
typedef void (*GtkAboutDialogSetLogoFn)(GtkWidget* dialog, GdkPixbuf* logo);

/* Typedef aliases for AboutDialog functions */
typedef GtkAboutDialogNewFn gtk_about_dialog_newFn;
typedef GtkAboutDialogSetProgramNameFn gtk_about_dialog_set_program_nameFn;
typedef GtkAboutDialogSetVersionFn gtk_about_dialog_set_versionFn;
typedef GtkAboutDialogSetCommentsFn gtk_about_dialog_set_commentsFn;
typedef GtkAboutDialogSetWebsiteFn gtk_about_dialog_set_websiteFn;
typedef GtkAboutDialogSetLicenseFn gtk_about_dialog_set_licenseFn;

/* GTK4 function pointer types - MessageDialog */
typedef GtkWidget* (*GtkMessageDialogNewFn)(GtkWindow* parent, int flags, int type, int buttons, const char* message_format, ...);
typedef void (*GtkMessageDialogSetMarkupFn)(GtkWidget* dialog, const char* str);
typedef void (*GtkMessageDialogFormatSecondaryTextFn)(GtkWidget* dialog, const char* message_format, ...);

/* Typedef aliases for MessageDialog functions */
typedef GtkMessageDialogNewFn gtk_message_dialog_newFn;
typedef GtkMessageDialogSetMarkupFn gtk_message_dialog_set_markupFn;

/* GTK4 function pointer types - FileChooserDialog */
typedef GtkWidget* (*GtkFileChooserDialogNewFn)(const char* title, GtkWindow* parent, int action, const char* first_button_text, int first_response_id, void* end);
typedef void (*GtkFileChooserSetActionFn)(GtkWidget* chooser, int action);
typedef int (*GtkFileChooserGetActionFn)(GtkWidget* chooser);
typedef void (*GtkFileChooserSetCurrentFolderFn)(GtkWidget* chooser, void* file, void** error);
typedef void* (*GtkFileChooserGetFileFn)(GtkWidget* chooser);
typedef void (*GtkFileChooserSetFilterFn)(GtkWidget* chooser, void* filter);

/* Typedef aliases for FileChooser functions */
typedef GtkFileChooserDialogNewFn gtk_file_chooser_dialog_newFn;
typedef GtkFileChooserSetActionFn gtk_file_chooser_set_actionFn;
typedef GtkFileChooserGetActionFn gtk_file_chooser_get_actionFn;
typedef GtkFileChooserSetCurrentFolderFn gtk_file_chooser_set_current_folderFn;
typedef GtkFileChooserGetFileFn gtk_file_chooser_get_fileFn;
typedef GtkFileChooserSetFilterFn gtk_file_chooser_set_filterFn;

/* GTK4 function pointer types - HeaderBar */
typedef GtkWidget* (*GtkHeaderBarNewFn)(void);
typedef void (*GtkHeaderBarSetTitleFn)(GtkWidget* bar, const char* title);
typedef const char* (*GtkHeaderBarGetTitleFn)(GtkWidget* bar);
typedef void (*GtkHeaderBarSetSubtitleFn)(GtkWidget* bar, const char* subtitle);
typedef const char* (*GtkHeaderBarGetSubtitleFn)(GtkWidget* bar);
typedef void (*GtkHeaderBarSetShowTitleButtonsFn)(GtkWidget* bar, int show);
typedef void (*GtkHeaderBarPackStartFn)(GtkWidget* bar, GtkWidget* child);
typedef void (*GtkHeaderBarPackEndFn)(GtkWidget* bar, GtkWidget* child);
typedef void (*GtkHeaderBarAppendFn)(GtkWidget* bar, GtkWidget* child);
typedef void (*GtkHeaderBarInsertChildAfterFn)(GtkWidget* bar, GtkWidget* child, GtkWidget* sibling);

/* Typedef aliases for HeaderBar functions */
typedef GtkHeaderBarNewFn gtk_header_bar_newFn;
typedef GtkHeaderBarSetTitleFn gtk_header_bar_set_titleFn;
typedef GtkHeaderBarGetTitleFn gtk_header_bar_get_titleFn;
typedef GtkHeaderBarSetSubtitleFn gtk_header_bar_set_subtitleFn;
typedef GtkHeaderBarGetSubtitleFn gtk_header_bar_get_subtitleFn;
typedef GtkHeaderBarSetShowTitleButtonsFn gtk_header_bar_set_show_title_buttonsFn;
typedef GtkHeaderBarPackStartFn gtk_header_bar_pack_startFn;
typedef GtkHeaderBarPackEndFn gtk_header_bar_pack_endFn;
typedef GtkHeaderBarAppendFn gtk_header_bar_appendFn;
typedef GtkHeaderBarInsertChildAfterFn gtk_header_bar_insert_child_afterFn;

/* GTK4 function pointer types - ActionBar */
typedef GtkWidget* (*GtkActionBarNewFn)(void);
typedef void (*GtkActionBarSetRevealChildFn)(GtkWidget* bar, int reveals);
typedef int (*GtkActionBarGetRevealChildFn)(GtkWidget* bar);
typedef void (*GtkActionBarPackStartFn)(GtkWidget* bar, GtkWidget* child);
typedef void (*GtkActionBarPackEndFn)(GtkWidget* bar, GtkWidget* child);
typedef void (*GtkActionBarSetCenterWidgetFn)(GtkWidget* action_bar, GtkWidget* widget);

/* Typedef aliases for ActionBar functions */
typedef GtkActionBarNewFn gtk_action_bar_newFn;
typedef GtkActionBarSetRevealChildFn gtk_action_bar_set_reveal_childFn;
typedef GtkActionBarGetRevealChildFn gtk_action_bar_get_reveal_childFn;
typedef GtkActionBarPackStartFn gtk_action_bar_pack_startFn;
typedef GtkActionBarPackEndFn gtk_action_bar_pack_endFn;
typedef GtkActionBarSetCenterWidgetFn gtk_action_bar_set_center_widgetFn;

/* GTK4 function pointer types - Toolbar */
typedef GtkWidget* (*GtkToolbarNewFn)(void);
typedef void (*GtkToolbarInsertFn)(GtkWidget* toolbar, GtkWidget* item, int pos);
typedef void (*GtkToolbarAppendFn)(GtkWidget* toolbar, GtkWidget* item);
typedef void (*GtkToolbarPrependFn)(GtkWidget* toolbar, GtkWidget* item);

/* GTK4 function pointer types - ToolButton */
typedef GtkWidget* (*GtkToolButtonNewFn)(GtkWidget* icon_widget, const char* text);
typedef void (*GtkToolButtonSetLabelFn)(GtkWidget* button, const char* label);
typedef void (*GtkToolButtonSetIconNameFn)(GtkWidget* button, const char* icon_name);

/* GTK4 function pointer types - Spinner */
typedef GtkWidget* (*GtkSpinnerNewFn)(void);
typedef void (*GtkSpinnerStartFn)(GtkWidget* spinner);
typedef void (*GtkSpinnerStopFn)(GtkWidget* spinner);
typedef void (*GtkSpinnerSetSpinningFn)(GtkWidget* spinner, int spinning);

/* Typedef aliases for Spinner functions */
typedef GtkSpinnerNewFn gtk_spinner_newFn;
typedef GtkSpinnerStartFn gtk_spinner_startFn;
typedef GtkSpinnerStopFn gtk_spinner_stopFn;
typedef GtkSpinnerSetSpinningFn gtk_spinner_set_spinningFn;

/* GTK4 function pointer types - SearchEntry */
typedef GtkWidget* (*GtkSearchEntryNewFn)(void);

/* GTK4 function pointer types - PasswordEntry */
typedef GtkWidget* (*GtkPasswordEntryNewFn)(void);

/* GTK4 function pointer types - DropDown */
typedef GtkWidget* (*GtkDropDownNewFn)(void);
typedef void (*GtkDropDownSetExpressionFn)(GtkWidget* dropdown, void* expression);
typedef void (*GtkDropDownSetListFactoryFn)(GtkWidget* dropdown, void* factory);
typedef void (*GtkDropDownSetSelectedFn)(GtkWidget* dropdown, unsigned int selected);
typedef unsigned int (*GtkDropDownGetSelectedFn)(GtkWidget* dropdown);

/* GTK4 function pointer types - CenterBox */
typedef GtkWidget* (*GtkCenterBoxNewFn)(int orientation);
typedef void (*GtkCenterBoxSetStartWidgetFn)(GtkWidget* box, GtkWidget* widget);
typedef void (*GtkCenterBoxSetCenterWidgetFn)(GtkWidget* box, GtkWidget* widget);
typedef void (*GtkCenterBoxSetEndWidgetFn)(GtkWidget* box, GtkWidget* widget);
typedef GtkWidget* (*GtkCenterBoxGetStartWidgetFn)(GtkWidget* box);
typedef GtkWidget* (*GtkCenterBoxGetCenterWidgetFn)(GtkWidget* box);
typedef GtkWidget* (*GtkCenterBoxGetEndWidgetFn)(GtkWidget* box);

/* Typedef aliases for CenterBox functions */
typedef GtkCenterBoxNewFn gtk_center_box_newFn;
typedef GtkCenterBoxSetStartWidgetFn gtk_center_box_set_start_widgetFn;
typedef GtkCenterBoxSetCenterWidgetFn gtk_center_box_set_center_widgetFn;
typedef GtkCenterBoxSetEndWidgetFn gtk_center_box_set_end_widgetFn;
typedef GtkCenterBoxGetStartWidgetFn gtk_center_box_get_start_widgetFn;
typedef GtkCenterBoxGetCenterWidgetFn gtk_center_box_get_center_widgetFn;
typedef GtkCenterBoxGetEndWidgetFn gtk_center_box_get_end_widgetFn;

/* GTK4 function pointer types - ShortcutsLabel */
typedef GtkWidget* (*GtkShortcutLabelNewFn)(const char* accelerator);
typedef void (*GtkShortcutLabelSetAcceleratorFn)(GtkWidget* label, const char* accelerator);

/* GTK4 function pointer types - SearchEntry */
typedef GtkWidget* (*GtkSearchEntryNewFn)(void);

/* Typedef aliases for SearchEntry functions */
typedef GtkSearchEntryNewFn gtk_search_entry_newFn;

/* GTK4 function pointer types - PasswordEntry */
typedef GtkWidget* (*GtkPasswordEntryNewFn)(void);

/* Typedef aliases for PasswordEntry functions */
typedef GtkPasswordEntryNewFn gtk_password_entry_newFn;

/* GTK4 function pointer types - Overlay */
typedef GtkWidget* (*GtkOverlayNewFn)(void);
typedef void (*GtkOverlaySetChildFn)(GtkWidget* overlay, GtkWidget* child);
typedef void (*GtkOverlayAddOverlayFn)(GtkWidget* overlay, GtkWidget* widget);

/* Typedef aliases for overlay functions */
typedef GtkOverlayNewFn gtk_overlay_newFn;
typedef GtkOverlaySetChildFn gtk_overlay_set_childFn;
typedef GtkOverlayAddOverlayFn gtk_overlay_add_overlayFn;

/* GTK4 function pointer types - AspectFrame */
typedef GtkWidget* (*GtkAspectFrameNewFn)(const char* label, float xalign, float yalign, float ratio, int obey_child);

/* GTK4 function pointer types - Widget base */
typedef void (*GtkWidgetDestroyFn)(GtkWidget* widget);
typedef GtkWidgetDestroyFn gtk_widget_destroyFn;
typedef void (*GtkWidgetShowFn)(GtkWidget* widget);
typedef GtkWidgetShowFn gtk_widget_showFn;
typedef void (*GtkWidgetHideFn)(GtkWidget* widget);
typedef GtkWidgetHideFn gtk_widget_hideFn;
typedef void (*GtkWidgetSetVisibleFn)(GtkWidget* widget, int visible);
typedef GtkWidgetSetVisibleFn gtk_widget_set_visibleFn;
typedef int (*GtkWidgetGetVisibleFn)(GtkWidget* widget);
typedef GtkWidgetGetVisibleFn gtk_widget_get_visibleFn;
typedef void (*GtkWidgetSetSensitiveFn)(GtkWidget* widget, int sensitive);
typedef GtkWidgetSetSensitiveFn gtk_widget_set_sensitiveFn;
typedef int (*GtkWidgetGetSensitiveFn)(GtkWidget* widget);
typedef GtkWidgetGetSensitiveFn gtk_widget_get_sensitiveFn;
typedef void (*GtkWidgetSetTooltipTextFn)(GtkWidget* widget, const char* text);
typedef GtkWidgetSetTooltipTextFn gtk_widget_set_tooltip_textFn;
typedef const char* (*GtkWidgetGetTooltipTextFn)(GtkWidget* widget);
typedef GtkWidgetGetTooltipTextFn gtk_widget_get_tooltip_textFn;
typedef void (*GtkWidgetSetCursorFromNameFn)(GtkWidget* widget, const char* name);
typedef GtkWidgetSetCursorFromNameFn gtk_widget_set_cursor_from_nameFn;
typedef void (*GtkWidgetSetCssClassesFn)(GtkWidget* widget, void** classes);
typedef GtkWidgetSetCssClassesFn gtk_widget_set_css_classesFn;
typedef void (*GtkWidgetAddCssClassFn)(GtkWidget* widget, const char* css_class);
typedef GtkWidgetAddCssClassFn gtk_widget_add_css_classFn;
typedef void (*GtkWidgetRemoveCssClassFn)(GtkWidget* widget, const char* css_class);
typedef GtkWidgetRemoveCssClassFn gtk_widget_remove_css_classFn;
typedef int (*GtkWidgetHasCssClassFn)(GtkWidget* widget, const char* css_class);
typedef GtkWidgetHasCssClassFn gtk_widget_has_css_classFn;
typedef void (*GtkWidgetSetMarginTopFn)(GtkWidget* widget, int margin);
typedef GtkWidgetSetMarginTopFn gtk_widget_set_margin_topFn;
typedef void (*GtkWidgetSetMarginBottomFn)(GtkWidget* widget, int margin);
typedef GtkWidgetSetMarginBottomFn gtk_widget_set_margin_bottomFn;
typedef void (*GtkWidgetSetMarginStartFn)(GtkWidget* widget, int margin);
typedef GtkWidgetSetMarginStartFn gtk_widget_set_margin_startFn;
typedef void (*GtkWidgetSetMarginEndFn)(GtkWidget* widget, int margin);
typedef GtkWidgetSetMarginEndFn gtk_widget_set_margin_endFn;
typedef int (*GtkWidgetGetWidthFn)(GtkWidget* widget);
typedef GtkWidgetGetWidthFn gtk_widget_get_widthFn;
typedef int (*GtkWidgetGetHeightFn)(GtkWidget* widget);
typedef GtkWidgetGetHeightFn gtk_widget_get_heightFn;
typedef void (*GtkWidgetSetSizeRequestFn)(GtkWidget* widget, int width, int height);
typedef GtkWidgetSetSizeRequestFn gtk_widget_set_size_requestFn;
typedef void (*GtkWidgetQueueDrawFn)(GtkWidget* widget);
typedef GtkWidgetQueueDrawFn gtk_widget_queue_drawFn;
typedef void (*GtkWidgetGrabFocusFn)(GtkWidget* widget);
typedef GtkWidgetGrabFocusFn gtk_widget_grab_focusFn;
typedef int (*GtkWidgetHasFocusFn)(GtkWidget* widget);
typedef GtkWidgetHasFocusFn gtk_widget_has_focusFn;
typedef void (*GtkWidgetSetFocusableFn)(GtkWidget* widget, int focusable);
typedef GtkWidgetSetFocusableFn gtk_widget_set_focusableFn;
typedef void (*GtkWidgetSetFocusOnClickFn)(GtkWidget* widget, int focus_on_click);
typedef GtkWidgetSetFocusOnClickFn gtk_widget_set_focus_on_clickFn;

/* GTK4 function pointer types - Application */
typedef void* (*GtkApplicationNewFn)(const char* app_id, int flags);

/* GTK4 function pointer types - Settings */
typedef void (*GtkSettingsSetPropertyValueFn)(void* settings, const char* name, void* value);

/* GTK4 function pointer types - Event controllers */
typedef void* (*GtkGestureClickNewFn)(void);
typedef void (*GtkEventControllerSetPropagationPhaseFn)(void* controller, int phase);

/* GTK4 function pointer types - Misc */
typedef void (*GtkFreeFn)(void* ptr);
typedef void* (*GtkCssProviderNewFn)(void);
typedef void (*GtkCssProviderLoadFromPathFn)(void* provider, const char* path);
typedef void (*GtkStyleContextAddProviderFn)(void* context, void* provider, unsigned int priority);
typedef void* (*GtkStyleContextNewFn)(void);
typedef void* (*GtkWidgetGetStyleContextFn)(GtkWidget* widget);

/* GTK4 constants */
enum {
    GTK_WIN_POS_NONE = 0,
    GTK_WIN_POS_CENTER = 1,
    GTK_WIN_POS_MOUSE = 2,
    GTK_WIN_POS_CENTER_ALWAYS = 3,
    GTK_WIN_POS_CENTER_ON_PARENT = 4,
    
    GTK_ORIENTATION_HORIZONTAL = 0,
    GTK_ORIENTATION_VERTICAL = 1,
    
    GTK_POLICY_AUTOMATIC = 0,
    GTK_POLICY_ALWAYS = 1,
    GTK_POLICY_EXTERNAL = 2,
    GTK_POLICY_NEVER = 3,
    
    GTK_SELECTION_NONE = 0,
    GTK_SELECTION_SINGLE = 1,
    GTK_SELECTION_BROWSE = 2,
    GTK_SELECTION_MULTIPLE = 3,
    
    GTK_BUTTONS_NONE = 0,
    GTK_BUTTONS_OK = 1,
    GTK_BUTTONS_CLOSE = 2,
    GTK_BUTTONS_CANCEL = 3,
    GTK_BUTTONS_YES_NO = 4,
    GTK_BUTTONS_OK_CANCEL = 5,
    
    GTK_MESSAGE_INFO = 0,
    GTK_MESSAGE_WARNING = 1,
    GTK_MESSAGE_QUESTION = 2,
    GTK_MESSAGE_ERROR = 3,
    GTK_MESSAGE_OTHER = 4,
    
    GTK_RESPONSE_NONE = -1,
    GTK_RESPONSE_OK = -3,
    GTK_RESPONSE_YES = -9,
    GTK_RESPONSE_NO = -10,
    GTK_RESPONSE_CANCEL = -6,
    GTK_RESPONSE_CLOSE = -7,
    GTK_RESPONSE_DELETE_EVENT = -4,
    
    GTK_ALIGN_FILL = 0,
    GTK_ALIGN_START = 1,
    GTK_ALIGN_END = 2,
    GTK_ALIGN_CENTER = 3,
    GTK_ALIGN_BASELINE = 4,
    
    GTK_JUSTIFY_LEFT = 0,
    GTK_JUSTIFY_RIGHT = 1,
    GTK_JUSTIFY_CENTER = 2,
    GTK_JUSTIFY_FILL = 3,
    
    GTK_SHADOW_NONE = 0,
    GTK_SHADOW_IN = 1,
    GTK_SHADOW_OUT = 2,
    GTK_SHADOW_ETCHED_IN = 3,
    GTK_SHADOW_ETCHED_OUT = 4,
    
    GTK_POSITION_TOP = 0,
    GTK_POSITION_BOTTOM = 1,
    GTK_POSITION_LEFT = 2,
    GTK_POSITION_RIGHT = 3,
    
    GTK_LEVEL_BAR_MODE_CONTINUOUS = 0,
    GTK_LEVEL_BAR_MODE_DISCRETE = 1,
    
    GTK_REVEALER_TRANSITION_TYPE_NONE = 0,
    GTK_REVEALER_TRANSITION_TYPE_CROSSFADE = 1,
    GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT = 2,
    GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT = 3,
    GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP = 4,
    GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN = 5,
    
    GTK_STACK_TRANSITION_TYPE_NONE = 0,
    GTK_STACK_TRANSITION_TYPE_CROSSFADE = 1,
    GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT = 2,
    GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT = 3,
    GTK_STACK_TRANSITION_TYPE_SLIDE_UP = 4,
    GTK_STACK_TRANSITION_TYPE_SLIDE_DOWN = 5,
    GTK_STACK_TRANSITION_TYPE_SLIDE_UP_DOWN = 6,
    GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT = 7,
    GTK_STACK_TRANSITION_TYPE_OVER_UP = 8,
    GTK_STACK_TRANSITION_TYPE_OVER_DOWN = 9,
    GTK_STACK_TRANSITION_TYPE_OVER_LEFT = 10,
    GTK_STACK_TRANSITION_TYPE_OVER_RIGHT = 11,
    GTK_STACK_TRANSITION_TYPE_UNDER_UP = 12,
    GTK_STACK_TRANSITION_TYPE_UNDER_DOWN = 13,
    GTK_STACK_TRANSITION_TYPE_UNDER_LEFT = 14,
    GTK_STACK_TRANSITION_TYPE_UNDER_RIGHT = 15,
    GTK_STACK_TRANSITION_TYPE_OVER_UP_DOWN = 16,
    GTK_STACK_TRANSITION_TYPE_OVER_LEFT_RIGHT = 17,
    
    GTK_FILE_CHOOSER_ACTION_OPEN = 0,
    GTK_FILE_CHOOSER_ACTION_SAVE = 1,
    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER = 2,
    GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER = 3,
    
    GTK_INPUT_PURPOSE_FREE_FORM = 0,
    GTK_INPUT_PURPOSE_ALPHA = 1,
    GTK_INPUT_PURPOSE_DIGITS = 2,
    GTK_INPUT_PURPOSE_NUMBER = 3,
    GTK_INPUT_PURPOSE_PHONE = 4,
    GTK_INPUT_PURPOSE_URL = 5,
    GTK_INPUT_PURPOSE_EMAIL = 6,
    GTK_INPUT_PURPOSE_NAME = 7,
    GTK_INPUT_PURPOSE_PASSWORD = 8,
    GTK_INPUT_PURPOSE_PIN = 9,
};

/* Dynamic loader for GTK4 */
struct Gtk4Libs {
    havel::DynamicLoader gtkLoader;
    havel::DynamicLoader glibLoader;
    havel::DynamicLoader gobjectLoader;
    havel::DynamicLoader gdkLoader;
    
    /* Function pointers - Core */
    GtkInitFn gtk_init = nullptr;
    GtkMainFn gtk_main = nullptr;
    GtkQuitFn gtk_quit = nullptr;
    
    /* Function pointers - Window */
    GtkWindowNewFn gtk_window_new = nullptr;
    GtkWindowSetTitleFn gtk_window_set_title = nullptr;
    GtkWindowSetDefaultSizeFn gtk_window_set_default_size = nullptr;
    GtkWindowMoveFn gtk_window_move = nullptr;
    GtkWindowSetPositionFn gtk_window_set_position = nullptr;
    GtkWindowShowFn gtk_window_show = nullptr;
    GtkWindowSetChildFn gtk_window_set_child = nullptr;
    GtkWindowDestroyFn gtk_window_destroy = nullptr;
    GtkWindowCloseFn gtk_window_close = nullptr;
    GtkWindowGetWidthFn gtk_window_get_width = nullptr;
    GtkWindowGetHeightFn gtk_window_get_height = nullptr;
    GtkWindowSetResizableFn gtk_window_set_resizable = nullptr;
    GtkWindowGetResizableFn gtk_window_get_resizable = nullptr;
    GtkWindowSetDecoratedFn gtk_window_set_decorated = nullptr;
    GtkWindowSetModalFn gtk_window_set_modal = nullptr;
    GtkWindowIsModalFn gtk_window_is_modal = nullptr;
    GtkWindowMaximizeFn gtk_window_maximize = nullptr;
    GtkWindowUnmaximizeFn gtk_window_unmaximize = nullptr;
    GtkWindowIsMaximizedFn gtk_window_is_maximized = nullptr;
    GtkWindowMinimizeFn gtk_window_minimize = nullptr;
    GtkWindowPresentFn gtk_window_present = nullptr;
    
    /* Function pointers - Label */
    GtkLabelNewFn gtk_label_new = nullptr;
    GtkLabelSetTextFn gtk_label_set_text = nullptr;
    GtkLabelGetTextFn gtk_label_get_text = nullptr;
    GtkLabelSetMarkupFn gtk_label_set_markup = nullptr;
    GtkLabelSetJustifyFn gtk_label_set_justify = nullptr;
    GtkLabelSetXalignFn gtk_label_set_xalign = nullptr;
    GtkLabelSetYalignFn gtk_label_set_yalign = nullptr;
    GtkLabelSetSelectableFn gtk_label_set_selectable = nullptr;
    GtkLabelGetSelectableFn gtk_label_get_selectable = nullptr;
    GtkLabelSetLineWrapFn gtk_label_set_line_wrap = nullptr;
    GtkLabelSetEllipsizeFn gtk_label_set_ellipsize = nullptr;
    
    /* Function pointers - Button */
    GtkButtonNewFn gtk_button_new = nullptr;
    GtkButtonNewWithLabelFn gtk_button_new_with_label = nullptr;
    GtkButtonSetLabelFn gtk_button_set_label = nullptr;
    GtkButtonGetLabelFn gtk_button_get_label = nullptr;
    GtkButtonSetIconNameFn gtk_button_set_icon_name = nullptr;
    GtkButtonSetChildFn gtk_button_set_child = nullptr;
    GtkButtonClickedFn gtk_button_clicked = nullptr;
    GtkButtonGetHasFrameFn gtk_button_get_has_frame = nullptr;
    GtkButtonSetHasFrameFn gtk_button_set_has_frame = nullptr;
    
    /* Function pointers - ToggleButton/CheckButton */
    GtkToggleButtonNewFn gtk_toggle_button_new = nullptr;
    GtkToggleButtonNewWithLabelFn gtk_toggle_button_new_with_label = nullptr;
    GtkToggleButtonSetActiveFn gtk_toggle_button_set_active = nullptr;
    GtkToggleButtonGetActiveFn gtk_toggle_button_get_active = nullptr;
    GtkToggleButtonToggledFn gtk_toggle_button_toggled = nullptr;
    GtkCheckButtonNewFn gtk_check_button_new = nullptr;
    GtkCheckButtonNewWithLabelFn gtk_check_button_new_with_label = nullptr;
    GtkCheckButtonSetGroupFn gtk_check_button_set_group = nullptr;
    GtkCheckButtonGetGroupFn gtk_check_button_get_group = nullptr;
    
    /* Function pointers - Switch */
    GtkSwitchNewFn gtk_switch_new = nullptr;
    GtkSwitchSetActiveFn gtk_switch_set_active = nullptr;
    GtkSwitchGetActiveFn gtk_switch_get_active = nullptr;
    GtkSwitchSetStateFn gtk_switch_set_state = nullptr;
    GtkSwitchGetStateFn gtk_switch_get_state = nullptr;
    
    /* Function pointers - Entry */
    GtkEntryNewFn gtk_entry_new = nullptr;
    GtkEntryGetTextFn gtk_entry_get_text = nullptr;
    GtkEntrySetTextFn gtk_entry_set_text = nullptr;
    GtkEntryAppendTextFn gtk_entry_append_text = nullptr;
    GtkEntryGetTextLengthFn gtk_entry_get_text_length = nullptr;
    GtkEntrySetPlaceholderTextFn gtk_entry_set_placeholder_text = nullptr;
    GtkEntryGetPlaceholderTextFn gtk_entry_get_placeholder_text = nullptr;
    GtkEntrySetEditableFn gtk_entry_set_editable = nullptr;
    GtkEntryGetEditableFn gtk_entry_get_editable = nullptr;
    GtkEntrySetVisibilityFn gtk_entry_set_visibility = nullptr;
    GtkEntryGetVisibilityFn gtk_entry_get_visibility = nullptr;
    GtkEntrySetMaxLengthFn gtk_entry_set_max_length = nullptr;
    GtkEntryGetMaxLengthFn gtk_entry_get_max_length = nullptr;
    GtkEntrySetActivatesDefaultFn gtk_entry_set_activates_default = nullptr;
    GtkEntrySetWidthCharsFn gtk_entry_set_width_chars = nullptr;
    GtkEntrySetXalignFn gtk_entry_set_xalign = nullptr;
    GtkEntrySetInputPurposeFn gtk_entry_set_input_purpose = nullptr;
    
    /* Function pointers - Box */
    GtkBoxNewFn gtk_box_new = nullptr;
    GtkBoxAppendFn gtk_box_append = nullptr;
    GtkBoxPrependFn gtk_box_prepend = nullptr;
    GtkBoxInsertChildAfterFn gtk_box_insert_child_after = nullptr;
    GtkBoxInsertChildBeforeFn gtk_box_insert_child_before = nullptr;
    GtkBoxRemoveFn gtk_box_remove = nullptr;
    GtkBoxReorderChildFn gtk_box_reorder_child = nullptr;
    GtkBoxSetSpacingFn gtk_box_set_spacing = nullptr;
    GtkBoxGetSpacingFn gtk_box_get_spacing = nullptr;
    GtkBoxSetHomogeneousFn gtk_box_set_homogeneous = nullptr;
    GtkBoxGetHomogeneousFn gtk_box_get_homogeneous = nullptr;
    
    /* Function pointers - Grid */
    GtkGridNewFn gtk_grid_new = nullptr;
    GtkGridAttachFn gtk_grid_attach = nullptr;
    GtkGridRemoveFn gtk_grid_remove = nullptr;
    GtkGridInsertRowFn gtk_grid_insert_row = nullptr;
    GtkGridInsertColumnFn gtk_grid_insert_column = nullptr;
    GtkGridSetRowSpacingFn gtk_grid_set_row_spacing = nullptr;
    GtkGridGetRowSpacingFn gtk_grid_get_row_spacing = nullptr;
    GtkGridSetColumnSpacingFn gtk_grid_set_column_spacing = nullptr;
    GtkGridGetColumnSpacingFn gtk_grid_get_column_spacing = nullptr;
    GtkGridSetRowHomogeneousFn gtk_grid_set_row_homogeneous = nullptr;
    GtkGridGetRowHomogeneousFn gtk_grid_get_row_homogeneous = nullptr;
    GtkGridSetColumnHomogeneousFn gtk_grid_set_column_homogeneous = nullptr;
    GtkGridGetColumnHomogeneousFn gtk_grid_get_column_homogeneous = nullptr;
    
    /* Function pointers - Fixed */
    GtkFixedNewFn gtk_fixed_new = nullptr;
    GtkFixedPutFn gtk_fixed_put = nullptr;
    GtkFixedMoveFn gtk_fixed_move = nullptr;
    
    /* Function pointers - Frame */
    GtkFrameNewFn gtk_frame_new = nullptr;
    GtkFrameSetLabelFn gtk_frame_set_label = nullptr;
    GtkFrameGetLabelFn gtk_frame_get_label = nullptr;
    GtkFrameSetLabelAlignFn gtk_frame_set_label_align = nullptr;
    GtkFrameSetChildFn gtk_frame_set_child = nullptr;
    
    /* Function pointers - Separator */
    GtkSeparatorNewFn gtk_separator_new = nullptr;
    
    /* Function pointers - Paned */
    GtkPanedNewFn gtk_paned_new = nullptr;
    GtkPanedPack1Fn gtk_paned_pack1 = nullptr;
    GtkPanedPack2Fn gtk_paned_pack2 = nullptr;
    GtkPanedSetPositionFn gtk_paned_set_position = nullptr;
    GtkPanedGetPositionFn gtk_paned_get_position = nullptr;
    GtkPanedSetWideHandleFn gtk_paned_set_wide_handle = nullptr;
    
    /* Function pointers - Notebook */
    GtkNotebookNewFn gtk_notebook_new = nullptr;
    GtkNotebookAppendPageFn gtk_notebook_append_page = nullptr;
    GtkNotebookPrependPageFn gtk_notebook_prepend_page = nullptr;
    GtkNotebookInsertPageFn gtk_notebook_insert_page = nullptr;
    GtkNotebookRemovePageFn gtk_notebook_remove_page = nullptr;
    GtkNotebookGetCurrentPageFn gtk_notebook_get_current_page = nullptr;
    GtkNotebookSetCurrentPageFn gtk_notebook_set_current_page = nullptr;
    GtkNotebookGetNPagesFn gtk_notebook_get_n_pages = nullptr;
    GtkNotebookSetTabLabelTextFn gtk_notebook_set_tab_label_text = nullptr;
    GtkNotebookSetShowTabsFn gtk_notebook_set_show_tabs = nullptr;
    GtkNotebookSetScrollableFn gtk_notebook_set_scrollable = nullptr;
    
    /* Function pointers - Stack */
    GtkStackNewFn gtk_stack_new = nullptr;
    GtkStackAddNamedFn gtk_stack_add_named = nullptr;
    GtkStackAddTitledFn gtk_stack_add_titled = nullptr;
    GtkStackSetVisibleChildFn gtk_stack_set_visible_child = nullptr;
    GtkStackSetVisibleChildByNameFn gtk_stack_set_visible_child_by_name = nullptr;
    GtkStackGetVisibleChildNameFn gtk_stack_get_visible_child_name = nullptr;
    GtkStackSetTransitionTypeFn gtk_stack_set_transition_type = nullptr;
    GtkStackSetTransitionDurationFn gtk_stack_set_transition_duration = nullptr;
    
    /* Function pointers - Revealer */
    GtkRevealerNewFn gtk_revealer_new = nullptr;
    GtkRevealerSetChildFn gtk_revealer_set_child = nullptr;
    GtkRevealerSetRevealChildFn gtk_revealer_set_reveal_child = nullptr;
    GtkRevealerGetRevealChildFn gtk_revealer_get_reveal_child = nullptr;
    GtkRevealerSetTransitionTypeFn gtk_revealer_set_transition_type = nullptr;
    GtkRevealerSetTransitionDurationFn gtk_revealer_set_transition_duration = nullptr;
    
    /* Function pointers - Expander */
    GtkExpanderNewFn gtk_expander_new = nullptr;
    GtkExpanderSetExpandedFn gtk_expander_set_expanded = nullptr;
    GtkExpanderGetExpandedFn gtk_expander_get_expanded = nullptr;
    GtkExpanderSetLabelFn gtk_expander_set_label = nullptr;
    
    /* Function pointers - ScrolledWindow */
    GtkScrolledWindowNewFn gtk_scrolled_window_new = nullptr;
    GtkScrolledWindowSetChildFn gtk_scrolled_window_set_child = nullptr;
    GtkScrolledWindowSetPolicyFn gtk_scrolled_window_set_policy = nullptr;
    GtkScrolledWindowSetKineticScrollingFn gtk_scrolled_window_set_kinetic_scrolling = nullptr;
    
    /* Function pointers - Viewport */
    GtkViewportNewFn gtk_viewport_new = nullptr;
    GtkViewportSetChildFn gtk_viewport_set_child = nullptr;
    
    /* Function pointers - TextView */
    GtkTextViewNewFn gtk_text_view_new = nullptr;
    GtkTextViewGetBufferFn gtk_text_view_get_buffer = nullptr;
    GtkTextViewSetBufferFn gtk_text_view_set_buffer = nullptr;
    GtkTextViewSetEditableFn gtk_text_view_set_editable = nullptr;
    GtkTextViewSetWrapModeFn gtk_text_view_set_wrap_mode = nullptr;
    GtkTextViewSetLeftMarginFn gtk_text_view_set_left_margin = nullptr;
    GtkTextViewSetRightMarginFn gtk_text_view_set_right_margin = nullptr;
    GtkTextViewSetTopMarginFn gtk_text_view_set_top_margin = nullptr;
    GtkTextViewSetBottomMarginFn gtk_text_view_set_bottom_margin = nullptr;
    GtkTextViewSetMonospaceFn gtk_text_view_set_monospace = nullptr;
    
    /* Function pointers - TextBuffer */
    GtkTextBufferNewFn gtk_text_buffer_new = nullptr;
    GtkTextBufferGetTextFn gtk_text_buffer_get_text = nullptr;
    GtkTextBufferSetTextFn gtk_text_buffer_set_text = nullptr;
    GtkTextBufferGetCharCountFn gtk_text_buffer_get_char_count = nullptr;
    GtkTextBufferInsertFn gtk_text_buffer_insert = nullptr;
    GtkTextBufferInsertAtCursorFn gtk_text_buffer_insert_at_cursor = nullptr;
    
    /* Function pointers - Image */
    GtkImageNewFn gtk_image_new = nullptr;
    GtkImageNewFromIconNameFn gtk_image_new_from_icon_name = nullptr;
    GtkImageSetFromIconNameFn gtk_image_set_from_icon_name = nullptr;
    GtkImageSetFromPixbufFn gtk_image_set_from_pixbuf = nullptr;
    GtkImageClearFn gtk_image_clear = nullptr;
    
    /* Function pointers - ProgressBar */
    GtkProgressBarNewFn gtk_progress_bar_new = nullptr;
    GtkProgressBarSetFractionFn gtk_progress_bar_set_fraction = nullptr;
    GtkProgressBarGetFractionFn gtk_progress_bar_get_fraction = nullptr;
    GtkProgressBarSetPulseFn gtk_progress_bar_set_pulse = nullptr;
    GtkProgressBarPulseFn gtk_progress_bar_pulse = nullptr;
    GtkProgressBarSetTextFn gtk_progress_bar_set_text = nullptr;
    GtkProgressBarGetTextFn gtk_progress_bar_get_text = nullptr;
    GtkProgressBarSetShowTextFn gtk_progress_bar_set_show_text = nullptr;
    GtkProgressBarSetEllipsizeFn gtk_progress_bar_set_ellipsize = nullptr;
    
    /* Function pointers - LevelBar */
    GtkLevelBarNewFn gtk_level_bar_new = nullptr;
    GtkLevelBarNewForIntervalFn gtk_level_bar_new_for_interval = nullptr;
    GtkLevelBarSetValueFn gtk_level_bar_set_value = nullptr;
    GtkLevelBarGetValueFn gtk_level_bar_get_value = nullptr;
    GtkLevelBarSetMinValueFn gtk_level_bar_set_min_value = nullptr;
    GtkLevelBarGetMinValueFn gtk_level_bar_get_min_value = nullptr;
    GtkLevelBarSetMaxValueFn gtk_level_bar_set_max_value = nullptr;
    GtkLevelBarGetMaxValueFn gtk_level_bar_get_max_value = nullptr;
    GtkLevelBarSetBarModeFn gtk_level_bar_set_bar_mode = nullptr;
    GtkLevelBarGetBarModeFn gtk_level_bar_get_bar_mode = nullptr;
    
    /* Function pointers - Scale */
    GtkScaleNewFn gtk_scale_new = nullptr;
    GtkScaleNewWithRangeFn gtk_scale_new_with_range = nullptr;
    GtkScaleSetDigitsFn gtk_scale_set_digits = nullptr;
    GtkScaleGetDigitsFn gtk_scale_get_digits = nullptr;
    GtkScaleSetDrawValueFn gtk_scale_set_draw_value = nullptr;
    GtkScaleGetDrawValueFn gtk_scale_get_draw_value = nullptr;
    GtkScaleSetValueFn gtk_scale_set_value = nullptr;
    GtkScaleGetValueFn gtk_scale_get_value = nullptr;
    GtkScaleSetHasOriginFn gtk_scale_set_has_origin = nullptr;
    GtkScaleSetFillLevelFn gtk_scale_set_fill_level = nullptr;
    
    /* Function pointers - SpinButton */
    GtkSpinButtonNewFn gtk_spin_button_new = nullptr;
    GtkSpinButtonNewWithRangeFn gtk_spin_button_new_with_range = nullptr;
    GtkSpinButtonSetValueFn gtk_spin_button_set_value = nullptr;
    GtkSpinButtonGetValueFn gtk_spin_button_get_value = nullptr;
    GtkSpinButtonGetValueAsIntFn gtk_spin_button_get_value_as_int = nullptr;
    GtkSpinButtonSetRangeFn gtk_spin_button_set_range = nullptr;
    GtkSpinButtonSetIncrementsFn gtk_spin_button_set_increments = nullptr;
    GtkSpinButtonSetDigitsFn gtk_spin_button_set_digits = nullptr;
    GtkSpinButtonSpinFn gtk_spin_button_spin = nullptr;
    
    /* Function pointers - ComboBox/ComboBoxText */
    GtkComboBoxTextNewFn gtk_combo_box_text_new = nullptr;
    GtkComboBoxTextAppendFn gtk_combo_box_text_append = nullptr;
    GtkComboBoxTextPrependFn gtk_combo_box_text_prepend = nullptr;
    GtkComboBoxTextInsertFn gtk_combo_box_text_insert = nullptr;
    GtkComboBoxTextRemoveFn gtk_combo_box_text_remove = nullptr;
    GtkComboBoxTextRemoveAllFn gtk_combo_box_text_remove_all = nullptr;
    GtkComboBoxTextGetActiveIdFn gtk_combo_box_text_get_active_id = nullptr;
    GtkComboBoxTextSetActiveIdFn gtk_combo_box_text_set_active_id = nullptr;
    GtkComboBoxTextSetActiveFn gtk_combo_box_text_set_active = nullptr;
    GtkComboBoxTextGetActiveFn gtk_combo_box_text_get_active = nullptr;
    GtkComboBoxTextGetActiveTextFn gtk_combo_box_text_get_active_text = nullptr;
    
    /* Function pointers - ListBox */
    GtkListBoxNewFn gtk_list_box_new = nullptr;
    GtkListBoxAppendFn gtk_list_box_append = nullptr;
    GtkListBoxPrependFn gtk_list_box_prepend = nullptr;
    GtkListBoxInsertFn gtk_list_box_insert = nullptr;
    GtkListBoxRemoveFn gtk_list_box_remove = nullptr;
    GtkListBoxSelectRowFn gtk_list_box_select_row = nullptr;
    GtkListBoxUnselectRowFn gtk_list_box_unselect_row = nullptr;
    GtkListBoxUnselectAllFn gtk_list_box_unselect_all = nullptr;
    GtkListBoxSetSelectionModeFn gtk_list_box_set_selection_mode = nullptr;
    GtkListBoxGetSelectionModeFn gtk_list_box_get_selection_mode = nullptr;
    
    /* Function pointers - FlowBox */
    GtkFlowBoxNewFn gtk_flow_box_new = nullptr;
    GtkFlowBoxAppendFn gtk_flow_box_append = nullptr;
    GtkFlowBoxInsertFn gtk_flow_box_insert = nullptr;
    GtkFlowBoxSelectChildFn gtk_flow_box_select_child = nullptr;
    GtkFlowBoxUnselectAllFn gtk_flow_box_unselect_all = nullptr;
    GtkFlowBoxSetSelectionModeFn gtk_flow_box_set_selection_mode = nullptr;
    GtkFlowBoxSetMinChildrenPerLineFn gtk_flow_box_set_min_children_per_line = nullptr;
    GtkFlowBoxSetMaxChildrenPerLineFn gtk_flow_box_set_max_children_per_line = nullptr;
    GtkFlowBoxSetColumnSpacingFn gtk_flow_box_set_column_spacing = nullptr;
    GtkFlowBoxSetRowSpacingFn gtk_flow_box_set_row_spacing = nullptr;
    
    /* Function pointers - Menu */
    GtkMenuBarNewFn gtk_menu_bar_new = nullptr;
    GtkMenuBarAppendFn gtk_menu_bar_append = nullptr;
    GtkMenuNewFn gtk_menu_new = nullptr;
    GtkMenuAppendFn gtk_menu_append = nullptr;
    GtkMenuPrependFn gtk_menu_prepend = nullptr;
    GtkMenuInsertFn gtk_menu_insert = nullptr;
    GtkMenuRemoveFn gtk_menu_remove = nullptr;
    GtkMenuPopupAtPointerFn gtk_menu_popup_at_pointer = nullptr;
    GtkMenuItemNewFn gtk_menu_item_new = nullptr;
    GtkMenuItemNewWithLabelFn gtk_menu_item_new_with_label = nullptr;
    GtkMenuItemSetLabelFn gtk_menu_item_set_label = nullptr;
    GtkMenuItemSetSubmenuFn gtk_menu_item_set_submenu = nullptr;
    GtkMenuItemActivateFn gtk_menu_item_activate = nullptr;
    
    /* Function pointers - Popover */
    GtkPopoverNewFn gtk_popover_new = nullptr;
    GtkPopoverSetChildFn gtk_popover_set_child = nullptr;
    GtkPopoverPopupFn gtk_popover_popup = nullptr;
    GtkPopoverPopdownFn gtk_popover_popdown = nullptr;
    
    /* Function pointers - Dialog */
    GtkDialogNewFn gtk_dialog_new = nullptr;
    GtkDialogAddButtonFn gtk_dialog_add_button = nullptr;
    GtkDialogAddActionWidgetFn gtk_dialog_add_action_widget = nullptr;
    GtkDialogRunFn gtk_dialog_run = nullptr;
    GtkDialogResponseFn gtk_dialog_response = nullptr;
    GtkDialogSetDefaultResponseFn gtk_dialog_set_default_response = nullptr;
    GtkDialogSetUseHeaderBarFn gtk_dialog_set_use_header_bar = nullptr;
    GtkDialogGetContentAreaFn gtk_dialog_get_content_area = nullptr;
    GtkDialogGetHeaderBarFn gtk_dialog_get_header_bar = nullptr;
    
    /* Function pointers - AboutDialog */
    GtkAboutDialogNewFn gtk_about_dialog_new = nullptr;
    GtkAboutDialogSetProgramNameFn gtk_about_dialog_set_program_name = nullptr;
    GtkAboutDialogSetVersionFn gtk_about_dialog_set_version = nullptr;
    GtkAboutDialogSetCommentsFn gtk_about_dialog_set_comments = nullptr;
    GtkAboutDialogSetWebsiteFn gtk_about_dialog_set_website = nullptr;
    GtkAboutDialogSetLicenseFn gtk_about_dialog_set_license = nullptr;
    
    /* Function pointers - MessageDialog */
    GtkMessageDialogNewFn gtk_message_dialog_new = nullptr;
    GtkMessageDialogSetMarkupFn gtk_message_dialog_set_markup = nullptr;
    
    /* Function pointers - HeaderBar */
    GtkHeaderBarNewFn gtk_header_bar_new = nullptr;
    GtkHeaderBarSetTitleFn gtk_header_bar_set_title = nullptr;
    GtkHeaderBarGetTitleFn gtk_header_bar_get_title = nullptr;
    GtkHeaderBarSetSubtitleFn gtk_header_bar_set_subtitle = nullptr;
    GtkHeaderBarGetSubtitleFn gtk_header_bar_get_subtitle = nullptr;
    GtkHeaderBarSetShowTitleButtonsFn gtk_header_bar_set_show_title_buttons = nullptr;
    GtkHeaderBarPackStartFn gtk_header_bar_pack_start = nullptr;
    GtkHeaderBarPackEndFn gtk_header_bar_pack_end = nullptr;
    GtkHeaderBarAppendFn gtk_header_bar_append = nullptr;
    GtkHeaderBarInsertChildAfterFn gtk_header_bar_insert_child_after = nullptr;
    
    /* Function pointers - ActionBar */
    GtkActionBarNewFn gtk_action_bar_new = nullptr;
    GtkActionBarSetRevealChildFn gtk_action_bar_set_reveal_child = nullptr;
    GtkActionBarGetRevealChildFn gtk_action_bar_get_reveal_child = nullptr;
    GtkActionBarPackStartFn gtk_action_bar_pack_start = nullptr;
    GtkActionBarPackEndFn gtk_action_bar_pack_end = nullptr;
    GtkActionBarSetCenterWidgetFn gtk_action_bar_set_center_widget = nullptr;
    
    /* Function pointers - Spinner */
    GtkSpinnerNewFn gtk_spinner_new = nullptr;
    GtkSpinnerStartFn gtk_spinner_start = nullptr;
    GtkSpinnerStopFn gtk_spinner_stop = nullptr;
    GtkSpinnerSetSpinningFn gtk_spinner_set_spinning = nullptr;
    
    /* Function pointers - SearchEntry */
    GtkSearchEntryNewFn gtk_search_entry_new = nullptr;
    
    /* Function pointers - PasswordEntry */
    GtkPasswordEntryNewFn gtk_password_entry_new = nullptr;
    
    /* Function pointers - CenterBox */
    GtkCenterBoxNewFn gtk_center_box_new = nullptr;
    GtkCenterBoxSetStartWidgetFn gtk_center_box_set_start_widget = nullptr;
    GtkCenterBoxSetCenterWidgetFn gtk_center_box_set_center_widget = nullptr;
    GtkCenterBoxSetEndWidgetFn gtk_center_box_set_end_widget = nullptr;
    GtkCenterBoxGetStartWidgetFn gtk_center_box_get_start_widget = nullptr;
    GtkCenterBoxGetCenterWidgetFn gtk_center_box_get_center_widget = nullptr;
    GtkCenterBoxGetEndWidgetFn gtk_center_box_get_end_widget = nullptr;
    
    /* Function pointers - Overlay */
    GtkOverlayNewFn gtk_overlay_new = nullptr;
    GtkOverlaySetChildFn gtk_overlay_set_child = nullptr;
    GtkOverlayAddOverlayFn gtk_overlay_add_overlay = nullptr;
    
    /* Function pointers - Widget base */
    GtkWidgetDestroyFn gtk_widget_destroy = nullptr;
    GtkWidgetShowFn gtk_widget_show = nullptr;
    GtkWidgetHideFn gtk_widget_hide = nullptr;
    GtkWidgetSetVisibleFn gtk_widget_set_visible = nullptr;
    GtkWidgetGetVisibleFn gtk_widget_get_visible = nullptr;
    GtkWidgetSetSensitiveFn gtk_widget_set_sensitive = nullptr;
    GtkWidgetGetSensitiveFn gtk_widget_get_sensitive = nullptr;
    GtkWidgetSetTooltipTextFn gtk_widget_set_tooltip_text = nullptr;
    GtkWidgetGetTooltipTextFn gtk_widget_get_tooltip_text = nullptr;
    GtkWidgetSetCursorFromNameFn gtk_widget_set_cursor_from_name = nullptr;
    GtkWidgetAddCssClassFn gtk_widget_add_css_class = nullptr;
    GtkWidgetRemoveCssClassFn gtk_widget_remove_css_class = nullptr;
    GtkWidgetHasCssClassFn gtk_widget_has_css_class = nullptr;
    GtkWidgetSetMarginTopFn gtk_widget_set_margin_top = nullptr;
    GtkWidgetSetMarginBottomFn gtk_widget_set_margin_bottom = nullptr;
    GtkWidgetSetMarginStartFn gtk_widget_set_margin_start = nullptr;
    GtkWidgetSetMarginEndFn gtk_widget_set_margin_end = nullptr;
    GtkWidgetGetWidthFn gtk_widget_get_width = nullptr;
    GtkWidgetGetHeightFn gtk_widget_get_height = nullptr;
    GtkWidgetSetSizeRequestFn gtk_widget_set_size_request = nullptr;
    GtkWidgetQueueDrawFn gtk_widget_queue_draw = nullptr;
    GtkWidgetGrabFocusFn gtk_widget_grab_focus = nullptr;
    GtkWidgetHasFocusFn gtk_widget_has_focus = nullptr;
    GtkWidgetSetFocusableFn gtk_widget_set_focusable = nullptr;
    GtkWidgetSetFocusOnClickFn gtk_widget_set_focus_on_click = nullptr;
    
    /* Function pointers - Misc */
    GtkFreeFn g_free = nullptr;
    
    bool load() {
        /* Load GLib first (required by GTK) */
        if (!glibLoader.load(LibNames::GLIB2)) {
            fprintf(stderr, "[GTK] Failed to load GLib\n");
            return false;
        }
        
        /* Load GObject */
        if (!gobjectLoader.load(LibNames::GOBJECT2)) {
            fprintf(stderr, "[GTK] Failed to load GObject\n");
            return false;
        }
        
        /* Load GDK */
        if (!gdkLoader.load(LibNames::GDK4)) {
            fprintf(stderr, "[GTK] Warning: Failed to load GDK4\n");
        }
        
        /* Load GTK4 */
        if (!gtkLoader.load(LibNames::GTK4)) {
            fprintf(stderr, "[GTK] Failed to load GTK4\n");
            return false;
        }
        
        /* Load all required symbols */
#define LOAD_SYMBOL(name) \
    name = gtkLoader.getSymbol<name##Fn>(#name); \
    if (!name) { \
        fprintf(stderr, "[GTK] Failed to load symbol: %s\n", #name); \
        return false; \
    }
        
        LOAD_SYMBOL(gtk_init)
        LOAD_SYMBOL(gtk_main)
        LOAD_SYMBOL(gtk_quit)
        
        /* Window */
        LOAD_SYMBOL(gtk_window_new)
        LOAD_SYMBOL(gtk_window_set_title)
        LOAD_SYMBOL(gtk_window_set_default_size)
        LOAD_SYMBOL(gtk_window_move)
        LOAD_SYMBOL(gtk_window_set_position)
        LOAD_SYMBOL(gtk_window_show)
        LOAD_SYMBOL(gtk_window_set_child)
        LOAD_SYMBOL(gtk_window_destroy)
        LOAD_SYMBOL(gtk_window_close)
        LOAD_SYMBOL(gtk_window_get_width)
        LOAD_SYMBOL(gtk_window_get_height)
        LOAD_SYMBOL(gtk_window_set_resizable)
        LOAD_SYMBOL(gtk_window_get_resizable)
        LOAD_SYMBOL(gtk_window_set_decorated)
        LOAD_SYMBOL(gtk_window_set_modal)
        LOAD_SYMBOL(gtk_window_is_modal)
        LOAD_SYMBOL(gtk_window_maximize)
        LOAD_SYMBOL(gtk_window_unmaximize)
        LOAD_SYMBOL(gtk_window_is_maximized)
        LOAD_SYMBOL(gtk_window_minimize)
        LOAD_SYMBOL(gtk_window_present)
        
        /* Label */
        LOAD_SYMBOL(gtk_label_new)
        LOAD_SYMBOL(gtk_label_set_text)
        LOAD_SYMBOL(gtk_label_get_text)
        LOAD_SYMBOL(gtk_label_set_markup)
        LOAD_SYMBOL(gtk_label_set_justify)
        LOAD_SYMBOL(gtk_label_set_xalign)
        LOAD_SYMBOL(gtk_label_set_yalign)
        LOAD_SYMBOL(gtk_label_set_selectable)
        LOAD_SYMBOL(gtk_label_get_selectable)
        LOAD_SYMBOL(gtk_label_set_line_wrap)
        LOAD_SYMBOL(gtk_label_set_ellipsize)
        
        /* Button */
        LOAD_SYMBOL(gtk_button_new)
        LOAD_SYMBOL(gtk_button_new_with_label)
        LOAD_SYMBOL(gtk_button_set_label)
        LOAD_SYMBOL(gtk_button_get_label)
        LOAD_SYMBOL(gtk_button_set_icon_name)
        LOAD_SYMBOL(gtk_button_set_child)
        LOAD_SYMBOL(gtk_button_clicked)
        LOAD_SYMBOL(gtk_button_get_has_frame)
        LOAD_SYMBOL(gtk_button_set_has_frame)
        
        /* ToggleButton/CheckButton */
        LOAD_SYMBOL(gtk_toggle_button_new)
        LOAD_SYMBOL(gtk_toggle_button_new_with_label)
        LOAD_SYMBOL(gtk_toggle_button_set_active)
        LOAD_SYMBOL(gtk_toggle_button_get_active)
        LOAD_SYMBOL(gtk_toggle_button_toggled)
        LOAD_SYMBOL(gtk_check_button_new)
        LOAD_SYMBOL(gtk_check_button_new_with_label)
        LOAD_SYMBOL(gtk_check_button_set_group)
        LOAD_SYMBOL(gtk_check_button_get_group)
        
        /* Switch */
        LOAD_SYMBOL(gtk_switch_new)
        LOAD_SYMBOL(gtk_switch_set_active)
        LOAD_SYMBOL(gtk_switch_get_active)
        LOAD_SYMBOL(gtk_switch_set_state)
        LOAD_SYMBOL(gtk_switch_get_state)
        
        /* Entry */
        LOAD_SYMBOL(gtk_entry_new)
        LOAD_SYMBOL(gtk_entry_get_text)
        LOAD_SYMBOL(gtk_entry_set_text)
        LOAD_SYMBOL(gtk_entry_append_text)
        LOAD_SYMBOL(gtk_entry_get_text_length)
        LOAD_SYMBOL(gtk_entry_set_placeholder_text)
        LOAD_SYMBOL(gtk_entry_get_placeholder_text)
        LOAD_SYMBOL(gtk_entry_set_editable)
        LOAD_SYMBOL(gtk_entry_get_editable)
        LOAD_SYMBOL(gtk_entry_set_visibility)
        LOAD_SYMBOL(gtk_entry_get_visibility)
        LOAD_SYMBOL(gtk_entry_set_max_length)
        LOAD_SYMBOL(gtk_entry_get_max_length)
        LOAD_SYMBOL(gtk_entry_set_activates_default)
        LOAD_SYMBOL(gtk_entry_set_width_chars)
        LOAD_SYMBOL(gtk_entry_set_xalign)
        LOAD_SYMBOL(gtk_entry_set_input_purpose)
        
        /* Box */
        LOAD_SYMBOL(gtk_box_new)
        LOAD_SYMBOL(gtk_box_append)
        LOAD_SYMBOL(gtk_box_prepend)
        LOAD_SYMBOL(gtk_box_insert_child_after)
        LOAD_SYMBOL(gtk_box_insert_child_before)
        LOAD_SYMBOL(gtk_box_remove)
        LOAD_SYMBOL(gtk_box_reorder_child)
        LOAD_SYMBOL(gtk_box_set_spacing)
        LOAD_SYMBOL(gtk_box_get_spacing)
        LOAD_SYMBOL(gtk_box_set_homogeneous)
        LOAD_SYMBOL(gtk_box_get_homogeneous)
        
        /* Grid */
        LOAD_SYMBOL(gtk_grid_new)
        LOAD_SYMBOL(gtk_grid_attach)
        LOAD_SYMBOL(gtk_grid_remove)
        LOAD_SYMBOL(gtk_grid_insert_row)
        LOAD_SYMBOL(gtk_grid_insert_column)
        LOAD_SYMBOL(gtk_grid_set_row_spacing)
        LOAD_SYMBOL(gtk_grid_get_row_spacing)
        LOAD_SYMBOL(gtk_grid_set_column_spacing)
        LOAD_SYMBOL(gtk_grid_get_column_spacing)
        LOAD_SYMBOL(gtk_grid_set_row_homogeneous)
        LOAD_SYMBOL(gtk_grid_get_row_homogeneous)
        LOAD_SYMBOL(gtk_grid_set_column_homogeneous)
        LOAD_SYMBOL(gtk_grid_get_column_homogeneous)
        
        /* Fixed */
        LOAD_SYMBOL(gtk_fixed_new)
        LOAD_SYMBOL(gtk_fixed_put)
        LOAD_SYMBOL(gtk_fixed_move)
        
        /* Frame */
        LOAD_SYMBOL(gtk_frame_new)
        LOAD_SYMBOL(gtk_frame_set_label)
        LOAD_SYMBOL(gtk_frame_get_label)
        LOAD_SYMBOL(gtk_frame_set_label_align)
        LOAD_SYMBOL(gtk_frame_set_child)
        
        /* Separator */
        LOAD_SYMBOL(gtk_separator_new)
        
        /* Paned */
        LOAD_SYMBOL(gtk_paned_new)
        LOAD_SYMBOL(gtk_paned_pack1)
        LOAD_SYMBOL(gtk_paned_pack2)
        LOAD_SYMBOL(gtk_paned_set_position)
        LOAD_SYMBOL(gtk_paned_get_position)
        LOAD_SYMBOL(gtk_paned_set_wide_handle)
        
        /* Notebook */
        LOAD_SYMBOL(gtk_notebook_new)
        LOAD_SYMBOL(gtk_notebook_append_page)
        LOAD_SYMBOL(gtk_notebook_prepend_page)
        LOAD_SYMBOL(gtk_notebook_insert_page)
        LOAD_SYMBOL(gtk_notebook_remove_page)
        LOAD_SYMBOL(gtk_notebook_get_current_page)
        LOAD_SYMBOL(gtk_notebook_set_current_page)
        LOAD_SYMBOL(gtk_notebook_get_n_pages)
        LOAD_SYMBOL(gtk_notebook_set_tab_label_text)
        LOAD_SYMBOL(gtk_notebook_set_show_tabs)
        LOAD_SYMBOL(gtk_notebook_set_scrollable)
        
        /* Stack */
        LOAD_SYMBOL(gtk_stack_new)
        LOAD_SYMBOL(gtk_stack_add_named)
        LOAD_SYMBOL(gtk_stack_add_titled)
        LOAD_SYMBOL(gtk_stack_set_visible_child)
        LOAD_SYMBOL(gtk_stack_set_visible_child_by_name)
        LOAD_SYMBOL(gtk_stack_get_visible_child_name)
        LOAD_SYMBOL(gtk_stack_set_transition_type)
        LOAD_SYMBOL(gtk_stack_set_transition_duration)
        
        /* Revealer */
        LOAD_SYMBOL(gtk_revealer_new)
        LOAD_SYMBOL(gtk_revealer_set_child)
        LOAD_SYMBOL(gtk_revealer_set_reveal_child)
        LOAD_SYMBOL(gtk_revealer_get_reveal_child)
        LOAD_SYMBOL(gtk_revealer_set_transition_type)
        LOAD_SYMBOL(gtk_revealer_set_transition_duration)
        
        /* Expander */
        LOAD_SYMBOL(gtk_expander_new)
        LOAD_SYMBOL(gtk_expander_set_expanded)
        LOAD_SYMBOL(gtk_expander_get_expanded)
        LOAD_SYMBOL(gtk_expander_set_label)
        
        /* ScrolledWindow */
        LOAD_SYMBOL(gtk_scrolled_window_new)
        LOAD_SYMBOL(gtk_scrolled_window_set_child)
        LOAD_SYMBOL(gtk_scrolled_window_set_policy)
        LOAD_SYMBOL(gtk_scrolled_window_set_kinetic_scrolling)
        
        /* Viewport */
        LOAD_SYMBOL(gtk_viewport_new)
        LOAD_SYMBOL(gtk_viewport_set_child)
        
        /* TextView */
        LOAD_SYMBOL(gtk_text_view_new)
        LOAD_SYMBOL(gtk_text_view_get_buffer)
        LOAD_SYMBOL(gtk_text_view_set_buffer)
        LOAD_SYMBOL(gtk_text_view_set_editable)
        LOAD_SYMBOL(gtk_text_view_set_wrap_mode)
        LOAD_SYMBOL(gtk_text_view_set_left_margin)
        LOAD_SYMBOL(gtk_text_view_set_right_margin)
        LOAD_SYMBOL(gtk_text_view_set_top_margin)
        LOAD_SYMBOL(gtk_text_view_set_bottom_margin)
        LOAD_SYMBOL(gtk_text_view_set_monospace)
        
        /* TextBuffer */
        LOAD_SYMBOL(gtk_text_buffer_new)
        LOAD_SYMBOL(gtk_text_buffer_get_text)
        LOAD_SYMBOL(gtk_text_buffer_set_text)
        LOAD_SYMBOL(gtk_text_buffer_get_char_count)
        LOAD_SYMBOL(gtk_text_buffer_insert)
        LOAD_SYMBOL(gtk_text_buffer_insert_at_cursor)
        
        /* Image */
        LOAD_SYMBOL(gtk_image_new)
        LOAD_SYMBOL(gtk_image_new_from_icon_name)
        LOAD_SYMBOL(gtk_image_set_from_icon_name)
        LOAD_SYMBOL(gtk_image_set_from_pixbuf)
        LOAD_SYMBOL(gtk_image_clear)
        
        /* ProgressBar */
        LOAD_SYMBOL(gtk_progress_bar_new)
        LOAD_SYMBOL(gtk_progress_bar_set_fraction)
        LOAD_SYMBOL(gtk_progress_bar_get_fraction)
        LOAD_SYMBOL(gtk_progress_bar_set_pulse)
        LOAD_SYMBOL(gtk_progress_bar_pulse)
        LOAD_SYMBOL(gtk_progress_bar_set_text)
        LOAD_SYMBOL(gtk_progress_bar_get_text)
        LOAD_SYMBOL(gtk_progress_bar_set_show_text)
        LOAD_SYMBOL(gtk_progress_bar_set_ellipsize)
        
        /* LevelBar */
        LOAD_SYMBOL(gtk_level_bar_new)
        LOAD_SYMBOL(gtk_level_bar_new_for_interval)
        LOAD_SYMBOL(gtk_level_bar_set_value)
        LOAD_SYMBOL(gtk_level_bar_get_value)
        LOAD_SYMBOL(gtk_level_bar_set_min_value)
        LOAD_SYMBOL(gtk_level_bar_get_min_value)
        LOAD_SYMBOL(gtk_level_bar_set_max_value)
        LOAD_SYMBOL(gtk_level_bar_get_max_value)
        LOAD_SYMBOL(gtk_level_bar_set_bar_mode)
        LOAD_SYMBOL(gtk_level_bar_get_bar_mode)
        
        /* Scale */
        LOAD_SYMBOL(gtk_scale_new)
        LOAD_SYMBOL(gtk_scale_new_with_range)
        LOAD_SYMBOL(gtk_scale_set_digits)
        LOAD_SYMBOL(gtk_scale_get_digits)
        LOAD_SYMBOL(gtk_scale_set_draw_value)
        LOAD_SYMBOL(gtk_scale_get_draw_value)
        LOAD_SYMBOL(gtk_scale_set_value)
        LOAD_SYMBOL(gtk_scale_get_value)
        LOAD_SYMBOL(gtk_scale_set_has_origin)
        LOAD_SYMBOL(gtk_scale_set_fill_level)
        
        /* SpinButton */
        LOAD_SYMBOL(gtk_spin_button_new)
        LOAD_SYMBOL(gtk_spin_button_new_with_range)
        LOAD_SYMBOL(gtk_spin_button_set_value)
        LOAD_SYMBOL(gtk_spin_button_get_value)
        LOAD_SYMBOL(gtk_spin_button_get_value_as_int)
        LOAD_SYMBOL(gtk_spin_button_set_range)
        LOAD_SYMBOL(gtk_spin_button_set_increments)
        LOAD_SYMBOL(gtk_spin_button_set_digits)
        LOAD_SYMBOL(gtk_spin_button_spin)
        
        /* ComboBoxText */
        LOAD_SYMBOL(gtk_combo_box_text_new)
        LOAD_SYMBOL(gtk_combo_box_text_append)
        LOAD_SYMBOL(gtk_combo_box_text_prepend)
        LOAD_SYMBOL(gtk_combo_box_text_insert)
        LOAD_SYMBOL(gtk_combo_box_text_remove)
        LOAD_SYMBOL(gtk_combo_box_text_remove_all)
        LOAD_SYMBOL(gtk_combo_box_text_get_active_id)
        LOAD_SYMBOL(gtk_combo_box_text_set_active_id)
        LOAD_SYMBOL(gtk_combo_box_text_set_active)
        LOAD_SYMBOL(gtk_combo_box_text_get_active)
        LOAD_SYMBOL(gtk_combo_box_text_get_active_text)
        
        /* ListBox */
        LOAD_SYMBOL(gtk_list_box_new)
        LOAD_SYMBOL(gtk_list_box_append)
        LOAD_SYMBOL(gtk_list_box_prepend)
        LOAD_SYMBOL(gtk_list_box_insert)
        LOAD_SYMBOL(gtk_list_box_remove)
        LOAD_SYMBOL(gtk_list_box_select_row)
        LOAD_SYMBOL(gtk_list_box_unselect_row)
        LOAD_SYMBOL(gtk_list_box_unselect_all)
        LOAD_SYMBOL(gtk_list_box_set_selection_mode)
        LOAD_SYMBOL(gtk_list_box_get_selection_mode)
        
        /* FlowBox */
        LOAD_SYMBOL(gtk_flow_box_new)
        LOAD_SYMBOL(gtk_flow_box_append)
        LOAD_SYMBOL(gtk_flow_box_insert)
        LOAD_SYMBOL(gtk_flow_box_select_child)
        LOAD_SYMBOL(gtk_flow_box_unselect_all)
        LOAD_SYMBOL(gtk_flow_box_set_selection_mode)
        LOAD_SYMBOL(gtk_flow_box_set_min_children_per_line)
        LOAD_SYMBOL(gtk_flow_box_set_max_children_per_line)
        LOAD_SYMBOL(gtk_flow_box_set_column_spacing)
        LOAD_SYMBOL(gtk_flow_box_set_row_spacing)
        
        /* Menu */
        LOAD_SYMBOL(gtk_menu_bar_new)
        LOAD_SYMBOL(gtk_menu_bar_append)
        LOAD_SYMBOL(gtk_menu_new)
        LOAD_SYMBOL(gtk_menu_append)
        LOAD_SYMBOL(gtk_menu_prepend)
        LOAD_SYMBOL(gtk_menu_insert)
        LOAD_SYMBOL(gtk_menu_remove)
        LOAD_SYMBOL(gtk_menu_popup_at_pointer)
        LOAD_SYMBOL(gtk_menu_item_new)
        LOAD_SYMBOL(gtk_menu_item_new_with_label)
        LOAD_SYMBOL(gtk_menu_item_set_label)
        LOAD_SYMBOL(gtk_menu_item_set_submenu)
        LOAD_SYMBOL(gtk_menu_item_activate)
        
        /* Popover */
        LOAD_SYMBOL(gtk_popover_new)
        LOAD_SYMBOL(gtk_popover_set_child)
        LOAD_SYMBOL(gtk_popover_popup)
        LOAD_SYMBOL(gtk_popover_popdown)
        
        /* Dialog */
        LOAD_SYMBOL(gtk_dialog_new)
        LOAD_SYMBOL(gtk_dialog_add_button)
        LOAD_SYMBOL(gtk_dialog_add_action_widget)
        LOAD_SYMBOL(gtk_dialog_run)
        LOAD_SYMBOL(gtk_dialog_response)
        LOAD_SYMBOL(gtk_dialog_set_default_response)
        LOAD_SYMBOL(gtk_dialog_set_use_header_bar)
        LOAD_SYMBOL(gtk_dialog_get_content_area)
        LOAD_SYMBOL(gtk_dialog_get_header_bar)
        
        /* AboutDialog */
        LOAD_SYMBOL(gtk_about_dialog_new)
        LOAD_SYMBOL(gtk_about_dialog_set_program_name)
        LOAD_SYMBOL(gtk_about_dialog_set_version)
        LOAD_SYMBOL(gtk_about_dialog_set_comments)
        LOAD_SYMBOL(gtk_about_dialog_set_website)
        LOAD_SYMBOL(gtk_about_dialog_set_license)
        
        /* MessageDialog */
        LOAD_SYMBOL(gtk_message_dialog_new)
        LOAD_SYMBOL(gtk_message_dialog_set_markup)
        
        /* HeaderBar */
        LOAD_SYMBOL(gtk_header_bar_new)
        LOAD_SYMBOL(gtk_header_bar_set_title)
        LOAD_SYMBOL(gtk_header_bar_get_title)
        LOAD_SYMBOL(gtk_header_bar_set_subtitle)
        LOAD_SYMBOL(gtk_header_bar_get_subtitle)
        LOAD_SYMBOL(gtk_header_bar_set_show_title_buttons)
        LOAD_SYMBOL(gtk_header_bar_pack_start)
        LOAD_SYMBOL(gtk_header_bar_pack_end)
        LOAD_SYMBOL(gtk_header_bar_append)
        LOAD_SYMBOL(gtk_header_bar_insert_child_after)
        
        /* ActionBar */
        LOAD_SYMBOL(gtk_action_bar_new)
        LOAD_SYMBOL(gtk_action_bar_set_reveal_child)
        LOAD_SYMBOL(gtk_action_bar_get_reveal_child)
        LOAD_SYMBOL(gtk_action_bar_pack_start)
        LOAD_SYMBOL(gtk_action_bar_pack_end)
        LOAD_SYMBOL(gtk_action_bar_set_center_widget)
        
        /* Spinner */
        LOAD_SYMBOL(gtk_spinner_new)
        LOAD_SYMBOL(gtk_spinner_start)
        LOAD_SYMBOL(gtk_spinner_stop)
        LOAD_SYMBOL(gtk_spinner_set_spinning)
        
        /* SearchEntry */
        LOAD_SYMBOL(gtk_search_entry_new)
        
        /* PasswordEntry */
        LOAD_SYMBOL(gtk_password_entry_new)
        
        /* CenterBox */
        LOAD_SYMBOL(gtk_center_box_new)
        LOAD_SYMBOL(gtk_center_box_set_start_widget)
        LOAD_SYMBOL(gtk_center_box_set_center_widget)
        LOAD_SYMBOL(gtk_center_box_set_end_widget)
        LOAD_SYMBOL(gtk_center_box_get_start_widget)
        LOAD_SYMBOL(gtk_center_box_get_center_widget)
        LOAD_SYMBOL(gtk_center_box_get_end_widget)
        
        /* Overlay */
        LOAD_SYMBOL(gtk_overlay_new)
        LOAD_SYMBOL(gtk_overlay_set_child)
        LOAD_SYMBOL(gtk_overlay_add_overlay)
        
        /* Widget base */
        LOAD_SYMBOL(gtk_widget_destroy)
        LOAD_SYMBOL(gtk_widget_show)
        LOAD_SYMBOL(gtk_widget_hide)
        LOAD_SYMBOL(gtk_widget_set_visible)
        LOAD_SYMBOL(gtk_widget_get_visible)
        LOAD_SYMBOL(gtk_widget_set_sensitive)
        LOAD_SYMBOL(gtk_widget_get_sensitive)
        LOAD_SYMBOL(gtk_widget_set_tooltip_text)
        LOAD_SYMBOL(gtk_widget_get_tooltip_text)
        LOAD_SYMBOL(gtk_widget_set_cursor_from_name)
        LOAD_SYMBOL(gtk_widget_add_css_class)
        LOAD_SYMBOL(gtk_widget_remove_css_class)
        LOAD_SYMBOL(gtk_widget_has_css_class)
        LOAD_SYMBOL(gtk_widget_set_margin_top)
        LOAD_SYMBOL(gtk_widget_set_margin_bottom)
        LOAD_SYMBOL(gtk_widget_set_margin_start)
        LOAD_SYMBOL(gtk_widget_set_margin_end)
        LOAD_SYMBOL(gtk_widget_get_width)
        LOAD_SYMBOL(gtk_widget_get_height)
        LOAD_SYMBOL(gtk_widget_set_size_request)
        LOAD_SYMBOL(gtk_widget_queue_draw)
        LOAD_SYMBOL(gtk_widget_grab_focus)
        LOAD_SYMBOL(gtk_widget_has_focus)
        LOAD_SYMBOL(gtk_widget_set_focusable)
        LOAD_SYMBOL(gtk_widget_set_focus_on_click)
        
#undef LOAD_SYMBOL
        
        /* g_free might be in GLib */
        g_free = glibLoader.getSymbol<GtkFreeFn>("g_free");
        if (!g_free) {
            g_free = gtkLoader.getSymbol<GtkFreeFn>("g_free");
        }
        
        return true;
    }
    
    bool isLoaded() const {
        return gtkLoader.isLoaded();
    }
};

static Gtk4Libs* g_gtkLibs = nullptr;

/* GTK widget handle system */
std::unordered_map<int64_t, GtkWidget*> g_widgets;
std::unordered_map<int64_t, GtkWindow*> g_windows;
int64_t g_nextWidgetId = 1;

void cleanupWidget(void* ptr) {
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) {
        return;
    }
    
    int64_t id = reinterpret_cast<int64_t>(ptr);
    
    auto winIt = g_windows.find(id);
    if (winIt != g_windows.end()) {
        g_gtkLibs->gtk_window_destroy(winIt->second);
        g_windows.erase(winIt);
    }
    
    auto widgetIt = g_widgets.find(id);
    if (widgetIt != g_widgets.end()) {
        g_gtkLibs->gtk_widget_destroy(widgetIt->second);
        g_widgets.erase(widgetIt);
    }
}

int64_t storeWidget(GtkWidget* widget) {
    int64_t id = g_nextWidgetId++;
    g_widgets[id] = widget;
    return id;
}

int64_t storeWindow(GtkWindow* window) {
    int64_t id = g_nextWidgetId++;
    g_windows[id] = window;
    g_widgets[id] = (GtkWidget*)window;
    return id;
}

GtkWidget* getWidget(int64_t id) {
    auto it = g_widgets.find(id);
    if (it == g_widgets.end()) {
        return nullptr;
    }
    return it->second;
}

GtkWidget* getWidgetChecked(int64_t id, const char* typeName) {
    GtkWidget* widget = getWidget(id);
    if (!widget) {
        fprintf(stderr, "[GTK] Widget ID %ld not found\n", (long)id);
        return nullptr;
    }
    return widget;
}

/* ============================================================================
 * EVENT CALLBACK SYSTEM
 * ============================================================================ */

/* Event callback types */
enum GtkEventType {
    GTK_EVENT_CLICKED = 0,
    GTK_EVENT_TEXT_CHANGED = 1,
    GTK_EVENT_VALUE_CHANGED = 2,
    GTK_EVENT_STATE_CHANGED = 3,
    GTK_EVENT_SELECTION_CHANGED = 4,
    GTK_EVENT_TAB_CHANGED = 5,
    GTK_EVENT_ROW_ACTIVATED = 6,
};

/* Callback entry for a widget event */
struct GtkEventCallback {
    HavelValue* callback;  /* Havel function to call */
    int64_t widgetId;      /* Widget that triggered this */
    GtkEventType type;     /* Type of event */

    GtkEventCallback() : callback(nullptr), widgetId(0), type(GTK_EVENT_CLICKED) {}
    GtkEventCallback(HavelValue* cb, int64_t id, GtkEventType t)
        : callback(cb), widgetId(id), type(t) {
        if (callback) havel_incref(callback);
    }
    ~GtkEventCallback() {
        if (callback) havel_decref(callback);
    }
};

/* Map: widgetId -> list of callbacks for that widget */
std::unordered_map<int64_t, std::vector<GtkEventCallback>> g_gtkWidgetCallbacks;

/* Store last known values for change detection */
std::unordered_map<int64_t, int> g_gtkLastIntValue;
std::unordered_map<int64_t, std::string> g_gtkLastTextValue;

/* Register a callback for a widget event */
void registerGtkWidgetCallback(int64_t widgetId, HavelValue* callback, GtkEventType type) {
    GtkEventCallback cb(callback, widgetId, type);
    g_gtkWidgetCallbacks[widgetId].push_back(cb);
}

/* Trigger callback with optional argument */
void triggerGtkCallback(const GtkEventCallback& cb, HavelValue* arg = nullptr) {
    if (!cb.callback) return;
    /* Callback will be invoked from processCallbacks */
    (void)arg;
}

/* Check and trigger value changes */
void checkGtkWidgetChanges() {
    for (auto& pair : g_gtkWidgetCallbacks) {
        int64_t widgetId = pair.first;
        GtkWidget* widget = getWidget(widgetId);
        if (!widget) continue;

        for (auto& cb : pair.second) {
            /* Check text changes for Entry */
            if (cb.type == GTK_EVENT_TEXT_CHANGED) {
                const char* currentText = g_gtkLibs->gtk_entry_get_text(widget);
                std::string current(currentText ? currentText : "");
                auto it = g_gtkLastTextValue.find(widgetId);
                if (it == g_gtkLastTextValue.end() || it->second != current) {
                    g_gtkLastTextValue[widgetId] = current;
                    HavelValue* arg = havel_new_string(current.c_str());
                    triggerGtkCallback(cb, arg);
                    havel_free_value(arg);
                }
            }
            /* Check value changes for Scale/SpinButton */
            else if (cb.type == GTK_EVENT_VALUE_CHANGED) {
                /* Value checking done in widget-specific functions */
            }
            /* Check state changes for CheckButton/Switch */
            else if (cb.type == GTK_EVENT_STATE_CHANGED) {
                /* State checking done in widget-specific functions */
            }
        }
    }
}

/* Process all registered GTK callbacks */
void processGtkCallbacks() {
    checkGtkWidgetChanges();
}

/* ============================================================================
 * STATIC C FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_init_app(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    
    /* Lazy load GTK4 on first use */
    if (!g_gtkLibs) {
        g_gtkLibs = new Gtk4Libs();
        if (!g_gtkLibs->load()) {
            fprintf(stderr, "[GTK] Failed to load GTK4 libraries\n");
            delete g_gtkLibs;
            g_gtkLibs = nullptr;
            return havel_new_bool(0);
        }
        fprintf(stderr, "[GTK] GTK4 libraries loaded dynamically\n");
    }
    
    g_gtkLibs->gtk_init(nullptr, nullptr);
    return havel_new_bool(1);
}

static HavelValue* gtk_run_main(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) {
        fprintf(stderr, "[GTK] GTK4 not initialized\n");
        return havel_new_null();
    }
    
    g_gtkLibs->gtk_main();
    return havel_new_null();
}

static HavelValue* gtk_quit_main(int argc, HavelValue** argv) {
    (void)argc; (void)argv;

    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) {
        return havel_new_null();
    }

    g_gtkLibs->gtk_quit();
    return havel_new_null();
}

/* ============================================================================
 * WINDOW FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_window_new(int argc, HavelValue** argv) {
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) {
        fprintf(stderr, "[GTK] GTK4 not initialized. Call gtk.init() first.\n");
        return havel_new_int(0);
    }
    
    const char* title = "Window";
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_STRING) {
        title = havel_get_string(argv[0]);
    }
    
    GtkWindow* window = (GtkWindow*)g_gtkLibs->gtk_window_new();
    g_gtkLibs->gtk_window_set_title(window, title);
    g_gtkLibs->gtk_window_set_default_size(window, 800, 600);
    
    int64_t id = storeWindow(window);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_window_show(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    g_gtkLibs->gtk_window_show(widget);
    return havel_new_bool(1);
}

static HavelValue* gtk_window_hide(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    g_gtkLibs->gtk_widget_hide(widget);
    return havel_new_bool(1);
}

static HavelValue* gtk_window_close(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto winIt = g_windows.find(id);
    if (winIt == g_windows.end()) return havel_new_bool(0);
    
    g_gtkLibs->gtk_window_close(winIt->second);
    return havel_new_bool(1);
}

static HavelValue* gtk_window_set_title(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto winIt = g_windows.find(id);
    if (winIt == g_windows.end()) return havel_new_bool(0);
    
    const char* title = havel_get_string(argv[1]);
    if (!title) return havel_new_bool(0);
    
    g_gtkLibs->gtk_window_set_title(winIt->second, title);
    return havel_new_bool(1);
}

static HavelValue* gtk_window_set_size(int argc, HavelValue** argv) {
    if (argc < 3) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto winIt = g_windows.find(id);
    if (winIt == g_windows.end()) return havel_new_bool(0);
    
    int64_t width = havel_get_int(argv[1]);
    int64_t height = havel_get_int(argv[2]);
    
    g_gtkLibs->gtk_window_set_default_size(winIt->second, (int)width, (int)height);
    return havel_new_bool(1);
}

static HavelValue* gtk_window_get_size(int argc, HavelValue** argv) {
    if (argc < 1) {
        HavelValue* result = havel_new_object();
        havel_object_set(result, "width", havel_new_int(0));
        havel_object_set(result, "height", havel_new_int(0));
        return result;
    }
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) {
        HavelValue* result = havel_new_object();
        havel_object_set(result, "width", havel_new_int(0));
        havel_object_set(result, "height", havel_new_int(0));
        return result;
    }
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto winIt = g_windows.find(id);
    if (winIt == g_windows.end()) {
        HavelValue* result = havel_new_object();
        havel_object_set(result, "width", havel_new_int(0));
        havel_object_set(result, "height", havel_new_int(0));
        return result;
    }
    
    int width = g_gtkLibs->gtk_window_get_width((GtkWindow*)winIt->second);
    int height = g_gtkLibs->gtk_window_get_height((GtkWindow*)winIt->second);
    
    HavelValue* result = havel_new_object();
    havel_object_set(result, "width", havel_new_int(width));
    havel_object_set(result, "height", havel_new_int(height));
    return result;
}

static HavelValue* gtk_window_set_position(int argc, HavelValue** argv) {
    if (argc < 3) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto winIt = g_windows.find(id);
    if (winIt == g_windows.end()) return havel_new_bool(0);
    
    int64_t x = havel_get_int(argv[1]);
    int64_t y = havel_get_int(argv[2]);
    
    g_gtkLibs->gtk_window_set_position(winIt->second, GTK_WIN_POS_NONE);
    g_gtkLibs->gtk_window_move(winIt->second, (int)x, (int)y);
    return havel_new_bool(1);
}

static HavelValue* gtk_window_set_child(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t windowId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t childId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    
    auto winIt = g_windows.find(windowId);
    GtkWidget* child = getWidget(childId);
    
    if (winIt == g_windows.end()) return havel_new_bool(0);
    if (!child) return havel_new_bool(0);
    
    g_gtkLibs->gtk_window_set_child(winIt->second, child);
    return havel_new_bool(1);
}

static HavelValue* gtk_window_set_resizable(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto winIt = g_windows.find(id);
    if (winIt == g_windows.end()) return havel_new_bool(0);
    
    int resizable = havel_get_bool(argv[1]);
    g_gtkLibs->gtk_window_set_resizable(winIt->second, resizable);
    return havel_new_bool(1);
}

static HavelValue* gtk_window_set_modal(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto winIt = g_windows.find(id);
    if (winIt == g_windows.end()) return havel_new_bool(0);
    
    int modal = havel_get_bool(argv[1]);
    g_gtkLibs->gtk_window_set_modal(winIt->second, modal);
    return havel_new_bool(1);
}

static HavelValue* gtk_window_maximize(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto winIt = g_windows.find(id);
    if (winIt == g_windows.end()) return havel_new_bool(0);
    
    g_gtkLibs->gtk_window_maximize(winIt->second);
    return havel_new_bool(1);
}

static HavelValue* gtk_window_minimize(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto winIt = g_windows.find(id);
    if (winIt == g_windows.end()) return havel_new_bool(0);
    
    g_gtkLibs->gtk_window_minimize(winIt->second);
    return havel_new_bool(1);
}

/* ============================================================================
 * LABEL FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_label_new(int argc, HavelValue** argv) {
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    const char* text = "";
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_STRING) {
        text = havel_get_string(argv[0]);
    }
    
    GtkWidget* label = g_gtkLibs->gtk_label_new(text);
    int64_t id = storeWidget(label);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_label_set_text(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkLabel");
    if (!widget) return havel_new_bool(0);
    
    const char* text = havel_get_string(argv[1]);
    if (!text) return havel_new_bool(0);
    
    g_gtkLibs->gtk_label_set_text(widget, text);
    return havel_new_bool(1);
}

static HavelValue* gtk_label_get_text(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_string("");
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_string("");
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkLabel");
    if (!widget) return havel_new_string("");
    
    const char* text = g_gtkLibs->gtk_label_get_text(widget);
    return havel_new_string(text ? text : "");
}

static HavelValue* gtk_label_set_markup(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkLabel");
    if (!widget) return havel_new_bool(0);
    
    const char* markup = havel_get_string(argv[1]);
    if (!markup) return havel_new_bool(0);
    
    g_gtkLibs->gtk_label_set_markup(widget, markup);
    return havel_new_bool(1);
}

static HavelValue* gtk_label_set_selectable(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkLabel");
    if (!widget) return havel_new_bool(0);
    
    int selectable = havel_get_bool(argv[1]);
    g_gtkLibs->gtk_label_set_selectable(widget, selectable);
    return havel_new_bool(1);
}

/* ============================================================================
 * BUTTON FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_button_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* button = g_gtkLibs->gtk_button_new();
    int64_t id = storeWidget(button);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_button_new_with_label(int argc, HavelValue** argv) {
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    const char* label = "";
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_STRING) {
        label = havel_get_string(argv[0]);
    }
    
    GtkWidget* button = g_gtkLibs->gtk_button_new_with_label(label);
    int64_t id = storeWidget(button);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_button_set_label(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkButton");
    if (!widget) return havel_new_bool(0);
    
    const char* label = havel_get_string(argv[1]);
    if (!label) return havel_new_bool(0);
    
    g_gtkLibs->gtk_button_set_label(widget, label);
    return havel_new_bool(1);
}

static HavelValue* gtk_button_set_icon_name(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkButton");
    if (!widget) return havel_new_bool(0);
    
    const char* iconName = havel_get_string(argv[1]);
    if (!iconName) return havel_new_bool(0);
    
    g_gtkLibs->gtk_button_set_icon_name(widget, iconName);
    return havel_new_bool(1);
}

static HavelValue* gtk_button_clicked(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_null();
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_null();
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkButton");
    if (!widget) return havel_new_null();
    
    g_gtkLibs->gtk_button_clicked(widget);
    return havel_new_null();
}

/* ============================================================================
 * TOGGLE BUTTON / CHECK BUTTON FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_toggle_button_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* button = g_gtkLibs->gtk_toggle_button_new();
    int64_t id = storeWidget(button);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_toggle_button_set_active(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    int active = havel_get_bool(argv[1]);
    g_gtkLibs->gtk_toggle_button_set_active(widget, active);
    return havel_new_bool(1);
}

static HavelValue* gtk_toggle_button_get_active(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    return havel_new_bool(g_gtkLibs->gtk_toggle_button_get_active(widget));
}

static HavelValue* gtk_check_button_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* button = g_gtkLibs->gtk_check_button_new();
    int64_t id = storeWidget(button);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_check_button_new_with_label(int argc, HavelValue** argv) {
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    const char* label = "";
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_STRING) {
        label = havel_get_string(argv[0]);
    }
    
    GtkWidget* button = g_gtkLibs->gtk_check_button_new_with_label(label);
    int64_t id = storeWidget(button);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

/* ============================================================================
 * SWITCH FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_switch_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* sw = g_gtkLibs->gtk_switch_new();
    int64_t id = storeWidget(sw);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_switch_set_active(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    int active = havel_get_bool(argv[1]);
    g_gtkLibs->gtk_switch_set_active(widget, active);
    return havel_new_bool(1);
}

static HavelValue* gtk_switch_get_active(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    return havel_new_bool(g_gtkLibs->gtk_switch_get_active(widget));
}

/* ============================================================================
 * ENTRY FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_entry_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* entry = g_gtkLibs->gtk_entry_new();
    int64_t id = storeWidget(entry);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_entry_get_text(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_string("");
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_string("");
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkEntry");
    if (!widget) return havel_new_string("");
    
    const char* text = g_gtkLibs->gtk_entry_get_text(widget);
    return havel_new_string(text ? text : "");
}

static HavelValue* gtk_entry_set_text(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkEntry");
    if (!widget) return havel_new_bool(0);
    
    const char* text = havel_get_string(argv[1]);
    if (!text) return havel_new_bool(0);
    
    g_gtkLibs->gtk_entry_set_text(widget, text);
    return havel_new_bool(1);
}

static HavelValue* gtk_entry_set_placeholder_text(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkEntry");
    if (!widget) return havel_new_bool(0);
    
    const char* text = havel_get_string(argv[1]);
    g_gtkLibs->gtk_entry_set_placeholder_text(widget, text);
    return havel_new_bool(1);
}

static HavelValue* gtk_entry_set_editable(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkEntry");
    if (!widget) return havel_new_bool(0);
    
    int editable = havel_get_bool(argv[1]);
    g_gtkLibs->gtk_entry_set_editable(widget, editable);
    return havel_new_bool(1);
}

static HavelValue* gtk_entry_set_visibility(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkEntry");
    if (!widget) return havel_new_bool(0);
    
    int visible = havel_get_bool(argv[1]);
    g_gtkLibs->gtk_entry_set_visibility(widget, visible);
    return havel_new_bool(1);
}

static HavelValue* gtk_entry_set_max_length(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkEntry");
    if (!widget) return havel_new_bool(0);
    
    int maxLength = (int)havel_get_int(argv[1]);
    g_gtkLibs->gtk_entry_set_max_length(widget, maxLength);
    return havel_new_bool(1);
}

static HavelValue* gtk_search_entry_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* entry = g_gtkLibs->gtk_search_entry_new();
    int64_t id = storeWidget(entry);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_password_entry_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* entry = g_gtkLibs->gtk_password_entry_new();
    int64_t id = storeWidget(entry);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

/* ============================================================================
 * BOX FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_box_new(int argc, HavelValue** argv) {
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    int orientation = GTK_ORIENTATION_VERTICAL;
    int spacing = 0;
    
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_STRING) {
        const char* orientStr = havel_get_string(argv[0]);
        if (orientStr && strcmp(orientStr, "horizontal") == 0) {
            orientation = GTK_ORIENTATION_HORIZONTAL;
        }
    }
    
    if (argc >= 2 && havel_get_type(argv[1]) == HAVEL_INT) {
        spacing = (int)havel_get_int(argv[1]);
    }
    
    GtkWidget* box = g_gtkLibs->gtk_box_new(orientation, spacing);
    int64_t id = storeWidget(box);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_box_append(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t boxId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t childId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    
    GtkWidget* box = getWidget(boxId);
    GtkWidget* child = getWidget(childId);
    
    if (!box) return havel_new_bool(0);
    if (!child) return havel_new_bool(0);
    
    g_gtkLibs->gtk_box_append(box, child);
    return havel_new_bool(1);
}

static HavelValue* gtk_box_prepend(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t boxId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t childId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    
    GtkWidget* box = getWidget(boxId);
    GtkWidget* child = getWidget(childId);
    
    if (!box) return havel_new_bool(0);
    if (!child) return havel_new_bool(0);
    
    g_gtkLibs->gtk_box_prepend(box, child);
    return havel_new_bool(1);
}

static HavelValue* gtk_box_set_spacing(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t boxId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* box = getWidget(boxId);
    if (!box) return havel_new_bool(0);
    
    int spacing = (int)havel_get_int(argv[1]);
    g_gtkLibs->gtk_box_set_spacing(box, spacing);
    return havel_new_bool(1);
}

static HavelValue* gtk_box_set_homogeneous(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t boxId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* box = getWidget(boxId);
    if (!box) return havel_new_bool(0);
    
    int homogeneous = havel_get_bool(argv[1]);
    g_gtkLibs->gtk_box_set_homogeneous(box, homogeneous);
    return havel_new_bool(1);
}

/* ============================================================================
 * GRID FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_grid_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* grid = g_gtkLibs->gtk_grid_new();
    int64_t id = storeWidget(grid);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_grid_attach(int argc, HavelValue** argv) {
    if (argc < 5) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t gridId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t childId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    int64_t column = havel_get_int(argv[2]);
    int64_t row = havel_get_int(argv[3]);
    int64_t colspan = havel_get_int(argv[4]);
    int64_t rowspan = (argc >= 6) ? havel_get_int(argv[5]) : 1;
    
    GtkWidget* grid = getWidget(gridId);
    GtkWidget* child = getWidget(childId);
    
    if (!grid) return havel_new_bool(0);
    if (!child) return havel_new_bool(0);
    
    g_gtkLibs->gtk_grid_attach(grid, child, (int)column, (int)row, (int)colspan, (int)rowspan);
    return havel_new_bool(1);
}

static HavelValue* gtk_grid_set_row_spacing(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t gridId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* grid = getWidget(gridId);
    if (!grid) return havel_new_bool(0);
    
    int spacing = (int)havel_get_int(argv[1]);
    g_gtkLibs->gtk_grid_set_row_spacing(grid, spacing);
    return havel_new_bool(1);
}

static HavelValue* gtk_grid_set_column_spacing(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t gridId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* grid = getWidget(gridId);
    if (!grid) return havel_new_bool(0);
    
    int spacing = (int)havel_get_int(argv[1]);
    g_gtkLibs->gtk_grid_set_column_spacing(grid, spacing);
    return havel_new_bool(1);
}

/* ============================================================================
 * FRAME FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_frame_new(int argc, HavelValue** argv) {
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    const char* label = "";
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_STRING) {
        label = havel_get_string(argv[0]);
    }
    
    GtkWidget* frame = g_gtkLibs->gtk_frame_new(label);
    int64_t id = storeWidget(frame);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_frame_set_label(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkFrame");
    if (!widget) return havel_new_bool(0);
    
    const char* label = havel_get_string(argv[1]);
    g_gtkLibs->gtk_frame_set_label(widget, label);
    return havel_new_bool(1);
}

static HavelValue* gtk_frame_set_child(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t frameId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t childId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    
    GtkWidget* frame = getWidget(frameId);
    GtkWidget* child = getWidget(childId);
    
    if (!frame) return havel_new_bool(0);
    if (!child) return havel_new_bool(0);
    
    g_gtkLibs->gtk_frame_set_child(frame, child);
    return havel_new_bool(1);
}

/* ============================================================================
 * SEPARATOR FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_separator_new(int argc, HavelValue** argv) {
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    int orientation = GTK_ORIENTATION_HORIZONTAL;
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_STRING) {
        const char* orientStr = havel_get_string(argv[0]);
        if (orientStr && strcmp(orientStr, "vertical") == 0) {
            orientation = GTK_ORIENTATION_VERTICAL;
        }
    }
    
    GtkWidget* separator = g_gtkLibs->gtk_separator_new(orientation);
    int64_t id = storeWidget(separator);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

/* ============================================================================
 * SCROLLED WINDOW FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_scrolled_window_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* scrolled = g_gtkLibs->gtk_scrolled_window_new();
    int64_t id = storeWidget(scrolled);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_scrolled_window_set_child(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t scrolledId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t childId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    
    GtkWidget* scrolled = getWidget(scrolledId);
    GtkWidget* child = getWidget(childId);
    
    if (!scrolled) return havel_new_bool(0);
    if (!child) return havel_new_bool(0);
    
    g_gtkLibs->gtk_scrolled_window_set_child(scrolled, child);
    return havel_new_bool(1);
}

static HavelValue* gtk_scrolled_window_set_policy(int argc, HavelValue** argv) {
    if (argc < 3) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t scrolledId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* scrolled = getWidget(scrolledId);
    if (!scrolled) return havel_new_bool(0);
    
    const char* hPolicyStr = havel_get_string(argv[1]);
    const char* vPolicyStr = havel_get_string(argv[2]);
    
    int hPolicy = GTK_POLICY_AUTOMATIC;
    int vPolicy = GTK_POLICY_AUTOMATIC;
    
    if (hPolicyStr) {
        if (strcmp(hPolicyStr, "always") == 0) hPolicy = GTK_POLICY_ALWAYS;
        else if (strcmp(hPolicyStr, "never") == 0) hPolicy = GTK_POLICY_NEVER;
        else if (strcmp(hPolicyStr, "external") == 0) hPolicy = GTK_POLICY_EXTERNAL;
    }
    
    if (vPolicyStr) {
        if (strcmp(vPolicyStr, "always") == 0) vPolicy = GTK_POLICY_ALWAYS;
        else if (strcmp(vPolicyStr, "never") == 0) vPolicy = GTK_POLICY_NEVER;
        else if (strcmp(vPolicyStr, "external") == 0) vPolicy = GTK_POLICY_EXTERNAL;
    }
    
    g_gtkLibs->gtk_scrolled_window_set_policy(scrolled, hPolicy, vPolicy);
    return havel_new_bool(1);
}

/* ============================================================================
 * TEXT VIEW FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_text_view_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* textview = g_gtkLibs->gtk_text_view_new();
    int64_t id = storeWidget(textview);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_text_view_get_buffer(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_null();
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_null();
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkTextView");
    if (!widget) return havel_new_null();
    
    void* buffer = g_gtkLibs->gtk_text_view_get_buffer(widget);
    int64_t bufferId = storeWidget((GtkWidget*)buffer);
    return havel_new_handle(reinterpret_cast<void*>(bufferId), nullptr);
}

static HavelValue* gtk_text_buffer_set_text(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    const char* text = havel_get_string(argv[1]);
    if (!text) return havel_new_bool(0);
    
    g_gtkLibs->gtk_text_buffer_set_text((GtkWidget*)widget, text, -1);
    return havel_new_bool(1);
}

static HavelValue* gtk_text_buffer_get_text(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_string("");
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_string("");
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_string("");
    
    char* text = g_gtkLibs->gtk_text_buffer_get_text((GtkWidget*)widget, nullptr, nullptr, 0);
    
    HavelValue* result = havel_new_string(text ? text : "");
    if (text && g_gtkLibs->g_free) {
        g_gtkLibs->g_free(text);
    }
    return result;
}

static HavelValue* gtk_text_view_set_editable(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkTextView");
    if (!widget) return havel_new_bool(0);
    
    int editable = havel_get_bool(argv[1]);
    g_gtkLibs->gtk_text_view_set_editable(widget, editable);
    return havel_new_bool(1);
}

static HavelValue* gtk_text_view_set_wrap_mode(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkTextView");
    if (!widget) return havel_new_bool(0);
    
    int wrapMode = (int)havel_get_int(argv[1]);
    g_gtkLibs->gtk_text_view_set_wrap_mode(widget, wrapMode);
    return havel_new_bool(1);
}

/* ============================================================================
 * IMAGE FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_image_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* image = g_gtkLibs->gtk_image_new();
    int64_t id = storeWidget(image);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_image_new_from_icon_name(int argc, HavelValue** argv) {
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    const char* iconName = "";
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_STRING) {
        iconName = havel_get_string(argv[0]);
    }
    
    GtkWidget* image = g_gtkLibs->gtk_image_new_from_icon_name(iconName);
    int64_t id = storeWidget(image);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_image_set_from_icon_name(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkImage");
    if (!widget) return havel_new_bool(0);
    
    const char* iconName = havel_get_string(argv[1]);
    g_gtkLibs->gtk_image_set_from_icon_name(widget, iconName);
    return havel_new_bool(1);
}

/* ============================================================================
 * PROGRESS BAR FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_progress_bar_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* progress = g_gtkLibs->gtk_progress_bar_new();
    int64_t id = storeWidget(progress);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_progress_bar_set_fraction(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkProgressBar");
    if (!widget) return havel_new_bool(0);
    
    double fraction = havel_get_float(argv[1]);
    g_gtkLibs->gtk_progress_bar_set_fraction(widget, fraction);
    return havel_new_bool(1);
}

static HavelValue* gtk_progress_bar_set_text(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkProgressBar");
    if (!widget) return havel_new_bool(0);
    
    const char* text = havel_get_string(argv[1]);
    g_gtkLibs->gtk_progress_bar_set_text(widget, text);
    return havel_new_bool(1);
}

static HavelValue* gtk_progress_bar_pulse(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_null();
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_null();
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkProgressBar");
    if (!widget) return havel_new_null();
    
    g_gtkLibs->gtk_progress_bar_pulse(widget);
    return havel_new_null();
}

/* ============================================================================
 * SPINNER FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_spinner_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* spinner = g_gtkLibs->gtk_spinner_new();
    int64_t id = storeWidget(spinner);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_spinner_start(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_null();
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_null();
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkSpinner");
    if (!widget) return havel_new_null();
    
    g_gtkLibs->gtk_spinner_start(widget);
    return havel_new_null();
}

static HavelValue* gtk_spinner_stop(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_null();
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_null();
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkSpinner");
    if (!widget) return havel_new_null();
    
    g_gtkLibs->gtk_spinner_stop(widget);
    return havel_new_null();
}

/* ============================================================================
 * COMBO BOX TEXT FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_combo_box_text_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* combo = g_gtkLibs->gtk_combo_box_text_new();
    int64_t id = storeWidget(combo);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_combo_box_text_append(int argc, HavelValue** argv) {
    if (argc < 3) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkComboBoxText");
    if (!widget) return havel_new_bool(0);
    
    const char* idStr = havel_get_string(argv[1]);
    const char* text = havel_get_string(argv[2]);
    
    g_gtkLibs->gtk_combo_box_text_append(widget, idStr, text);
    return havel_new_bool(1);
}

static HavelValue* gtk_combo_box_text_set_active_id(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkComboBoxText");
    if (!widget) return havel_new_bool(0);
    
    const char* activeId = havel_get_string(argv[1]);
    g_gtkLibs->gtk_combo_box_text_set_active_id(widget, activeId);
    return havel_new_bool(1);
}

static HavelValue* gtk_combo_box_text_get_active_id(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_string("");
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_string("");
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkComboBoxText");
    if (!widget) return havel_new_string("");
    
    const char* activeId = g_gtkLibs->gtk_combo_box_text_get_active_id(widget);
    return havel_new_string(activeId ? activeId : "");
}

/* ============================================================================
 * NOTEBOOK FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_notebook_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* notebook = g_gtkLibs->gtk_notebook_new();
    int64_t id = storeWidget(notebook);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_notebook_append_page(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_int(-1);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(-1);
    
    int64_t notebookId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t childId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    
    GtkWidget* notebook = getWidget(notebookId);
    GtkWidget* child = getWidget(childId);
    
    if (!notebook) return havel_new_int(-1);
    if (!child) return havel_new_int(-1);
    
    GtkWidget* tabLabel = (argc >= 3) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[2]))) : nullptr;
    
    int pageNum = g_gtkLibs->gtk_notebook_append_page(notebook, child, tabLabel);
    return havel_new_int(pageNum);
}

static HavelValue* gtk_notebook_set_current_page(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t notebookId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* notebook = getWidget(notebookId);
    if (!notebook) return havel_new_bool(0);
    
    int pageNum = (int)havel_get_int(argv[1]);
    g_gtkLibs->gtk_notebook_set_current_page(notebook, pageNum);
    return havel_new_bool(1);
}

/* ============================================================================
 * STACK FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_stack_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* stack = g_gtkLibs->gtk_stack_new();
    int64_t id = storeWidget(stack);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_stack_add_titled(int argc, HavelValue** argv) {
    if (argc < 4) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t stackId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t childId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    const char* name = havel_get_string(argv[2]);
    const char* title = havel_get_string(argv[3]);
    
    GtkWidget* stack = getWidget(stackId);
    GtkWidget* child = getWidget(childId);
    
    if (!stack || !child || !name || !title) return havel_new_bool(0);
    
    g_gtkLibs->gtk_stack_add_titled(stack, child, name, title);
    return havel_new_bool(1);
}

static HavelValue* gtk_stack_set_visible_child_name(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t stackId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* stack = getWidget(stackId);
    if (!stack) return havel_new_bool(0);
    
    const char* name = havel_get_string(argv[1]);
    g_gtkLibs->gtk_stack_set_visible_child_by_name(stack, name);
    return havel_new_bool(1);
}

/* ============================================================================
 * EXPANDER FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_expander_new(int argc, HavelValue** argv) {
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    const char* label = "";
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_STRING) {
        label = havel_get_string(argv[0]);
    }
    
    GtkWidget* expander = g_gtkLibs->gtk_expander_new(label);
    int64_t id = storeWidget(expander);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_expander_set_expanded(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkExpander");
    if (!widget) return havel_new_bool(0);
    
    int expanded = havel_get_bool(argv[1]);
    g_gtkLibs->gtk_expander_set_expanded(widget, expanded);
    return havel_new_bool(1);
}

/* ============================================================================
 * HEADER BAR FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_header_bar_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* headerbar = g_gtkLibs->gtk_header_bar_new();
    int64_t id = storeWidget(headerbar);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_header_bar_set_title(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkHeaderBar");
    if (!widget) return havel_new_bool(0);
    
    const char* title = havel_get_string(argv[1]);
    g_gtkLibs->gtk_header_bar_set_title(widget, title);
    return havel_new_bool(1);
}

static HavelValue* gtk_header_bar_pack_start(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t headerId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t childId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    
    GtkWidget* header = getWidgetChecked(headerId, "GtkHeaderBar");
    GtkWidget* child = getWidget(childId);
    
    if (!header || !child) return havel_new_bool(0);
    
    g_gtkLibs->gtk_header_bar_pack_start(header, child);
    return havel_new_bool(1);
}

static HavelValue* gtk_header_bar_pack_end(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t headerId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t childId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    
    GtkWidget* header = getWidgetChecked(headerId, "GtkHeaderBar");
    GtkWidget* child = getWidget(childId);
    
    if (!header || !child) return havel_new_bool(0);
    
    g_gtkLibs->gtk_header_bar_pack_end(header, child);
    return havel_new_bool(1);
}

/* ============================================================================
 * MENU BAR FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_menu_bar_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* menubar = g_gtkLibs->gtk_menu_bar_new();
    int64_t id = storeWidget(menubar);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_menu_item_new_with_label(int argc, HavelValue** argv) {
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    const char* label = "";
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_STRING) {
        label = havel_get_string(argv[0]);
    }
    
    GtkWidget* menuItem = g_gtkLibs->gtk_menu_item_new_with_label(label);
    int64_t id = storeWidget(menuItem);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_menu_bar_append(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t menubarId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t itemId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    
    GtkWidget* menubar = getWidget(menubarId);
    GtkWidget* item = getWidget(itemId);
    
    if (!menubar || !item) return havel_new_bool(0);
    
    g_gtkLibs->gtk_menu_bar_append(menubar, item);
    return havel_new_bool(1);
}

/* ============================================================================
 * POPOVER FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_popover_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* popover = g_gtkLibs->gtk_popover_new();
    int64_t id = storeWidget(popover);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_popover_popup(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_null();
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_null();
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidgetChecked(id, "GtkPopover");
    if (!widget) return havel_new_null();
    
    g_gtkLibs->gtk_popover_popup(widget);
    return havel_new_null();
}

/* ============================================================================
 * DIALOG FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_dialog_new(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWidget* dialog = g_gtkLibs->gtk_dialog_new();
    int64_t id = storeWidget(dialog);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_dialog_add_button(int argc, HavelValue** argv) {
    if (argc < 3) return havel_new_int(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    int64_t dialogId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    const char* buttonText = havel_get_string(argv[1]);
    int responseId = (int)havel_get_int(argv[2]);
    
    GtkWidget* dialog = getWidget(dialogId);
    if (!dialog) return havel_new_int(0);
    
    g_gtkLibs->gtk_dialog_add_button((GtkWidget*)dialog, buttonText, responseId);
    return havel_new_int(responseId);
}

static HavelValue* gtk_dialog_run(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_int(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    int64_t dialogId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* dialog = getWidget(dialogId);
    if (!dialog) return havel_new_int(0);
    
    int response = g_gtkLibs->gtk_dialog_run((GtkWidget*)dialog);
    return havel_new_int(response);
}

/* ============================================================================
 * MESSAGE DIALOG FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_message_dialog_new(int argc, HavelValue** argv) {
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_int(0);
    
    GtkWindow* parent = nullptr;
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_HANDLE) {
        int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
        auto winIt = g_windows.find(id);
        if (winIt != g_windows.end()) {
            parent = winIt->second;
        }
    }
    
    int flags = 0;
    int type = GTK_MESSAGE_INFO;
    int buttons = GTK_BUTTONS_OK;
    const char* message = "";
    
    if (argc >= 2 && havel_get_type(argv[1]) == HAVEL_INT) {
        type = (int)havel_get_int(argv[1]);
    }
    if (argc >= 3 && havel_get_type(argv[2]) == HAVEL_INT) {
        buttons = (int)havel_get_int(argv[2]);
    }
    if (argc >= 4 && havel_get_type(argv[3]) == HAVEL_STRING) {
        message = havel_get_string(argv[3]);
    }
    
    GtkWidget* dialog = g_gtkLibs->gtk_message_dialog_new(parent, flags, type, buttons, "%s", message);
    int64_t id = storeWidget(dialog);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupWidget);
}

static HavelValue* gtk_message_dialog_set_markup(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    const char* markup = havel_get_string(argv[1]);
    g_gtkLibs->gtk_message_dialog_set_markup(widget, markup);
    return havel_new_bool(1);
}

/* ============================================================================
 * WIDGET BASE FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_widget_destroy(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_null();
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_null();
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_null();
    
    g_gtkLibs->gtk_widget_destroy(widget);
    g_widgets.erase(id);
    return havel_new_null();
}

static HavelValue* gtk_widget_set_visible(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    int visible = havel_get_bool(argv[1]);
    g_gtkLibs->gtk_widget_set_visible(widget, visible);
    return havel_new_bool(1);
}

static HavelValue* gtk_widget_set_sensitive(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    int sensitive = havel_get_bool(argv[1]);
    g_gtkLibs->gtk_widget_set_sensitive(widget, sensitive);
    return havel_new_bool(1);
}

static HavelValue* gtk_widget_set_tooltip_text(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    const char* text = havel_get_string(argv[1]);
    g_gtkLibs->gtk_widget_set_tooltip_text(widget, text);
    return havel_new_bool(1);
}

static HavelValue* gtk_widget_add_css_class(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    const char* cssClass = havel_get_string(argv[1]);
    g_gtkLibs->gtk_widget_add_css_class(widget, cssClass);
    return havel_new_bool(1);
}

static HavelValue* gtk_widget_set_size_request(int argc, HavelValue** argv) {
    if (argc < 3) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    int width = (int)havel_get_int(argv[1]);
    int height = (int)havel_get_int(argv[2]);
    g_gtkLibs->gtk_widget_set_size_request(widget, width, height);
    return havel_new_bool(1);
}

static HavelValue* gtk_widget_grab_focus(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    if (!g_gtkLibs || !g_gtkLibs->isLoaded()) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    GtkWidget* widget = getWidget(id);
    if (!widget) return havel_new_bool(0);
    
    g_gtkLibs->gtk_widget_grab_focus(widget);
    return havel_new_bool(1);
}

/* ============================================================================
 * EVENT CONNECTION FUNCTIONS
 * ============================================================================ */

static HavelValue* gtk_onClicked(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);

    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];

    GtkWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);

    registerGtkWidgetCallback(widgetId, callback, GTK_EVENT_CLICKED);
    return havel_new_bool(1);
}

static HavelValue* gtk_onTextChanged(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);

    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];

    GtkWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);

    /* Store initial text value */
    const char* text = g_gtkLibs->gtk_entry_get_text(widget);
    if (text) g_gtkLastTextValue[widgetId] = text;

    registerGtkWidgetCallback(widgetId, callback, GTK_EVENT_TEXT_CHANGED);
    return havel_new_bool(1);
}

static HavelValue* gtk_onValueChanged(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);

    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];

    GtkWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);

    registerGtkWidgetCallback(widgetId, callback, GTK_EVENT_VALUE_CHANGED);
    return havel_new_bool(1);
}

static HavelValue* gtk_onStateChanged(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);

    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];

    GtkWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);

    registerGtkWidgetCallback(widgetId, callback, GTK_EVENT_STATE_CHANGED);
    return havel_new_bool(1);
}

static HavelValue* gtk_onSelectionChanged(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);

    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];

    GtkWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);

    registerGtkWidgetCallback(widgetId, callback, GTK_EVENT_SELECTION_CHANGED);
    return havel_new_bool(1);
}

static HavelValue* gtk_onTabChanged(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);

    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];

    GtkWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);

    registerGtkWidgetCallback(widgetId, callback, GTK_EVENT_TAB_CHANGED);
    return havel_new_bool(1);
}

static HavelValue* gtk_onRowActivated(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);

    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    HavelValue* callback = argv[1];

    GtkWidget* widget = getWidget(widgetId);
    if (!widget) return havel_new_bool(0);

    registerGtkWidgetCallback(widgetId, callback, GTK_EVENT_ROW_ACTIVATED);
    return havel_new_bool(1);
}

static HavelValue* gtk_processCallbacks(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    processGtkCallbacks();
    return havel_new_null();
}

/* ============================================================================
 * REGISTRATION
 * ============================================================================ */

} /* anonymous namespace */

extern "C" void havel_extension_init(HavelAPI* api) {
    /* Core functions */
    api->register_function("gtk", "init", gtk_init_app);
    api->register_function("gtk", "run", gtk_run_main);
    api->register_function("gtk", "quit", gtk_quit_main);
    
    /* Window functions */
    api->register_function("gtk", "windowNew", gtk_window_new);
    api->register_function("gtk", "windowShow", gtk_window_show);
    api->register_function("gtk", "windowHide", gtk_window_hide);
    api->register_function("gtk", "windowClose", gtk_window_close);
    api->register_function("gtk", "windowSetTitle", gtk_window_set_title);
    api->register_function("gtk", "windowSetSize", gtk_window_set_size);
    api->register_function("gtk", "windowGetSize", gtk_window_get_size);
    api->register_function("gtk", "windowSetPosition", gtk_window_set_position);
    api->register_function("gtk", "windowSetChild", gtk_window_set_child);
    api->register_function("gtk", "windowSetResizable", gtk_window_set_resizable);
    api->register_function("gtk", "windowSetModal", gtk_window_set_modal);
    api->register_function("gtk", "windowMaximize", gtk_window_maximize);
    api->register_function("gtk", "windowMinimize", gtk_window_minimize);
    
    /* Label functions */
    api->register_function("gtk", "labelNew", gtk_label_new);
    api->register_function("gtk", "labelSetText", gtk_label_set_text);
    api->register_function("gtk", "labelGetText", gtk_label_get_text);
    api->register_function("gtk", "labelSetMarkup", gtk_label_set_markup);
    api->register_function("gtk", "labelSetSelectable", gtk_label_set_selectable);
    
    /* Button functions */
    api->register_function("gtk", "buttonNew", gtk_button_new);
    api->register_function("gtk", "buttonNewWithLabel", gtk_button_new_with_label);
    api->register_function("gtk", "buttonSetLabel", gtk_button_set_label);
    api->register_function("gtk", "buttonSetIconName", gtk_button_set_icon_name);
    api->register_function("gtk", "buttonClicked", gtk_button_clicked);
    
    /* ToggleButton functions */
    api->register_function("gtk", "toggleButtonNew", gtk_toggle_button_new);
    api->register_function("gtk", "toggleButtonSetActive", gtk_toggle_button_set_active);
    api->register_function("gtk", "toggleButtonGetActive", gtk_toggle_button_get_active);
    
    /* CheckButton functions */
    api->register_function("gtk", "checkButtonNew", gtk_check_button_new);
    api->register_function("gtk", "checkButtonNewWithLabel", gtk_check_button_new_with_label);
    
    /* Switch functions */
    api->register_function("gtk", "switchNew", gtk_switch_new);
    api->register_function("gtk", "switchSetActive", gtk_switch_set_active);
    api->register_function("gtk", "switchGetActive", gtk_switch_get_active);
    
    /* Entry functions */
    api->register_function("gtk", "entryNew", gtk_entry_new);
    api->register_function("gtk", "entryGetText", gtk_entry_get_text);
    api->register_function("gtk", "entrySetText", gtk_entry_set_text);
    api->register_function("gtk", "entrySetPlaceholderText", gtk_entry_set_placeholder_text);
    api->register_function("gtk", "entrySetEditable", gtk_entry_set_editable);
    api->register_function("gtk", "entrySetVisibility", gtk_entry_set_visibility);
    api->register_function("gtk", "entrySetMaxLength", gtk_entry_set_max_length);
    api->register_function("gtk", "searchEntryNew", gtk_search_entry_new);
    api->register_function("gtk", "passwordEntryNew", gtk_password_entry_new);
    
    /* Box functions */
    api->register_function("gtk", "boxNew", gtk_box_new);
    api->register_function("gtk", "boxAppend", gtk_box_append);
    api->register_function("gtk", "boxPrepend", gtk_box_prepend);
    api->register_function("gtk", "boxSetSpacing", gtk_box_set_spacing);
    api->register_function("gtk", "boxSetHomogeneous", gtk_box_set_homogeneous);
    
    /* Grid functions */
    api->register_function("gtk", "gridNew", gtk_grid_new);
    api->register_function("gtk", "gridAttach", gtk_grid_attach);
    api->register_function("gtk", "gridSetRowSpacing", gtk_grid_set_row_spacing);
    api->register_function("gtk", "gridSetColumnSpacing", gtk_grid_set_column_spacing);
    
    /* Frame functions */
    api->register_function("gtk", "frameNew", gtk_frame_new);
    api->register_function("gtk", "frameSetLabel", gtk_frame_set_label);
    api->register_function("gtk", "frameSetChild", gtk_frame_set_child);
    
    /* Separator functions */
    api->register_function("gtk", "separatorNew", gtk_separator_new);
    
    /* ScrolledWindow functions */
    api->register_function("gtk", "scrolledWindowNew", gtk_scrolled_window_new);
    api->register_function("gtk", "scrolledWindowSetChild", gtk_scrolled_window_set_child);
    api->register_function("gtk", "scrolledWindowSetPolicy", gtk_scrolled_window_set_policy);
    
    /* TextView functions */
    api->register_function("gtk", "textViewNew", gtk_text_view_new);
    api->register_function("gtk", "textViewGetBuffer", gtk_text_view_get_buffer);
    api->register_function("gtk", "textBufferSetText", gtk_text_buffer_set_text);
    api->register_function("gtk", "textBufferGetText", gtk_text_buffer_get_text);
    api->register_function("gtk", "textViewSetEditable", gtk_text_view_set_editable);
    api->register_function("gtk", "textViewSetWrapMode", gtk_text_view_set_wrap_mode);
    
    /* Image functions */
    api->register_function("gtk", "imageNew", gtk_image_new);
    api->register_function("gtk", "imageNewFromIconName", gtk_image_new_from_icon_name);
    api->register_function("gtk", "imageSetFromIconName", gtk_image_set_from_icon_name);
    
    /* ProgressBar functions */
    api->register_function("gtk", "progressBarNew", gtk_progress_bar_new);
    api->register_function("gtk", "progressBarSetFraction", gtk_progress_bar_set_fraction);
    api->register_function("gtk", "progressBarSetText", gtk_progress_bar_set_text);
    api->register_function("gtk", "progressBarPulse", gtk_progress_bar_pulse);
    
    /* Spinner functions */
    api->register_function("gtk", "spinnerNew", gtk_spinner_new);
    api->register_function("gtk", "spinnerStart", gtk_spinner_start);
    api->register_function("gtk", "spinnerStop", gtk_spinner_stop);
    
    /* ComboBoxText functions */
    api->register_function("gtk", "comboBoxTextNew", gtk_combo_box_text_new);
    api->register_function("gtk", "comboBoxTextAppend", gtk_combo_box_text_append);
    api->register_function("gtk", "comboBoxTextSetActiveId", gtk_combo_box_text_set_active_id);
    api->register_function("gtk", "comboBoxTextGetActiveId", gtk_combo_box_text_get_active_id);
    
    /* Notebook functions */
    api->register_function("gtk", "notebookNew", gtk_notebook_new);
    api->register_function("gtk", "notebookAppendPage", gtk_notebook_append_page);
    api->register_function("gtk", "notebookSetCurrentPage", gtk_notebook_set_current_page);
    
    /* Stack functions */
    api->register_function("gtk", "stackNew", gtk_stack_new);
    api->register_function("gtk", "stackAddTitled", gtk_stack_add_titled);
    api->register_function("gtk", "stackSetVisibleChildName", gtk_stack_set_visible_child_name);
    
    /* Expander functions */
    api->register_function("gtk", "expanderNew", gtk_expander_new);
    api->register_function("gtk", "expanderSetExpanded", gtk_expander_set_expanded);
    
    /* HeaderBar functions */
    api->register_function("gtk", "headerBarNew", gtk_header_bar_new);
    api->register_function("gtk", "headerBarSetTitle", gtk_header_bar_set_title);
    api->register_function("gtk", "headerBarPackStart", gtk_header_bar_pack_start);
    api->register_function("gtk", "headerBarPackEnd", gtk_header_bar_pack_end);
    
    /* MenuBar functions */
    api->register_function("gtk", "menuBarNew", gtk_menu_bar_new);
    api->register_function("gtk", "menuBarAppend", gtk_menu_bar_append);
    api->register_function("gtk", "menuItemNewWithLabel", gtk_menu_item_new_with_label);
    
    /* Popover functions */
    api->register_function("gtk", "popoverNew", gtk_popover_new);
    api->register_function("gtk", "popoverPopup", gtk_popover_popup);
    
    /* Dialog functions */
    api->register_function("gtk", "dialogNew", gtk_dialog_new);
    api->register_function("gtk", "dialogAddButton", gtk_dialog_add_button);
    api->register_function("gtk", "dialogRun", gtk_dialog_run);
    
    /* MessageDialog functions */
    api->register_function("gtk", "messageDialogNew", gtk_message_dialog_new);
    api->register_function("gtk", "messageDialogSetMarkup", gtk_message_dialog_set_markup);
    
    /* Widget base functions */
    api->register_function("gtk", "widgetDestroy", gtk_widget_destroy);
    api->register_function("gtk", "widgetSetVisible", gtk_widget_set_visible);
    api->register_function("gtk", "widgetSetSensitive", gtk_widget_set_sensitive);
    api->register_function("gtk", "widgetSetTooltipText", gtk_widget_set_tooltip_text);
    api->register_function("gtk", "widgetAddCssClass", gtk_widget_add_css_class);
    api->register_function("gtk", "widgetSetSizeRequest", gtk_widget_set_size_request);
    api->register_function("gtk", "widgetGrabFocus", gtk_widget_grab_focus);

    /* Event functions */
    api->register_function("gtk", "onClicked", gtk_onClicked);
    api->register_function("gtk", "onTextChanged", gtk_onTextChanged);
    api->register_function("gtk", "onValueChanged", gtk_onValueChanged);
    api->register_function("gtk", "onStateChanged", gtk_onStateChanged);
    api->register_function("gtk", "onSelectionChanged", gtk_onSelectionChanged);
    api->register_function("gtk", "onTabChanged", gtk_onTabChanged);
    api->register_function("gtk", "onRowActivated", gtk_onRowActivated);
    api->register_function("gtk", "processCallbacks", gtk_processCallbacks);
}

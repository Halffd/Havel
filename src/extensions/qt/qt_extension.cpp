/*
 * qt_extension.cpp - Native Qt6 UI extension with dynamic loading
 *
 * Uses C ABI (HavelCAPI.h) for stability.
 * Qt6 libraries are loaded dynamically at runtime via dlopen/dlsym.
 * No hard link-time dependency on Qt6.
 */

#include "HavelCAPI.h"
#include "DynamicLoader.hpp"

#include <unordered_map>
#include <cstdio>
#include <cstring>

namespace {

/* Qt6 opaque types */
typedef struct QWidget QWidget;
typedef struct QMainWindow QMainWindow;
typedef struct QString QString;
typedef struct QObject QObject;

/* Core function types */
typedef int (*QAppInitFn)(int* argc, char** argv);
typedef int (*QAppExecFn)();
typedef void (*QAppQuitFn)();
typedef void (*QAppProcessEventsFn)();

/* QWidget function types */
typedef QWidget* (*QWidgetCtorFn)(QWidget* parent);
typedef void (*QWidgetDtorFn)(QWidget* w);
typedef void (*QWidgetShowFn)(QWidget* w);
typedef void (*QWidgetHideFn)(QWidget* w);
typedef void (*QWidgetCloseFn)(QWidget* w);
typedef void (*QWidgetSetVisibleFn)(QWidget* w, bool v);
typedef bool (*QWidgetIsVisibleFn)(QWidget* w);
typedef void (*QWidgetSetEnabledFn)(QWidget* w, bool v);
typedef bool (*QWidgetIsEnabledFn)(QWidget* w);
typedef void (*QWidgetSetWindowTitleFn)(QWidget* w, const char* t);
typedef const char* (*QWidgetWindowTitleFn)(QWidget* w);
typedef void (*QWidgetResizeFn)(QWidget* w, int wd, int ht);
typedef void (*QWidgetMoveFn)(QWidget* w, int x, int y);
typedef void (*QWidgetSetGeometryFn)(QWidget* w, int x, int y, int wd, int ht);
typedef int (*QWidgetXFn)(QWidget* w);
typedef int (*QWidgetYFn)(QWidget* w);
typedef int (*QWidgetWidthFn)(QWidget* w);
typedef int (*QWidgetHeightFn)(QWidget* w);
typedef void (*QWidgetSetMinimumSizeFn)(QWidget* w, int wd, int ht);
typedef void (*QWidgetSetMaximumSizeFn)(QWidget* w, int wd, int ht);
typedef void (*QWidgetAdjustSizeFn)(QWidget* w);
typedef void (*QWidgetUpdateFn)(QWidget* w);
typedef void (*QWidgetSetToolTipFn)(QWidget* w, const char* tip);
typedef void (*QWidgetSetStatusTipFn)(QWidget* w, const char* tip);
typedef void (*QWidgetSetFocusFn)(QWidget* w);
typedef void (*QWidgetSetCursorFn)(QWidget* w, int cursorShape);
typedef void (*QWidgetUnsetCursorFn)(QWidget* w);
typedef void (*QWidgetSetWindowFlagsFn)(QWidget* w, int flags);
typedef void (*QWidgetSetWindowModalityFn)(QWidget* w, int modality);
typedef void (*QWidgetRaiseFn)(QWidget* w);
typedef void (*QWidgetLowerFn)(QWidget* w);
typedef void (*QWidgetActivateWindowFn)(QWidget* w);
typedef void (*QWidgetSetStyleSheetFn)(QWidget* w, const char* ss);
typedef const char* (*QWidgetStyleSheetFn)(QWidget* w);
typedef void (*QWidgetSetFontFn)(QWidget* w, void* font);
typedef void (*QWidgetSetLayoutFn)(QWidget* w, void* layout);
typedef void* (*QWidgetLayoutFn)(QWidget* w);

/* QMainWindow function types */
typedef QMainWindow* (*QMainWindowCtorFn)(QWidget* parent);
typedef void (*QMainWindowSetCentralWidgetFn)(QMainWindow* w, QWidget* cw);
typedef QWidget* (*QMainWindowCentralWidgetFn)(QMainWindow* w);
typedef void (*QMainWindowSetMenuBarFn)(QMainWindow* w, void* menubar);
typedef void (*QMainWindowSetStatusBarFn)(QMainWindow* w, void* statusbar);

/* QLabel function types */
typedef QWidget* (*QLabelCtorFn)(const char* text, QWidget* parent);
typedef void (*QLabelSetTextFn)(QWidget* l, const char* t);
typedef const char* (*QLabelTextFn)(QWidget* l);
typedef void (*QLabelSetAlignmentFn)(QWidget* l, int alignment);
typedef void (*QLabelSetWordWrapFn)(QWidget* l, bool wrap);
typedef void (*QLabelSetPixmapFn)(QWidget* l, void* pixmap);
typedef void (*QLabelClearFn)(QWidget* l);

/* QPushButton function types */
typedef QWidget* (*QPushButtonCtorFn)(const char* text, QWidget* parent);
typedef void (*QPushButtonSetTextFn)(QWidget* b, const char* t);
typedef const char* (*QPushButtonTextFn)(QWidget* b);
typedef void (*QPushButtonSetIconFn)(QWidget* b, void* icon);
typedef void (*QPushButtonSetCheckableFn)(QWidget* b, bool c);
typedef bool (*QPushButtonIsCheckableFn)(QWidget* b);
typedef void (*QPushButtonSetCheckedFn)(QWidget* b, bool c);
typedef bool (*QPushButtonIsCheckedFn)(QWidget* b);
typedef void (*QPushButtonSetDefaultFn)(QWidget* b, bool d);
typedef void (*QPushButtonSetFlatFn)(QWidget* b, bool f);
typedef void (*QPushButtonClickFn)(QWidget* b);
typedef void (*QPushButtonToggleFn)(QWidget* b);

/* QCheckBox function types */
typedef QWidget* (*QCheckBoxCtorFn)(const char* text, QWidget* parent);
typedef void (*QCheckBoxSetTextFn)(QWidget* cb, const char* t);
typedef const char* (*QCheckBoxTextFn)(QWidget* cb);
typedef void (*QCheckBoxSetCheckStateFn)(QWidget* cb, int state);
typedef int (*QCheckBoxCheckStateFn)(QWidget* cb);
typedef void (*QCheckBoxSetTristateFn)(QWidget* cb, bool t);

/* QRadioButton function types */
typedef QWidget* (*QRadioButtonCtorFn)(const char* text, QWidget* parent);
typedef void (*QRadioButtonSetTextFn)(QWidget* rb, const char* t);

/* QGroupBox function types */
typedef QWidget* (*QGroupBoxCtorFn)(const char* title, QWidget* parent);
typedef void (*QGroupBoxSetTitleFn)(QWidget* gb, const char* t);
typedef void (*QGroupBoxSetCheckableFn)(QWidget* gb, bool c);

/* QLineEdit function types */
typedef QWidget* (*QLineEditCtorFn)(const char* text, QWidget* parent);
typedef void (*QLineEditSetTextFn)(QWidget* le, const char* t);
typedef const char* (*QLineEditTextFn)(QWidget* le);
typedef void (*QLineEditSetPlaceholderTextFn)(QWidget* le, const char* t);
typedef const char* (*QLineEditPlaceholderTextFn)(QWidget* le);
typedef void (*QLineEditSetMaxLengthFn)(QWidget* le, int max);
typedef void (*QLineEditSetReadOnlyFn)(QWidget* le, bool ro);
typedef bool (*QLineEditIsReadOnlyFn)(QWidget* le);
typedef void (*QLineEditSetEchoModeFn)(QWidget* le, int mode);
typedef void (*QLineEditSetAlignmentFn)(QWidget* le, int alignment);
typedef void (*QLineEditSetFrameFn)(QWidget* le, bool frame);
typedef void (*QLineEditClearFn)(QWidget* le);
typedef void (*QLineEditDeselectFn)(QWidget* le);
typedef void (*QLineEditUndoFn)(QWidget* le);
typedef void (*QLineEditRedoFn)(QWidget* le);

/* QTextEdit function types */
typedef QWidget* (*QTextEditCtorFn)(const char* text, QWidget* parent);
typedef void (*QTextEditSetPlainTextFn)(QWidget* te, const char* t);
typedef const char* (*QTextEditToPlainTextFn)(QWidget* te);
typedef void (*QTextEditSetHtmlFn)(QWidget* te, const char* html);
typedef const char* (*QTextEditToHtmlFn)(QWidget* te);
typedef void (*QTextEditAppendFn)(QWidget* te, const char* t);
typedef void (*QTextEditClearFn)(QWidget* te);
typedef void (*QTextEditSetReadOnlyFn)(QWidget* te, bool ro);
typedef bool (*QTextEditIsReadOnlyFn)(QWidget* te);
typedef void (*QTextEditSetPlaceholderTextFn)(QWidget* te, const char* t);
typedef void (*QTextEditCopyFn)(QWidget* te);
typedef void (*QTextEditCutFn)(QWidget* te);
typedef void (*QTextEditPasteFn)(QWidget* te);

/* QComboBox function types */
typedef QWidget* (*QComboBoxCtorFn)(QWidget* parent);
typedef void (*QComboBoxAddItemFn)(QWidget* cb, const char* text, void* userData);
typedef void (*QComboBoxInsertItemFn)(QWidget* cb, int idx, const char* text, void* userData);
typedef void (*QComboBoxRemoveItemFn)(QWidget* cb, int idx);
typedef void (*QComboBoxClearFn)(QWidget* cb);
typedef int (*QComboBoxCountFn)(QWidget* cb);
typedef void (*QComboBoxSetCurrentIndexFn)(QWidget* cb, int idx);
typedef int (*QComboBoxCurrentIndexFn)(QWidget* cb);
typedef const char* (*QComboBoxCurrentTextFn)(QWidget* cb);
typedef const char* (*QComboBoxItemTextFn)(QWidget* cb, int idx);
typedef void (*QComboBoxSetEditableFn)(QWidget* cb, bool editable);
typedef void (*QComboBoxSetMaxVisibleItemsFn)(QWidget* cb, int max);

/* QSpinBox function types */
typedef QWidget* (*QSpinBoxCtorFn)(QWidget* parent);
typedef void (*QSpinBoxSetRangeFn)(QWidget* sb, int min, int max);
typedef int (*QSpinBoxMinimumFn)(QWidget* sb);
typedef int (*QSpinBoxMaximumFn)(QWidget* sb);
typedef void (*QSpinBoxSetValueFn)(QWidget* sb, int v);
typedef int (*QSpinBoxValueFn)(QWidget* sb);
typedef void (*QSpinBoxSetSingleStepFn)(QWidget* sb, int step);
typedef void (*QSpinBoxSetPrefixFn)(QWidget* sb, const char* p);
typedef void (*QSpinBoxSetSuffixFn)(QWidget* sb, const char* s);
typedef void (*QSpinBoxSetWrappingFn)(QWidget* sb, bool wrap);

/* QDoubleSpinBox function types */
typedef QWidget* (*QDoubleSpinBoxCtorFn)(QWidget* parent);
typedef void (*QDoubleSpinBoxSetRangeFn)(QWidget* sb, double min, double max);
typedef void (*QDoubleSpinBoxSetValueFn)(QWidget* sb, double v);
typedef double (*QDoubleSpinBoxValueFn)(QWidget* sb);
typedef void (*QDoubleSpinBoxSetDecimalsFn)(QWidget* sb, int decimals);

/* QSlider function types */
typedef QWidget* (*QSliderCtorFn)(int orientation, QWidget* parent);
typedef void (*QSliderSetOrientationFn)(QWidget* s, int o);
typedef void (*QSliderSetMinimumFn)(QWidget* s, int min);
typedef void (*QSliderSetMaximumFn)(QWidget* s, int max);
typedef void (*QSliderSetRangeFn)(QWidget* s, int min, int max);
typedef void (*QSliderSetValueFn)(QWidget* s, int v);
typedef int (*QSliderValueFn)(QWidget* s);
typedef void (*QSliderSetSingleStepFn)(QWidget* s, int step);
typedef void (*QSliderSetTickPositionFn)(QWidget* s, int pos);
typedef void (*QSliderSetInvertedAppearanceFn)(QWidget* s, bool inv);

/* QScrollBar function types */
typedef QWidget* (*QScrollBarCtorFn)(int orientation, QWidget* parent);
typedef void (*QScrollBarSetMinimumFn)(QWidget* sb, int min);
typedef void (*QScrollBarSetMaximumFn)(QWidget* sb, int max);
typedef void (*QScrollBarSetValueFn)(QWidget* sb, int v);
typedef int (*QScrollBarValueFn)(QWidget* sb);

/* QDial function types */
typedef QWidget* (*QDialCtorFn)(QWidget* parent);
typedef void (*QDialSetMinimumFn)(QWidget* d, int min);
typedef void (*QDialSetMaximumFn)(QWidget* d, int max);
typedef void (*QDialSetValueFn)(QWidget* d, int v);
typedef int (*QDialValueFn)(QWidget* d);
typedef void (*QDialSetWrappingFn)(QWidget* d, bool wrap);
typedef void (*QDialSetNotchesVisibleFn)(QWidget* d, bool visible);

/* QProgressBar function types */
typedef QWidget* (*QProgressBarCtorFn)(QWidget* parent);
typedef void (*QProgressBarSetRangeFn)(QWidget* pb, int min, int max);
typedef void (*QProgressBarSetValueFn)(QWidget* pb, int v);
typedef int (*QProgressBarValueFn)(QWidget* pb);
typedef void (*QProgressBarSetFormatFn)(QWidget* pb, const char* fmt);
typedef void (*QProgressBarSetOrientationFn)(QWidget* pb, int o);
typedef void (*QProgressBarSetTextVisibleFn)(QWidget* pb, bool v);
typedef void (*QProgressBarResetFn)(QWidget* pb);

/* QFrame function types */
typedef QWidget* (*QFrameCtorFn)(QWidget* parent);
typedef void (*QFrameSetFrameShapeFn)(QWidget* f, int shape);
typedef void (*QFrameSetFrameShadowFn)(QWidget* f, int shadow);
typedef void (*QFrameSetLineWidthFn)(QWidget* f, int w);
typedef void (*QFrameSetFrameStyleFn)(QWidget* f, int style);

/* QLayout function types */
typedef void (*QLayoutDtorFn)(void* l);
typedef void (*QLayoutSetContentsMarginsFn)(void* l, int lft, int top, int rgt, int btm);
typedef void (*QLayoutSetSpacingFn)(void* l, int sp);
typedef int (*QLayoutSpacingFn)(void* l);
typedef void (*QLayoutAddWidgetFn)(void* l, QWidget* w);
typedef void (*QLayoutAddLayoutFn)(void* l, void* child);
typedef void (*QLayoutSetAlignmentFn)(void* l, QWidget* w, int align);
typedef void (*QLayoutRemoveWidgetFn)(void* l, QWidget* w);

/* QVBoxLayout function types */
typedef void* (*QVBoxLayoutCtorFn)(QWidget* parent);
typedef void (*QVBoxLayoutAddStretchFn)(void* l, int stretch);
typedef void (*QVBoxLayoutAddSpacingFn)(void* l, int size);

/* QHBoxLayout function types */
typedef void* (*QHBoxLayoutCtorFn)(QWidget* parent);
typedef void (*QHBoxLayoutAddStretchFn)(void* l, int stretch);
typedef void (*QHBoxLayoutAddSpacingFn)(void* l, int size);

/* QGridLayout function types */
typedef void* (*QGridLayoutCtorFn)(QWidget* parent);
typedef void (*QGridLayoutAddWidgetFn)(void* l, QWidget* w, int row, int col, int rowSpan, int colSpan);
typedef void (*QGridLayoutSetRowSpacingFn)(void* l, int row, int sp);
typedef void (*QGridLayoutSetColumnSpacingFn)(void* l, int col, int sp);
typedef void (*QGridLayoutSetRowStretchFn)(void* l, int row, int stretch);
typedef void (*QGridLayoutSetColumnStretchFn)(void* l, int col, int stretch);

/* QTabWidget function types */
typedef QWidget* (*QTabWidgetCtorFn)(QWidget* parent);
typedef int (*QTabWidgetAddTabFn)(QWidget* tw, QWidget* w, const char* label);
typedef int (*QTabWidgetInsertTabFn)(QWidget* tw, int idx, QWidget* w, const char* label);
typedef void (*QTabWidgetRemoveTabFn)(QWidget* tw, int idx);
typedef void (*QTabWidgetSetCurrentIndexFn)(QWidget* tw, int idx);
typedef int (*QTabWidgetCurrentIndexFn)(QWidget* tw);
typedef QWidget* (*QTabWidgetCurrentWidgetFn)(QWidget* tw);
typedef void (*QTabWidgetSetCurrentWidgetFn)(QWidget* tw, QWidget* w);
typedef int (*QTabWidgetCountFn)(QWidget* tw);
typedef void (*QTabWidgetSetTabTextFn)(QWidget* tw, int idx, const char* label);
typedef const char* (*QTabWidgetTabTextFn)(QWidget* tw, int idx);
typedef void (*QTabWidgetSetTabEnabledFn)(QWidget* tw, int idx, bool enable);
typedef bool (*QTabWidgetIsTabEnabledFn)(QWidget* tw, int idx);
typedef void (*QTabWidgetSetTabsClosableFn)(QWidget* tw, bool closable);
typedef void (*QTabWidgetSetMovableFn)(QWidget* tw, bool movable);
typedef void (*QTabWidgetSetTabPositionFn)(QWidget* tw, int pos);
typedef void (*QTabWidgetClearFn)(QWidget* tw);

/* QStackedWidget function types */
typedef QWidget* (*QStackedWidgetCtorFn)(QWidget* parent);
typedef int (*QStackedWidgetAddWidgetFn)(QWidget* sw, QWidget* w);
typedef int (*QStackedWidgetInsertWidgetFn)(QWidget* sw, int idx, QWidget* w);
typedef void (*QStackedWidgetSetCurrentIndexFn)(QWidget* sw, int idx);
typedef int (*QStackedWidgetCurrentIndexFn)(QWidget* sw);
typedef QWidget* (*QStackedWidgetCurrentWidgetFn)(QWidget* sw);
typedef void (*QStackedWidgetSetCurrentWidgetFn)(QWidget* sw, QWidget* w);
typedef int (*QStackedWidgetCountFn)(QWidget* sw);
typedef QWidget* (*QStackedWidgetWidgetFn)(QWidget* sw, int idx);
typedef int (*QStackedWidgetIndexOfFn)(QWidget* sw, QWidget* w);
typedef void (*QStackedWidgetRemoveWidgetFn)(QWidget* sw, QWidget* w);

/* QScrollArea function types */
typedef QWidget* (*QScrollAreaCtorFn)(QWidget* parent);
typedef void (*QScrollAreaSetWidgetFn)(QWidget* sa, QWidget* w);
typedef QWidget* (*QScrollAreaWidgetFn)(QWidget* sa);
typedef void (*QScrollAreaSetWidgetResizableFn)(QWidget* sa, bool res);
typedef void (*QScrollAreaSetHorizontalScrollBarPolicyFn)(QWidget* sa, int policy);
typedef void (*QScrollAreaSetVerticalScrollBarPolicyFn)(QWidget* sa, int policy);

/* QGroupBox function types - already defined above */

/* QSplitter function types */
typedef QWidget* (*QSplitterCtorFn)(int orientation, QWidget* parent);
typedef void (*QSplitterAddWidgetFn)(QWidget* sp, QWidget* w);
typedef void (*QSplitterSetOrientationFn)(QWidget* sp, int o);
typedef void (*QSplitterSetHandleWidthFn)(QWidget* sp, int w);

/* QMenuBar function types */
typedef void* (*QMenuBarCtorFn)(QWidget* parent);
typedef void* (*QMenuBarAddMenuFn)(QWidget* mb, const char* title);
typedef void* (*QMenuBarAddActionFn)(QWidget* mb, const char* text);

/* QMenu function types */
typedef void* (*QMenuCtorFn)(const char* title, QWidget* parent);
typedef void* (*QMenuAddActionFn)(void* m, const char* text);
typedef void* (*QMenuAddSeparatorFn)(void* m);
typedef void (*QMenuClearFn)(void* m);

/* QStatusBar function types */
typedef QWidget* (*QStatusBarCtorFn)(QWidget* parent);
typedef void (*QStatusBarShowMessageFn)(QWidget* sb, const char* msg, int timeout);
typedef void (*QStatusBarClearMessageFn)(QWidget* sb);
typedef void (*QStatusBarAddWidgetFn)(QWidget* sb, QWidget* w, int stretch);
typedef void (*QStatusBarAddPermanentWidgetFn)(QWidget* sb, QWidget* w, int stretch);

/* QMessageBox static functions */
typedef void (*QMessageBoxAboutFn)(QWidget* parent, const char* title, const char* text);
typedef void (*QMessageBoxAboutQtFn)(QWidget* parent, const char* title);
typedef int (*QMessageBoxQuestionFn)(QWidget* parent, const char* title, const char* text, int buttons, int defaultBtn);
typedef int (*QMessageBoxInformationFn)(QWidget* parent, const char* title, const char* text, int buttons, int defaultBtn);
typedef int (*QMessageBoxWarningFn)(QWidget* parent, const char* title, const char* text, int buttons, int defaultBtn);
typedef int (*QMessageBoxCriticalFn)(QWidget* parent, const char* title, const char* text, int buttons, int defaultBtn);

/* QInputDialog static functions */
typedef const char* (*QInputDialogGetTextFn)(QWidget* parent, const char* title, const char* label, int echo, const char* text, bool* ok, int flags);
typedef int (*QInputDialogGetIntegerFn)(QWidget* parent, const char* title, const char* label, bool* ok, int val, int min, int max, int step, int flags);

/* QFileDialog static functions */
typedef const char* (*QFileDialogGetOpenFileNameFn)(QWidget* parent, const char* caption, const char* dir, const char* filter, char** selFilter, int options);
typedef const char* (*QFileDialogGetSaveFileNameFn)(QWidget* parent, const char* caption, const char* dir, const char* filter, char** selFilter, int options);
typedef const char* (*QFileDialogGetExistingDirectoryFn)(QWidget* parent, const char* caption, const char* dir, int options);

/* QColorDialog static functions */
typedef const char* (*QColorDialogGetColorFn)(bool* ok, const char* initial, QWidget* parent, const char* title, int options);

/* QFontDialog static functions */
typedef const char* (*QFontDialogGetFontFn)(bool* ok, const char* initial, QWidget* parent, const char* title, int options);

/* QClipboard function types */
typedef void* (*QAppClipboardFn)();
typedef const char* (*QClipboardTextFn)(void* cb, int mode);
typedef void (*QClipboardSetTextFn)(void* cb, const char* text, int mode);
typedef void (*QClipboardClearFn)(void* cb);

/* QTimer function types */
typedef void* (*QTimerCtorFn)(QObject* parent);
typedef void (*QTimerStartFn)(void* t, int msec);
typedef void (*QTimerStopFn)(void* t);
typedef bool (*QTimerIsActiveFn)(void* t);
typedef void (*QTimerSetIntervalFn)(void* t, int msec);
typedef void (*QTimerSetSingleShotFn)(void* t, bool ss);

/* QSettings function types */
typedef void* (*QSettingsCtorFn)(const char* fileName, int format, QObject* parent);
typedef void (*QSettingsDtorFn)(void* s);
typedef void (*QSettingsSetValueFn)(void* s, const char* key, const char* val);
typedef const char* (*QSettingsValueFn)(void* s, const char* key, const char* defVal);
typedef bool (*QSettingsContainsFn)(void* s, const char* key);
typedef void (*QSettingsRemoveFn)(void* s, const char* key);
typedef void (*QSettingsSyncFn)(void* s);

/* QCoreApplication functions */
typedef const char* (*QAppApplicationNameFn)();
typedef void (*QAppSetApplicationNameFn)(const char* name);
typedef const char* (*QAppOrganizationNameFn)();
typedef void (*QAppSetOrganizationNameFn)(const char* name);
typedef void (*QAppExitFn)(int code);
typedef void (*QAppFlushFn)();

/* Qt6 constants */
enum {
    Qt_AlignLeft = 0x0001, Qt_AlignRight = 0x0002, Qt_AlignHCenter = 0x0004,
    Qt_AlignTop = 0x0020, Qt_AlignBottom = 0x0040, Qt_AlignVCenter = 0x0080,
    Qt_AlignCenter = 0x0084,
    Qt_OrientationHorizontal = 0, Qt_OrientationVertical = 1,
    Qt_NoFrame = 0, Qt_Box = 1, Qt_Panel = 2, Qt_StyledPanel = 6,
    Qt_Plain = 0, Qt_Shadow = 1, Qt_Raised = 2, Qt_Sunken = 3,
    Qt_NoFocus = 0, Qt_TabFocus = 0x1, Qt_ClickFocus = 0x2, Qt_StrongFocus = 0x3,
    Qt_ArrowCursor = 0, Qt_IBeamCursor = 4, Qt_WaitCursor = 3, Qt_CrossCursor = 2,
    Qt_Window = 0x00000001, Qt_Dialog = 0x00000002, Qt_Popup = 0x00000010,
    Qt_Tool = 0x00000020, Qt_FramelessWindowHint = 0x08000000,
    Qt_WindowStaysOnTopHint = 0x00000800,
    Qt_NonModal = 0, Qt_WindowModal = 1, Qt_ApplicationModal = 2,
    Qt_NoButton = 0x0, Qt_Ok = 0x00000400, Qt_Cancel = 0x00400000,
    Qt_Yes = 0x00002000, Qt_No = 0x00004000,
    Qt_NoIcon = 0, Qt_Info = 1, Qt_Warning = 2, Qt_Critical = 3, Qt_Question = 4,
    Qt_NormalEcho = 0, Qt_NoEcho = 1, Qt_PasswordEcho = 2,
    Qt_ScrollBarAsNeeded = 0, Qt_ScrollBarAlwaysOff = 1, Qt_ScrollBarAlwaysOn = 2,
    Qt_NoTabBar = 0, Qt_TabAtTop = 1, Qt_TabAtBottom = 2, Qt_TabAtLeft = 3, Qt_TabAtRight = 4,
    Qt_IniFormat = 1, Qt_NativeFormat = 2,
};

/* Dynamic loader for Qt6 */
struct Qt6Libs {
    DynamicLoader qtCore;
    DynamicLoader qtGui;
    DynamicLoader qtWidgets;
    
    /* Core functions */
    QAppInitFn qApp_init = nullptr;
    QAppExecFn qApp_exec = nullptr;
    QAppQuitFn qApp_quit = nullptr;
    QAppProcessEventsFn qApp_processEvents = nullptr;
    QAppApplicationNameFn qApp_applicationName = nullptr;
    QAppSetApplicationNameFn qApp_setApplicationName = nullptr;
    QAppOrganizationNameFn qApp_organizationName = nullptr;
    QAppSetOrganizationNameFn qApp_setOrganizationName = nullptr;
    QAppExitFn qApp_exit = nullptr;
    QAppFlushFn qApp_flush = nullptr;
    QAppClipboardFn qApp_clipboard = nullptr;
    
    /* QWidget functions */
    QWidgetCtorFn qWidget_new = nullptr;
    QWidgetDtorFn qWidget_delete = nullptr;
    QWidgetShowFn qWidget_show = nullptr;
    QWidgetHideFn qWidget_hide = nullptr;
    QWidgetCloseFn qWidget_close = nullptr;
    QWidgetSetVisibleFn qWidget_setVisible = nullptr;
    QWidgetIsVisibleFn qWidget_isVisible = nullptr;
    QWidgetSetEnabledFn qWidget_setEnabled = nullptr;
    QWidgetIsEnabledFn qWidget_isEnabled = nullptr;
    QWidgetSetWindowTitleFn qWidget_setWindowTitle = nullptr;
    QWidgetWindowTitleFn qWidget_windowTitle = nullptr;
    QWidgetResizeFn qWidget_resize = nullptr;
    QWidgetMoveFn qWidget_move = nullptr;
    QWidgetSetGeometryFn qWidget_setGeometry = nullptr;
    QWidgetXFn qWidget_x = nullptr;
    QWidgetYFn qWidget_y = nullptr;
    QWidgetWidthFn qWidget_width = nullptr;
    QWidgetHeightFn qWidget_height = nullptr;
    QWidgetSetMinimumSizeFn qWidget_setMinimumSize = nullptr;
    QWidgetSetMaximumSizeFn qWidget_setMaximumSize = nullptr;
    QWidgetAdjustSizeFn qWidget_adjustSize = nullptr;
    QWidgetUpdateFn qWidget_update = nullptr;
    QWidgetSetToolTipFn qWidget_setToolTip = nullptr;
    QWidgetSetStatusTipFn qWidget_setStatusTip = nullptr;
    QWidgetSetFocusFn qWidget_setFocus = nullptr;
    QWidgetSetCursorFn qWidget_setCursor = nullptr;
    QWidgetUnsetCursorFn qWidget_unsetCursor = nullptr;
    QWidgetSetWindowFlagsFn qWidget_setWindowFlags = nullptr;
    QWidgetSetWindowModalityFn qWidget_setWindowModality = nullptr;
    QWidgetRaiseFn qWidget_raise = nullptr;
    QWidgetLowerFn qWidget_lower = nullptr;
    QWidgetActivateWindowFn qWidget_activateWindow = nullptr;
    QWidgetSetStyleSheetFn qWidget_setStyleSheet = nullptr;
    QWidgetStyleSheetFn qWidget_styleSheet = nullptr;
    QWidgetSetFontFn qWidget_setFont = nullptr;
    QWidgetSetLayoutFn qWidget_setLayout = nullptr;
    QWidgetLayoutFn qWidget_layout = nullptr;
    
    /* QMainWindow functions */
    QMainWindowCtorFn qMainWindow_new = nullptr;
    QMainWindowSetCentralWidgetFn qMainWindow_setCentralWidget = nullptr;
    QMainWindowCentralWidgetFn qMainWindow_centralWidget = nullptr;
    QMainWindowSetMenuBarFn qMainWindow_setMenuBar = nullptr;
    QMainWindowSetStatusBarFn qMainWindow_setStatusBar = nullptr;
    
    /* QLabel functions */
    QLabelCtorFn qLabel_new = nullptr;
    QLabelSetTextFn qLabel_setText = nullptr;
    QLabelTextFn qLabel_text = nullptr;
    QLabelSetAlignmentFn qLabel_setAlignment = nullptr;
    QLabelSetWordWrapFn qLabel_setWordWrap = nullptr;
    QLabelSetPixmapFn qLabel_setPixmap = nullptr;
    QLabelClearFn qLabel_clear = nullptr;
    
    /* QPushButton functions */
    QPushButtonCtorFn qPushButton_new = nullptr;
    QPushButtonSetTextFn qPushButton_setText = nullptr;
    QPushButtonTextFn qPushButton_text = nullptr;
    QPushButtonSetIconFn qPushButton_setIcon = nullptr;
    QPushButtonSetCheckableFn qPushButton_setCheckable = nullptr;
    QPushButtonIsCheckableFn qPushButton_isCheckable = nullptr;
    QPushButtonSetCheckedFn qPushButton_setChecked = nullptr;
    QPushButtonIsCheckedFn qPushButton_isChecked = nullptr;
    QPushButtonSetDefaultFn qPushButton_setDefault = nullptr;
    QPushButtonSetFlatFn qPushButton_setFlat = nullptr;
    QPushButtonClickFn qPushButton_click = nullptr;
    QPushButtonToggleFn qPushButton_toggle = nullptr;
    
    /* QCheckBox functions */
    QCheckBoxCtorFn qCheckBox_new = nullptr;
    QCheckBoxSetTextFn qCheckBox_setText = nullptr;
    QCheckBoxTextFn qCheckBox_text = nullptr;
    QCheckBoxSetCheckStateFn qCheckBox_setCheckState = nullptr;
    QCheckBoxCheckStateFn qCheckBox_checkState = nullptr;
    QCheckBoxSetTristateFn qCheckBox_setTristate = nullptr;
    
    /* QRadioButton functions */
    QRadioButtonCtorFn qRadioButton_new = nullptr;
    QRadioButtonSetTextFn qRadioButton_setText = nullptr;
    
    /* QGroupBox functions */
    QGroupBoxCtorFn qGroupBox_new = nullptr;
    QGroupBoxSetTitleFn qGroupBox_setTitle = nullptr;
    QGroupBoxSetCheckableFn qGroupBox_setCheckable = nullptr;
    
    /* QLineEdit functions */
    QLineEditCtorFn qLineEdit_new = nullptr;
    QLineEditSetTextFn qLineEdit_setText = nullptr;
    QLineEditTextFn qLineEdit_text = nullptr;
    QLineEditSetPlaceholderTextFn qLineEdit_setPlaceholderText = nullptr;
    QLineEditPlaceholderTextFn qLineEdit_placeholderText = nullptr;
    QLineEditSetMaxLengthFn qLineEdit_setMaxLength = nullptr;
    QLineEditSetReadOnlyFn qLineEdit_setReadOnly = nullptr;
    QLineEditIsReadOnlyFn qLineEdit_isReadOnly = nullptr;
    QLineEditSetEchoModeFn qLineEdit_setEchoMode = nullptr;
    QLineEditSetAlignmentFn qLineEdit_setAlignment = nullptr;
    QLineEditSetFrameFn qLineEdit_setFrame = nullptr;
    QLineEditClearFn qLineEdit_clear = nullptr;
    QLineEditDeselectFn qLineEdit_deselect = nullptr;
    QLineEditUndoFn qLineEdit_undo = nullptr;
    QLineEditRedoFn qLineEdit_redo = nullptr;
    
    /* QTextEdit functions */
    QTextEditCtorFn qTextEdit_new = nullptr;
    QTextEditSetPlainTextFn qTextEdit_setPlainText = nullptr;
    QTextEditToPlainTextFn qTextEdit_toPlainText = nullptr;
    QTextEditSetHtmlFn qTextEdit_setHtml = nullptr;
    QTextEditToHtmlFn qTextEdit_toHtml = nullptr;
    QTextEditAppendFn qTextEdit_append = nullptr;
    QTextEditClearFn qTextEdit_clear = nullptr;
    QTextEditSetReadOnlyFn qTextEdit_setReadOnly = nullptr;
    QTextEditIsReadOnlyFn qTextEdit_isReadOnly = nullptr;
    QTextEditSetPlaceholderTextFn qTextEdit_setPlaceholderText = nullptr;
    QTextEditCopyFn qTextEdit_copy = nullptr;
    QTextEditCutFn qTextEdit_cut = nullptr;
    QTextEditPasteFn qTextEdit_paste = nullptr;
    
    /* QComboBox functions */
    QComboBoxCtorFn qComboBox_new = nullptr;
    QComboBoxAddItemFn qComboBox_addItem = nullptr;
    QComboBoxInsertItemFn qComboBox_insertItem = nullptr;
    QComboBoxRemoveItemFn qComboBox_removeItem = nullptr;
    QComboBoxClearFn qComboBox_clear = nullptr;
    QComboBoxCountFn qComboBox_count = nullptr;
    QComboBoxSetCurrentIndexFn qComboBox_setCurrentIndex = nullptr;
    QComboBoxCurrentIndexFn qComboBox_currentIndex = nullptr;
    QComboBoxCurrentTextFn qComboBox_currentText = nullptr;
    QComboBoxItemTextFn qComboBox_itemText = nullptr;
    QComboBoxSetEditableFn qComboBox_setEditable = nullptr;
    QComboBoxSetMaxVisibleItemsFn qComboBox_setMaxVisibleItems = nullptr;
    
    /* QSpinBox functions */
    QSpinBoxCtorFn qSpinBox_new = nullptr;
    QSpinBoxSetRangeFn qSpinBox_setRange = nullptr;
    QSpinBoxMinimumFn qSpinBox_minimum = nullptr;
    QSpinBoxMaximumFn qSpinBox_maximum = nullptr;
    QSpinBoxSetValueFn qSpinBox_setValue = nullptr;
    QSpinBoxValueFn qSpinBox_value = nullptr;
    QSpinBoxSetSingleStepFn qSpinBox_setSingleStep = nullptr;
    QSpinBoxSetPrefixFn qSpinBox_setPrefix = nullptr;
    QSpinBoxSetSuffixFn qSpinBox_setSuffix = nullptr;
    QSpinBoxSetWrappingFn qSpinBox_setWrapping = nullptr;
    
    /* QDoubleSpinBox functions */
    QDoubleSpinBoxCtorFn qDoubleSpinBox_new = nullptr;
    QDoubleSpinBoxSetRangeFn qDoubleSpinBox_setRange = nullptr;
    QDoubleSpinBoxSetValueFn qDoubleSpinBox_setValue = nullptr;
    QDoubleSpinBoxValueFn qDoubleSpinBox_value = nullptr;
    QDoubleSpinBoxSetDecimalsFn qDoubleSpinBox_setDecimals = nullptr;
    
    /* QSlider functions */
    QSliderCtorFn qSlider_new = nullptr;
    QSliderSetOrientationFn qSlider_setOrientation = nullptr;
    QSliderSetMinimumFn qSlider_setMinimum = nullptr;
    QSliderSetMaximumFn qSlider_setMaximum = nullptr;
    QSliderSetRangeFn qSlider_setRange = nullptr;
    QSliderSetValueFn qSlider_setValue = nullptr;
    QSliderValueFn qSlider_value = nullptr;
    QSliderSetSingleStepFn qSlider_setSingleStep = nullptr;
    QSliderSetTickPositionFn qSlider_setTickPosition = nullptr;
    QSliderSetInvertedAppearanceFn qSlider_setInvertedAppearance = nullptr;
    
    /* QScrollBar functions */
    QScrollBarCtorFn qScrollBar_new = nullptr;
    QScrollBarSetMinimumFn qScrollBar_setMinimum = nullptr;
    QScrollBarSetMaximumFn qScrollBar_setMaximum = nullptr;
    QScrollBarSetValueFn qScrollBar_setValue = nullptr;
    QScrollBarValueFn qScrollBar_value = nullptr;
    
    /* QDial functions */
    QDialCtorFn qDial_new = nullptr;
    QDialSetMinimumFn qDial_setMinimum = nullptr;
    QDialSetMaximumFn qDial_setMaximum = nullptr;
    QDialSetValueFn qDial_setValue = nullptr;
    QDialValueFn qDial_value = nullptr;
    QDialSetWrappingFn qDial_setWrapping = nullptr;
    QDialSetNotchesVisibleFn qDial_setNotchesVisible = nullptr;
    
    /* QProgressBar functions */
    QProgressBarCtorFn qProgressBar_new = nullptr;
    QProgressBarSetRangeFn qProgressBar_setRange = nullptr;
    QProgressBarSetValueFn qProgressBar_setValue = nullptr;
    QProgressBarValueFn qProgressBar_value = nullptr;
    QProgressBarSetFormatFn qProgressBar_setFormat = nullptr;
    QProgressBarSetOrientationFn qProgressBar_setOrientation = nullptr;
    QProgressBarSetTextVisibleFn qProgressBar_setTextVisible = nullptr;
    QProgressBarResetFn qProgressBar_reset = nullptr;
    
    /* QFrame functions */
    QFrameCtorFn qFrame_new = nullptr;
    QFrameSetFrameShapeFn qFrame_setFrameShape = nullptr;
    QFrameSetFrameShadowFn qFrame_setFrameShadow = nullptr;
    QFrameSetLineWidthFn qFrame_setLineWidth = nullptr;
    QFrameSetFrameStyleFn qFrame_setFrameStyle = nullptr;
    
    /* Layout functions */
    QLayoutDtorFn qLayout_delete = nullptr;
    QLayoutSetContentsMarginsFn qLayout_setContentsMargins = nullptr;
    QLayoutSetSpacingFn qLayout_setSpacing = nullptr;
    QLayoutSpacingFn qLayout_spacing = nullptr;
    QLayoutAddWidgetFn qLayout_addWidget = nullptr;
    QLayoutAddLayoutFn qLayout_addLayout = nullptr;
    QLayoutSetAlignmentFn qLayout_setAlignment = nullptr;
    QLayoutRemoveWidgetFn qLayout_removeWidget = nullptr;
    
    QVBoxLayoutCtorFn qVBoxLayout_new = nullptr;
    QVBoxLayoutAddStretchFn qVBoxLayout_addStretch = nullptr;
    QVBoxLayoutAddSpacingFn qVBoxLayout_addSpacing = nullptr;
    
    QHBoxLayoutCtorFn qHBoxLayout_new = nullptr;
    QHBoxLayoutAddStretchFn qHBoxLayout_addStretch = nullptr;
    QHBoxLayoutAddSpacingFn qHBoxLayout_addSpacing = nullptr;
    
    QGridLayoutCtorFn qGridLayout_new = nullptr;
    QGridLayoutAddWidgetFn qGridLayout_addWidget = nullptr;
    QGridLayoutAddLayoutFn qGridLayout_addLayout = nullptr;
    QGridLayoutSetRowSpacingFn qGridLayout_setRowSpacing = nullptr;
    QGridLayoutSetColumnSpacingFn qGridLayout_setColumnSpacing = nullptr;
    QGridLayoutSetRowStretchFn qGridLayout_setRowStretch = nullptr;
    QGridLayoutSetColumnStretchFn qGridLayout_setColumnStretch = nullptr;
    
    /* QTabWidget functions */
    QTabWidgetCtorFn qTabWidget_new = nullptr;
    QTabWidgetAddTabFn qTabWidget_addTab = nullptr;
    QTabWidgetInsertTabFn qTabWidget_insertTab = nullptr;
    QTabWidgetRemoveTabFn qTabWidget_removeTab = nullptr;
    QTabWidgetSetCurrentIndexFn qTabWidget_setCurrentIndex = nullptr;
    QTabWidgetCurrentIndexFn qTabWidget_currentIndex = nullptr;
    QTabWidgetCurrentWidgetFn qTabWidget_currentWidget = nullptr;
    QTabWidgetSetCurrentWidgetFn qTabWidget_setCurrentWidget = nullptr;
    QTabWidgetCountFn qTabWidget_count = nullptr;
    QTabWidgetSetTabTextFn qTabWidget_setTabText = nullptr;
    QTabWidgetTabTextFn qTabWidget_tabText = nullptr;
    QTabWidgetSetTabEnabledFn qTabWidget_setTabEnabled = nullptr;
    QTabWidgetIsTabEnabledFn qTabWidget_isTabEnabled = nullptr;
    QTabWidgetSetTabsClosableFn qTabWidget_setTabsClosable = nullptr;
    QTabWidgetSetMovableFn qTabWidget_setMovable = nullptr;
    QTabWidgetSetTabPositionFn qTabWidget_setTabPosition = nullptr;
    QTabWidgetClearFn qTabWidget_clear = nullptr;
    
    /* QStackedWidget functions */
    QStackedWidgetCtorFn qStackedWidget_new = nullptr;
    QStackedWidgetAddWidgetFn qStackedWidget_addWidget = nullptr;
    QStackedWidgetInsertWidgetFn qStackedWidget_insertWidget = nullptr;
    QStackedWidgetSetCurrentIndexFn qStackedWidget_setCurrentIndex = nullptr;
    QStackedWidgetCurrentIndexFn qStackedWidget_currentIndex = nullptr;
    QStackedWidgetCurrentWidgetFn qStackedWidget_currentWidget = nullptr;
    QStackedWidgetSetCurrentWidgetFn qStackedWidget_setCurrentWidget = nullptr;
    QStackedWidgetCountFn qStackedWidget_count = nullptr;
    QStackedWidgetWidgetFn qStackedWidget_widget = nullptr;
    QStackedWidgetIndexOfFn qStackedWidget_indexOf = nullptr;
    QStackedWidgetRemoveWidgetFn qStackedWidget_removeWidget = nullptr;
    
    /* QScrollArea functions */
    QScrollAreaCtorFn qScrollArea_new = nullptr;
    QScrollAreaSetWidgetFn qScrollArea_setWidget = nullptr;
    QScrollAreaWidgetFn qScrollArea_widget = nullptr;
    QScrollAreaSetWidgetResizableFn qScrollArea_setWidgetResizable = nullptr;
    QScrollAreaSetHorizontalScrollBarPolicyFn qScrollArea_setHScrollBarPolicy = nullptr;
    QScrollAreaSetVerticalScrollBarPolicyFn qScrollArea_setVScrollBarPolicy = nullptr;
    
    /* QSplitter functions */
    QSplitterCtorFn qSplitter_new = nullptr;
    QSplitterAddWidgetFn qSplitter_addWidget = nullptr;
    QSplitterSetOrientationFn qSplitter_setOrientation = nullptr;
    QSplitterSetHandleWidthFn qSplitter_setHandleWidth = nullptr;
    
    /* QMenuBar functions */
    QMenuBarCtorFn qMenuBar_new = nullptr;
    QMenuBarAddMenuFn qMenuBar_addMenu = nullptr;
    QMenuBarAddActionFn qMenuBar_addAction = nullptr;
    
    /* QMenu functions */
    QMenuCtorFn qMenu_new = nullptr;
    QMenuAddActionFn qMenu_addAction = nullptr;
    QMenuAddSeparatorFn qMenu_addSeparator = nullptr;
    QMenuClearFn qMenu_clear = nullptr;
    
    /* QStatusBar functions */
    QStatusBarCtorFn qStatusBar_new = nullptr;
    QStatusBarShowMessageFn qStatusBar_showMessage = nullptr;
    QStatusBarClearMessageFn qStatusBar_clearMessage = nullptr;
    QStatusBarAddWidgetFn qStatusBar_addWidget = nullptr;
    QStatusBarAddPermanentWidgetFn qStatusBar_addPermanentWidget = nullptr;
    
    /* QMessageBox functions */
    QMessageBoxAboutFn qMessageBox_about = nullptr;
    QMessageBoxAboutQtFn qMessageBox_aboutQt = nullptr;
    QMessageBoxQuestionFn qMessageBox_question = nullptr;
    QMessageBoxInformationFn qMessageBox_information = nullptr;
    QMessageBoxWarningFn qMessageBox_warning = nullptr;
    QMessageBoxCriticalFn qMessageBox_critical = nullptr;
    
    /* QInputDialog functions */
    QInputDialogGetTextFn qInputDialog_getText = nullptr;
    QInputDialogGetIntegerFn qInputDialog_getInteger = nullptr;
    
    /* QFileDialog functions */
    QFileDialogGetOpenFileNameFn qFileDialog_getOpenFileName = nullptr;
    QFileDialogGetSaveFileNameFn qFileDialog_getSaveFileName = nullptr;
    QFileDialogGetExistingDirectoryFn qFileDialog_getExistingDirectory = nullptr;
    
    /* QColorDialog functions */
    QColorDialogGetColorFn qColorDialog_getColor = nullptr;
    
    /* QFontDialog functions */
    QFontDialogGetFontFn qFontDialog_getFont = nullptr;
    
    /* QClipboard functions */
    QClipboardTextFn qClipboard_text = nullptr;
    QClipboardSetTextFn qClipboard_setText = nullptr;
    QClipboardClearFn qClipboard_clear = nullptr;
    
    /* QTimer functions */
    QTimerCtorFn qTimer_new = nullptr;
    QTimerStartFn qTimer_start = nullptr;
    QTimerStopFn qTimer_stop = nullptr;
    QTimerIsActiveFn qTimer_isActive = nullptr;
    QTimerSetIntervalFn qTimer_setInterval = nullptr;
    QTimerSetSingleShotFn qTimer_setSingleShot = nullptr;
    
    /* QSettings functions */
    QSettingsCtorFn qSettings_new = nullptr;
    QSettingsDtorFn qSettings_delete = nullptr;
    QSettingsSetValueFn qSettings_setValue = nullptr;
    QSettingsValueFn qSettings_value = nullptr;
    QSettingsContainsFn qSettings_contains = nullptr;
    QSettingsRemoveFn qSettings_remove = nullptr;
    QSettingsSyncFn qSettings_sync = nullptr;
    
    bool load() {
        /* Load Qt6Core */
        if (!qtCore.load("libQt6Core.so.6")) {
            if (!qtCore.load("libQt5Core.so.5")) {
                fprintf(stderr, "[Qt] Failed to load Qt Core\n");
                return false;
            }
        }
        
        /* Load Qt6Gui */
        if (!qtGui.load("libQt6Gui.so.6")) {
            if (!qtGui.load("libQt5Gui.so.5")) {
                fprintf(stderr, "[Qt] Failed to load Qt Gui\n");
                return false;
            }
        }
        
        /* Load Qt6Widgets */
        if (!qtWidgets.load("libQt6Widgets.so.6")) {
            if (!qtWidgets.load("libQt5Widgets.so.5")) {
                fprintf(stderr, "[Qt] Failed to load Qt Widgets\n");
                return false;
            }
        }
        
        fprintf(stderr, "[Qt] Qt libraries loaded dynamically\n");
        
        /* Load Core symbols */
#define LOAD_CORE(name) name = qtCore.getSymbol<Q##name##Fn>("_" QT_STRINGIFY(name)); if (!name) name = qtCore.getSymbol<Q##name##Fn>(#name);
#define LOAD(name) name = qtWidgets.getSymbol<Q##name##Fn>("_" QT_STRINGIFY(name)); if (!name) name = qtWidgets.getSymbol<Q##name##Fn>(#name);
        
        qApp_init = qtCore.getSymbol<QAppInitFn>("_ZN12QApplicationC1EPiPPci");
        qApp_exec = qtCore.getSymbol<QAppExecFn>("_ZN12QApplication4execEv");
        qApp_quit = qtCore.getSymbol<QAppQuitFn>("_ZN12QApplication4quitEv");
        qApp_processEvents = qtCore.getSymbol<QAppProcessEventsFn>("_ZN16QCoreApplication16processEventsEv");
        qApp_applicationName = qtCore.getSymbol<QAppApplicationNameFn>("_ZN16QCoreApplication14applicationNameEv");
        qApp_setApplicationName = qtCore.getSymbol<QAppSetApplicationNameFn>("_ZN16QCoreApplication19setApplicationNameERK7QString");
        qApp_organizationName = qtCore.getSymbol<QAppOrganizationNameFn>("_ZN16QCoreApplication17organizationNameEv");
        qApp_setOrganizationName = qtCore.getSymbol<QAppSetOrganizationNameFn>("_ZN16QCoreApplication22setOrganizationNameERK7QString");
        qApp_exit = qtCore.getSymbol<QAppExitFn>("_ZN16QCoreApplication4exitEi");
        qApp_flush = qtCore.getSymbol<QAppFlushFn>("_ZN16QCoreApplication5flushEv");
        qApp_clipboard = qtGui.getSymbol<QAppClipboardFn>("_ZN15QGuiApplication9clipboardEv");
        
        /* QWidget */
        LOAD_CORE(QWidget);
        LOAD_CORE(show);
        LOAD_CORE(hide);
        LOAD_CORE(close);
        LOAD_CORE(setVisible);
        LOAD_CORE(isVisible);
        LOAD_CORE(setEnabled);
        LOAD_CORE(isEnabled);
        LOAD_CORE(setWindowTitle);
        LOAD_CORE(windowTitle);
        LOAD_CORE(resize);
        LOAD_CORE(move);
        LOAD_CORE(setGeometry);
        LOAD_CORE(x);
        LOAD_CORE(y);
        LOAD_CORE(width);
        LOAD_CORE(height);
        LOAD_CORE(setMinimumSize);
        LOAD_CORE(setMaximumSize);
        LOAD_CORE(adjustSize);
        LOAD_CORE(update);
        LOAD_CORE(setToolTip);
        LOAD_CORE(setStatusTip);
        LOAD_CORE(setFocus);
        LOAD_CORE(setCursor);
        LOAD_CORE(unsetCursor);
        LOAD_CORE(setWindowFlags);
        LOAD_CORE(setWindowModality);
        LOAD_CORE(raise);
        LOAD_CORE(lower);
        LOAD_CORE(activateWindow);
        LOAD_CORE(setStyleSheet);
        LOAD_CORE(styleSheet);
        LOAD_CORE(setFont);
        LOAD_CORE(setLayout);
        LOAD_CORE(layout);
        
        /* QMainWindow */
        LOAD(QMainWindow);
        LOAD(setCentralWidget);
        LOAD(centralWidget);
        LOAD(setMenuBar);
        LOAD(setStatusBar);
        
        /* QLabel */
        LOAD(QLabel);
        LOAD(setText);
        LOAD(text);
        LOAD(setAlignment);
        LOAD(setWordWrap);
        LOAD(setPixmap);
        LOAD(clear);
        
        /* QPushButton */
        LOAD(QPushButton);
        LOAD(setText);
        LOAD(text);
        LOAD(setIcon);
        LOAD(setCheckable);
        LOAD(isCheckable);
        LOAD(setChecked);
        LOAD(isChecked);
        LOAD(setDefault);
        LOAD(setFlat);
        LOAD(click);
        LOAD(toggle);
        
        /* QCheckBox */
        LOAD(QCheckBox);
        LOAD(setText);
        LOAD(text);
        LOAD(setCheckState);
        LOAD(checkState);
        LOAD(setTristate);
        
        /* QRadioButton */
        LOAD(QRadioButton);
        LOAD(setText);
        
        /* QGroupBox */
        LOAD(QGroupBox);
        LOAD(setTitle);
        LOAD(setCheckable);
        
        /* QLineEdit */
        LOAD(QLineEdit);
        LOAD(setText);
        LOAD(text);
        LOAD(setPlaceholderText);
        LOAD(placeholderText);
        LOAD(setMaxLength);
        LOAD(setReadOnly);
        LOAD(isReadOnly);
        LOAD(setEchoMode);
        LOAD(setAlignment);
        LOAD(setFrame);
        LOAD(clear);
        LOAD(deselect);
        LOAD(undo);
        LOAD(redo);
        
        /* QTextEdit */
        LOAD(QTextEdit);
        LOAD(setPlainText);
        LOAD(toPlainText);
        LOAD(setHtml);
        LOAD(toHtml);
        LOAD(append);
        LOAD(clear);
        LOAD(setReadOnly);
        LOAD(isReadOnly);
        LOAD(setPlaceholderText);
        LOAD(copy);
        LOAD(cut);
        LOAD(paste);
        
        /* QComboBox */
        LOAD(QComboBox);
        LOAD(addItem);
        LOAD(insertItem);
        LOAD(removeItem);
        LOAD(clear);
        LOAD(count);
        LOAD(setCurrentIndex);
        LOAD(currentIndex);
        LOAD(currentText);
        LOAD(itemText);
        LOAD(setEditable);
        LOAD(setMaxVisibleItems);
        
        /* QSpinBox */
        LOAD(QSpinBox);
        LOAD(setRange);
        LOAD(minimum);
        LOAD(maximum);
        LOAD(setValue);
        LOAD(value);
        LOAD(setSingleStep);
        LOAD(setPrefix);
        LOAD(setSuffix);
        LOAD(setWrapping);
        
        /* QDoubleSpinBox */
        LOAD(QDoubleSpinBox);
        LOAD(setRange);
        LOAD(setValue);
        LOAD(value);
        LOAD(setDecimals);
        
        /* QSlider */
        LOAD(QSlider);
        LOAD(setOrientation);
        LOAD(setMinimum);
        LOAD(setMaximum);
        LOAD(setRange);
        LOAD(setValue);
        LOAD(value);
        LOAD(setSingleStep);
        LOAD(setTickPosition);
        LOAD(setInvertedAppearance);
        
        /* QScrollBar */
        LOAD(QScrollBar);
        LOAD(setMinimum);
        LOAD(setMaximum);
        LOAD(setValue);
        LOAD(value);
        
        /* QDial */
        LOAD(QDial);
        LOAD(setMinimum);
        LOAD(setMaximum);
        LOAD(setValue);
        LOAD(value);
        LOAD(setWrapping);
        LOAD(setNotchesVisible);
        
        /* QProgressBar */
        LOAD(QProgressBar);
        LOAD(setRange);
        LOAD(setValue);
        LOAD(value);
        LOAD(setFormat);
        LOAD(setOrientation);
        LOAD(setTextVisible);
        LOAD(reset);
        
        /* QFrame */
        LOAD(QFrame);
        LOAD(setFrameShape);
        LOAD(setFrameShadow);
        LOAD(setLineWidth);
        LOAD(setFrameStyle);
        
        /* Layouts */
        LOAD(QVBoxLayout);
        LOAD(addStretch);
        LOAD(addSpacing);
        LOAD(QHBoxLayout);
        LOAD(QGridLayout);
        LOAD(setRowSpacing);
        LOAD(setColumnSpacing);
        LOAD(setRowStretch);
        LOAD(setColumnStretch);
        
        /* QTabWidget */
        LOAD(QTabWidget);
        LOAD(addTab);
        LOAD(insertTab);
        LOAD(removeTab);
        LOAD(setCurrentIndex);
        LOAD(currentIndex);
        LOAD(currentWidget);
        LOAD(setCurrentWidget);
        LOAD(count);
        LOAD(setTabText);
        LOAD(tabText);
        LOAD(setTabEnabled);
        LOAD(isTabEnabled);
        LOAD(setTabsClosable);
        LOAD(setMovable);
        LOAD(setTabPosition);
        LOAD(clear);
        
        /* QStackedWidget */
        LOAD(QStackedWidget);
        LOAD(addWidget);
        LOAD(insertWidget);
        LOAD(setCurrentIndex);
        LOAD(currentIndex);
        LOAD(currentWidget);
        LOAD(setCurrentWidget);
        LOAD(count);
        LOAD(widget);
        LOAD(indexOf);
        LOAD(removeWidget);
        
        /* QScrollArea */
        LOAD(QScrollArea);
        LOAD(setWidget);
        LOAD(widget);
        LOAD(setWidgetResizable);
        LOAD(setHScrollBarPolicy);
        LOAD(setVScrollBarPolicy);
        
        /* QSplitter */
        LOAD(QSplitter);
        LOAD(addWidget);
        LOAD(setOrientation);
        LOAD(setHandleWidth);
        
        /* QMenuBar */
        LOAD(QMenuBar);
        LOAD(addMenu);
        LOAD(addAction);
        
        /* QMenu */
        LOAD(QMenu);
        LOAD(addAction);
        LOAD(addSeparator);
        LOAD(clear);
        
        /* QStatusBar */
        LOAD(QStatusBar);
        LOAD(showMessage);
        LOAD(clearMessage);
        LOAD(addWidget);
        LOAD(addPermanentWidget);
        
        /* QMessageBox */
        LOAD_CORE(about);
        LOAD_CORE(aboutQt);
        LOAD_CORE(question);
        LOAD_CORE(information);
        LOAD_CORE(warning);
        LOAD_CORE(critical);
        
        /* QInputDialog */
        LOAD_CORE(getText);
        LOAD_CORE(getInteger);
        
        /* QFileDialog */
        LOAD_CORE(getOpenFileName);
        LOAD_CORE(getSaveFileName);
        LOAD_CORE(getExistingDirectory);
        
        /* QColorDialog */
        LOAD_CORE(getColor);
        
        /* QFontDialog */
        LOAD_CORE(getFont);
        
        /* QClipboard */
        /* qApp_clipboard already loaded */
        LOAD_CORE(text);
        LOAD_CORE(setText);
        LOAD_CORE(clear);
        
        /* QTimer */
        LOAD(QTimer);
        LOAD(start);
        LOAD(stop);
        LOAD(isActive);
        LOAD(setInterval);
        LOAD(setSingleShot);
        
        /* QSettings */
        LOAD(QSettings);
        LOAD(setValue);
        LOAD(value);
        LOAD(contains);
        LOAD(remove);
        LOAD(sync);
        
        return true;
    }
    
    bool isLoaded() const {
        return qtWidgets.isLoaded();
    }
};

static Qt6Libs* g_qtLibs = nullptr;

/* Widget handle system */
std::unordered_map<int64_t, QWidget*> g_widgets;
std::unordered_map<int64_t, void*> g_menus;
std::unordered_map<int64_t, void*> g_layouts;
std::unordered_map<int64_t, void*> g_timers;
std::unordered_map<int64_t, void*> g_settings;
int64_t g_nextId = 1;

void cleanupWidget(void* ptr) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return;
    int64_t id = reinterpret_cast<int64_t>(ptr);
    auto it = g_widgets.find(id);
    if (it != g_widgets.end()) {
        g_qtLibs->qWidget_delete(it->second);
        g_widgets.erase(it);
    }
}

int64_t storeWidget(QWidget* w) {
    int64_t id = g_nextId++;
    g_widgets[id] = w;
    return id;
}

QWidget* getWidget(int64_t id) {
    auto it = g_widgets.find(id);
    return (it != g_widgets.end()) ? it->second : nullptr;
}

int64_t storeMenu(void* m) {
    int64_t id = g_nextId++;
    g_menus[id] = m;
    return id;
}

void* getMenu(int64_t id) {
    auto it = g_menus.find(id);
    return (it != g_menus.end()) ? it->second : nullptr;
}

int64_t storeLayout(void* l) {
    int64_t id = g_nextId++;
    g_layouts[id] = l;
    return id;
}

void* getLayout(int64_t id) {
    auto it = g_layouts.find(id);
    return (it != g_layouts.end()) ? it->second : nullptr;
}

/* ============================================================================
 * CORE FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_init(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    
    if (!g_qtLibs) {
        g_qtLibs = new Qt6Libs();
        if (!g_qtLibs->load()) {
            delete g_qtLibs;
            g_qtLibs = nullptr;
            return havel_new_bool(0);
        }
    }
    
    g_qtLibs->qApp_init(nullptr, nullptr);
    return havel_new_bool(1);
}

static HavelValue* qt_exec(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    return havel_new_int(g_qtLibs->qApp_exec());
}

static HavelValue* qt_quit(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    g_qtLibs->qApp_quit();
    return havel_new_null();
}

static HavelValue* qt_processEvents(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    g_qtLibs->qApp_processEvents();
    return havel_new_null();
}

static HavelValue* qt_setApplicationName(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    const char* name = havel_get_string(argv[0]);
    g_qtLibs->qApp_setApplicationName(name);
    return havel_new_null();
}

static HavelValue* qt_setOrganizationName(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    const char* name = havel_get_string(argv[0]);
    g_qtLibs->qApp_setOrganizationName(name);
    return havel_new_null();
}

/* ============================================================================
 * WIDGET BASE FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_widget_show(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_show(w);
    return havel_new_bool(1);
}

static HavelValue* qt_widget_hide(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_hide(w);
    return havel_new_bool(1);
}

static HavelValue* qt_widget_close(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_close(w);
    return havel_new_bool(1);
}

static HavelValue* qt_widget_setVisible(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_setVisible(w, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_setEnabled(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_setEnabled(w, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_setWindowTitle(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_setWindowTitle(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_resize(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_resize(w, (int)havel_get_int(argv[1]), (int)havel_get_int(argv[2]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_move(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_move(w, (int)havel_get_int(argv[1]), (int)havel_get_int(argv[2]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_setStyleSheet(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_setStyleSheet(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_setToolTip(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_setToolTip(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_widget_setFocus(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_setFocus(w);
    return havel_new_bool(1);
}

static HavelValue* qt_widget_update(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qWidget_update(w);
    return havel_new_bool(1);
}

/* ============================================================================
 * MAIN WINDOW
 * ============================================================================ */

static HavelValue* qt_mainWindow_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    QMainWindow* mw = g_qtLibs->qMainWindow_new(parent);
    return havel_new_handle(reinterpret_cast<void*>(storeWidget(mw)), cleanupWidget);
}

static HavelValue* qt_mainWindow_setCentralWidget(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QMainWindow* mw = reinterpret_cast<QMainWindow*>(getWidget(id));
    if (!mw) return havel_new_bool(0);
    QWidget* cw = getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[1])));
    if (!cw) return havel_new_bool(0);
    g_qtLibs->qMainWindow_setCentralWidget(mw, cw);
    return havel_new_bool(1);
}

/* ============================================================================
 * LABEL
 * ============================================================================ */

static HavelValue* qt_label_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    const char* text = (argc >= 1) ? havel_get_string(argv[0]) : "";
    QWidget* parent = (argc >= 2) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[1]))) : nullptr;
    QWidget* label = g_qtLibs->qLabel_new(text, parent);
    return havel_new_handle(reinterpret_cast<void*>(storeWidget(label)), cleanupWidget);
}

static HavelValue* qt_label_setText(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qLabel_setText(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_label_setAlignment(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qLabel_setAlignment(w, (int)havel_get_int(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_label_setWordWrap(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qLabel_setWordWrap(w, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

/* ============================================================================
 * PUSH BUTTON
 * ============================================================================ */

static HavelValue* qt_pushButton_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    const char* text = (argc >= 1) ? havel_get_string(argv[0]) : "";
    QWidget* parent = (argc >= 2) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[1]))) : nullptr;
    QWidget* btn = g_qtLibs->qPushButton_new(text, parent);
    return havel_new_handle(reinterpret_cast<void*>(storeWidget(btn)), cleanupWidget);
}

static HavelValue* qt_pushButton_setText(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qPushButton_setText(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_pushButton_setCheckable(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qPushButton_setCheckable(w, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_pushButton_setChecked(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qPushButton_setChecked(w, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_pushButton_click(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_null();
    g_qtLibs->qPushButton_click(w);
    return havel_new_null();
}

/* ============================================================================
 * LINE EDIT
 * ============================================================================ */

static HavelValue* qt_lineEdit_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    const char* text = (argc >= 1) ? havel_get_string(argv[0]) : "";
    QWidget* parent = (argc >= 2) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[1]))) : nullptr;
    QWidget* le = g_qtLibs->qLineEdit_new(text, parent);
    return havel_new_handle(reinterpret_cast<void*>(storeWidget(le)), cleanupWidget);
}

static HavelValue* qt_lineEdit_setText(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qLineEdit_setText(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_lineEdit_text(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_string("");
    const char* text = g_qtLibs->qLineEdit_text(w);
    return havel_new_string(text ? text : "");
}

static HavelValue* qt_lineEdit_setPlaceholderText(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qLineEdit_setPlaceholderText(w, havel_get_string(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_lineEdit_setReadOnly(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_bool(0);
    g_qtLibs->qLineEdit_setReadOnly(w, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_lineEdit_clear(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    QWidget* w = getWidget(id);
    if (!w) return havel_new_null();
    g_qtLibs->qLineEdit_clear(w);
    return havel_new_null();
}

/* ============================================================================
 * LAYOUTS
 * ============================================================================ */

static HavelValue* qt_vBoxLayout_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    void* layout = g_qtLibs->qVBoxLayout_new(parent);
    return havel_new_handle(reinterpret_cast<void*>(storeLayout(layout)), nullptr);
}

static HavelValue* qt_hBoxLayout_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    void* layout = g_qtLibs->qHBoxLayout_new(parent);
    return havel_new_handle(reinterpret_cast<void*>(storeLayout(layout)), nullptr);
}

static HavelValue* qt_layout_addWidget(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t layoutId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    void* layout = getLayout(layoutId);
    QWidget* w = getWidget(widgetId);
    if (!layout || !w) return havel_new_bool(0);
    g_qtLibs->qLayout_addWidget(layout, w);
    return havel_new_bool(1);
}

static HavelValue* qt_layout_addStretch(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    int64_t layoutId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    void* layout = getLayout(layoutId);
    if (!layout) return havel_new_null();
    int stretch = (argc >= 2) ? (int)havel_get_int(argv[1]) : 1;
    g_qtLibs->qVBoxLayout_addStretch(layout, stretch);
    return havel_new_null();
}

static HavelValue* qt_widget_setLayout(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t widgetId = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t layoutId = reinterpret_cast<int64_t>(havel_get_handle(argv[1]));
    QWidget* w = getWidget(widgetId);
    void* layout = getLayout(layoutId);
    if (!w || !layout) return havel_new_bool(0);
    g_qtLibs->qWidget_setLayout(w, layout);
    return havel_new_bool(1);
}

/* ============================================================================
 * MESSAGE BOX
 * ============================================================================ */

static HavelValue* qt_messageBox_about(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_null();
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* title = (argc >= 2) ? havel_get_string(argv[1]) : "";
    const char* text = (argc >= 3) ? havel_get_string(argv[2]) : "";
    g_qtLibs->qMessageBox_about(parent, title, text);
    return havel_new_null();
}

static HavelValue* qt_messageBox_information(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* title = havel_get_string(argv[1]);
    const char* text = havel_get_string(argv[2]);
    int buttons = (argc >= 4) ? (int)havel_get_int(argv[3]) : Qt_Ok;
    int defaultBtn = (argc >= 5) ? (int)havel_get_int(argv[4]) : Qt_Ok;
    return havel_new_int(g_qtLibs->qMessageBox_information(parent, title, text, buttons, defaultBtn));
}

static HavelValue* qt_messageBox_question(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* title = havel_get_string(argv[1]);
    const char* text = havel_get_string(argv[2]);
    int buttons = (argc >= 4) ? (int)havel_get_int(argv[3]) : Qt_Yes | Qt_No;
    int defaultBtn = (argc >= 5) ? (int)havel_get_int(argv[4]) : Qt_No;
    return havel_new_int(g_qtLibs->qMessageBox_question(parent, title, text, buttons, defaultBtn));
}

static HavelValue* qt_messageBox_warning(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* title = havel_get_string(argv[1]);
    const char* text = havel_get_string(argv[2]);
    int buttons = (argc >= 4) ? (int)havel_get_int(argv[3]) : Qt_Ok;
    int defaultBtn = (argc >= 5) ? (int)havel_get_int(argv[4]) : Qt_Ok;
    return havel_new_int(g_qtLibs->qMessageBox_warning(parent, title, text, buttons, defaultBtn));
}

/* ============================================================================
 * TIMER FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_timer_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    void* timer = g_qtLibs->qTimer_new(nullptr);
    int64_t id = g_nextId++;
    g_timers[id] = timer;
    return havel_new_handle(reinterpret_cast<void*>(id), nullptr);
}

static HavelValue* qt_timer_start(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_timers.find(id);
    if (it == g_timers.end()) return havel_new_bool(0);
    int msec = (int)havel_get_int(argv[1]);
    g_qtLibs->qTimer_start(it->second, msec);
    return havel_new_bool(1);
}

static HavelValue* qt_timer_stop(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_timers.find(id);
    if (it == g_timers.end()) return havel_new_bool(0);
    g_qtLibs->qTimer_stop(it->second);
    return havel_new_bool(1);
}

static HavelValue* qt_timer_setInterval(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_timers.find(id);
    if (it == g_timers.end()) return havel_new_bool(0);
    int msec = (int)havel_get_int(argv[1]);
    g_qtLibs->qTimer_setInterval(it->second, msec);
    return havel_new_bool(1);
}

static HavelValue* qt_timer_setSingleShot(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_timers.find(id);
    if (it == g_timers.end()) return havel_new_bool(0);
    g_qtLibs->qTimer_setSingleShot(it->second, havel_get_bool(argv[1]));
    return havel_new_bool(1);
}

static HavelValue* qt_timer_isActive(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_timers.find(id);
    if (it == g_timers.end()) return havel_new_bool(0);
    return havel_new_bool(g_qtLibs->qTimer_isActive(it->second));
}

/* ============================================================================
 * SETTINGS FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_settings_new(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    const char* fileName = (argc >= 1) ? havel_get_string(argv[0]) : "settings.ini";
    void* settings = g_qtLibs->qSettings_new(fileName, Qt_IniFormat, nullptr);
    int64_t id = g_nextId++;
    g_settings[id] = settings;
    return havel_new_handle(reinterpret_cast<void*>(id), nullptr);
}

static HavelValue* qt_settings_setValue(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_settings.find(id);
    if (it == g_settings.end()) return havel_new_bool(0);
    const char* key = havel_get_string(argv[1]);
    const char* val = havel_get_string(argv[2]);
    g_qtLibs->qSettings_setValue(it->second, key, val);
    return havel_new_bool(1);
}

static HavelValue* qt_settings_value(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_settings.find(id);
    if (it == g_settings.end()) return havel_new_string("");
    const char* key = havel_get_string(argv[1]);
    const char* defVal = (argc >= 3) ? havel_get_string(argv[2]) : "";
    const char* val = g_qtLibs->qSettings_value(it->second, key, defVal);
    return havel_new_string(val ? val : "");
}

static HavelValue* qt_settings_contains(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_settings.find(id);
    if (it == g_settings.end()) return havel_new_bool(0);
    const char* key = havel_get_string(argv[1]);
    return havel_new_bool(g_qtLibs->qSettings_contains(it->second, key));
}

static HavelValue* qt_settings_remove(int argc, HavelValue** argv) {
    if (argc < 2 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_settings.find(id);
    if (it == g_settings.end()) return havel_new_bool(0);
    const char* key = havel_get_string(argv[1]);
    g_qtLibs->qSettings_remove(it->second, key);
    return havel_new_bool(1);
}

static HavelValue* qt_settings_sync(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_settings.find(id);
    if (it == g_settings.end()) return havel_new_bool(0);
    g_qtLibs->qSettings_sync(it->second);
    return havel_new_bool(1);
}

/* ============================================================================
 * CLIPBOARD FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_clipboard_text(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    void* cb = g_qtLibs->qApp_clipboard();
    if (!cb) return havel_new_string("");
    const char* text = g_qtLibs->qClipboard_text(cb, 0);
    return havel_new_string(text ? text : "");
}

static HavelValue* qt_clipboard_setText(int argc, HavelValue** argv) {
    if (argc < 1 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    void* cb = g_qtLibs->qApp_clipboard();
    if (!cb) return havel_new_bool(0);
    const char* text = havel_get_string(argv[0]);
    g_qtLibs->qClipboard_setText(cb, text, 0);
    return havel_new_bool(1);
}

static HavelValue* qt_clipboard_clear(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_bool(0);
    void* cb = g_qtLibs->qApp_clipboard();
    if (!cb) return havel_new_bool(0);
    g_qtLibs->qClipboard_clear(cb);
    return havel_new_bool(1);
}

/* ============================================================================
 * FILE DIALOG FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_fileDialog_getOpenFileName(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* caption = (argc >= 2) ? havel_get_string(argv[1]) : "Open File";
    const char* dir = (argc >= 3) ? havel_get_string(argv[2]) : "";
    const char* filter = (argc >= 4) ? havel_get_string(argv[3]) : "All Files (*)";
    const char* fileName = g_qtLibs->qFileDialog_getOpenFileName(parent, caption, dir, filter, nullptr, 0);
    return havel_new_string(fileName ? fileName : "");
}

static HavelValue* qt_fileDialog_getSaveFileName(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* caption = (argc >= 2) ? havel_get_string(argv[1]) : "Save File";
    const char* dir = (argc >= 3) ? havel_get_string(argv[2]) : "";
    const char* filter = (argc >= 4) ? havel_get_string(argv[3]) : "All Files (*)";
    const char* fileName = g_qtLibs->qFileDialog_getSaveFileName(parent, caption, dir, filter, nullptr, 0);
    return havel_new_string(fileName ? fileName : "");
}

static HavelValue* qt_fileDialog_getExistingDirectory(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* caption = (argc >= 2) ? havel_get_string(argv[1]) : "Select Directory";
    const char* dir = (argc >= 3) ? havel_get_string(argv[2]) : "";
    const char* path = g_qtLibs->qFileDialog_getExistingDirectory(parent, caption, dir, 0);
    return havel_new_string(path ? path : "");
}

/* ============================================================================
 * COLOR DIALOG FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_colorDialog_getColor(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    const char* initial = (argc >= 1) ? havel_get_string(argv[0]) : "#ffffff";
    QWidget* parent = (argc >= 2) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[1]))) : nullptr;
    const char* color = g_qtLibs->qColorDialog_getColor(nullptr, initial, parent, "Select Color", 0);
    return havel_new_string(color ? color : "");
}

/* ============================================================================
 * FONT DIALOG FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_fontDialog_getFont(int argc, HavelValue** argv) {
    if (!g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* font = g_qtLibs->qFontDialog_getFont(nullptr, nullptr, parent, "Select Font", 0);
    return havel_new_string(font ? font : "");
}

/* ============================================================================
 * INPUT DIALOG FUNCTIONS
 * ============================================================================ */

static HavelValue* qt_inputDialog_getText(int argc, HavelValue** argv) {
    if (argc < 3 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_string("");
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* title = havel_get_string(argv[1]);
    const char* label = havel_get_string(argv[2]);
    const char* text = (argc >= 4) ? havel_get_string(argv[3]) : "";
    bool ok = false;
    const char* result = g_qtLibs->qInputDialog_getText(parent, title, label, Qt_NormalEcho, text, &ok, 0);
    return havel_new_string(ok ? (result ? result : "") : "");
}

static HavelValue* qt_inputDialog_getInteger(int argc, HavelValue** argv) {
    if (argc < 4 || !g_qtLibs || !g_qtLibs->isLoaded()) return havel_new_int(0);
    QWidget* parent = (argc >= 1) ? getWidget(reinterpret_cast<int64_t>(havel_get_handle(argv[0]))) : nullptr;
    const char* title = havel_get_string(argv[1]);
    const char* label = havel_get_string(argv[2]);
    int value = (int)havel_get_int(argv[3]);
    int min = (argc >= 5) ? (int)havel_get_int(argv[4]) : 0;
    int max = (argc >= 6) ? (int)havel_get_int(argv[5]) : 100;
    int step = (argc >= 7) ? (int)havel_get_int(argv[6]) : 1;
    bool ok = false;
    int result = g_qtLibs->qInputDialog_getInteger(parent, title, label, &ok, value, min, max, step, 0);
    return ok ? havel_new_int(result) : havel_new_int(0);
}

/* ============================================================================
 * REGISTRATION
 * ============================================================================ */

} /* anonymous namespace */

extern "C" void havel_extension_init(HavelAPI* api) {
    /* Core */
    api->register_function("qt", "init", qt_init);
    api->register_function("qt", "exec", qt_exec);
    api->register_function("qt", "quit", qt_quit);
    api->register_function("qt", "processEvents", qt_processEvents);
    api->register_function("qt", "setApplicationName", qt_setApplicationName);
    api->register_function("qt", "setOrganizationName", qt_setOrganizationName);

    /* Widget base */
    api->register_function("qt", "widgetShow", qt_widget_show);
    api->register_function("qt", "widgetHide", qt_widget_hide);
    api->register_function("qt", "widgetClose", qt_widget_close);
    api->register_function("qt", "widgetSetVisible", qt_widget_setVisible);
    api->register_function("qt", "widgetSetEnabled", qt_widget_setEnabled);
    api->register_function("qt", "widgetSetWindowTitle", qt_widget_setWindowTitle);
    api->register_function("qt", "widgetResize", qt_widget_resize);
    api->register_function("qt", "widgetMove", qt_widget_move);
    api->register_function("qt", "widgetSetStyleSheet", qt_widget_setStyleSheet);
    api->register_function("qt", "widgetSetToolTip", qt_widget_setToolTip);
    api->register_function("qt", "widgetSetFocus", qt_widget_setFocus);
    api->register_function("qt", "widgetUpdate", qt_widget_update);

    /* QMainWindow */
    api->register_function("qt", "mainWindowNew", qt_mainWindow_new);
    api->register_function("qt", "mainWindowSetCentralWidget", qt_mainWindow_setCentralWidget);

    /* QLabel */
    api->register_function("qt", "labelNew", qt_label_new);
    api->register_function("qt", "labelSetText", qt_label_setText);
    api->register_function("qt", "labelSetAlignment", qt_label_setAlignment);
    api->register_function("qt", "labelSetWordWrap", qt_label_setWordWrap);

    /* QPushButton */
    api->register_function("qt", "pushButtonNew", qt_pushButton_new);
    api->register_function("qt", "pushButtonSetText", qt_pushButton_setText);
    api->register_function("qt", "pushButtonSetCheckable", qt_pushButton_setCheckable);
    api->register_function("qt", "pushButtonSetChecked", qt_pushButton_setChecked);
    api->register_function("qt", "pushButtonClick", qt_pushButton_click);

    /* QLineEdit */
    api->register_function("qt", "lineEditNew", qt_lineEdit_new);
    api->register_function("qt", "lineEditSetText", qt_lineEdit_setText);
    api->register_function("qt", "lineEditText", qt_lineEdit_text);
    api->register_function("qt", "lineEditSetPlaceholderText", qt_lineEdit_setPlaceholderText);
    api->register_function("qt", "lineEditSetReadOnly", qt_lineEdit_setReadOnly);
    api->register_function("qt", "lineEditClear", qt_lineEdit_clear);

    /* Layouts */
    api->register_function("qt", "vBoxLayoutNew", qt_vBoxLayout_new);
    api->register_function("qt", "hBoxLayoutNew", qt_hBoxLayout_new);
    api->register_function("qt", "layoutAddWidget", qt_layout_addWidget);
    api->register_function("qt", "layoutAddStretch", qt_layout_addStretch);
    api->register_function("qt", "widgetSetLayout", qt_widget_setLayout);

    /* QMessageBox */
    api->register_function("qt", "messageBoxAbout", qt_messageBox_about);
    api->register_function("qt", "messageBoxInformation", qt_messageBox_information);
    api->register_function("qt", "messageBoxQuestion", qt_messageBox_question);
    api->register_function("qt", "messageBoxWarning", qt_messageBox_warning);

    /* QTimer */
    api->register_function("qt", "timerNew", qt_timer_new);
    api->register_function("qt", "timerStart", qt_timer_start);
    api->register_function("qt", "timerStop", qt_timer_stop);
    api->register_function("qt", "timerSetInterval", qt_timer_setInterval);
    api->register_function("qt", "timerSetSingleShot", qt_timer_setSingleShot);
    api->register_function("qt", "timerIsActive", qt_timer_isActive);

    /* QSettings */
    api->register_function("qt", "settingsNew", qt_settings_new);
    api->register_function("qt", "settingsSetValue", qt_settings_setValue);
    api->register_function("qt", "settingsValue", qt_settings_value);
    api->register_function("qt", "settingsContains", qt_settings_contains);
    api->register_function("qt", "settingsRemove", qt_settings_remove);
    api->register_function("qt", "settingsSync", qt_settings_sync);

    /* QClipboard */
    api->register_function("qt", "clipboardText", qt_clipboard_text);
    api->register_function("qt", "clipboardSetText", qt_clipboard_setText);
    api->register_function("qt", "clipboardClear", qt_clipboard_clear);

    /* QFileDialog */
    api->register_function("qt", "fileDialogGetOpenFileName", qt_fileDialog_getOpenFileName);
    api->register_function("qt", "fileDialogGetSaveFileName", qt_fileDialog_getSaveFileName);
    api->register_function("qt", "fileDialogGetExistingDirectory", qt_fileDialog_getExistingDirectory);

    /* QColorDialog */
    api->register_function("qt", "colorDialogGetColor", qt_colorDialog_getColor);

    /* QFontDialog */
    api->register_function("qt", "fontDialogGetFont", qt_fontDialog_getFont);

    /* QInputDialog */
    api->register_function("qt", "inputDialogGetText", qt_inputDialog_getText);
    api->register_function("qt", "inputDialogGetInteger", qt_inputDialog_getInteger);
}
